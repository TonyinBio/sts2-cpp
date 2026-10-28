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
    public static async System.Threading.Tasks.Task BeginPileCommand(object[] cards) {
        var owner=((CardModel)cards[0]).Owner;
        foreach(var card in cards.Take(3))await MegaCrit.Sts2.Core.Commands.CardPileCmd.Add((CardModel)card,MegaCrit.Sts2.Core.Entities.Cards.PileType.Discard,skipVisuals:true);
        var source=owner.Creature.CombatState!.CreateCard<MegaCrit.Sts2.Core.Models.Cards.Headbutt>(owner);
        MegaCrit.Sts2.Core.Helpers.NonInteractiveMode.AutoSlayerCheck=()=>true;
        var selected=await MegaCrit.Sts2.Core.Commands.CardSelectCmd.FromCombatPile(
            new MegaCrit.Sts2.Core.GameActions.Multiplayer.BlockingPlayerChoiceContext(),
            owner.PlayerCombatState!.DiscardPile,owner,
            new MegaCrit.Sts2.Core.CardSelection.CardSelectorPrefs((MegaCrit.Sts2.Core.Localization.LocString)typeof(CardModel).GetProperty("SelectionScreenPrompt",System.Reflection.BindingFlags.Instance|System.Reflection.BindingFlags.NonPublic)!.GetValue(source)!,1));
        foreach(var card in selected)await MegaCrit.Sts2.Core.Commands.CardPileCmd.Add(card,MegaCrit.Sts2.Core.Entities.Cards.PileType.Draw,MegaCrit.Sts2.Core.Entities.Cards.CardPilePosition.Top,skipVisuals:true);
    }
    public static bool IsDrawTop(object[] cards,int index) => ReferenceEquals(((CardModel)cards[0]).Owner.PlayerCombatState!.DrawPile.Cards.First(),cards[index]);
    public static object[] CandidateModels(object[] cards) => cards.Select(c=>(object)new{model_id=((CardModel)c).Id.ToString(),upgraded=((CardModel)c).IsUpgraded}).ToArray();
    public static string GameplayFingerprint(object[] cards) {
        var owner=((CardModel)cards[0]).Owner;
        var combat=owner.Creature.CombatState!;
        return System.Text.Json.JsonSerializer.Serialize(new {
            run_rng=owner.RunState.Rng.ToSerializable(),player_rng=owner.PlayerRng.ToSerializable(),
            hp=owner.Creature.CurrentHp,block=owner.Creature.Block,energy=owner.PlayerCombatState!.Energy,
            round=combat.RoundNumber,
            enemies=combat.Enemies.Select(c=>new{hp=c.CurrentHp,block=c.Block}).ToArray(),
            piles=owner.PlayerCombatState.AllPiles.Select(p=>new{type=p.Type.ToString(),cards=p.Cards.Select(c=>new{id=c.Id.ToString(),upgrade=c.CurrentUpgradeLevel}).ToArray()}).ToArray()
        },new System.Text.Json.JsonSerializerOptions{IncludeFields=true});
    }
    public static System.Threading.Tasks.Task BeginArmaments(object[] cards) {
        var owner=((CardModel)cards[0]).Owner;
        var source=(MegaCrit.Sts2.Core.Models.Cards.Armaments)owner.PlayerCombatState!.Hand.Cards.Single(c=>c is MegaCrit.Sts2.Core.Models.Cards.Armaments);
        MegaCrit.Sts2.Core.Helpers.NonInteractiveMode.AutoSlayerCheck=()=>true;
        var task=source.OnPlayWrapper(new MegaCrit.Sts2.Core.GameActions.Multiplayer.BlockingPlayerChoiceContext(),null,true,
            new MegaCrit.Sts2.Core.Entities.Cards.ResourceInfo{EnergySpent=0,EnergyValue=0,StarsSpent=0,StarValue=0},true);
        TestMode.IsOn=false;
        return task;
    }
    public static int[] UpgradedIndices(object[] cards) => cards.Select((c,i)=>(c,i)).Where(x=>((CardModel)x.c).IsUpgraded).Select(x=>x.i).ToArray();
    public static System.Threading.Tasks.Task BeginPurity(object[] cards) {
        var owner=((CardModel)cards[0]).Owner;
        var source=(MegaCrit.Sts2.Core.Models.Cards.Purity)owner.PlayerCombatState!.Hand.Cards.Single(c=>c is MegaCrit.Sts2.Core.Models.Cards.Purity);
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
        foreach(var card in ((CardModel)cards[0]).Owner.PlayerCombatState!.Hand.Cards)((MegaCrit.Sts2.Core.Nodes.Combat.NPlayerHand)hand).Add(MegaCrit.Sts2.Core.Nodes.Cards.NCard.Create(card)!);
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
        player.Deck.Clear(silent:true);
        for(var index=0;index<4;index++)MegaCrit.Sts2.Core.Commands.CardPileCmd.Add(state.CreateCard<MegaCrit.Sts2.Core.Models.Cards.DefendIronclad>(player),player.Deck,skipVisuals:true).GetAwaiter().GetResult();
        var probeCase=Environment.GetEnvironmentVariable("STS2_CHOICE_CASE");
        if(probeCase=="purity-consumer")MegaCrit.Sts2.Core.Commands.CardPileCmd.Add(state.CreateCard<MegaCrit.Sts2.Core.Models.Cards.Purity>(player),player.Deck,skipVisuals:true).GetAwaiter().GetResult();
        if(probeCase=="armaments-consumer")MegaCrit.Sts2.Core.Commands.CardPileCmd.Add(state.CreateCard<MegaCrit.Sts2.Core.Models.Cards.Armaments>(player),player.Deck,skipVisuals:true).GetAwaiter().GetResult();
        var encounter=ModelDb.Encounter<MegaCrit.Sts2.Core.Models.Encounters.ShrinkerBeetleWeak>().ToMutable();
        encounter.GenerateMonstersWithSlots(state);
        var combat=new CombatState(encounter:encounter,runState:state);
        combat.AddPlayer(player);
        foreach(var (monster,slot) in encounter.MonstersWithSlots)combat.AddCreature(combat.CreateCreature(monster,CombatSide.Enemy,slot));
        CombatManager.Instance.SetUpCombat(combat);
        CombatManager.Instance.AfterCombatRoomLoaded();
        if(!CombatManager.Instance.IsInProgress)throw new Exception("Native combat did not start");
        var cards=player.PlayerCombatState!.Hand.Cards.Where(c=>c is MegaCrit.Sts2.Core.Models.Cards.DefendIronclad).ToArray();
        return cards.Cast<object>().ToArray();
    }
}
