// Opt-in diagnostic lifecycle. Recording commands remain single-run by default.
using System.Text.Json;
using MegaCrit.Sts2.Core.Combat;
using MegaCrit.Sts2.Core.Commands;
using MegaCrit.Sts2.Core.Rewards;
using MegaCrit.Sts2.Core.Rooms;
using MegaCrit.Sts2.Core.Runs;
using MegaCrit.Sts2.Core.Saves;
using MegaCrit.Sts2.Core.Saves.Validation;

namespace Sts2Headless;
public partial class RunSimulator
{
    private IDisposable? _warmSelectorScope;
    private static string? _warmInitialProgress;
    private static void WarmCaptureProgress() {
        EnsureModelDbInitialized();
        _warmInitialProgress ??= JsonSerializer.Serialize(SaveManager.Instance.Progress.ToSerializable(),
            JsonSerializationUtility.Options);
    }
    private void WarmTurnStarted(CombatState _) => _turnStarted.Set();
    private void WarmCombatEnded(CombatRoom _) => _combatEnded.Set();
    private void WarmCombatWon(CombatRoom _) => _runCombatWon = true;
    private void WarmRunCombatEnded(CombatRoom _) {
        if (!_runCombatWon && _oracleRunEndingReason == null) _runDeathObserved = true;
    }
    public Dictionary<string, object?> OracleReset(JsonElement command)
    {
        Require(_oracleRunMode, "Reset requires an oracle run");
        Require(_runSelector.Pending == null, "Cannot reset a pending selection");
        Require(_runRewards.All(r => r.Done.Task.IsCompleted), "Cannot reset pending rewards");
        Require(_runOperations.All(o => o.Task.IsCompleted), "Cannot reset suspended operations");
        // Dispose the registered selector before CleanUp resets the native stack.
        _warmSelectorScope?.Dispose();
        _warmSelectorScope = null;
        CombatManager.Instance.TurnStarted -= WarmTurnStarted;
        CombatManager.Instance.CombatEnded -= WarmCombatEnded;
        CombatManager.Instance.CombatWon -= WarmCombatWon;
        CombatManager.Instance.CombatEnded -= WarmRunCombatEnded;
        RunManager.Instance.CleanUp();
        Require(CardSelectCmd.Selector == null && CardSelectCmd.LocalSelector == null,
            "Native teardown retained selectors");
        var listeners = new Dictionary<string, int>();
        var audit = !command.TryGetProperty("audit", out var requestedAudit) || requestedAudit.GetBoolean();
        foreach (var name in audit ? new[] { "TurnStarted", "CombatEnded", "CombatWon" } : Array.Empty<string>()) {
            var field = typeof(CombatManager).GetField(name,
                System.Reflection.BindingFlags.Instance | System.Reflection.BindingFlags.NonPublic)!;
            var handlers = (field.GetValue(CombatManager.Instance) as Delegate)?.GetInvocationList()
                ?? Array.Empty<Delegate>();
            Require(handlers.All(handler => !ReferenceEquals(handler.Target, this)),
                "Native singleton retained disposed simulator listener");
            listeners[name] = handlers.Length;
        }
        Require(typeof(RunManager).GetProperty("State", System.Reflection.BindingFlags.Instance | System.Reflection.BindingFlags.NonPublic)!.GetValue(RunManager.Instance) == null, "Native teardown retained state");
        SaveManager.Instance.Progress = ProgressState.FromSerializable(
            JsonSerializer.Deserialize<SerializableProgress>(_warmInitialProgress!, JsonSerializationUtility.Options)!,
            new DeserializationContext());
        RewardsSet.testSelector = null;
        RunEndingSession = null;
        RunOwnerContext.Value = null;
        RunChoiceContextCapture.Prefs.Value = null;
        LocPatches._bundleSimRef = null;
        _recordingClockSeconds = null;
        _recordingClockStep = 0;
        _turnStarted.Dispose();
        _combatEnded.Dispose();
        return new() { ["type"] = "oracle_reset", ["reset"] = true, ["singleton_listeners"] = listeners };
    }
}
