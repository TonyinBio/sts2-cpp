// Read-only boundary probe plus an isolated cost-modifier example; injected into a disposable CLI copy.
using System.Reflection;
using System.Text.Json;
using MegaCrit.Sts2.Core.Combat;
using MegaCrit.Sts2.Core.Entities.Cards;
using MegaCrit.Sts2.Core.Models;
using MegaCrit.Sts2.Core.GameActions;
using MegaCrit.Sts2.Core.Runs;
namespace Sts2Headless;
public partial class RunSimulator
{
    public Dictionary<string, object?> AuditAddSelectionCard()
    {
        var player = _runState!.Players[0];
        var state = CombatManager.Instance.DebugOnlyGetState()!;
        var card = state.CreateCard<MegaCrit.Sts2.Core.Models.Cards.Survivor>(player);
        player.PlayerCombatState!.Hand.AddInternal(card);
        return new() { ["type"] = "audit_selection_setup", ["card_index"] = player.PlayerCombatState.Hand.Cards.Count - 1 };
    }
    private static object? Field(object owner, string name)
    {
        var field = owner.GetType().GetField(name, BindingFlags.Instance | BindingFlags.NonPublic | BindingFlags.Public)
            ?? throw new InvalidOperationException($"Missing audited field {owner.GetType().FullName}.{name}");
        return field.GetValue(owner);
    }
    public object AuditCombatFields()
    {
        var run = _runState!;
        var player = run.Players[0];
        var state = CombatManager.Instance.DebugOnlyGetState()!;
        var pcs = player.PlayerCombatState!;
        object Cost(CardModel card) => new {
            base_cost = Field(card.EnergyCost, "_base"), captured_x = Field(card.EnergyCost, "_capturedXValue"),
            modifiers = ((System.Collections.IEnumerable)Field(card.EnergyCost, "_localModifiers")!).Cast<LocalCostModifier>().Select(m => new { m.Amount, m.Type, m.Expiration, m.IsReduceOnly }).ToArray()
        };
        var standalone = ModelDb.Card<MegaCrit.Sts2.Core.Models.Cards.Bash>().ToMutable();
        standalone.EnergyCost.SetThisTurnOrUntilPlayed(0, true);
        var beforeCleanup = Cost(standalone);
        standalone.EnergyCost.EndOfTurnCleanup();
        return new {
            type = "combat_field_audit", round = state.RoundNumber, turn = pcs.TurnNumber,
            phase = pcs.Phase.ToString(), native_player = player.ToSerializable(),
            block = player.Creature.Block, energy = pcs.Energy,
            pending_selection = _cardSelector.HasPending,
            pending_min = _cardSelector.HasPending ? _cardSelector.PendingMinSelect : 0,
            pending_max = _cardSelector.HasPending ? _cardSelector.PendingMaxSelect : 0,
            pending_cards = _cardSelector.HasPending ? _cardSelector.PendingOptions!.Select(c => c.Id.ToString()).ToArray() : Array.Empty<string>(),
            action_state = RunManager.Instance.ActionExecutor.CurrentlyRunningAction?.State.ToString(),
            started_plays = CombatManager.Instance.History.CardPlaysStarted.Select(e => new {
                card = e.CardPlay.Card.Id.ToString(), result_pile = e.CardPlay.ResultPile.ToString(),
                play_index = e.CardPlay.PlayIndex, play_count = e.CardPlay.PlayCount, autoplay = e.CardPlay.IsAutoPlay,
                energy_spent = e.CardPlay.Resources.EnergySpent, energy_value = e.CardPlay.Resources.EnergyValue,
                stars_spent = e.CardPlay.Resources.StarsSpent, star_value = e.CardPlay.Resources.StarValue
            }).ToArray(),
            cards = pcs.AllCards.Select(c => new { id = c.Id.ToString(),
                has_distinct_deck_version = c.DeckVersion != null && !ReferenceEquals(c, c.DeckVersion),
                deck_index = c.DeckVersion == null ? -1 : player.Deck.Cards.ToList().IndexOf(c.DeckVersion),
                pile = c.Pile!.Type.ToString(), cost = Cost(c), saved = c.ToSerializable() }).ToArray(),
            monsters = state.Enemies.Select(c => new { id = c.ModelId.ToString(), combat_id = c.CombatId,
                monster_rng = c.Monster!.Rng.ToSerializable(), same_rng_as_run_monster_ai = ReferenceEquals(c.Monster.Rng, run.Rng.MonsterAi) }).ToArray(),
            history_entry_types = CombatManager.Instance.History.Entries.Select(e => e.GetType().Name).ToArray(),
            cost_example = new { before_cleanup = beforeCleanup, after_cleanup = Cost(standalone) }
        };
    }
}
