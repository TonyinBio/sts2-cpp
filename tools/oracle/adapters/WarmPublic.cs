// Benchmark-only structured projection. No privileged exporter is used here.
using System.Text.Json;
using MegaCrit.Sts2.Core.Combat;
using MegaCrit.Sts2.Core.Models;
using MegaCrit.Sts2.Core.Entities.Cards;
using MegaCrit.Sts2.Core.Models.Powers;
using MegaCrit.Sts2.Core.Rewards;
using MegaCrit.Sts2.Core.Rooms;
using MegaCrit.Sts2.Core.Runs;
namespace Sts2Headless;
public partial class RunSimulator
{
    private bool _warmPublic;
    private static bool _warmSuppressLogs;
    private object WarmCard(CardModel card) {
        var enemy = CombatManager.Instance.DebugOnlyGetState()!.Enemies.Single();
        card.DynamicVars.ClearPreview();
        try {
            card.UpdateDynamicVarPreview(CardPreviewMode.MultiCreatureTargeting, enemy, card.DynamicVars);
            int Value(string name) => card.DynamicVars.Values.Where(v => v.Name == name).Select(v => (int)v.PreviewValue).SingleOrDefault();
            var instance = _runState!.Players.Single().Deck.Cards.ToList().IndexOf(card.DeckVersion!);
            Require(instance >= 0, "Benchmark card has no deck correspondence");
            return new { instance, id = card.Id.ToString(), cost = card.EnergyCost.GetResolved(), upgraded = card.IsUpgraded,
                damage = Value("Damage"), block = Value("Block"), vulnerable = Value("VulnerablePower"), enemy_target = card.TargetType == TargetType.AnyEnemy };
        } finally { card.DynamicVars.ClearPreview(); }
    }
    private static int WarmRestriction(CardModel card) {
        if (card.CanPlay(out var reason, out _)) return 0;
        Require(reason == UnplayableReason.EnergyCostTooHigh, $"Unsupported benchmark restriction: {reason}");
        return 6;
    }
    private object WarmPublicObservation()
    {
        var player = _runState!.Players.Single();
        var active = _runState.CurrentRoom is CombatRoom && CombatManager.Instance.IsInProgress;
        var pcs = player.PlayerCombatState;
        var frames = RewardFrames();
        var choices = new List<object>();
        string prompt;
        if (_runSelector.Pending != null || _pendingBundles != null)
            throw new InvalidOperationException("Benchmark projection does not support pending selections");
        if (active) prompt = "combat";
        else if (frames.Length != 0) prompt = "rewards";
        else if (_runState.CurrentRoom is EventRoom) {
            prompt = "neow";
            var e = RunManager.Instance.EventSynchronizer.GetLocalEvent()!;
            Require(e.Id.ToString() == "EVENT.NEOW", "Unsupported benchmark event");
            foreach (var option in e.CurrentOptions)
                choices.Add(new { kind = "choose_option", index = choices.Count, enabled = !option.IsLocked, restriction = option.IsLocked ? throw new InvalidOperationException("Locked benchmark option") : 0 });
        } else if (_runState.CurrentRoom == null || _runState.CurrentRoom is MapRoom) {
            prompt = "map";
            foreach (var point in RunMapChoices())
                choices.Add(new { kind = "select_map_node", col = (int)point.coord.col, row = (int)point.coord.row, enabled = true, restriction = 0 });
        } else throw new InvalidOperationException("Unsupported benchmark prompt");
        if (active) {
            for (int i = 0; i < pcs!.Hand.Cards.Count; ++i)
                choices.Add(new { kind = "play_card", index = i, enabled = pcs.Hand.Cards[i].CanPlay(out _, out _), restriction = WarmRestriction(pcs.Hand.Cards[i]), target_enemy = pcs.Hand.Cards[i].TargetType == TargetType.AnyEnemy });
            choices.Add(new { kind = "end_turn", enabled = IsPlayPhase(), restriction = IsPlayPhase() ? 0 : throw new InvalidOperationException("Unsupported benchmark phase") });
        }
        if (frames.Length != 0) {
            Require(frames.Length == 1, "Multiple benchmark reward sets");
            for (int i = 0; i < frames[0].Set.Rewards.Count; ++i)
                if (!frames[0].Set.Rewards[i].SuccessfullySelected) choices.Add(new { kind = "take_reward", index = i, enabled = true, restriction = 0 });
            choices.Add(new { kind = "skip_rewards", enabled = !frames[0].Set.DisallowSkipping, restriction = frames[0].Set.DisallowSkipping ? throw new InvalidOperationException("Unskippable benchmark rewards") : 0 });
        }
        if (prompt is "map" or "rewards")
            for (int i = 0; i < player.PotionSlots.Count; ++i)
                if (player.PotionSlots[i] != null) choices.Add(new { kind = "discard_potion", index = i, enabled = player.CanUseOrRemovePotions, restriction = player.CanUseOrRemovePotions ? 0 : throw new InvalidOperationException("Restricted benchmark potion") });
        object? combat = null;
        if (active) {
            var enemies = CombatManager.Instance.DebugOnlyGetState()!.Enemies.ToArray();
            Require(enemies.Length == 1, "Unsupported benchmark enemy count");
            combat = new {
                phase = IsPlayPhase() ? "play" : throw new InvalidOperationException("Unsupported benchmark combat phase"),
                hp = player.Creature.CurrentHp, max_hp = player.Creature.MaxHp, block = player.Creature.Block,
                energy = pcs!.Energy, turn = pcs.TurnNumber,
                enemy_hp = enemies[0].CurrentHp, enemy_max_hp = enemies[0].MaxHp,
                draw = pcs.DrawPile.Cards.Count, discard = pcs.DiscardPile.Cards.Count, exhaust = pcs.ExhaustPile.Cards.Count,
                enemy_vulnerable = enemies[0].Powers.Where(p => p is VulnerablePower).Select(p => p.Amount).SingleOrDefault(),
                enemy_attack = enemies[0].Monster!.NextMove!.Id switch { "SHRINKER_MOVE" => 0, "CHOMP_MOVE" => 7, "STOMP_MOVE" => 13, _ => throw new InvalidOperationException("Unsupported benchmark intent") },
                shrunk = player.Creature.Powers.Any(p => p is ShrinkPower),
                intent = enemies[0].Monster!.NextMove!.Id switch { "SHRINKER_MOVE" => 0, "CHOMP_MOVE" => 1, "STOMP_MOVE" => 2, _ => throw new InvalidOperationException("Unsupported benchmark move") },
                hand = pcs.Hand.Cards.Select(WarmCard).ToArray()
            };
        }
        var map = _runState.Map!;
        var nodes = new[] { map.StartingMapPoint }.Concat(map.GetAllMapPoints())
            .Concat(new[] { map.BossMapPoint }).Distinct().ToArray();
        var available = prompt == "map" ? RunMapChoices().ToArray() : Array.Empty<MegaCrit.Sts2.Core.Map.MapPoint>();
        var options = prompt == "neow" ? RunManager.Instance.EventSynchronizer.GetLocalEvent()!.CurrentOptions
            .Select((o, index) => (object)new { id = index, model_id = o.Relic!.Id.ToString() }).ToArray() : Array.Empty<object>();
        var rewards = frames.Length == 0 ? Array.Empty<object>() : frames.Single().Set.Rewards.Select((r, index) => (object)new {
            id = index, kind = r is GoldReward ? 0 : r is PotionReward ? 1 : r is CardReward ? 2 : throw new InvalidOperationException("Unsupported benchmark reward"),
            gold = r is GoldReward gold ? gold.Amount : 0, model_id = r is PotionReward potion ? (potion.Potion ?? potion.ClaimedPotion)!.Id.ToString() : "", claimed = r.SuccessfullySelected }).ToArray();
        return JsonSerializer.SerializeToElement(new {
            contract_revision = 2, build = "0.111.0+41cef1ea", valid = true,
            character = "CHARACTER." + player.Character.Id.Entry, seed = _runState.Rng.StringSeed,
            ascension = _runState.AscensionLevel, act = _runState.CurrentActIndex + 1,
            potion_capacity = player.PotionSlots.Count,
            minimum_selection = 0, maximum_selection = 0, submits_immediately = true,
            selecting_reward_card = false, can_skip_rewards = frames.Length != 0, can_skip_card_reward = false,
            map = nodes.Select(p => new { coord = new[] { (int)p.coord.col, (int)p.coord.row }, kind = p.PointType.ToString(),
                children = p.Children.Select(c => new[] { (int)c.coord.col, (int)c.coord.row }).ToArray(),
                visited = _runState.VisitedMapCoords.Contains(p.coord), current = _runState.CurrentMapCoord == p.coord,
                available = available.Contains(p) }).ToArray(),
            path = _runState.VisitedMapCoords.Select(c => new[] { (int)c.col, (int)c.row }).ToArray(), options, rewards, reward_cards = Array.Empty<object>(),
            draw_cards = active ? pcs!.DrawPile.Cards.OrderBy(c => c.Rarity).ThenBy(c => c.Id.Entry, StringComparer.Ordinal).Select(WarmCard).ToArray() : Array.Empty<object>(),
            discard_cards = active ? pcs!.DiscardPile.Cards.Select(WarmCard).ToArray() : Array.Empty<object>(),
            exhaust_cards = active ? pcs!.ExhaustPile.Cards.Select(WarmCard).ToArray() : Array.Empty<object>(),
            profile = "warm-public-structured-v1", hp = player.Creature.CurrentHp, max_hp = player.Creature.MaxHp, gold = player.Gold,
            deck = player.Deck.Cards.Select((c, instance) => new { instance, id = c.Id.ToString(), upgrade = c.CurrentUpgradeLevel, energy_cost = c.EnergyCost.GetResolved() }).ToArray(),
            relics = player.Relics.Select((r, id) => new { id, model_id = r.Id.ToString() }).ToArray(),
            potions = player.PotionSlots.Select((p, id) => new { id, model_id = p?.Id.ToString() ?? "" }).Where(p => p.model_id != "").ToArray(),
            prompt, run_phase = prompt, choices, combat
        }, new JsonSerializerOptions { Encoder = System.Text.Encodings.Web.JavaScriptEncoder.UnsafeRelaxedJsonEscaping });
    }
    public Dictionary<string, object?> WarmRuntimeInfo(JsonElement command) => new() {
        ["runtime"] = Environment.Version.ToString(),
        ["server_gc"] = System.Runtime.GCSettings.IsServerGC,
        ["latency_mode"] = System.Runtime.GCSettings.LatencyMode.ToString(),
        ["architecture"] = System.Runtime.InteropServices.RuntimeInformation.ProcessArchitecture.ToString(),
        ["tiered_compilation_environment"] = Environment.GetEnvironmentVariable("DOTNET_TieredCompilation")
            ?? Environment.GetEnvironmentVariable("COMPlus_TieredCompilation") ?? "unset; runtime default"
    };
    public Dictionary<string, object?> WarmQueryAudit(JsonElement command) {
        Require(_warmPublic, "Benchmark session required");
        var before = ExportRunFlowState();
        var observation = WarmPublicObservation();
        var after = ExportRunFlowState();
        return new() { ["before"] = before, ["after"] = after, ["observation"] = observation };
    }
    public Dictionary<string, object?> WarmBenchmark(JsonElement command)
    {
        _warmPublic = true;
        _warmSuppressLogs = true;
        var operation = command.GetProperty("operation");
        var kind = operation.GetProperty("cmd").GetString();
        if (kind == "start") {
            OracleRunStart(JsonSerializer.SerializeToElement(new { setup = operation.GetProperty("setup"), request_id = "benchmark", required_features = new[] { "run_flow_v1" } }));
        } else if (kind == "scenario_enter_combat") {
            CheckResponse(EnterRoom("combat", operation.GetProperty("encounter").GetString()!.Replace("ENCOUNTER.", ""), null));
            AwaitRunBoundary();
        } else if (kind == "action") {
            var action = operation.GetProperty("action");
            var args = action.EnumerateObject().Where(p => p.Name != "kind").ToDictionary(p => p.Name, p => p.Value);
            OracleRunAction(JsonSerializer.SerializeToElement(new { action = action.GetProperty("kind").GetString(), args, request_id = "benchmark" }));
        } else throw new InvalidOperationException("Unsupported benchmark operation");
        for (int i = 0; i < 4; ++i) {
            var room = _runState!.CurrentRoom;
            bool forced = room is EventRoom && RunManager.Instance.EventSynchronizer.GetLocalEvent()!.IsFinished
                || room is CombatRoom && !CombatManager.Instance.IsInProgress && RewardFrames().Length == 0;
            if (!forced) {
                var observation = (JsonElement)WarmPublicObservation();
                return new() { ["ok"] = true, ["observation"] = observation,
                    ["capabilities"] = observation.GetProperty("choices").EnumerateArray().Select(c => c.GetProperty("restriction").GetInt32()).ToArray() };
            }
            OracleRunAction(JsonSerializer.SerializeToElement(new { action = "proceed", args = new { }, request_id = "benchmark" }));
        }
        throw new InvalidOperationException("Benchmark forced transitions did not settle");
    }
}
