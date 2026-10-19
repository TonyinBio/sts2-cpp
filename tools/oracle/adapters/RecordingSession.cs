// Compiled only into the recorder's disposable sts2-cli build.
using System.Diagnostics;
using System.Globalization;
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
    private readonly Dictionary<object, string> _oracleIds = new(ReferenceEqualityComparer.Instance);
    private readonly List<CardModel> _oracleCards = new();
    private readonly List<Creature> _oracleEnemies = new();
    private readonly List<MonsterMoveStateMachine> _oracleMachines = new();
    private string[] _oracleFeatures = Array.Empty<string>();
    private int _oraclePowerId;

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
        CheckResponse(StartRun("Ironclad", 0, setup.GetProperty("seed").GetString(), "en"));
        CheckResponse(EnterRoom("combat", "SHRINKER_BEETLE_WEAK", null));
        var player = _runState!.Players[0];
        _oracleIds.Add(player.Creature, "player_0");
        foreach (var creature in CombatManager.Instance.DebugOnlyGetState()!.Enemies)
        {
            Require(creature.Monster?.Id.Entry == "SHRINKER_BEETLE", "Unsupported enemy");
            _oracleIds.Add(creature, $"enemy_{_oracleEnemies.Count}");
            _oracleEnemies.Add(creature);
            _oracleMachines.Add(creature.Monster!.MoveStateMachine!);
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
            ["runtime_version"] = Environment.Version.ToString(), ["state"] = ExportCombat() };
    }

    private object[] Powers(Creature creature)
    {
        return creature.Powers.Select(power =>
        {
            Require(power.Id.Entry is "SHRINK_POWER" or "VULNERABLE_POWER", $"Unsupported power: {power.Id}");
            if (!_oracleIds.ContainsKey(power)) _oracleIds.Add(power, $"power_{_oraclePowerId++}");
            var counters = new Dictionary<string, long> {
                ["amount_on_turn_start"] = power.AmountOnTurnStart,
                ["skip_next_duration_tick"] = power.SkipNextDurationTick ? 1 : 0,
                ["applier_actor"] = power.Applier == null ? -1 : Id(power.Applier) == "player_0" ? 0 : 1,
            };
            if (power.Id.Entry == "SHRINK_POWER")
            {
                Require(power.DynamicVars["DamageDecrease"].BaseValue == 30m, "Unexpected shrink multiplier");
                counters["damage_decrease_percent"] = 30;
            }
            else Require(power.DynamicVars["DamageIncrease"].BaseValue == 1.5m, "Unexpected vulnerable multiplier");
            return (object)new { instance_id = Id(power), model_id = power.Id.ToString(), amount = power.Amount, counters };
        }).ToArray();
    }

    private object RngStreams(object set)
    {
        var element = JsonSerializer.SerializeToElement(set, new JsonSerializerOptions { IncludeFields = true });
        return element.GetProperty("rngs").EnumerateObject().ToDictionary(p => p.Name, p => new {
            counter = p.Value.GetProperty("counter").GetInt64(),
            s0 = p.Value.GetProperty("s0").GetUInt64().ToString(CultureInfo.InvariantCulture),
            s1 = p.Value.GetProperty("s1").GetUInt64().ToString(CultureInfo.InvariantCulture),
            s2 = p.Value.GetProperty("s2").GetUInt64().ToString(CultureInfo.InvariantCulture),
            s3 = p.Value.GetProperty("s3").GetUInt64().ToString(CultureInfo.InvariantCulture),
        });
    }

    private object ExportCombat()
    {
        var run = _runState!;
        var player = run.Players[0];
        var pcs = player.PlayerCombatState!;
        var combat = CombatManager.Instance.DebugOnlyGetState()!;
        var ended = !CombatManager.Instance.IsInProgress;
        Require(player.Character.Id.Entry == "IRONCLAD" && run.Players.Count == 1, "Unsupported player");
        Require(player.Relics.Count == 1 && player.Relics[0].Id.Entry == "BURNING_BLOOD", "Unsupported relics");
        Require(!player.Potions.Any() && !pcs.Pets.Any(), "Unsupported potions or pets");
        Require(!combat.Modifiers.Any(), "Unsupported combat modifiers");
        if (ended) Require(player.Creature.IsDead || _oracleEnemies.All(e => e.IsDead), "Unsupported combat termination");
        var piles = Piles();
        var liveCards = new HashSet<CardModel>(piles.Values.SelectMany(p => p.Cards), ReferenceEqualityComparer.Instance);
        Require(liveCards.All(c => _oracleIds.ContainsKey(c)), "Unsupported generated card");
        var cards = _oracleCards.Where(c => liveCards.Contains(c)).Select(card =>
        {
            Require(card.Id.Entry is "STRIKE_IRONCLAD" or "DEFEND_IRONCLAD" or "BASH", $"Unsupported card {card.Id}");
            Require(card.CurrentUpgradeLevel == 0 && card.Enchantment == null && card.Affliction == null &&
                !card.EnergyCost.HasLocalModifiers && !card.EnergyCost.CostsX && card.CurrentStarCost == -1 &&
                !card.ExhaustOnNextPlay && !card.ShouldRetainThisTurn && !card.IsSlyThisTurn &&
                !card.Keywords.Any(k => k != CardKeyword.None), $"Unsupported modified card state: {card.Id}, upgrade={card.CurrentUpgradeLevel}, enchantment={card.Enchantment?.Id}, affliction={card.Affliction?.Id}, local_cost={card.EnergyCost.HasLocalModifiers}, x={card.EnergyCost.CostsX}, stars={card.CurrentStarCost}, exhaust={card.ExhaustOnNextPlay}, retain={card.ShouldRetainThisTurn}, sly={card.IsSlyThisTurn}, keywords={string.Join(',', card.Keywords)}");
            var cost = card.EnergyCost.GetResolved();
            Require(cost == (card.Id.Entry == "BASH" ? 2 : 1), "Unexpected card cost");
            return new { instance_id = Id(card), model_id = card.Id.ToString(), upgrade_level = card.CurrentUpgradeLevel,
                cost = new { kind = "fixed", amount = cost }, flags = Array.Empty<string>(),
                counters = new Dictionary<string, long> { ["current_play_index"] = card.CurrentPlayIndex } };
        }).ToArray();
        var enemies = _oracleEnemies.Select((enemy, i) =>
        {
            var machine = _oracleMachines[i];
            var current = Field<MonsterState>(machine, "_currentState");
            var counters = new Dictionary<string, long> { ["performed_first_move"] = Field<bool>(machine, "_performedFirstMove") ? 1 : 0 };
            foreach (var pair in machine.States)
            {
                Require(pair.Value is MoveState, "Unsupported monster state type");
                counters["performed_move:" + pair.Key] = Field<bool>(pair.Value, "_performedAtLeastOnce") ? 1 : 0;
            }
            return new { instance_id = Id(enemy), model_id = enemy.Monster!.Id.ToString(), hp = enemy.CurrentHp,
                max_hp = enemy.MaxHp, block = enemy.Block, powers = Powers(enemy), counters,
                move_id = current.Id, move_history = machine.StateLog
                    .Take(machine.StateLog.Count - (Field<bool>(current, "_performedAtLeastOnce") ? 0 : 1))
                    .Select(s => s.Id).ToArray() };
        }).ToArray();
        return new {
            schema_version = 1, profile = "combat-v1", required_features = _oracleFeatures,
            boundary = ended ? "combat_end" : "player_action",
            combat = new { round = combat.RoundNumber, active_side = ended ? "none" : "player",
                outcome = ended ? player.Creature.IsDead ? "defeat" : "victory" : "ongoing" },
            player = new { instance_id = Id(player.Creature), model_id = player.Character.Id.ToString(),
                hp = player.Creature.CurrentHp, max_hp = player.Creature.MaxHp, block = player.Creature.Block,
                energy = pcs.Energy, max_energy = pcs.MaxEnergy, powers = Powers(player.Creature),
                counters = new Dictionary<string, long> { ["turn_number"] = pcs.TurnNumber },
                relics = player.Relics.Select(r => new { instance_id = Id(r), model_id = r.Id.ToString(), counters = new Dictionary<string, long>() }).ToArray() },
            enemies, cards, piles = piles.ToDictionary(p => p.Key, p => p.Value.Cards.Select(Id).ToArray()),
            selection = new { kind = "none", options = Array.Empty<string>(), min = 0, max = 0 },
            randomness = new { run = RngStreams(run.Rng.ToSerializable()), player = RngStreams(player.PlayerRng.ToSerializable()) },
        };
    }
}
