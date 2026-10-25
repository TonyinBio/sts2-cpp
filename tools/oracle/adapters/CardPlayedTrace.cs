// Declared perturbation probe; compiled only into the disposable oracle build.
// Observes the real Hook.AfterCardPlayed consumer. Concrete callbacks are default
// implementations in this probe, so this certifies enumeration, not model effects.
using System.Reflection;
using System.Text.Json;
using HarmonyLib;
using MegaCrit.Sts2.Core.Combat;
using MegaCrit.Sts2.Core.Combat.History.Entries;
using MegaCrit.Sts2.Core.Commands;
using MegaCrit.Sts2.Core.GameActions.Multiplayer;
using MegaCrit.Sts2.Core.Hooks;
using MegaCrit.Sts2.Core.Models;
using MegaCrit.Sts2.Core.Models.Powers;

namespace Sts2Headless;
public partial class RunSimulator
{
    private static bool _cardPlayedTracePatched;
    private static RunSimulator? _cardPlayedTraceSession;
    private AbstractModel? _cardPlayedTraceFirst, _cardPlayedTraceLast;
    private CardModel? _cardPlayedTraceRemovedCard;
    private PowerModel? _cardPlayedTraceRemovedPower;
    private readonly List<string> _cardPlayedOrdinary = new(), _cardPlayedLate = new();

    public static void TraceCardPlayed(AbstractModel __instance)
    {
        if (__instance is PowerModel or RelicModel or CardModel or MonsterModel)
            _cardPlayedTraceSession?._cardPlayedOrdinary.Add(__instance.Id.ToString());
    }
    public static void TraceCardPlayedLate(AbstractModel __instance)
    {
        if (__instance is PowerModel or RelicModel or CardModel or MonsterModel)
            _cardPlayedTraceSession?._cardPlayedLate.Add(__instance.Id.ToString());
    }
    public static void PerturbCardPlayed(AbstractModel __instance, ref Task __result)
    {
        var session = _cardPlayedTraceSession;
        if (session == null) return;
        var first = ReferenceEquals(__instance, session._cardPlayedTraceFirst);
        var last = ReferenceEquals(__instance, session._cardPlayedTraceLast);
        if (first || last) __result = session.PerturbAfterCardPlayed(__result, first, last);
    }
    private async Task PerturbAfterCardPlayed(Task original, bool first, bool last)
    {
        await original;
        if (first) {
            await PowerCmd.Remove(_cardPlayedTraceRemovedPower!);
            await CardPileCmd.RemoveFromCombat(_cardPlayedTraceRemovedCard!, true);
        }
        if (last) {
            var player = _runState!.Players[0];
            await PowerCmd.Apply<StrengthPower>(new ThrowingPlayerChoiceContext(), player.Creature, 1, player.Creature, null);
        }
    }
    public Dictionary<string, object?> OracleCardPlayedProbe(JsonElement command)
    {
        AwaitRunBoundary();
        Require(_cardPlayedTraceSession == null, "Card-play trace already active");
        if (!_cardPlayedTracePatched) {
            var harmony = new Harmony("sts2.oracle.card-played-perturbation-probe");
            harmony.Patch(typeof(AbstractModel).GetMethod(nameof(AbstractModel.AfterCardPlayed))!,
                prefix: new HarmonyMethod(typeof(RunSimulator).GetMethod(nameof(TraceCardPlayed))!),
                postfix: new HarmonyMethod(typeof(RunSimulator).GetMethod(nameof(PerturbCardPlayed))!));
            harmony.Patch(typeof(AbstractModel).GetMethod(nameof(AbstractModel.AfterCardPlayedLate))!,
                prefix: new HarmonyMethod(typeof(RunSimulator).GetMethod(nameof(TraceCardPlayedLate))!));
            _cardPlayedTracePatched = true;
        }
        var state = CombatManager.Instance.DebugOnlyGetState()!;
        var listeners = state.IterateHookListeners().ToArray();
        // Badges are meta progression, excluded by the existing gameplay-state
        // contract. Their real callbacks still execute; only gameplay candidates
        // are traced here. The perturbation waits for the actual last listener.
        foreach (var listener in listeners.Where(model => model is PowerModel or RelicModel or CardModel or MonsterModel))
            foreach (var method in new[] { nameof(AbstractModel.AfterCardPlayed), nameof(AbstractModel.AfterCardPlayedLate) })
                Require(listener.GetType().GetMethod(method)!.DeclaringType == typeof(AbstractModel),
                    "Probe requires declared default callback models: " + listener.Id);
        _cardPlayedTraceFirst = listeners.First(); _cardPlayedTraceLast = listeners.Last();
        var lastMethod = _cardPlayedTraceLast.GetType().GetMethod(nameof(AbstractModel.AfterCardPlayed))!;
        if (lastMethod.DeclaringType != typeof(AbstractModel)) {
            var harmony = new Harmony("sts2.oracle.card-played-perturbation-probe");
            if (!(Harmony.GetPatchInfo(lastMethod)?.Owners.Contains(harmony.Id) ?? false))
                harmony.Patch(lastMethod, postfix: new HarmonyMethod(typeof(RunSimulator).GetMethod(nameof(PerturbCardPlayed))!));
        }
        _cardPlayedTraceRemovedCard = _runState!.Players[0].PlayerCombatState!.Hand.Cards[command.GetProperty("card_index").GetInt32()];
        _cardPlayedTraceRemovedPower = state.Enemies.Single().Powers.Single();
        var play = CombatManager.Instance.History.Entries.OfType<CardPlayFinishedEntry>().Last().CardPlay;
        _cardPlayedOrdinary.Clear(); _cardPlayedLate.Clear(); _cardPlayedTraceSession = this;
        try {
            BeginRunOperation("scenario_card_played_probe", null,
                () => Hook.AfterCardPlayed(state, new ThrowingPlayerChoiceContext(), play));
            AwaitRunBoundary();
        } finally { _cardPlayedTraceSession = null; }
        RegisterCombatIdentities();
        var reply = OracleRunResponse(command);
        reply["dispatch_trace"] = new { ordinary = _cardPlayedOrdinary.ToArray(), late = _cardPlayedLate.ToArray() };
        return reply;
    }
}
