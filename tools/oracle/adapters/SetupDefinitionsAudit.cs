// Build-time immutable definitions and independent seeded-setup evidence only.
using System.Reflection;
using System.Text.Json;
using MegaCrit.Sts2.Core.Models;
using MegaCrit.Sts2.Core.Models.RelicPools;
using MegaCrit.Sts2.Core.Models.PotionPools;
using MegaCrit.Sts2.Core.Entities.Cards;
using MegaCrit.Sts2.Core.Models.Events;
using MegaCrit.Sts2.Core.Events;
using MegaCrit.Sts2.Core.Unlocks;
using MegaCrit.Sts2.Core.Runs;
using MegaCrit.Sts2.Core.Random;
using MegaCrit.Sts2.Core.Map;
using MegaCrit.Sts2.Core.Entities.Ascension;

namespace Sts2Headless;
public partial class RunSimulator
{
    public Dictionary<string, object?> SetupDefinitions(JsonElement command)
    {
        object Encounters(IEnumerable<EncounterModel> list) => list.Select(e => new { id = e.Id.ToString(), tags = e.Tags.Select(t => t.ToString()).ToArray() }).ToArray();
        object Relics(IEnumerable<RelicModel> list) => list.Select(r => new { id = r.Id.ToString(), rarity = r.Rarity.ToString() }).ToArray();
        IEnumerable<CardModel> Cards(CharacterModel c) => c.CardPool.GetUnlockedCards(UnlockState.all, CardMultiplayerConstraint.SingleplayerOnly);
        object CardEntries(IEnumerable<CardModel> cards) => cards.Select(c => new { id = c.Id.ToString(), rarity = c.Rarity.ToString(), upgradable = c.ToMutable().IsUpgradable }).ToArray();
        object PotionEntries(IEnumerable<PotionModel> potions) => potions.Select(p => new { id = p.Id.ToString(), rarity = p.Rarity.ToString() }).ToArray();
        var sharedPotions = ModelDb.PotionPool<SharedPotionPool>().GetUnlockedPotions(UnlockState.all).ToArray();
        var acts = ModelDb.Acts.Where(a => a.Index >= 0).Select(a => new {
            id = a.Id.ToString(), index = a.Index, is_default = a.IsDefault, rooms = a.GetNumberOfRooms(false),
            weak_count = (int)typeof(ActModel).GetProperty("NumberOfWeakEncounters", BindingFlags.Instance | BindingFlags.NonPublic)!.GetValue(a)!,
            events = a.AllEvents.Select(e => e.Id.ToString()).ToArray(),
            ancients = a.GetUnlockedAncients(UnlockState.all).Select(e => e.Id.ToString()).ToArray(),
            weak = Encounters(a.AllWeakEncounters), normal = Encounters(a.AllRegularEncounters),
            elite = Encounters(a.AllEliteEncounters), bosses = Encounters(a.AllBossEncounters)
        }).ToArray();
        var characters = ModelDb.AllCharacters.Select(c => new {
            id = c.Id.ToString(), hp = c.StartingHp, gold = c.StartingGold, orb_slots = c.BaseOrbSlotCount, energy = c.MaxEnergy,
            deck = c.StartingDeck.Select(card => card.Id.ToString()).ToArray(),
            relics = c.StartingRelics.Select(r => r.Id.ToString()).ToArray(),
            relic_pool = Relics(c.RelicPool.GetUnlockedRelics(UnlockState.all)),
            card_pool_id = c.CardPool.Id.ToString(), card_pool = CardEntries(Cards(c)),
            potion_pool = PotionEntries(c.PotionPool.GetUnlockedPotions(UnlockState.all))
        }).ToArray();
        var neow = (Neow)ModelDb.AncientEvent<Neow>().ToMutable();
        string[] NeowOptions(string name) => ((IEnumerable<EventOption>)typeof(Neow).GetProperty(name, BindingFlags.Instance | BindingFlags.NonPublic)!.GetValue(neow)!)
            .Select(o => o.Relic!.Id.ToString()).ToArray();
        var defaultCards = ModelDb.AllCharacters.SelectMany(c => c.StartingDeck.Concat(Cards(c)))
            .Append(ModelDb.Card<MegaCrit.Sts2.Core.Models.Cards.AscendersBane>()).DistinctBy(c => c.Id)
            .ToDictionary(c => c.Id.ToString(), c => NativeRunRoundTrip.Capture(c.ToMutable().ToSerializable()));
        var defaultPotions = ModelDb.AllCharacters.SelectMany(c => c.PotionPool.GetUnlockedPotions(UnlockState.all)).Concat(sharedPotions).DistinctBy(p => p.Id)
            .ToDictionary(p => p.Id.ToString(), p => NativeRunRoundTrip.Capture(p.ToMutable().ToSerializable(-1)));
        var defaultRelics = ModelDb.AllCharacters.SelectMany(c => c.StartingRelics)
            .Concat(neow.AllPossibleOptions.Select(o => o.Relic!)).DistinctBy(r => r.Id)
            .ToDictionary(r => r.Id.ToString(), r => NativeRunRoundTrip.Capture((r.IsMutable ? r : r.ToMutable()).ToSerializable()));
        return new() { ["native_defaults"] = new { cards = defaultCards, relics = defaultRelics, potions = defaultPotions, unlock_state = NativeRunRoundTrip.Capture(UnlockState.all.ToSerializable()) },
            ["shared_potions"] = PotionEntries(sharedPotions),
            ["neow_positive"] = NeowOptions("PositiveOptions"), ["neow_curses"] = NeowOptions("CurseOptions"),
            ["neow_all"] = neow.AllPossibleOptions.Select(o => o.Relic!.Id.ToString()).ToArray(),
            ["type"] = "setup_definitions", ["acts"] = acts, ["characters"] = characters,
            ["shared_events"] = ModelDb.AllSharedEvents.Select(e => e.Id.ToString()).ToArray(),
            ["shared_ancients"] = UnlockState.all.SharedAncients.Select(e => e.Id.ToString()).ToArray(),
            ["shared_relics"] = Relics(ModelDb.RelicPool<SharedRelicPool>().GetUnlockedRelics(UnlockState.all)) };
    }
    public Dictionary<string, object?> SetupMapAudit(JsonElement command)
    {
        var act = ModelDb.Acts.Single(a => a.Id.ToString() == command.GetProperty("act").GetString());
        int ascension = command.GetProperty("ascension").GetInt32();
        typeof(RunManager).GetProperty(nameof(RunManager.AscensionManager))!.SetValue(RunManager.Instance, new AscensionManager(ascension));
        var rng = new Rng(command.GetProperty("numeric_seed").GetUInt64(), $"act_{act.Index + 1}_map");
        var map = new StandardActMap(rng, act, false, false, act.Index == 2 && ascension >= 10);
        object Node(MapPoint n) => new { col = n.coord.col, row = n.coord.row, kind = (int)n.PointType,
            can_modify = n.CanBeModified, children = n.Children.Select(c => new { col = c.coord.col, row = c.coord.row }).ToArray() };
        return new() { ["type"] = "setup_map", ["points"] = map.GetAllMapPoints().Select(Node).ToArray(),
            ["start"] = Node(map.StartingMapPoint), ["boss"] = Node(map.BossMapPoint),
            ["second_boss"] = map.SecondBossMapPoint == null ? null : Node(map.SecondBossMapPoint),
            ["rng"] = new { counter = rng.ToSerializable().counter, words = new[] { rng.ToSerializable().state0, rng.ToSerializable().state1, rng.ToSerializable().state2, rng.ToSerializable().state3 } } };
    }
}
