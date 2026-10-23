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
using MegaCrit.Sts2.Core.Models.Potions;

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
    public Dictionary<string, object?> AuditProcureBlockPotion(System.Text.Json.JsonElement command)
    {
        var potion = ModelDb.Potion<BlockPotion>().ToMutable();
        var result = PotionCmd.TryToProcure(potion, _runState!.Players.Single()).GetAwaiter().GetResult();
        Require(result.success, "Audit could not obtain Block Potion");
        Require(CombatModelFields.DynamicValues(potion).Count == 0, "Expected uninitialized potion variables before boundary");
        var response = OracleRunState(command);
        response["potion_vars"] = CombatModelFields.DynamicValues(potion);
        return response;
    }
    public Dictionary<string, object?> AuditRemovedMonster(System.Text.Json.JsonElement command)
    {
        var combat = CombatManager.Instance.DebugOnlyGetState()!;
        var target = combat.Enemies.Single(c => c.Monster!.Id.Entry == "TWIG_SLIME_S");
        Require(target.Monster!.MoveStateMachine != null, "Expected live monster AI before death");
        CreatureCmd.Damage(new BlockingPlayerChoiceContext(), target, 100m,
            MegaCrit.Sts2.Core.ValueProps.ValueProp.Unpowered, _runState!.Players.Single().Creature)
            .GetAwaiter().GetResult();
        AwaitRunBoundary();
        Require(target.IsDead && target.Monster.MoveStateMachine == null, "Monster removal did not clear AI");
        return OracleRunState(command);
    }
    public Dictionary<string, object?> AuditPrepareReattach(System.Text.Json.JsonElement command)
    {
        var player = _runState!.Players.Single();
        var combat = CombatManager.Instance.DebugOnlyGetState()!;
        Require(combat.Enemies.All(c => c.HasPower<ReattachPower>()), "Expected Decimillipede segments");
        foreach (var enemy in combat.Enemies) SetAuditField(enemy, "_currentHp", 1);
        for (var i = 0; i < 3; ++i)
            player.PlayerCombatState!.ExhaustPile.AddInternal(combat.CreateCard<StrikeIronclad>(player));
        var card = combat.CreateCard<PactsEnd>(player);
        player.PlayerCombatState!.Hand.AddInternal(card);
        var response = OracleRunState(command);
        response["card_index"] = player.PlayerCombatState.Hand.Cards.Count - 1;
        return response;
    }
    public Dictionary<string, object?> AuditPrepareEggKill(System.Text.Json.JsonElement command)
    {
        var player = _runState!.Players.Single();
        var combat = CombatManager.Instance.DebugOnlyGetState()!;
        var eggs = combat.Enemies.Where(c => c.Monster is MegaCrit.Sts2.Core.Models.Monsters.ToughEgg).ToArray();
        Require(eggs.Length > 0, "Expected Tough Eggs");
        foreach (var egg in eggs) SetAuditField(egg, "_currentHp", 1);
        for (var i = 0; i < 3; ++i)
            player.PlayerCombatState!.ExhaustPile.AddInternal(combat.CreateCard<StrikeIronclad>(player));
        player.PlayerCombatState!.Hand.AddInternal(combat.CreateCard<PactsEnd>(player));
        var response = OracleRunState(command);
        response["card_index"] = player.PlayerCombatState.Hand.Cards.Count - 1;
        return response;
    }
    public Dictionary<string, object?> AuditKillHopper(System.Text.Json.JsonElement command)
    {
        var player = _runState!.Players.Single();
        var hopper = CombatManager.Instance.DebugOnlyGetState()!.Enemies.Single();
        var stolen = hopper.Powers.OfType<SwipePower>().Single().StolenCard!;
        var original = stolen.DeckVersion!;
        Require(original.HasBeenRemovedFromState, "Hopper did not remove the original card");
        CreatureCmd.Damage(new BlockingPlayerChoiceContext(), hopper, 1000m,
            MegaCrit.Sts2.Core.ValueProps.ValueProp.Unpowered, player.Creature).GetAwaiter().GetResult();
        Require(!original.HasBeenRemovedFromState, "Hopper death did not register the original for return");
        CombatManager.Instance.CheckWinCondition().GetAwaiter().GetResult();
        var response = OracleRunState(command);
        response["returned_card"] = NativeRunRoundTrip.Capture(original.ToSerializable());
        return response;
    }
    public Dictionary<string, object?> AuditEventCombat(System.Text.Json.JsonElement command)
    {
        var room = _runState!.CurrentRoom as MegaCrit.Sts2.Core.Rooms.CombatRoom
            ?? throw new InvalidOperationException("Expected an event combat room");
        Require(room.ParentEventId != null && !room.IsPreFinished, "Expected active nested event combat");
        bool rejected = false;
        try { room.ToSerializable(); }
        catch (InvalidOperationException e) { rejected = e.Message.Contains("ParentEventId"); }
        Require(rejected && !room.IsPreFinished, "Native save guard or live pre-finished flag changed");
        if (command.TryGetProperty("win", out var win) && win.GetBoolean()) {
            foreach (var enemy in room.CombatState.Enemies.ToArray())
                CreatureCmd.Damage(new BlockingPlayerChoiceContext(), enemy, 10000m,
                    MegaCrit.Sts2.Core.ValueProps.ValueProp.Unpowered, _runState.Players.Single().Creature).GetAwaiter().GetResult();
            CombatManager.Instance.CheckWinCondition().GetAwaiter().GetResult();
            Require(!CombatManager.Instance.IsInProgress, "Audit did not complete event combat");
        }
        return OracleRunState(command);
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
