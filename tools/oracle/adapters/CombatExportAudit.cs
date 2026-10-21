// Optional live audit fixture. Never injected into ordinary recording builds.
using System.Collections;
using System.Reflection;
using MegaCrit.Sts2.Core.Combat;
using MegaCrit.Sts2.Core.Commands;
using MegaCrit.Sts2.Core.Commands.Builders;
using MegaCrit.Sts2.Core.Entities.Powers;
using MegaCrit.Sts2.Core.GameActions.Multiplayer;
using MegaCrit.Sts2.Core.Models;
using MegaCrit.Sts2.Core.Models.Cards;
using MegaCrit.Sts2.Core.Models.Enchantments;
using MegaCrit.Sts2.Core.Models.Afflictions;
using MegaCrit.Sts2.Core.Models.Orbs;
using MegaCrit.Sts2.Core.Models.Powers;
using MegaCrit.Sts2.Core.Models.Relics;

namespace Sts2Headless {
public partial class RunSimulator {
    public static void FreezeExportAuditClock(MegaCrit.Sts2.Core.Saves.SerializableRun __result)
    {
        __result.SaveTime = 0;
        __result.RunTime = 0;
        __result.StartTime = 0;
    }
    public Dictionary<string, object?> AuditFreezeClock()
    {
        new HarmonyLib.Harmony("sts2.audit.export.clock").Patch(
            typeof(MegaCrit.Sts2.Core.Runs.RunManager).GetMethod("ToSave")!,
            postfix: new HarmonyLib.HarmonyMethod(typeof(RunSimulator).GetMethod(nameof(FreezeExportAuditClock))!));
        return new() { ["type"] = "audit_clock" };
    }
    private static void SetAuditField(object value, string name, object? replacement)
    {
        for (var type = value.GetType(); type != null; type = type.BaseType)
            if (type.GetField(name, BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.DeclaredOnly) is { } field) {
                field.SetValue(value, replacement); return;
            }
        throw new InvalidOperationException($"Missing audit field: {name}");
    }
    public Dictionary<string, object?> AuditGenericContract()
    {
        var count = CombatModelFields.CheckContract();
        bool unknownFailed = false;
        try { CombatModelFields.Capture(new MegaCrit.Sts2.Core.Models.Powers.UnclassifiedAuditPower(), _ => "/audit/ref"); }
        catch (InvalidOperationException e) { unknownFailed = e.Message.Contains("Unclassified combat model field"); }
        Require(unknownFailed, "Unclassified gameplay storage was silently ignored");
        var overridden = ModelDb.Power<ConfusedPower>().ToMutable();
        SetAuditField(overridden, "_testEnergyCostOverride", 2);
        bool overrideFailed = false;
        try { CombatModelFields.Capture(overridden, _ => "/audit/ref"); }
        catch (InvalidOperationException e) { overrideFailed = e.Message.Contains("Active test override"); }
        Require(overrideFailed, "Active test override was silently ignored");
        return new() { ["type"] = "audit_contract", ["members"] = count, ["unknown_field_rejected"] = unknownFailed };
    }
    public Dictionary<string, object?> AuditGenericGraph(System.Text.Json.JsonElement command)
    {
        var player = _runState!.Players.Single();
        var combat = CombatManager.Instance.DebugOnlyGetState()!;
        var choice = new BlockingPlayerChoiceContext();
        var card = combat.CreateCard<Claw>(player);
        player.PlayerCombatState!.Hand.AddInternal(card);
        SetAuditField(card, "_extraDamageFromClawPlays", 0.1234567890123456789012345678m);
        var clone = card.CreateClone();
        player.PlayerCombatState.DiscardPile.AddInternal(clone);
        CardCmd.Enchant<Glam>(card, 1);
        CardCmd.Afflict<Hexed>(card, 1).GetAwaiter().GetResult();
        var afterimage = PowerCmd.Apply<AfterimagePower>(choice, player.Creature, 1, player.Creature, null).GetAwaiter().GetResult()!;
        var data = Field<object>(afterimage, "_internalData");
        Field<Dictionary<CardModel, int>>(data, "amountsForPlayedCards").Add(card, 2);
        var nightmare = PowerCmd.Apply<NightmarePower>(choice, player.Creature, 1, player.Creature, null).GetAwaiter().GetResult()!;
        var floating = combat.CreateCard<Rampage>(player);
        SetAuditField(Field<object>(nightmare, "_internalData"), "selectedCard", floating);
        SetAuditField(floating, "_extraDamageFromPlays", 7m);
        var vigor = PowerCmd.Apply<VigorPower>(choice, player.Creature, 2, player.Creature, null).GetAwaiter().GetResult()!;
        var calculatedCard = combat.CreateCard<BodySlam>(player);
        var attack = new AttackCommand(calculatedCard.DynamicVars.CalculatedDamage).FromCard(calculatedCard, null).Targeting(combat.Enemies[0]);
        SetAuditField(Field<object>(vigor, "_internalData"), "commandToModify", attack);
        var art = RelicCmd.Obtain<ArtOfWar>(player).GetAwaiter().GetResult();
        SetAuditField(art, "_anyAttacksPlayedThisTurn", true);
        RelicCmd.Obtain<Pocketwatch>(player).GetAwaiter().GetResult();
        OrbCmd.AddSlots(player, 2).GetAwaiter().GetResult();
        OrbCmd.Channel<DarkOrb>(choice, player).GetAwaiter().GetResult();
        OrbCmd.Channel<GlassOrb>(choice, player).GetAwaiter().GetResult();
        AwaitRunBoundary();
        var result = OracleRunState(command);
        // A retained callback cannot be described by the stored attack fields.
        SetAuditField(attack, "_beforeDamage", (Func<Task>)(() => Task.CompletedTask));
        bool callbackFailed = false;
        try { ExportRunFlowState(); }
        catch (InvalidOperationException e) { callbackFailed = e.Message.Contains("explicit suspended-effect handler"); }
        finally { SetAuditField(attack, "_beforeDamage", null); }
        Require(callbackFailed, "Uncaptured attack callback was silently ignored");
        result["callback_rejected"] = callbackFailed;
        return result;
    }
}
}
namespace MegaCrit.Sts2.Core.Models.Powers {
// Verifies that adding an unknown field cannot silently pass generic export.
internal sealed class UnclassifiedAuditPower : PowerModel {
    private int _uncapturedGameplayCounter = 3;
    public override PowerType Type => PowerType.Buff;
    public override PowerStackType StackType => PowerStackType.Counter;
}
}
