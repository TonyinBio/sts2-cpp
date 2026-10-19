// Injected only into a disposable sts2-cli build by check_native_run.py.
using System.Collections;
using System.Text.Json;
using System.Text.Json.Nodes;
using System.Text.Json.Serialization;
using MegaCrit.Sts2.Core.Entities.Rngs;
using MegaCrit.Sts2.Core.Saves;

namespace Sts2Headless;

public static class NativeRunRoundTrip
{
    // Read the native members directly, before ShouldSerialize can omit a value.
    // Serializing twice alone would conceal lossy native default elision.
    private static JsonNode? State(object? value)
    {
        if (value == null) return null;
        var options = JsonSerializationUtility.Options;
        var info = options.GetTypeInfo(value.GetType());
        if (value is IDictionary dictionary)
        {
            var result = new JsonObject();
            foreach (DictionaryEntry entry in dictionary)
                result.Add(entry.Key.ToString()!, State(entry.Value));
            return result;
        }
        if (value is IEnumerable sequence && value is not string)
        {
            var result = new JsonArray();
            foreach (var item in sequence) result.Add(State(item));
            return result;
        }
        if (info.Kind != System.Text.Json.Serialization.Metadata.JsonTypeInfoKind.Object)
            return JsonSerializer.SerializeToNode(value, info);

        var record = new JsonObject();
        foreach (var property in info.Properties)
        {
            if (property.Get == null) continue; // Includes native JsonIgnore members.
            var member = property.Get(value);
            if (member != null && property.CustomConverter != null &&
                property.CustomConverter.GetType().Name == "LocStringVariablesJsonConverter")
            {
                // This dictionary's native representation is tagged dynamic values.
                var configured = new JsonSerializerOptions(options);
                configured.Converters.Insert(0, property.CustomConverter);
                record.Add(property.Name,
                    JsonSerializer.SerializeToNode(member, property.PropertyType, configured));
            }
            else
            {
                // Inspect drawings as data, not gzip bytes, and epoch lists as lists.
                record.Add(property.Name, State(member));
            }
        }
        return record;
    }

    public static Dictionary<string, object?> Run(JsonElement command)
    {
        var input = command.GetProperty("json").GetString()!;
        var first = SaveManager.FromJson<SerializableRun>(input);
        if (!first.Success || first.SaveData == null)
            throw new InvalidOperationException("Initial native decode failed: " + first.ErrorMessage);
        if (command.TryGetProperty("drawings_json", out var drawings))
            first.SaveData.MapDrawings = JsonSerializer.Deserialize<
                MegaCrit.Sts2.Core.Saves.MapDrawing.SerializableMapDrawings>(
                    drawings.GetString()!, JsonSerializationUtility.Options);
        var before = State(first.SaveData)!.ToJsonString();
        var serialized = SaveManager.ToJson(first.SaveData);
        var second = SaveManager.FromJson<SerializableRun>(serialized);
        if (!second.Success || second.SaveData == null)
            throw new InvalidOperationException("Round-trip native decode failed: " + second.ErrorMessage);

        // A deliberately corrupted native object verifies that the test sees one lost bit.
        if (command.TryGetProperty("flip_rng_bit", out var flip) && flip.GetBoolean())
            second.SaveData.SerializableRng.Rngs[RunRngType.Shuffle].state2 ^= 1UL;

        var after = State(second.SaveData)!.ToJsonString();
        var reserialized = SaveManager.ToJson(second.SaveData);
        return new()
        {
            ["type"] = "native_run_roundtrip",
            // Strings prevent the outer protocol serializer from rounding numbers.
            ["before"] = before, ["after"] = after,
            ["serialized"] = serialized, ["reserialized"] = reserialized,
        };
    }
}
