// Shared, checked model storage capture. Never invokes gameplay/preview getters.
using System.Collections;
using System.Reflection;
using System.Reflection.Metadata;
using System.Reflection.PortableExecutable;
using System.Text.Json;
using MegaCrit.Sts2.Core.Models;
using MegaCrit.Sts2.Core.Map;
using MegaCrit.Sts2.Core.Localization.DynamicVars;

namespace Sts2Headless;

internal static class CombatModelFields
{
    private static readonly Dictionary<string, string> Contract = JsonSerializer.Deserialize<Dictionary<string, string>>(CombatModelContract.Members)!;
    private static readonly JsonElement Schema = JsonDocument.Parse(CombatModelContract.Schema).RootElement;
    private static readonly Lazy<int> VerifiedContract = new(CheckContract);
    private const BindingFlags Storage = BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.DeclaredOnly;

    private static string Name(FieldInfo field) => field.Name.StartsWith('<') && field.Name.EndsWith(">k__BackingField")
        ? field.Name[1..field.Name.IndexOf('>')] : field.Name;
    private static string Key(Type type, FieldInfo field)
    {
        var owner = type.IsNested ? type.DeclaringType! : type;
        return owner.Namespace![("MegaCrit.Sts2.Core.Models.".Length)..] + "." + owner.Name + "." + Name(field);
    }
    private static bool Concrete(Type type) => type.Namespace?.StartsWith("MegaCrit.Sts2.Core.Models.") == true;

    public static Dictionary<string, decimal> DynamicValues(AbstractModel model)
    {
        for (var type = model.GetType(); type != null; type = type.BaseType)
            if (type.GetField("_dynamicVars", Storage) is { } field) {
                var values = (DynamicVarSet?)field.GetValue(model);
                return values == null ? new() : values.Where(v => v.Value is not StringVar)
                    .ToDictionary(v => v.Key, v => v.Value.BaseValue);
            }
        return new();
    }

    public static int CheckContract()
    {
        var roots = new[] { typeof(CardModel), typeof(PowerModel), typeof(RelicModel), typeof(MonsterModel),
            typeof(PotionModel), typeof(OrbModel), typeof(EnchantmentModel), typeof(AfflictionModel),
            typeof(EncounterModel), typeof(ModifierModel), typeof(CharacterModel) };
        int count = 0;
        // Read type names from metadata so unrelated Godot UI types need not load
        // in the headless runner. Every model type itself must load successfully.
        using var stream = File.OpenRead(typeof(CardModel).Assembly.Location);
        using var pe = new PEReader(stream);
        var metadata = pe.GetMetadataReader();
        var groups = new HashSet<string>(new[] { "Cards", "Powers", "Relics", "Monsters", "Potions", "Orbs",
            "Enchantments", "Afflictions", "Encounters", "Modifiers", "Characters" }.Select(g => "MegaCrit.Sts2.Core.Models." + g));
        foreach (var definition in metadata.TypeDefinitions.Select(h => metadata.GetTypeDefinition(h))) {
            var ns = metadata.GetString(definition.Namespace);
            if (!groups.Contains(ns)) continue;
            var type = typeof(CardModel).Assembly.GetType(ns + "." + metadata.GetString(definition.Name), throwOnError: true)!;
            if (!roots.Any(root => root.IsAssignableFrom(type))) continue;
            foreach (var storage in new[] { type, type.GetNestedType("Data", BindingFlags.Public | BindingFlags.NonPublic) }.OfType<Type>())
                foreach (var field in storage.GetFields(Storage)) {
                    var key = Key(storage, field);
                    if (!Contract.ContainsKey(key)) throw new InvalidOperationException($"Unclassified combat model field: {key}");
                    count++;
                }
        }
        foreach (var key in Contract.Keys) {
            var dot = key.LastIndexOf('.'); var ownerName = key[..dot]; var member = key[(dot + 1)..];
            var owner = typeof(CardModel).Assembly.GetType("MegaCrit.Sts2.Core.Models." + ownerName)
                ?? throw new InvalidOperationException($"Unknown contract model: {ownerName}");
            if (!owner.GetFields(Storage).Any(f => Name(f) == member) &&
                !(owner.GetNestedType("Data", BindingFlags.Public | BindingFlags.NonPublic)?.GetFields(Storage).Any(f => Name(f) == member) ?? false))
                throw new InvalidOperationException($"Missing contract field: {key}");
        }
        return count;
    }

    public static Dictionary<string, object?> Capture(AbstractModel model, Func<object?, string?> reference)
    {
        _ = VerifiedContract.Value;
        if (!Concrete(model.GetType()) || model.GetType().Namespace!.Contains(".Mocks"))
            throw new InvalidOperationException($"Unclassified model type: {model.GetType()}");
        var result = new Dictionary<string, object?>();
        void CaptureStorage(object instance, Type type)
        {
            foreach (var field in type.GetFields(Storage)) {
                var key = Key(type, field);
                if (!Contract.TryGetValue(key, out var classification))
                    throw new InvalidOperationException($"Unclassified combat model field: {key} ({field.FieldType})");
                if (classification == "exclude_test_override" && field.GetValue(instance) is { } testValue &&
                    !(testValue is int defaultOverride && defaultOverride == -1))
                    throw new InvalidOperationException($"Active test override cannot be exported: {key}");
                if (classification is not ("additional_gameplay" or "ai_state_alias")) continue;
                var name = key[(key.IndexOf('.') + 1)..];
                if (!Schema.TryGetProperty(name, out var shape))
                    throw new InvalidOperationException($"Combat field absent from schema: {name}");
                result.Add(name, Encode(field.GetValue(instance), shape, reference, key));
            }
        }
        for (var type = model.GetType(); Concrete(type); type = type.BaseType!) CaptureStorage(model, type);
        if (model is PowerModel) {
            var data = typeof(PowerModel).GetField("_internalData", Storage)!.GetValue(model);
            if (data == null && model.GetType().GetNestedType("Data", BindingFlags.Public | BindingFlags.NonPublic) is { } expectedData &&
                expectedData.GetFields(Storage).Length != 0)
                throw new InvalidOperationException($"Missing internal power data: {model.GetType()}");
            if (data != null) {
                if (!data.GetType().IsNested || !Concrete(data.GetType()))
                    throw new InvalidOperationException($"Unclassified internal power storage: {data.GetType()}");
                CaptureStorage(data, data.GetType());
            }
        }
        return result;
    }

    private static object? Encode(object? value, JsonElement shape, Func<object?, string?> reference, string path)
    {
        if (shape.TryGetProperty("anyOf", out var alternatives)) {
            if (value == null && alternatives.EnumerateArray().Any(s => s.TryGetProperty("type", out var t) && t.GetString() == "null")) return null;
            shape = alternatives.EnumerateArray().First(s => !s.TryGetProperty("type", out var t) || t.GetString() != "null");
        }
        if (shape.TryGetProperty("$ref", out var definition)) {
            var name = definition.GetString()!;
            if (name == "#/$defs/Ref") return reference(value) ?? throw new InvalidOperationException($"Null nonnullable reference: {path}");
            if (name.EndsWith("/Decimal") && value is decimal) return value;
            if (name.EndsWith("/Int32") && value is int) return value;
            if (name.EndsWith("/ModelId") && value is ModelId id) return id.ToString();
            if (name.EndsWith("/MapCoord") && value is MapCoord coord) return NativeRunRoundTrip.Capture(coord);
            throw new InvalidOperationException($"Unhandled combat field shape {path}: {name}, value type {value?.GetType()}");
        }
        if (shape.TryGetProperty("enum", out var values) && value is Enum) {
            var name = value.ToString();
            if (values.EnumerateArray().Any(v => v.GetString() == name)) return name;
        }
        if (shape.TryGetProperty("type", out var kind)) {
            if (kind.GetString() == "boolean" && value is bool) return value;
            if (kind.GetString() == "string" && value is string) return value;
            if (kind.GetString() == "array" && value is IEnumerable sequence) {
                var item = shape.GetProperty("items");
                if (value is IDictionary dictionary) {
                    var properties = item.GetProperty("properties");
                    var entries = new List<object>();
                    foreach (DictionaryEntry entry in dictionary) entries.Add(new {
                        key = Encode(entry.Key, properties.GetProperty("key"), reference, path + "/key"),
                        value = Encode(entry.Value, properties.GetProperty("value"), reference, path + "/value") });
                    return entries.ToArray();
                }
                return sequence.Cast<object?>().Select(v => Encode(v, item, reference, path + "/*")).ToArray();
            }
        }
        throw new InvalidOperationException($"Unhandled combat field value: {path} ({value?.GetType()})");
    }
}
