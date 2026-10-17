// Injected into a disposable sts2-cli copy by check_state_access.py.
using System.Text.Json;
using MegaCrit.Sts2.Core.Combat;
using MegaCrit.Sts2.Core.Entities.Cards;
using MegaCrit.Sts2.Core.Entities.Rngs;
using MegaCrit.Sts2.Core.Models;
using MegaCrit.Sts2.Core.Random;

namespace Sts2Headless;

public partial class RunSimulator
{
    private readonly Dictionary<CardModel, int> _probeIds = new(ReferenceEqualityComparer.Instance);
    private static readonly JsonSerializerOptions ProbeJson = new() { IncludeFields = true };

    private object ProbePile(CardPile pile)
    {
        var backing = GetBackingList<CardModel>(pile, "_cards")
            ?? throw new InvalidOperationException("CardPile._cards unavailable");
        if (!backing.SequenceEqual(pile.Cards, ReferenceEqualityComparer.Instance))
            throw new InvalidOperationException("Public pile order differs from backing list");
        return backing.Select((card, index) =>
        {
            if (!_probeIds.TryGetValue(card, out var id))
                _probeIds[card] = id = _probeIds.Count;
            return new { index, instance_id = id, model_id = card.Id.ToString() };
        }).ToArray();
    }

    public Dictionary<string, object?> ProbeState()
    {
        var run = _runState ?? throw new InvalidOperationException("No run");
        var player = run.Players[0];
        var pcs = player.PlayerCombatState ?? throw new InvalidOperationException("No combat");
        var combat = CombatManager.Instance.DebugOnlyGetState()
            ?? throw new InvalidOperationException("No combat state");
        var runRng = run.Rng.ToSerializable();
        var playerRng = player.PlayerRng.ToSerializable();

        // Verify restoration on fresh RNG objects, never by drawing from a live stream.
        var roundTrips = new Dictionary<string, bool>();
        foreach (var entry in runRng.Rngs.Select(e => ("run." + e.Key, e.Value))
            .Concat(playerRng.Rngs.Select(e => ("player." + e.Key, e.Value))))
        {
            var a = new Rng(0);
            var b = new Rng(1);
            a.LoadFromSerializable(entry.Value);
            b.LoadFromSerializable(entry.Value);
            bool matches = JsonSerializer.Serialize(a.ToSerializable(), ProbeJson)
                == JsonSerializer.Serialize(entry.Value, ProbeJson);
            for (int i = 0; i < 16; ++i)
                matches &= a.NextUnsignedLong() == b.NextUnsignedLong();
            matches &= JsonSerializer.Serialize(a.ToSerializable(), ProbeJson)
                == JsonSerializer.Serialize(b.ToSerializable(), ProbeJson);
            roundTrips[entry.Item1] = matches;
        }

        return new()
        {
            ["type"] = "probe_state",
            ["assembly_mvid"] = typeof(Rng).Assembly.ManifestModule.ModuleVersionId,
            ["round"] = combat.RoundNumber,
            ["phase"] = pcs.Phase.ToString(),
            ["piles"] = new Dictionary<string, object>
            {
                ["hand"] = ProbePile(pcs.Hand),
                ["draw"] = ProbePile(pcs.DrawPile),
                ["discard"] = ProbePile(pcs.DiscardPile),
                ["exhaust"] = ProbePile(pcs.ExhaustPile),
            },
            ["run_rng"] = JsonSerializer.SerializeToElement(runRng, ProbeJson),
            ["player_rng"] = JsonSerializer.SerializeToElement(playerRng, ProbeJson),
            ["run_rng_types"] = Enum.GetNames<RunRngType>(),
            ["player_rng_types"] = Enum.GetNames<PlayerRngType>(),
            ["rng_round_trips"] = roundTrips,
        };
    }
}
