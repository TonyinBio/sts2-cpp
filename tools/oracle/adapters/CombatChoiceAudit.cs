// Live fixtures only; these methods are never added to production recording builds.
using System.Text.Json;
using HarmonyLib;
using MegaCrit.Sts2.Core.Combat;
using MegaCrit.Sts2.Core.Models.Cards;
using MegaCrit.Sts2.Core.Runs;
using MegaCrit.Sts2.Core.Saves;
namespace Sts2Headless;
public partial class RunSimulator {
    public static void FreezeAuditClock(SerializableRun __result) {
        __result.SaveTime = 0;
        __result.RunTime = 0;
    }
    public Dictionary<string, object?> AuditChoiceSetup(JsonElement command) {
        new Harmony("sts2.audit.clock").Patch(typeof(RunManager).GetMethod("ToSave")!,
            postfix: new HarmonyMethod(typeof(RunSimulator).GetMethod(nameof(FreezeAuditClock))!));
        var player = _runState!.Players.Single();
        var combat = CombatManager.Instance.DebugOnlyGetState()!;
        var kind = command.GetProperty("card").GetString();
        var card = kind == "Survivor" ? (MegaCrit.Sts2.Core.Models.CardModel)combat.CreateCard<Survivor>(player)
            : combat.CreateCard<Discovery>(player);
        player.PlayerCombatState!.Hand.AddInternal(card);
        RegisterCombatIdentities();
        var result = OracleRunState(command);
        result["card_instance"] = Id(card);
        return result;
    }
}
