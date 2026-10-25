using System.Reflection;
using System.Reflection.Emit;
using System.Text.Json;
using HarmonyLib;
using MegaCrit.Sts2.Core.Runs;

namespace Sts2Headless;

public partial class RunSimulator
{
    private static long? _recordingClockSeconds;
    private static long _recordingClockStep;
    private static bool _recordingClockPatched;

    public static DateTimeOffset RecordingUtcNow() =>
        DateTimeOffset.FromUnixTimeSeconds(_recordingClockSeconds
            ?? throw new InvalidOperationException("Recording clock is not configured"));

    // Replace the source clock, rather than rewriting exported expected fields.
    // RunManager retains its native elapsed-time, pause, and win-time semantics.
    public static IEnumerable<CodeInstruction> RecordingClockSource(IEnumerable<CodeInstruction> instructions)
    {
        var original = typeof(DateTimeOffset).GetProperty(nameof(DateTimeOffset.UtcNow))!.GetMethod!;
        var replacement = typeof(RunSimulator).GetMethod(nameof(RecordingUtcNow))!;
        int replaced = 0;
        foreach (var instruction in instructions) {
            if (instruction.Calls(original)) {
                instruction.opcode = OpCodes.Call;
                instruction.operand = replacement;
                replaced++;
            }
            yield return instruction;
        }
        Require(replaced > 0, "Pinned RunManager clock source changed");
    }

    private static void ConfigureRecordingClock(JsonElement setup)
    {
        if (!setup.TryGetProperty("clock", out var clock)) return; // Legacy real-time recordings.
        Require(clock.GetProperty("kind").GetString() == "action_clock_v1", "Unknown recording clock");
        var epoch = clock.GetProperty("epoch_seconds").GetInt64();
        var step = clock.GetProperty("seconds_per_action").GetInt64();
        Require(epoch >= 0 && epoch <= 253402300799 && step >= 0 && step <= 86400, "Invalid recording clock");

        _recordingClockSeconds = epoch;
        _recordingClockStep = step;
        if (_recordingClockPatched) return;
        _recordingClockPatched = true;
        var harmony = new Harmony("sts2.recording.clock");
        foreach (var name in new[] { "SetUpTest", "SetUpNewSingleplayer", "InitializeShared", "get_RunTime", "set_IsPaused", "ToSave" }) {
            var method = typeof(RunManager).GetMethod(name,
                BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Instance)!;
            Require(method != null, $"Missing pinned clock method: {name}");
            harmony.Patch(method, transpiler: new HarmonyMethod(typeof(RunSimulator).GetMethod(nameof(RecordingClockSource))!));
        }
    }

    private static void AdvanceRecordingClock()
    {
        if (_recordingClockSeconds is not { } now) return;
        var next = checked(now + _recordingClockStep);
        Require(next <= 253402300799, "Recording clock exhausted");
        _recordingClockSeconds = next;
    }
}
