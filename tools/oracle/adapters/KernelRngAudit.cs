// Optional development audit, excluded from production recording builds.
using System.Text.Json;
using MegaCrit.Sts2.Core.Helpers;
using MegaCrit.Sts2.Core.Random;

namespace Sts2Headless;
public partial class RunSimulator
{
    public Dictionary<string, object?> AuditKernelRng(JsonElement command)
    {
        var hashes = new[] { "", "a", "ORACLECHECK01", "shuffle", "up_front", "01234567890123456789012345678901", "01234567890123456789012345678901234567890123456789", "é🐦" }
            .Select(text => new { text, hash = StringHelper.GetDeterministicHashCode(text) }).ToArray();
        var sequences = new[] { 0UL, 1UL, ulong.MaxValue, 987654321UL }.Select(seed => {
            var rng = new Rng(seed);
            var raw = Enumerable.Range(0, 16).Select(_ => rng.NextUnsignedLong()).ToArray();
            var ints = Enumerable.Range(0, 20).Select(i => rng.NextInt(i % 2 == 0 ? -123 : int.MinValue, i % 2 == 0 ? 456 : int.MaxValue)).ToArray();
            var bools = Enumerable.Range(0, 16).Select(_ => rng.NextBool()).ToArray();
            var floats = Enumerable.Range(0, 8).Select(_ => BitConverter.SingleToUInt32Bits(rng.NextFloat(-1.25f, 7.75f))).ToArray();
            var equalFloat = BitConverter.SingleToUInt32Bits(rng.NextFloat(2f, 2f));
            var shuffled = Enumerable.Range(0, 32).ToList(); rng.Shuffle(shuffled);
            var state = rng.ToSerializable();
            return new { seed, raw, ints, bools, floats, equal_float = equalFloat, shuffled,
                state = new { counter = state.counter, s0 = state.state0, s1 = state.state1, s2 = state.state2, s3 = state.state3 } };
        }).ToArray();
        // Check Array.Sort tie behavior with observable identities as well as keys.
        var sorted = new[] { 10, 16, 17, 32, 100 }.Select(count => {
            var values = Enumerable.Range(0, count).Select(i => (key: (i * 7) % 4, identity: i)).ToList();
            values.Sort((a, b) => a.key.CompareTo(b.key));
            return new { count, identities = values.Select(v => v.identity).ToArray() };
        }).ToArray();
        return new() { ["type"] = "kernel_rng_audit", ["hashes"] = hashes, ["sequences"] = sequences, ["sorted"] = sorted };
    }
}
