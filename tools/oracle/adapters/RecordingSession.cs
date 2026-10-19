// Compiled only into the recorder's disposable sts2-cli build.
using System.Diagnostics;
using System.Reflection;
using System.Text.Json;
using MegaCrit.Sts2.Core.Combat;
using MegaCrit.Sts2.Core.Entities.Cards;
using MegaCrit.Sts2.Core.Entities.Creatures;
using MegaCrit.Sts2.Core.Models;
using MegaCrit.Sts2.Core.MonsterMoves.MonsterMoveStateMachine;
using MegaCrit.Sts2.Core.Runs;

namespace Sts2Headless;

public partial class RunSimulator
{
    private bool _oracleActive;
    private bool _oracleCombatWon;
    private string? _oracleRunEndingReason;
    private readonly Dictionary<object, string> _oracleIds = new(ReferenceEqualityComparer.Instance);
    private readonly List<CardModel> _oracleCards = new();
    private readonly List<Creature> _oracleEnemies = new();
    private string[] _oracleFeatures = Array.Empty<string>();

    private static void Require(bool condition, string message)
    {
        if (!condition) throw new InvalidOperationException(message);
    }
    private static T Field<T>(object instance, string name)
    {
        for (var type = instance.GetType(); type != null; type = type.BaseType)
        {
            var field = type.GetField(name, BindingFlags.Instance | BindingFlags.NonPublic | BindingFlags.Public | BindingFlags.DeclaredOnly);
            if (field != null) return (T)field.GetValue(instance)!;
        }
        throw new InvalidOperationException($"Required field unavailable: {instance.GetType().Name}.{name}");
    }
    private string Id(object instance) => _oracleIds.TryGetValue(instance, out var id)
        ? id : throw new InvalidOperationException("Unregistered object identity");
    private static void CheckResponse(Dictionary<string, object?> response)
    {
        Require(!Equals(response.GetValueOrDefault("type"), "error"), JsonSerializer.Serialize(response));
    }

    public Dictionary<string, object?> OracleStart(JsonElement command)
    {
        Require(!_oracleActive, "One combat per recording process");
        var setup = command.GetProperty("setup");
        Require(setup.GetProperty("kind").GetString() == "seeded_combat" &&
            setup.GetProperty("character").GetString() == "Ironclad" &&
            setup.GetProperty("ascension").GetInt32() == 0 &&
            setup.GetProperty("encounter").GetString() == "SHRINKER_BEETLE_WEAK",
            "Exporter supports only Ironclad A0 / SHRINKER_BEETLE_WEAK");
        _oracleFeatures = command.GetProperty("required_features").EnumerateArray().Select(v => v.GetString()!).ToArray();
        Require(_oracleFeatures.SequenceEqual(new[] { "basic_combat", "ironclad_starter_shrinker_v1" }),
            "Required features must be basic_combat, ironclad_starter_shrinker_v1 in that order");
        _oracleActive = true;
        // Observe the actual ending path. Both victory and loss can leave dead
        // players, so the outcome must not be inferred from exported HP.
        CombatManager.Instance.CombatWon += _ => _oracleCombatWon = true;
        CombatManager.Instance.CombatEnded += _ => {
            if (!_oracleCombatWon) _oracleRunEndingReason = "death";
        };
        CheckResponse(StartRun("Ironclad", 0, setup.GetProperty("seed").GetString(), "en"));
        CheckResponse(EnterRoom("combat", "SHRINKER_BEETLE_WEAK", null));
        var player = _runState!.Players[0];
        _oracleIds.Add(player.Creature, "player_0");
        foreach (var creature in CombatManager.Instance.DebugOnlyGetState()!.Enemies)
        {
            Require(creature.Monster?.Id.Entry == "SHRINKER_BEETLE", "Unsupported enemy");
            _oracleIds.Add(creature, $"enemy_{_oracleEnemies.Count}");
            _oracleEnemies.Add(creature);
        }
        Require(_oracleEnemies.Count == 1, "Expected exactly one enemy");
        foreach (var pile in Piles())
            foreach (var card in pile.Value.Cards)
            {
                _oracleIds.Add(card, $"card_{_oracleCards.Count}");
                _oracleCards.Add(card);
            }
        Require(_oracleCards.Count == 10, "Unexpected starter deck");
        for (int i = 0; i < player.Relics.Count; ++i)
            _oracleIds.Add(player.Relics[i], $"relic_{i}");
        return OracleResponse(command);
    }

    private Dictionary<string, CardPile> Piles()
    {
        var pcs = _runState!.Players[0].PlayerCombatState!;
        return new() { ["hand"] = pcs.Hand, ["draw"] = pcs.DrawPile,
            ["discard"] = pcs.DiscardPile, ["exhaust"] = pcs.ExhaustPile, ["playing"] = pcs.PlayPile };
    }

    private void AwaitBoundary()
    {
        var watch = Stopwatch.StartNew();
        while (watch.ElapsedMilliseconds < 5000)
        {
            _syncCtx.Pump();
            var manager = CombatManager.Instance;
            var combat = manager.DebugOnlyGetState()!;
            var executor = RunManager.Instance.ActionExecutor;
            Require(!_cardSelector.HasPendingReward && _pendingBundles == null, "Unsupported reward/bundle boundary");
            if (_cardSelector.HasPending) throw new InvalidOperationException("Card selection is outside this exporter profile");
            var pcs = _runState!.Players[0].PlayerCombatState!;
            if (!executor.IsRunning && executor.CurrentlyRunningAction == null &&
                !_oracleEnemies.Any(e => e.Monster!.IsPerformingMove) &&
                (!manager.IsInProgress || (pcs.Phase == PlayerTurnPhase.Play && combat.CurrentSide == CombatSide.Player)))
                return;
            Thread.Sleep(1);
        }
        throw new TimeoutException("Combat did not reach a supported settled boundary");
    }

    public Dictionary<string, object?> OracleAction(JsonElement command)
    {
        Require(_oracleActive, "No recording session");
        AwaitBoundary();
        Require(CombatManager.Instance.IsInProgress, "Action after combat end");
        var action = command.GetProperty("action");
        var kind = action.GetProperty("kind").GetString();
        var player = _runState!.Players[0];
        Dictionary<string, object?> response;
        if (kind == "end_turn")
        {
            var before = player.PlayerCombatState!.TurnNumber;
            response = ExecuteAction("end_turn", null);
            CheckResponse(response);
            AwaitBoundary();
            Require(!CombatManager.Instance.IsInProgress || player.PlayerCombatState.TurnNumber > before,
                "End turn failed to advance combat");
        }
        else if (kind == "play_card")
        {
            var hand = player.PlayerCombatState!.Hand.Cards;
            var id = action.GetProperty("card_instance").GetString();
            var index = hand.ToList().FindIndex(c => Id(c) == id);
            Require(index >= 0, $"Card {id} is not in hand");
            var card = hand[index];
            var args = new Dictionary<string, object?> { ["card_index"] = index };
            if (card.TargetType == TargetType.AnyEnemy)
            {
                Require(action.TryGetProperty("target", out var target), "Target is required");
                var alive = CombatManager.Instance.DebugOnlyGetState()!.Enemies.Where(e => e.IsAlive).ToList();
                var targetIndex = alive.FindIndex(e => Id(e) == target.GetString());
                Require(targetIndex >= 0, "Target is not a living enemy");
                args["target_index"] = targetIndex;
            }
            else Require(!action.TryGetProperty("target", out _), "This card does not accept a target");
            response = ExecuteAction("play_card", args);
            CheckResponse(response);
        }
        else throw new InvalidOperationException($"Unsupported action in this exporter profile: {kind}");
        return OracleResponse(command);
    }

    private Dictionary<string, object?> OracleResponse(JsonElement command)
    {
        AwaitBoundary();
        return new() { ["type"] = "oracle_state", ["request_id"] = command.GetProperty("request_id").GetString(),
            ["assembly_mvid"] = typeof(CardModel).Assembly.ManifestModule.ModuleVersionId.ToString(),
            ["runtime_version"] = Environment.Version.ToString(), ["state"] = ExportRun() };
    }

    private static object? Member(object instance, string name)
    {
        for (var type = instance.GetType(); type != null; type = type.BaseType)
        {
            var property = type.GetProperty(name, BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.DeclaredOnly);
            if (property != null) return property.GetValue(instance);
        }
        throw new InvalidOperationException($"Required member unavailable: {instance.GetType().Name}.{name}");
    }

    private object ExportRun()
    {
        var run = _runState!;
        var player = run.Players.Single();
        var pcs = player.PlayerCombatState!;
        var manager = CombatManager.Instance;
        var combat = manager.DebugOnlyGetState();
        Require(combat != null, "No retained combat state; this exporter supports combat rooms only");
        Require(!_cardSelector.HasPending && !pcs.PlayPile.Cards.Any(), "Suspended effects are outside this exporter scope");
        Require(!player.Potions.Any() && !pcs.Pets.Any() && !pcs.OrbQueue.Orbs.Any() && !combat!.Modifiers.Any(), "Unsupported inventory, pets, or modifiers");
        Require(player.Relics.Count == 1 && player.Relics[0].Id.Entry == "BURNING_BLOOD", "Unsupported relics");
        var native = NativeRunRoundTrip.Capture(RunManager.Instance.ToSave(null))!.AsObject();
        string PlayerRef() => "/players/0";
        var creatures = new Dictionary<string, object>();
        var cards = new Dictionary<string, object>();
        var powers = new Dictionary<string, object>();
        var plays = new Dictionary<string, object>();
        var refs = new Dictionary<object, string>(ReferenceEqualityComparer.Instance) { [player] = PlayerRef() };
        var creatureQueue = new List<Creature>();
        var cardQueue = new List<CardModel>();
        var powerQueue = new List<PowerModel>();
        string? Ref(object? instance)
        {
            if (instance == null) return null;
            if (refs.TryGetValue(instance, out var found)) return found;
            string name;
            switch (instance)
            {
                case Creature c:
                    name = $"/combat/creatures/{creatureQueue.Count}"; creatureQueue.Add(c); break;
                case CardModel c:
                    Require(_oracleIds.ContainsKey(c), "Unsupported generated card");
                    name = $"/combat/cards/{Id(c)}"; cardQueue.Add(c); break;
                case PowerModel p:
                    name = $"/combat/powers/{powerQueue.Count}"; powerQueue.Add(p); break;
                case CardPlay p:
                    name = $"/combat/card_plays/{plays.Count}";
                    refs.Add(instance, name);
                    plays.Add(name.Split('/').Last(), new {
                        card = Ref(p.Card), player = Ref(p.Player), target = Ref(p.Target),
                        result_pile = p.ResultPile.ToString(), resources = new {
                            energy_spent = p.Resources.EnergySpent, energy_value = p.Resources.EnergyValue,
                            stars_spent = p.Resources.StarsSpent, star_value = p.Resources.StarValue },
                        is_auto_play = p.IsAutoPlay, play_index = p.PlayIndex, play_count = p.PlayCount });
                    return name;
                default: throw new InvalidOperationException($"Unsupported reference kind {instance.GetType().Name}");
            }
            refs.Add(instance, name);
            return name;
        }
        object Damage(DamageResult d) => new { receiver = Ref(d.Receiver), props = (int)d.Props,
            blocked_damage = d.BlockedDamage, unblocked_damage = d.UnblockedDamage,
            overkill_damage = d.OverkillDamage, was_block_broken = d.WasBlockBroken,
            was_fully_blocked = d.WasFullyBlocked, was_target_killed = d.WasTargetKilled };
        var history = new List<object>();
        foreach (var entry in manager.History.Entries)
        {
            var kind = entry.GetType().Name;
            // These diagnostic entries have no gameplay consumers in the audited build.
            if (kind is "MonsterPerformedMoveEntry" or "SummonedEntry" or "PotionUsedEntry") continue;
            var item = new Dictionary<string, object?> {
                ["kind"] = kind, ["actor"] = Ref(entry.Actor),
                ["round_number"] = Member(entry, "RoundNumber"),
                ["current_side"] = Member(entry, "CurrentSide")!.ToString(),
                ["player_turn_numbers"] = Field<Dictionary<ulong, int>>(entry, "_playerTurnNumbers")
                    .Select(pair => { Require(pair.Key == player.NetId, "Unknown historical player"); return new { player = PlayerRef(), turn_number = pair.Value }; }).ToArray()
            };
            switch (kind)
            {
                case "CardPlayStartedEntry": case "CardPlayFinishedEntry":
                    item["card_play"] = Ref(Member(entry, "CardPlay"));
                    if (kind == "CardPlayFinishedEntry") item["was_ethereal"] = Member(entry, "WasEthereal");
                    break;
                case "CardDrawnEntry": case "CardDiscardedEntry": case "CardExhaustedEntry": case "CardGeneratedEntry":
                    item["card"] = Ref(Member(entry, "Card"));
                    if (kind == "CardDrawnEntry") item["from_hand_draw"] = Member(entry, "FromHandDraw");
                    if (kind == "CardGeneratedEntry") item["creator"] = Ref(Member(entry, "Creator"));
                    break;
                case "EnergySpentEntry": case "StarsModifiedEntry": item["amount"] = Member(entry, "Amount"); break;
                case "BlockGainedEntry":
                    item["amount"] = Member(entry, "Amount"); item["props"] = Convert.ToInt32(Member(entry, "Props"));
                    item["card_play"] = Ref(Member(entry, "CardPlay")); break;
                case "CreatureAttackedEntry":
                    item["damage_results"] = ((IEnumerable<DamageResult>)Member(entry, "DamageResults")!).Select(Damage).ToArray(); break;
                case "DamageReceivedEntry":
                    item["result"] = Damage((DamageResult)Member(entry, "Result")!);
                    item["dealer"] = Ref(Member(entry, "Dealer")); item["card_source"] = Ref(Member(entry, "CardSource")); break;
                case "PowerReceivedEntry":
                    item["power"] = Ref(Member(entry, "Power")); item["amount"] = Member(entry, "Amount");
                    item["applier"] = Ref(Member(entry, "Applier")); break;
                default: throw new InvalidOperationException($"Unsupported gameplay history {kind}");
            }
            history.Add(item);
        }
        var allies = combat!.Allies.Select(c => Ref(c)).ToArray();
        var enemies = combat.Enemies.Select(c => Ref(c)).ToArray();
        var escaped = combat.EscapedCreatures.Select(c => Ref(c)).ToArray();
        var pc = new { player = PlayerRef(), creature = Ref(player.Creature), turn_number = pcs.TurnNumber,
            phase = pcs.Phase.ToString(), energy = pcs.Energy, stars = pcs.Stars,
            can_use_or_remove_potions = player.CanUseOrRemovePotions, is_active_for_hooks = player.IsActiveForHooks,
            piles = pcs.AllPiles.ToDictionary(p => p.Type.ToString(), p => p.Cards.Select(c => Ref(c)).ToArray()),
            pets = Array.Empty<string>(), orb_capacity = pcs.OrbQueue.Capacity, orbs = Array.Empty<string>() };
        // References can retain cards, removed powers and dead creatures through history.
        // Drain the queues to closure instead of exporting only current pile membership.
        int ci = 0, pi = 0, ai = 0;
        while (ci < creatureQueue.Count || pi < powerQueue.Count || ai < cardQueue.Count)
        {
            while (ci < creatureQueue.Count)
            {
                var creature = creatureQueue[ci++];
                object? monster = null;
                if (creature.Monster != null)
                {
                    var m = creature.Monster;
                    Require(m.Id.Entry == "SHRINKER_BEETLE", "Unsupported monster");
                    var machine = m.MoveStateMachine!;
                    monster = new { id = m.Id.ToString(), current_hp = creature.CurrentHp, max_hp = creature.MaxHp,
                        spawned_this_turn = m.SpawnedThisTurn, is_performing_move = m.IsPerformingMove,
                        ai = new { current_state = Field<MonsterState>(machine, "_currentState").Id,
                            initial_state = Field<MonsterState>(machine, "_initialState").Id, next_move = m.NextMove?.Id,
                            state_log = machine.StateLog.Select(s => s.Id).ToArray(),
                            performed_first_move = Field<bool>(machine, "_performedFirstMove"),
                            move_states = machine.States.Where(p => p.Value is MoveState).ToDictionary(p => p.Key, p => {
                                var move = (MoveState)p.Value;
                                return new { performed_at_least_once = Field<bool>(move, "_performedAtLeastOnce"),
                                    must_perform_once_before_transitioning = move.MustPerformOnceBeforeTransitioning,
                                    follow_up_state = move.FollowUpState?.Id }; }) }, model_fields = new { } };
                }
                creatures.Add(Ref(creature)!.Split('/').Last(), new { player = creature.IsPlayer ? PlayerRef() : null,
                    monster, side = creature.Side.ToString(), block = creature.Block,
                    hp_is_infinite = creature.HpDisplay.IsInfinite(),
                    slot_name = creature.SlotName, pet_owner = Ref(creature.PetOwner),
                    powers = creature.Powers.Select(p => Ref(p)).ToArray() });
            }
            while (pi < powerQueue.Count)
            {
                var p = powerQueue[pi++];
                Require(p.Id.Entry is "SHRINK_POWER" or "VULNERABLE_POWER", "Unsupported power");
                var item = new Dictionary<string, object?> {
                    ["id"] = p.Id.ToString(), ["amount"] = p.Amount, ["amount_on_turn_start"] = p.AmountOnTurnStart,
                    ["skip_next_duration_tick"] = p.SkipNextDurationTick, ["applier"] = Ref(p.Applier),
                    ["target"] = Ref(p.Target), ["dynamic_vars"] = p.DynamicVars
                        .Where(v => v.Value is not MegaCrit.Sts2.Core.Localization.DynamicVars.StringVar)
                        .ToDictionary(v => v.Key, v => v.Value.BaseValue), ["model_fields"] = new { }
                };
                if (!creatureQueue.Any(c => c.Powers.Contains(p))) item["detached_owner"] = Ref(p.Owner);
                powers.Add(Ref(p)!.Split('/').Last(), item);
            }
            while (ai < cardQueue.Count)
            {
                var card = cardQueue[ai++];
                Require(card.Id.Entry is "STRIKE_IRONCLAD" or "DEFEND_IRONCLAD" or "BASH", "Unsupported card");
                Require(card.Enchantment == null && card.Affliction == null && card.CloneOf == null, "Unsupported card attachment/clone");
                var deckIndex = card.DeckVersion == null ? -1 : player.Deck.Cards.ToList().IndexOf(card.DeckVersion);
                Require(card.DeckVersion == null || deckIndex >= 0, "Unresolved deck version");
                cards.Add(Ref(card)!.Split('/').Last(), new { saved = NativeRunRoundTrip.Capture(card.ToSerializable()),
                    owner = card.Owner == null ? null : PlayerRef(), deck_version = deckIndex < 0 ? null : $"/players/0/deck/{deckIndex}",
                    clone_of = (string?)null, is_dupe = card.IsDupe, base_replay_count = card.BaseReplayCount,
                    local_keywords = (Field<HashSet<CardKeyword>?>(card, "_keywords") ?? card.CanonicalKeywords.ToHashSet()).Select(k => k.ToString()).ToArray(),
                    exhaust_on_next_play = card.ExhaustOnNextPlay,
                    has_single_turn_retain = Field<bool>(card, "_hasSingleTurnRetain"),
                    has_single_turn_sly = Field<bool>(card, "_hasSingleTurnSly"),
                    has_been_removed_from_state = card.HasBeenRemovedFromState,
                    dynamic_vars = card.DynamicVars.ToDictionary(v => v.Key, v => v.Value.BaseValue),
                    energy_cost = new { @base = Field<int>(card.EnergyCost, "_base"),
                        captured_x_value = Field<int>(card.EnergyCost, "_capturedXValue"),
                        local_modifiers = Field<List<LocalCostModifier>>(card.EnergyCost, "_localModifiers").Select(m => new {
                            amount = m.Amount, type = m.Type.ToString(), expiration = (int)m.Expiration, is_reduce_only = m.IsReduceOnly }).ToArray() },
                    base_star_cost = card.BaseStarCost,
                    temporary_star_costs = Field<List<TemporaryCardCost>>(card, "_temporaryStarCosts").Select(c => new {
                        cost = c.Cost, clears_when_turn_ends = c.ClearsWhenTurnEnds, clears_when_card_is_played = c.ClearsWhenCardIsPlayed }).ToArray(),
                    last_stars_spent = card.LastStarsSpent, affliction = (string?)null,
                    enchantment_runtime = (object?)null, model_fields = new { } });
            }
        }
        var turn = Field<object>(manager, "_turnState");
        object Ready(string name) => ((System.Collections.IEnumerable)Member(turn, name)!).Cast<object>().Select(Ref).ToArray();
        var runtime = new { round_number = combat.RoundNumber, current_side = combat.CurrentSide.ToString(),
            turn_control = new { is_in_progress = manager.IsInProgress, is_starting = manager.IsStarting,
                is_enemy_turn_started = manager.IsEnemyTurnStarted, ending_player_turn_phase_one = manager.EndingPlayerTurnPhaseOne,
                ending_player_turn_phase_two = manager.EndingPlayerTurnPhaseTwo, has_pending_loss = manager.IsAboutToLose,
                player_actions_disabled = manager.PlayerActionsDisabled,
                card_or_potion_effect_depth = Field<Dictionary<MegaCrit.Sts2.Core.Entities.Players.Player, int>>(manager, "_cardOrPotionEffectDepth").GetValueOrDefault(player),
                players_taking_extra_turn = Ready("PlayersTakingExtraTurn"), players_ready_to_end_turn = Ready("PlayersReadyToEndTurn"),
                players_ready_to_begin_enemy_turn = Ready("PlayersReadyToBeginEnemyTurn") },
            allies, enemies, escaped_creatures = escaped,
            spawned_enemy_ids = combat.Encounter!.SpawnedEnemies.Select(m => m.Id.ToString()).Distinct().ToArray(),
            encounter_fields = new { }, players = new[] { pc }, creatures, cards, powers, card_plays = plays,
            orbs = new { }, afflictions = new { }, attacks = new { }, retained_inventory_models = new { },
            active_card_plays = Array.Empty<object>(), history };
        // This driver never enters a final victory room or abandons a run.
        // Winning this encounter continues the run; the observed loss path ends it.
        native["boundary_kind"] = _oracleRunEndingReason == null ? "awaiting_input" : "run_complete";
        native["run_ending_reason"] = _oracleRunEndingReason;
        native["current_room"] = NativeRunRoundTrip.Capture(run.CurrentRoom?.ToSerializable());
        native["native_model_state"] = new System.Text.Json.Nodes.JsonObject();
        native["combat"] = JsonSerializer.SerializeToNode(runtime);
        native["pending_choice"] = null;
        return native;
    }
}
