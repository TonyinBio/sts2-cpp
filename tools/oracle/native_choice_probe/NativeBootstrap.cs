using System;
using System.Linq;
using MegaCrit.Sts2.Core.Combat;
using MegaCrit.Sts2.Core.Context;
using MegaCrit.Sts2.Core.Entities.Players;
using MegaCrit.Sts2.Core.Models;
using MegaCrit.Sts2.Core.Models.Characters;
using MegaCrit.Sts2.Core.Multiplayer;
using MegaCrit.Sts2.Core.Runs;
using MegaCrit.Sts2.Core.Saves;
using MegaCrit.Sts2.Core.TestSupport;
using MegaCrit.Sts2.Core.Unlocks;
public static class NativeBootstrap {
    public static System.Threading.Tasks.Task BeginPurity(object[] cards) {
        var owner=((CardModel)cards[0]).Owner;
        var source=owner.Creature.CombatState!.CreateCard<MegaCrit.Sts2.Core.Models.Cards.Purity>(owner);
        MegaCrit.Sts2.Core.Helpers.NonInteractiveMode.AutoSlayerCheck=()=>true;
        var task=source.OnPlayWrapper(new MegaCrit.Sts2.Core.GameActions.Multiplayer.BlockingPlayerChoiceContext(),null,true,
            new MegaCrit.Sts2.Core.Entities.Cards.ResourceInfo{EnergySpent=0,EnergyValue=0,StarsSpent=0,StarValue=0},true);
        TestMode.IsOn=false;
        return task;
    }
    public static int[] ExhaustedIndices(object[] cards) {
        var owner=((CardModel)cards[0]).Owner;
        return owner.PlayerCombatState!.ExhaustPile.Cards.Select(c=>Array.FindIndex(cards,x=>ReferenceEquals(x,c))).ToArray();
    }

    public static void AddCardViews(object hand,object[] cards) {
        TestMode.IsOn=false;
        MegaCrit.Sts2.Core.Nodes.Pooling.NodePool.Init<MegaCrit.Sts2.Core.Nodes.Cards.NCard>("res://scenes/cards/card.tscn",0);
        foreach(var card in cards)((MegaCrit.Sts2.Core.Nodes.Combat.NPlayerHand)hand).Add(MegaCrit.Sts2.Core.Nodes.Cards.NCard.Create((CardModel)card)!);
    }

    public static object[] SetUp() {
        TestMode.IsOn=true;
        MegaCrit.Sts2.Core.Modding.ModManager.Initialize(new MegaCrit.Sts2.Core.Modding.ModManagerFileIo(),null,null).GetAwaiter().GetResult();
        MegaCrit.Sts2.Core.Modding.AssemblyInfo.Init();
        foreach(var type in AbstractModelSubtypes.All)ModelDb.Inject(type);
        SaveManager.Instance.InitProfileId(0);
        SaveManager.Instance.InitProgressData();
        var player=Player.CreateForNewRun<Ironclad>(UnlockState.all,1);
        var state=RunState.CreateForTest(players:new[]{player},seed:"CHOICEPROBE");
        var service=new NetSingleplayerGameService();
        RunManager.Instance.SetUpTest(state,service);
        LocalContext.NetId=service.NetId;
        var encounter=ModelDb.Encounter<MegaCrit.Sts2.Core.Models.Encounters.ShrinkerBeetleWeak>().ToMutable();
        encounter.GenerateMonstersWithSlots(state);
        var combat=new CombatState(encounter:encounter,runState:state);
        combat.AddPlayer(player);
        foreach(var (monster,slot) in encounter.MonstersWithSlots)combat.AddCreature(combat.CreateCreature(monster,CombatSide.Enemy,slot));
        CombatManager.Instance.SetUpCombat(combat);
        CombatManager.Instance.AfterCombatRoomLoaded();
        if(!CombatManager.Instance.IsInProgress)throw new Exception("Native combat did not start");
        var cards=player.PlayerCombatState!.Hand.Cards.Take(4).ToArray();
        return cards.Cast<object>().ToArray();
    }
}
