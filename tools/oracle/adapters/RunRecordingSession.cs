// Backend run flow only. Combat model serialization remains in RecordingSession.cs.
using System.Collections;
using System.Reflection;
using System.Text.Json;
using System.Text.Json.Nodes;
using HarmonyLib;
using MegaCrit.Sts2.Core.GameActions;
using MegaCrit.Sts2.Core.GameActions.Multiplayer;
using MegaCrit.Sts2.Core.CardSelection;
using MegaCrit.Sts2.Core.Commands;
using MegaCrit.Sts2.Core.Combat;
using MegaCrit.Sts2.Core.Entities.CardRewardAlternatives;
using MegaCrit.Sts2.Core.Entities.Cards;
using MegaCrit.Sts2.Core.Entities.Creatures;
using MegaCrit.Sts2.Core.Entities.Merchant;
using MegaCrit.Sts2.Core.Localization;
using MegaCrit.Sts2.Core.Localization.DynamicVars;
using MegaCrit.Sts2.Core.Map;
using MegaCrit.Sts2.Core.Models;
using MegaCrit.Sts2.Core.Rewards;
using MegaCrit.Sts2.Core.Rooms;
using MegaCrit.Sts2.Core.Runs;
using MegaCrit.Sts2.Core.TestSupport;

namespace Sts2Headless;

public partial class RunSimulator
{
    private bool _oracleRunMode;
    private bool _runDeathObserved;
    private bool _runEndingFinalized;
    private bool _runCombatWon;
    private bool _runCombatRewardsOffered;
    private bool _runTreasurePicked;
    private AbstractRoom? _runObservedRoom;
    private CombatState? _runCombatScope;
    private readonly object _runGate = new();
    private readonly List<RunOperation> _runOperations = new();
    private readonly List<RunRewards> _runRewards = new();
    private RunSelector _runSelector = null!;
    private RunOperation? _runBundleOwner;
    private volatile RunOperation? _runActionOwner;
    private static readonly AsyncLocal<RunOperation?> RunOwnerContext = new();
    private static RunSimulator? RunEndingSession;

    private sealed class RunOperation(string kind, int? index)
    {
        public string Kind = kind;
        public int? Index = index;
        public Task Task = Task.CompletedTask;
        public AbstractModel? Source;
    }
    private sealed class RunRewards(RewardsSet set, RunOperation owner)
    {
        public RewardsSet Set = set;
        public RunOperation Owner = owner;
        public TaskCompletionSource Done = new(TaskCreationOptions.RunContinuationsAsynchronously);
    }
    private sealed class RunSelection
    {
        public required string Kind;
        public required RunOperation Owner;
        public List<CardModel> Cards = new();
        public CardSelectorPrefs Prefs;
        public IReadOnlyList<CardRewardAlternative> Alternatives = Array.Empty<CardRewardAlternative>();
        public TaskCompletionSource<IEnumerable<CardModel>> CardsDone = new(TaskCreationOptions.RunContinuationsAsynchronously);
        public TaskCompletionSource<CardRewardSelection> RewardDone = new(TaskCreationOptions.RunContinuationsAsynchronously);
    }
    private sealed class RunSelector(RunSimulator simulator) : ICardSelector
    {
        private readonly object _gate = new();
        private RunSelection? _pending;
        public RunSelection? Pending { get { lock (_gate) return _pending; } }
        public Task<IEnumerable<CardModel>> GetSelectedCards(IEnumerable<CardModel> options, int minSelect, int maxSelect)
        {
            var prefs = RunChoiceContextCapture.Prefs.Value;
            Require(prefs.HasValue && prefs.Value.MinSelect == minSelect && prefs.Value.MaxSelect == maxSelect,
                "Card selection has no captured native preferences");
            var pending = new RunSelection { Kind = "card_select", Owner = (RunOwnerContext.Value ?? simulator._runActionOwner)
                ?? throw new InvalidOperationException("Card selection outside a tracked run operation"),
                Cards = options.ToList(), Prefs = prefs.Value };
            lock (_gate) { Require(_pending == null, "Overlapping card selections"); _pending = pending; }
            return pending.CardsDone.Task;
        }
        public CardRewardSelection GetSelectedCardReward(IReadOnlyList<CardCreationResult> options,
            IReadOnlyList<CardRewardAlternative> alternatives)
        {
            var pending = new RunSelection { Kind = "card_reward", Owner = (RunOwnerContext.Value ?? simulator._runActionOwner)
                ?? throw new InvalidOperationException("Card reward outside a tracked run operation"),
                Cards = options.Select(c => c.Card).ToList(), Alternatives = alternatives.ToArray() };
            lock (_gate) { Require(_pending == null, "Overlapping reward selections"); _pending = pending; }
            return pending.RewardDone.Task.GetAwaiter().GetResult();
        }
        public void Clear(RunSelection expected)
        {
            lock (_gate) { Require(ReferenceEquals(_pending, expected), "Choice changed before resolution"); _pending = null; }
        }
    }
    // ICardSelector drops confirmation/cancel flags. Capture the actual prefs at
    // command entry before native code passes only min/max to the test selector.
    public static class RunChoiceContextCapture
    {
        public static readonly AsyncLocal<CardSelectorPrefs?> Prefs = new();
        public static void BeforeWin()
        {
            if (RunEndingSession is { } session) {
                session._oracleRunEndingReason = "victory";
                session._runEndingFinalized = true;
            }
        }

        public static void Capture(MethodBase __originalMethod, object[] __args)
        {
            var parameters = __originalMethod.GetParameters();
            for (int i = 0; i < parameters.Length; ++i)
                if (parameters[i].ParameterType == typeof(CardSelectorPrefs)) {
                    Prefs.Value = (CardSelectorPrefs)__args[i]; return;
                }
            if (__originalMethod.Name == "FromChooseACardScreen") {
                var canSkip = (bool)__args[Array.FindIndex(parameters, p => p.Name == "canSkip")];
                Prefs.Value = new CardSelectorPrefs(new LocString("card_selection", "TO_SELECT"), canSkip ? 0 : 1, 1)
                    { RequireManualConfirmation = false, Cancelable = canSkip };
            }
        }
    }

    public Dictionary<string, object?> OracleRunStart(JsonElement command)
    {
        Require(!_oracleActive, "One run per recording process");
        var setup = command.GetProperty("setup");
        var character = setup.GetProperty("character").GetString()!;
        var ascension = setup.GetProperty("ascension").GetInt32();
        var seed = setup.GetProperty("seed").GetString()!;
        Require(new[] { "Ironclad", "Silent", "Defect", "Regent", "Necrobinder" }.Contains(character), "Unknown character");
        Require(ascension >= 0 && ascension <= 10 && !string.IsNullOrEmpty(seed), "Invalid ascension or seed");
        _oracleActive = _oracleRunMode = true;
        _runSelector = new RunSelector(this);
        var harmony = new Harmony("sts2.oracle.runchoices");
        foreach (var method in typeof(CardSelectCmd).GetMethods(BindingFlags.Static | BindingFlags.Public))
            if (method.GetParameters().Any(p => p.ParameterType == typeof(CardSelectorPrefs)) || method.Name == "FromChooseACardScreen")
                harmony.Patch(method, prefix: new HarmonyMethod(typeof(RunChoiceContextCapture).GetMethod(nameof(RunChoiceContextCapture.Capture))!));
        RunEndingSession = this;
        harmony.Patch(typeof(RunManager).GetMethod(nameof(RunManager.WinRun))!,
            prefix: new HarmonyMethod(typeof(RunChoiceContextCapture).GetMethod(nameof(RunChoiceContextCapture.BeforeWin))!));
        RewardsSet.testSelector = OfferRunRewards;
        CombatManager.Instance.CombatWon += _ => _runCombatWon = true;
        CombatManager.Instance.CombatEnded += _ => { if (!_runCombatWon && _oracleRunEndingReason == null) _runDeathObserved = true; };
        // StartRun performs the real seeded setup and enters the initial act/Neow;
        // recorder mode bypasses its UI-oriented decision detection.
        CheckResponse(StartRun(character, ascension, seed, "en"));
        if (setup.TryGetProperty("kind", out var setupKind) && setupKind.GetString() == "seeded_combat")
            CheckResponse(EnterRoom("combat", setup.GetProperty("encounter").GetString(), null));
        _runState!.Players[0].Creature.Died += _ => _runDeathObserved = true;
        AwaitRunBoundary();
        RegisterCombatIdentities();
        return OracleRunResponse(command);
    }

    private void BeginRunOperation(string kind, int? index, Func<Task> action)
    {
        var operation = new RunOperation(kind, index);
        _runOperations.Add(operation);
        operation.Task = Task.Run(async () => { RunOwnerContext.Value = operation; await action(); });
    }
    private async Task OfferRunRewards(RewardsSet set)
    {
        var frame = new RunRewards(set, RunOwnerContext.Value
            ?? throw new InvalidOperationException("Rewards outside a tracked run operation"));
        lock (_runGate) _runRewards.Add(frame);
        try { await frame.Done.Task; }
        finally { lock (_runGate) _runRewards.Remove(frame); }
    }
    private RunRewards[] RewardFrames() { lock (_runGate) return _runRewards.ToArray(); }
    private bool Blocked(RunOperation owner) => ReferenceEquals(_runSelector.Pending?.Owner, owner) ||
        RewardFrames().Any(f => ReferenceEquals(f.Owner, owner)) ||
        (_pendingBundles != null && ReferenceEquals(_runBundleOwner, owner));

    private void AwaitRunBoundary()
    {
        var timer = System.Diagnostics.Stopwatch.StartNew();
        while (timer.ElapsedMilliseconds < 10000) {
            _syncCtx.Pump();
            foreach (var operation in _runOperations)
                if (operation.Task.IsCompleted) {
                    if (!(operation.Task.IsCanceled && _oracleRunEndingReason == "abandoned")) operation.Task.GetAwaiter().GetResult();
                }
            var executing = _runOperations.Any(o => !o.Task.IsCompleted && !Blocked(o));
            var waiting = _runSelector.Pending != null || _pendingBundles != null || RewardFrames().Length != 0;
            var manager = CombatManager.Instance;
            var executor = RunManager.Instance.ActionExecutor;
            var inCombat = _runState!.CurrentRoom is CombatRoom && manager.IsInProgress;
            if (!executing && (waiting || (!executor.IsRunning && executor.CurrentlyRunningAction == null)) &&
                (!inCombat || waiting || (IsPlayPhase() && manager.DebugOnlyGetState()!.Enemies.All(e => !e.Monster!.IsPerformingMove)))) {
                _runOperations.RemoveAll(o => o.Task.IsCompleted);
                if (_runDeathObserved && _runState.Players.All(p => p.Creature.IsDead) && _oracleRunEndingReason == null)
                    _oracleRunEndingReason = "death";
                if (_oracleRunEndingReason == "death" && !_runEndingFinalized) {
                    RunManager.Instance.OnEnded(false); _runEndingFinalized = true;
                }
                ObserveRunRoom();
                return;
            }
            Thread.Sleep(1);
        }
        throw new TimeoutException("Run did not reach a settled input or captured choice boundary");
    }
    private void ObserveRunRoom()
    {
        var room = _runState!.CurrentRoom;
        if (ReferenceEquals(room, _runObservedRoom)) return;
        _runObservedRoom = room;
        _runCombatRewardsOffered = false;
        _runTreasurePicked = false;
        _runCombatWon = false;
    }

    public Dictionary<string, object?> OracleRunState(JsonElement command)
    {
        Require(_oracleRunMode, "No run recording session");
        AwaitRunBoundary();
        RegisterCombatIdentities();
        return OracleRunResponse(command);
    }

    public Dictionary<string, object?> OracleRunAction(JsonElement command)
    {
        Require(_oracleRunMode, "No run recording session");
        AwaitRunBoundary();
        Require(_oracleRunEndingReason == null, "Action after run completion");
        var kind = command.GetProperty("action").GetString()!;
        var args = command.TryGetProperty("args", out var supplied) ? supplied : JsonSerializer.SerializeToElement(new { });
        int Index(string name = "index") => args.GetProperty(name).GetInt32();
        var player = _runState!.Players.Single();
        var pending = _runSelector.Pending;
        var frames = RewardFrames();
        if (kind == "abandon") {
            // Mark the path before native death callbacks; victory/abandonment also
            // kill players and must never be relabelled as an HP-inferred death.
            _oracleRunEndingReason = "abandoned";
            if (pending != null) { _runSelector.Clear(pending); pending.CardsDone.TrySetCanceled(); pending.RewardDone.TrySetCanceled(); }
            _pendingBundleTcs?.TrySetCanceled(); _pendingBundles = null; _pendingBundleTcs = null;
            foreach (var frame in frames.Reverse()) { RunManager.Instance.RewardsSetSynchronizer.SkipLocalRewardsSet(); frame.Done.TrySetResult(); }
            BeginRunOperation(kind, null, async () => {
                await (Task)typeof(RunManager).GetMethod("AbandonInternal", BindingFlags.Instance | BindingFlags.NonPublic)!.Invoke(RunManager.Instance, null)!;
                RunManager.Instance.OnEnded(false); _runEndingFinalized = true;
            });
        } else if (pending != null) {
            if (pending.Kind == "card_select") {
                Require(kind is "select_cards" or "skip_select", "Resolve the pending card selection first");
                var indices = kind == "skip_select" ? Array.Empty<int>() : args.GetProperty("indices").EnumerateArray().Select(v => v.GetInt32()).ToArray();
                Require(indices.Distinct().Count() == indices.Length && indices.All(i => i >= 0 && i < pending.Cards.Count), "Invalid or duplicate card selection index");
                Require(kind == "skip_select" ? pending.Prefs.Cancelable : indices.Length >= pending.Prefs.MinSelect && indices.Length <= pending.Prefs.MaxSelect,
                    "Selection bounds or cancellation policy violated");
                _runSelector.Clear(pending); pending.CardsDone.TrySetResult(indices.Select(i => pending.Cards[i]).ToArray());
            } else {
                Require(kind is "select_card_reward" or "reward_alternative" or "skip_card_reward", "Resolve the pending card reward first");
                var selection = new CardRewardSelection();
                if (kind == "select_card_reward") { var i = Index("card_index"); Require(i >= 0 && i < pending.Cards.Count, "Invalid reward card index"); selection.card = pending.Cards[i]; }
                if (kind == "reward_alternative") { var i = Index(); Require(i >= 0 && i < pending.Alternatives.Count, "Invalid reward alternative"); selection.alternative = pending.Alternatives[i]; }
                if (kind == "skip_card_reward") {
                    Require(pending.Alternatives.Any(a => a.GetType().Name.Contains("Skip")), "This card reward cannot be skipped");
                    selection.alternative = pending.Alternatives.First(a => a.GetType().Name.Contains("Skip"));
                }
                _runSelector.Clear(pending); pending.RewardDone.TrySetResult(selection);
            }
        } else if (_pendingBundles != null) {
            Require(kind == "select_bundle", "Resolve the pending bundle selection first");
            var i = Index("bundle_index"); Require(i >= 0 && i < _pendingBundles.Count, "Invalid bundle index");
            var selected = _pendingBundles[i]; var done = _pendingBundleTcs!;
            _pendingBundles = null; _pendingBundleTcs = null; done.TrySetResult(selected);
        } else if (frames.Length != 0) {
            var frame = frames.Last();
            Require(kind is "take_reward" or "skip_rewards", "Resolve or skip the current rewards first");
            if (kind == "skip_rewards") {
                Require(!frame.Set.DisallowSkipping, "These rewards cannot be skipped");
                RunManager.Instance.RewardsSetSynchronizer.SkipLocalRewardsSet(); frame.Done.TrySetResult();
            } else {
                var i = Index(); Require(i >= 0 && i < frame.Set.Rewards.Count && !frame.Set.Rewards[i].SuccessfullySelected, "Invalid or already taken reward");
                BeginRunOperation(kind, i, async () => {
                    Require(await RunManager.Instance.RewardsSetSynchronizer.SelectLocalReward(frame.Set.Rewards[i]), "Reward was not obtained");
                    if (RunManager.Instance.RewardsSetSynchronizer.IsRewardsSetCompleted(frame.Set)) frame.Done.TrySetResult();
                });
            }
        } else RunRoomAction(kind, args);
        AwaitRunBoundary();
        RegisterCombatIdentities();
        return OracleRunResponse(command);
    }

    private void RunRoomAction(string kind, JsonElement args)
    {
        var run = _runState!; var player = run.Players.Single(); var room = run.CurrentRoom;
        int Index(string name = "index") => args.GetProperty(name).GetInt32();
        switch (kind) {
            case "select_map_node": {
                Require(room == null || room is MapRoom, "Map selection outside the map");
                var col = Index("col"); var row = Index("row");
                Require(col >= 0 && col <= 255 && row >= 0 && row <= 255, "Map coordinate out of range");
                var map = run.Map!;
                var point = run.CurrentMapCoord.HasValue ? map.GetPoint(run.CurrentMapCoord.Value) : map.StartingMapPoint;
                Require(point != null && point.Children.Any(p => p.coord.col == col && p.coord.row == row), "Map node is not reachable");
                BeginRunOperation(kind, null, () => RunManager.Instance.EnterMapCoord(new MapCoord((byte)col, (byte)row))); break;
            }
            case "choose_option": {
                var i = Index("option_index");
                if (room is EventRoom) {
                    var e = RunManager.Instance.EventSynchronizer.GetLocalEvent();
                    Require(e != null && !e.IsFinished && i >= 0 && i < e.CurrentOptions.Count && !e.CurrentOptions[i].IsLocked, "Invalid event option");
                    BeginRunOperation(kind, i, () => e!.CurrentOptions[i].Chosen());
                } else if (room is RestSiteRoom rest) {
                    Require(i >= 0 && i < rest.Options.Count && rest.Options[i].IsEnabled, "Invalid rest-site option");
                    BeginRunOperation(kind, i, async () => { await RunManager.Instance.RestSiteSynchronizer.ChooseLocalOption(i); });
                } else throw new InvalidOperationException("Option choice outside an event/rest site");
                break;
            }
            case "buy_card": case "buy_relic": case "buy_potion": case "remove_card": {
                Require(room is MerchantRoom, "Purchase outside a shop");
                var inv = ((MerchantRoom)room!).GetLocalInventory();
                var i = kind == "remove_card" ? 0 : Index(kind == "buy_card" ? "card_index" : kind == "buy_relic" ? "relic_index" : "potion_index");
                var entries = kind == "buy_card" ? inv.CardEntries.Cast<MerchantEntry>().ToList() :
                    kind == "buy_relic" ? inv.RelicEntries.Cast<MerchantEntry>().ToList() :
                    kind == "buy_potion" ? inv.PotionEntries.Cast<MerchantEntry>().ToList() : new List<MerchantEntry> { inv.CardRemovalEntry! };
                Require(i >= 0 && i < entries.Count && entries[i] != null && entries[i].IsStocked && entries[i].EnoughGold, "Invalid, unavailable, or unaffordable shop entry");
                BeginRunOperation(kind, i, async () => { await entries[i].OnTryPurchaseWrapper(inv); }); break;
            }
            case "pick_treasure": {
                Require(room is TreasureRoom && !_runTreasurePicked, "No unresolved treasure choice");
                var sync = RunManager.Instance.TreasureRoomRelicSynchronizer;
                var i = Index(); Require(sync.CurrentRelics != null && i >= 0 && i < sync.CurrentRelics.Count, "Invalid treasure index");
                BeginRunOperation(kind, i, async () => {
                    var done = new TaskCompletionSource<List<MegaCrit.Sts2.Core.Entities.TreasureRelicPicking.RelicPickingResult>>(TaskCreationOptions.RunContinuationsAsynchronously);
                    void Capture(List<MegaCrit.Sts2.Core.Entities.TreasureRelicPicking.RelicPickingResult> results) => done.TrySetResult(results);
                    sync.RelicsAwarded += Capture;
                    try {
                        sync.PickRelicLocally(i);
                        var awarded = await done.Task;
                        foreach (var result in awarded) if (result.relic != null && result.player != null)
                            await RelicCmd.Obtain(result.relic.ToMutable(), result.player);
                        _runTreasurePicked = true;
                    } finally { sync.RelicsAwarded -= Capture; }
                }); break;
            }
            case "proceed": case "leave_room": {
                Require(room != null && room is not MapRoom, "Opening the map is not a gameplay decision; select a reachable map node");
                Require(!(room is CombatRoom && CombatManager.Instance.IsInProgress), "Cannot leave an active combat");
                if (room is CombatRoom combat && !_runCombatRewardsOffered) {
                    _runCombatRewardsOffered = true;
                    BeginRunOperation("offer_combat_rewards", null, () => new RewardsSet(player).WithRewardsFromRoom(combat).Offer());
                    break;
                }
                if (room is EventRoom) Require(RunManager.Instance.EventSynchronizer.GetLocalEvent()?.IsFinished != false, "Event still has a required decision");
                if (room is TreasureRoom) Require(_runTreasurePicked, "Resolve the treasure choice first");
                BeginRunOperation(kind, null, async () => {
                    if (room is CombatRoom boss && boss.RoomType == RoomType.Boss) {
                        var finalPoint = run.Map!.SecondBossMapPoint ?? run.Map.BossMapPoint;
                        if (run.Map.SecondBossMapPoint == null || run.CurrentMapCoord == finalPoint.coord) {
                            // Native EnterNextAct enters the Architect victory event
                            // after the final act; do not skip that final room.
                            await RunManager.Instance.EnterNextAct(); return;
                        }
                    }
                    if (room?.IsVictoryRoom == true) {
                        _oracleRunEndingReason = "victory"; await RunManager.Instance.WinRun(); _runEndingFinalized = true; return;
                    }
                    if (room is TreasureRoom treasure) { await treasure.DoNormalRewards(); await treasure.DoExtraRewardsIfNeeded(); }
                    await RunManager.Instance.ProceedFromTerminalRewardsScreen();
                    if (ReferenceEquals(run.CurrentRoom, room)) await RunManager.Instance.EnterRoom(new MapRoom());
                }); break;
            }
            case "use_potion": case "discard_potion": {
                var slot = Index("potion_index");
                Require(slot >= 0 && slot < player.PotionSlots.Count && player.PotionSlots[slot] != null, "Invalid or empty potion slot");
                Require(player.CanUseOrRemovePotions, "Potions are currently disabled");
                var potion = player.PotionSlots[slot]!;
                if (kind == "discard_potion") BeginRunOperation(kind, slot, () => PotionCmd.Discard(potion));
                else {
                    var target = ResolveRunTarget(potion.TargetType, args);
                    QueueRunAction(kind, slot, new UsePotionAction(potion, target, CombatManager.Instance.IsInProgress), potion);
                }
                break;
            }
            case "play_card": {
                Require(room is CombatRoom && CombatManager.Instance.IsInProgress, "Card play outside combat");
                var i = Index("card_index"); var hand = player.PlayerCombatState!.Hand.Cards;
                Require(i >= 0 && i < hand.Count, "Invalid hand position");
                var card = hand[i];
                Require(card.CanPlay(out _, out _), "Card cannot currently be played");
                var target = ResolveRunTarget(card.TargetType, args, cardAction: true);
                Require(card.IsValidTarget(target), "Invalid card target");
                QueueRunAction(kind, i, new PlayCardAction(card, target), card);
                break;
            }
            case "end_turn": {
                Require(room is CombatRoom && CombatManager.Instance.IsInProgress && IsPlayPhase(), "End turn outside player input");
                var before = player.PlayerCombatState!.TurnNumber;
                BeginRunOperation(kind, null, async () => {
                    _runActionOwner = RunOwnerContext.Value;
                    YieldPatches.SuppressYield = true;
                    try {
                        PlayerCmd.EndTurn(player, canBackOut: false);
                        while (CombatManager.Instance.IsInProgress && (player.PlayerCombatState.TurnNumber <= before || !IsPlayPhase()))
                            await Task.Delay(1);
                    } finally { YieldPatches.SuppressYield = false; _runActionOwner = null; }
                }); break;
            }
            default: throw new InvalidOperationException($"Unsupported run backend action: {kind}");
        }
    }

    private Creature? ResolveRunTarget(TargetType type, JsonElement args, bool cardAction = false)
    {
        var player = _runState!.Players.Single();
        if (type == TargetType.AnyEnemy) {
            Require(args.TryGetProperty("target_index", out var supplied), "An explicit enemy target is required");
            var enemies = CombatManager.Instance.DebugOnlyGetState()?.Enemies.Where(e => e.IsAlive).ToList();
            var i = supplied.GetInt32();
            Require(enemies != null && i >= 0 && i < enemies.Count, "Invalid or dead enemy target");
            return enemies![i];
        }
        if (args.TryGetProperty("target_index", out _)) throw new InvalidOperationException("This action does not accept an enemy target");
        if (cardAction) return null; // Native cards resolve implicit self/player/Osty targeting internally.
        return type switch {
            TargetType.Self or TargetType.AnyPlayer or TargetType.TargetedNoCreature => player.Creature,
            TargetType.Osty => player.Osty,
            TargetType.AnyAlly => throw new InvalidOperationException("No other player exists in single-player"),
            _ => null
        };
    }
    private void QueueRunAction(string kind, int? index, GameAction action, AbstractModel source)
    {
        BeginRunOperation(kind, index, async () => {
            _runActionOwner = RunOwnerContext.Value;
            _runActionOwner!.Source = source;
            try {
                RunManager.Instance.ActionQueueSet.EnqueueWithoutSynchronizing(action);
                await action.CompletionTask;
                if (action.Exception != null) throw new InvalidOperationException("Native action failed", action.Exception);
            } finally { _runActionOwner = null; }
        });
    }

    private Dictionary<string, object?> OracleRunResponse(JsonElement command) => new() {
        ["type"] = "oracle_state", ["request_id"] = command.GetProperty("request_id").GetString(),
        ["assembly_mvid"] = typeof(CardModel).Assembly.ManifestModule.ModuleVersionId.ToString(),
        ["runtime_version"] = Environment.Version.ToString(), ["state"] = ExportRunFlowState()
    };
    private static JsonNode? Saved(object? value) => NativeRunRoundTrip.Capture(value);
    private static object[] SavedCards(IEnumerable<CardModel> cards) => cards.Select(c => (object)Saved(c.ToSerializable())!).ToArray();

    private void RegisterCombatIdentities()
    {
        if (_runState!.CurrentRoom is not CombatRoom || !CombatManager.Instance.IsInProgress) return;
        var combat = CombatManager.Instance.DebugOnlyGetState()!;
        var player = _runState.Players.Single();
        if (!ReferenceEquals(combat, _runCombatScope)) {
            _runCombatScope = combat; _oracleIds.Clear(); _oracleCards.Clear(); _oracleEnemies.Clear();
            _oracleIds.Add(player.Creature, "player_0");
        }
        foreach (var enemy in combat.Enemies) if (!_oracleIds.ContainsKey(enemy)) {
            _oracleIds.Add(enemy, $"enemy_{_oracleEnemies.Count}"); _oracleEnemies.Add(enemy);
        }
        foreach (var card in player.PlayerCombatState!.AllCards) if (!_oracleIds.ContainsKey(card)) {
            _oracleIds.Add(card, $"card_{_oracleCards.Count}"); _oracleCards.Add(card);
        }
    }

    private object ExportRunFlowState()
    {
        var run = _runState!; var player = run.Players.Single();
        if (_oracleRunEndingReason == null && run.CurrentRoom is CombatRoom && CombatManager.Instance.IsInProgress) {
            RegisterCombatIdentities();
            return ExportRun(); // Existing closed combat model guards are unchanged.
        }
        var state = Saved(RunManager.Instance.ToSave(null))!.AsObject();
        state["boundary_kind"] = _oracleRunEndingReason == null ? "awaiting_input" : "run_complete";
        state["run_ending_reason"] = _oracleRunEndingReason;
        state["current_room"] = Saved(run.CurrentRoom?.ToSerializable());
        state["native_model_state"] = new JsonObject(); state["combat"] = null; state["pending_choice"] = null;
        state["run_flow"] = _oracleRunEndingReason == null ? JsonSerializer.SerializeToNode(new {
            room = ExportRunRoom(), pending_choice = ExportRunChoice(), reward_sets = RewardFrames().Select(f => new {
                rewards = f.Set.Rewards.Select(reward => new {
                    kind = reward.GetType().Name, saved = SavedRunReward(reward), taken = reward.SuccessfullySelected,
                    cards = reward is CardReward card ? SavedCards(card.Cards) : Array.Empty<object>(),
                    relic = reward is RelicReward relic ? Saved(relic.Relic?.ToSerializable()) : null,
                    potion = reward is PotionReward potion ? Saved(potion.Potion?.ToSerializable(-1)) : null,
                    can_skip = reward is CardReward cr && cr.CanSkip }), can_skip = !f.Set.DisallowSkipping }),
            operations = _runOperations.Where(o => !o.Task.IsCompleted).Select(o => new { kind = o.Kind, index = o.Index }),
            combat_rewards_offered = _runCombatRewardsOffered }) : null;
        return state;
    }
    private static JsonNode? SavedRunReward(Reward reward)
    {
        try { return Saved(reward.ToSerializable()); }
        // Native saves cannot represent populated manual/filtered card rewards.
        // Their actual offered cards and skip policy are captured alongside this field.
        catch (NotImplementedException) when (reward is CardReward) { return null; }
    }

    private object? ExportRunChoice()
    {
        if (_pendingBundles != null) return new { kind = "bundle_select", bundles = _pendingBundles.Select(SavedCards).ToArray() };
        var pending = _runSelector.Pending;
        if (pending == null) return null;
        if (pending.Kind == "card_reward") return new { kind = "card_reward", cards = SavedCards(pending.Cards),
            alternatives = pending.Alternatives.Select(a => a.GetType().Name).ToArray(),
            can_skip = pending.Alternatives.Any(a => a.GetType().Name.Contains("Skip")) };
        var deck = _runState!.Players[0].Deck.Cards.ToList();
        return new { kind = "card_select", cards = SavedCards(pending.Cards),
            deck_indices = pending.Cards.Select(c => { var i = deck.IndexOf(c); return i < 0 ? (int?)null : i; }).ToArray(),
            min_select = pending.Prefs.MinSelect, max_select = pending.Prefs.MaxSelect,
            require_manual_confirmation = pending.Prefs.RequireManualConfirmation, cancelable = pending.Prefs.Cancelable };
    }
    private object ExportRunRoom()
    {
        var run = _runState!; var room = run.CurrentRoom;
        if (room == null || room is MapRoom) {
            var point = run.CurrentMapCoord.HasValue ? run.Map!.GetPoint(run.CurrentMapCoord.Value) : run.Map!.StartingMapPoint;
            return new { kind = "map", current_coord = Saved(run.CurrentMapCoord), choices = point!.Children.Select(p => new {
                col = (int)p.coord.col, row = (int)p.coord.row, point_type = p.PointType.ToString() }).ToArray() };
        }
        if (room is EventRoom) {
            var e = RunManager.Instance.EventSynchronizer.GetLocalEvent()!;
            return new { kind = "event", id = e.Id.ToString(), finished = e.IsFinished,
                page = e.Description?.LocEntryKey,
                options = e.CurrentOptions.Select(o => new { key = o.TextKey, is_locked = o.IsLocked,
                    is_proceed = o.IsProceed, was_chosen = o.WasChosen, relic = Saved(o.Relic?.ToSerializable()) }).ToArray(),
                dynamic_vars = e.DynamicVars.Where(v => v.Value is not StringVar).ToDictionary(v => v.Key, v => v.Value.BaseValue),
                string_vars = e.DynamicVars.Where(v => v.Value is StringVar).ToDictionary(v => v.Key, v => ((StringVar)v.Value).StringValue),
                rng = Saved(e.Rng.ToSerializable()) };
        }
        if (room is RestSiteRoom rest) return new { kind = "rest_site", options = rest.Options.Select(o => new { id = o.OptionId, enabled = o.IsEnabled }).ToArray() };
        if (room is MerchantRoom merchant) {
            var inv = merchant.GetLocalInventory(); var removal = inv.CardRemovalEntry;
            return new { kind = "shop", cards = inv.CardEntries.Select(e => new { saved = Saved(e.CreationResult?.Card.ToSerializable()),
                base_price = Field<int>(e, "_cost"), price = e.Cost, stocked = e.IsStocked, on_sale = e.IsOnSale }).ToArray(),
                relics = inv.RelicEntries.Select(e => new { saved = Saved(e.Model?.ToSerializable()), base_price = Field<int>(e, "_cost"), price = e.Cost, stocked = e.IsStocked }).ToArray(),
                potions = inv.PotionEntries.Select(e => new { saved = Saved(e.Model?.ToSerializable(-1)), base_price = Field<int>(e, "_cost"), price = e.Cost, stocked = e.IsStocked }).ToArray(),
                removal = removal == null ? null : (object)new { base_price = Field<int>(removal, "_cost"), price = removal.Cost, used = removal.Used } };
        }
        if (room is TreasureRoom) return new { kind = "treasure", relics = RunManager.Instance.TreasureRoomRelicSynchronizer.CurrentRelics?.Select(r => Saved(r.ToMutable().ToSerializable())).ToArray() ?? Array.Empty<JsonNode>(), picked = _runTreasurePicked };
        if (room is CombatRoom) return new { kind = "combat_end" };
        if (room.IsVictoryRoom) return new { kind = "victory" };
        throw new InvalidOperationException($"Unsupported run room: {room.GetType().Name}");
    }
}
