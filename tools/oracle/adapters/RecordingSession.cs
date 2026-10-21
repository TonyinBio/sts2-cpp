// Compiled only into the recorder's disposable sts2-cli build.
using System.Diagnostics;
using System.Collections;
using MegaCrit.Sts2.Core.Commands.Builders;
using MegaCrit.Sts2.Core.Localization.DynamicVars;
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
        PatchRecordingDebugAudio();
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
        Require(_oracleRunMode || (!_cardSelector.HasPending && !pcs.PlayPile.Cards.Any()), "Use the run recording backend for combat choices");
        var native = NativeRunRoundTrip.Capture(RunManager.Instance.ToSave(null))!.AsObject();
        string PlayerRef() => "/players/0";
        var creatures = new Dictionary<string, object>();
        var cards = new Dictionary<string, object>();
        var powers = new Dictionary<string, object>();
        var plays = new Dictionary<string, object>();
        var orbs = new Dictionary<string, object>();
        var afflictions = new Dictionary<string, object>();
        var attacks = new Dictionary<string, object>();
        var retained = new Dictionary<string, object>();
        var nativeModels = new Dictionary<string, object>();
        var modelQueue = new List<AbstractModel>();
        var attackQueue = new List<AttackCommand>();
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
                    if (!_oracleIds.ContainsKey(c)) {
                        if (c.Monster != null && c.Side == CombatSide.Enemy) {
                            _oracleIds.Add(c, $"enemy_{_oracleEnemies.Count}"); _oracleEnemies.Add(c);
                        } else _oracleIds.Add(c, $"ally_{creatureQueue.Count}");
                    }
                    name = $"/combat/creatures/{Id(c)}"; creatureQueue.Add(c); break;
                case CardModel c:
                    if (!_oracleIds.ContainsKey(c)) { _oracleIds.Add(c, $"card_{_oracleCards.Count}"); _oracleCards.Add(c); }
                    name = $"/combat/cards/{Id(c)}"; cardQueue.Add(c); break;
                case PowerModel p:
                    name = $"/combat/powers/{powerQueue.Count}"; powerQueue.Add(p); break;
                case MonsterModel m: return Ref(m.Creature);
                case OrbModel m:
                    name = $"/combat/orbs/{modelQueue.Count}"; modelQueue.Add(m); break;
                case AfflictionModel m:
                    name = $"/combat/afflictions/{modelQueue.Count}"; modelQueue.Add(m); break;
                case RelicModel m:
                    name = $"/combat/retained_inventory_models/{modelQueue.Count}"; modelQueue.Add(m); break;
                case PotionModel m:
                    name = $"/combat/retained_inventory_models/{modelQueue.Count}"; modelQueue.Add(m); break;
                case CalculatedDamageVar variable:
                    Require(variable.GetType() == typeof(CalculatedDamageVar), "Custom calculated variable requires an explicit handler");
                    var owner = Field<AbstractModel?>(variable, "_owner") as CardModel;
                    Require(owner != null && owner.DynamicVars.TryGetValue(variable.Name, out var stored) && ReferenceEquals(stored, variable),
                        "Calculated variable is not owned by an exported card");
                    var calculation = Field<Delegate?>(variable, "_multiplierCalc");
                    Require(calculation != null && (calculation.Target == null ||
                        (calculation.Target.GetType().IsDefined(typeof(System.Runtime.CompilerServices.CompilerGeneratedAttribute), false) &&
                        calculation.Target.GetType().GetFields(BindingFlags.Instance | BindingFlags.NonPublic | BindingFlags.Public).Length == 0)),
                        "Calculated attack callback captures storage; explicit handler required");
                    var ownerPath = Ref(owner)!;
                    name = (ownerPath.StartsWith("/combat/") ? ownerPath : "/native_model_state/" + ownerPath.Replace("~", "~0").Replace("/", "~1"))
                        + "/dynamic_vars/" + variable.Name.Replace("~", "~0").Replace("/", "~1");
                    refs.Add(instance, name); return name;
                case MonsterState state:
                    throw new InvalidOperationException($"AI state {state.Id} is not registered in an exported machine; explicit AI handler required");
                case AttackCommand a:
                    name = $"/combat/attacks/{attackQueue.Count}"; attackQueue.Add(a); break;
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
        Dictionary<string, object?> Fields(AbstractModel model) => CombatModelFields.Capture(model, Ref);
        void RegisterNative(AbstractModel model, string path) { refs.Add(model, path); modelQueue.Add(model); }
        for (int i = 0; i < player.Deck.Cards.Count; ++i) RegisterNative(player.Deck.Cards[i], $"/players/0/deck/{i}");
        for (int i = 0; i < player.Relics.Count; ++i) RegisterNative(player.Relics[i], $"/players/0/relics/{i}");
        for (int i = 0; i < player.Potions.Count(); ++i)
            RegisterNative(player.Potions.ElementAt(i), $"/players/0/potions/{i}");
        for (int i = 0; i < run.Modifiers.Count; ++i) RegisterNative(run.Modifiers[i], $"/modifiers/{i}");
        // Character data is definition-only in the audited build; still check for unclassified storage.
        Require(Fields(player.Character).Count == 0, "Character runtime state requires an explicit schema location");
        var encounterFields = Fields(combat!.Encounter!);
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
                case "CardAfflictedEntry":
                    item["card"] = Ref(Member(entry, "Card")); item["affliction"] = Ref(Member(entry, "Affliction")); break;
                case "OrbChanneledEntry": item["orb"] = Ref(Member(entry, "Orb")); break;
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
        var activePlays = manager.History.Entries.Where(e => e.GetType().Name == "CardPlayStartedEntry")
            .Select(e => (CardPlay)Member(e, "CardPlay")!)
            .Where(p => !manager.History.Entries.Any(e => e.GetType().Name == "CardPlayFinishedEntry" && ReferenceEquals(Member(e, "CardPlay"), p))).ToArray();
        var active = activePlays.Select(p => new { card_play = Ref(p), result_location = new {
            player = Ref(p.Player), pile_type = p.ResultPile.ToString(), position = "Bottom" } }).ToArray();
        object? pendingChoice = null;
        if (_oracleRunMode && (_runSelector.Pending != null || _pendingBundles != null)) {
            var pending = _runSelector.Pending;
            var sources = activePlays.Select(p => Ref(p.Card)).Concat(_runActionOwner?.Source is { } source ? new[] { Ref(source) } : Array.Empty<string>()).Distinct().ToArray();
            if (pending?.Kind == "card_select") pendingChoice = new { player = PlayerRef(), sources,
                candidates = pending.Cards.Select(c => Ref(c)).ToArray(), min_select = pending.Prefs.MinSelect,
                max_select = Math.Min(pending.Prefs.MaxSelect, pending.Cards.Count),
                require_manual_confirmation = pending.Prefs.RequireManualConfirmation, cancelable = pending.Prefs.Cancelable };
            else pendingChoice = new { kind = pending?.Kind ?? "bundle_select", player = PlayerRef(), sources,
                candidates = pending?.Cards.Select(c => Ref(c)).ToArray() ?? Array.Empty<string>(),
                bundles = _pendingBundles?.Select(b => b.Select(c => Ref(c)).ToArray()).ToArray() ?? Array.Empty<string[]>(),
                alternatives = pending?.Alternatives.Select(a => a.GetType().Name).ToArray() ?? Array.Empty<string>(),
                can_skip = pending?.Alternatives.Any(a => a.GetType().Name.Contains("Skip")) ?? false };
        }
        var allies = combat!.Allies.Select(c => Ref(c)).ToArray();
        var enemies = combat.Enemies.Select(c => Ref(c)).ToArray();
        var escaped = combat.EscapedCreatures.Select(c => Ref(c)).ToArray();
        var pc = new { player = PlayerRef(), creature = Ref(player.Creature), turn_number = pcs.TurnNumber,
            phase = pcs.Phase.ToString(), energy = pcs.Energy, stars = pcs.Stars,
            can_use_or_remove_potions = player.CanUseOrRemovePotions, is_active_for_hooks = player.IsActiveForHooks,
            piles = pcs.AllPiles.ToDictionary(p => p.Type.ToString(), p => p.Cards.Select(c => Ref(c)).ToArray()),
            pets = pcs.Pets.Select(c => Ref(c)).ToArray(), orb_capacity = pcs.OrbQueue.Capacity, orbs = pcs.OrbQueue.Orbs.Select(o => Ref(o)).ToArray() };
        // References can retain cards, removed powers and dead creatures through history.
        // Drain the queues to closure instead of exporting only current pile membership.
        int ci = 0, pi = 0, ai = 0, mi = 0, ti = 0;
        while (ci < creatureQueue.Count || pi < powerQueue.Count || ai < cardQueue.Count || mi < modelQueue.Count || ti < attackQueue.Count)
        {
            while (ci < creatureQueue.Count)
            {
                var creature = creatureQueue[ci++];
                object? monster = null;
                if (creature.Monster != null)
                {
                    var m = creature.Monster;

                    var machine = m.MoveStateMachine ?? throw new InvalidOperationException($"Missing monster AI: {m.Id}");
                    foreach (var entry in machine.States) if (entry.Value is MoveState) {
                        Require(entry.Value.GetType() == typeof(MoveState), "Custom move-state storage requires an explicit AI handler");
                        refs.TryAdd(entry.Value, Ref(creature) + "/monster/ai/move_states/" + entry.Key.Replace("~", "~0").Replace("/", "~1"));
                    }
                    monster = new { id = m.Id.ToString(), current_hp = creature.CurrentHp, max_hp = creature.MaxHp,
                        spawned_this_turn = m.SpawnedThisTurn, is_performing_move = m.IsPerformingMove, rng = NativeRunRoundTrip.Capture(m.Rng.ToSerializable()),
                        ai = new { current_state = Field<MonsterState>(machine, "_currentState").Id,
                            initial_state = Field<MonsterState>(machine, "_initialState").Id, next_move = m.NextMove?.Id,
                            state_log = machine.StateLog.Select(s => s.Id).ToArray(),
                            performed_first_move = Field<bool>(machine, "_performedFirstMove"),
                            move_states = machine.States.Where(p => p.Value is MoveState).ToDictionary(p => p.Key, p => {
                                var move = (MoveState)p.Value;
                                return new { performed_at_least_once = Field<bool>(move, "_performedAtLeastOnce"),
                                    must_perform_once_before_transitioning = move.MustPerformOnceBeforeTransitioning,
                                    follow_up_state = move.FollowUpState?.Id ?? move.FollowUpStateId }; }) }, model_fields = Fields(m) };
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
                var item = new Dictionary<string, object?> {
                    ["id"] = p.Id.ToString(), ["amount"] = p.Amount, ["amount_on_turn_start"] = p.AmountOnTurnStart,
                    ["skip_next_duration_tick"] = p.SkipNextDurationTick, ["applier"] = Ref(p.Applier),
                    ["target"] = Ref(p.Target), ["dynamic_vars"] = p.DynamicVars
                        .Where(v => v.Value is not MegaCrit.Sts2.Core.Localization.DynamicVars.StringVar)
                        .ToDictionary(v => v.Key, v => v.Value.BaseValue), ["model_fields"] = Fields(p)
                };
                if (!creatureQueue.Any(c => c.Powers.Contains(p))) item["detached_owner"] = Ref(p.Owner);
                powers.Add(Ref(p)!.Split('/').Last(), item);
            }
            while (ai < cardQueue.Count)
            {
                var card = cardQueue[ai++];
                var deckIndex = card.DeckVersion == null ? -1 : player.Deck.Cards.ToList().IndexOf(card.DeckVersion);
                Require(card.DeckVersion == null || deckIndex >= 0, "Unresolved deck version");
                cards.Add(Ref(card)!.Split('/').Last(), new { saved = NativeRunRoundTrip.Capture(card.ToSerializable()),
                    owner = card.Owner == null ? null : PlayerRef(), deck_version = deckIndex < 0 ? null : $"/players/0/deck/{deckIndex}",
                    clone_of = Ref(card.CloneOf), is_dupe = card.IsDupe, base_replay_count = card.BaseReplayCount,
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
                    last_stars_spent = card.LastStarsSpent, affliction = Ref(card.Affliction),
                    enchantment_runtime = card.Enchantment == null ? null : (object)new { status = card.Enchantment.Status.ToString(), model_fields = Fields(card.Enchantment), dynamic_vars = CombatModelFields.DynamicValues(card.Enchantment) }, model_fields = Fields(card) });
            }
            while (mi < modelQueue.Count) {
                var model = modelQueue[mi++]; var path = Ref(model)!;
                var fields = Fields(model);
                if (!path.StartsWith("/combat/")) {
                    nativeModels.Add(path, new { model_fields = fields, dynamic_vars = CombatModelFields.DynamicValues(model) });
                    if (model is CardModel deckCard && deckCard.Enchantment != null) {
                        var enchantment = deckCard.Enchantment;
                        nativeModels.Add(path + "/enchantment", new { model_fields = Fields(enchantment), dynamic_vars = CombatModelFields.DynamicValues(enchantment), enchantment_status = enchantment.Status.ToString() });
                    }
                    continue;
                }
                var key = path.Split('/').Last();
                if (model is OrbModel orb) orbs.Add(key, new { id = orb.Id.ToString(), owner = Ref(orb.Owner),
                    has_been_removed_from_state = orb.HasBeenRemovedFromState, model_fields = fields });
                else if (model is AfflictionModel affliction) {
                    Require(fields.Count == 0, "Affliction model fields require schema coverage");
                    afflictions.Add(key, new { id = affliction.Id.ToString(), amount = affliction.Amount });
                }
                else if (model is RelicModel relic) retained.Add(key, new { kind = "relic", saved = NativeRunRoundTrip.Capture(relic.ToSerializable()),
                    owner = Ref(relic.Owner), has_been_removed_from_state = relic.HasBeenRemovedFromState, model_fields = fields, dynamic_vars = CombatModelFields.DynamicValues(relic) });
                else if (model is PotionModel potion) retained.Add(key, new { kind = "potion", saved = NativeRunRoundTrip.Capture(potion.ToSerializable(-1)),
                    owner = Ref(potion.Owner), has_been_removed_from_state = potion.HasBeenRemovedFromState, model_fields = fields, dynamic_vars = CombatModelFields.DynamicValues(potion) });
                else throw new InvalidOperationException($"Unexported queued model: {model.GetType()}");
            }
            while (ti < attackQueue.Count) {
                var attack = attackQueue[ti++];
                Require(Field<object?>(attack, "_beforeDamage") == null && Field<object?>(attack, "_afterAttackerAnim") == null,
                    "Retained attack callbacks require an explicit suspended-effect handler");
                attacks.Add(Ref(attack)!.Split('/').Last(), new {
                    damage_per_hit = Field<decimal>(attack, "_damagePerHit"), calculated_damage_var = Ref(Field<CalculatedDamageVar?>(attack, "_calculatedDamageVar")),
                    hit_count = Field<int>(attack, "_hitCount"), source_type = Field<object>(attack, "_sourceType").ToString(),
                    single_target = Ref(Field<Creature?>(attack, "_singleTarget")), targets_combat_side = attack.IsMultiTargeted,
                    does_random_targeting_allow_duplicates = Field<bool>(attack, "_doesRandomTargetingAllowDuplicates"),
                    attacker = Ref(attack.Attacker), model_source = Ref(attack.ModelSource), card_play = Ref(attack.CardPlay),
                    target_side = attack.TargetSide.ToString(), damage_props = (int)attack.DamageProps,
                    is_randomly_targeted = attack.IsRandomlyTargeted,
                    results = attack.Results.Select(hit => hit.Select(Damage).ToArray()).ToArray() });
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
            encounter_fields = encounterFields, players = new[] { pc }, creatures, cards, powers, card_plays = plays,
            orbs, afflictions, attacks, retained_inventory_models = retained,
            active_card_plays = active, history };
        // This driver never enters a final victory room or abandons a run.
        // Winning this encounter continues the run; the observed loss path ends it.
        native["boundary_kind"] = _oracleRunEndingReason == null ? "awaiting_input" : "run_complete";
        native["run_ending_reason"] = _oracleRunEndingReason;
        native["current_room"] = NativeRunRoundTrip.Capture(run.CurrentRoom?.ToSerializable());
        native["native_model_state"] = JsonSerializer.SerializeToNode(nativeModels);
        native["combat"] = JsonSerializer.SerializeToNode(runtime);
        native["pending_choice"] = JsonSerializer.SerializeToNode(pendingChoice);
        native["run_flow"] = null;
        return native;
    }
}
