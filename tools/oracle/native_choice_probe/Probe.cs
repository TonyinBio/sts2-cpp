using Godot;
using System;
using System.IO;
using System.Reflection;
using System.Runtime.Loader;
public partial class Probe : Node {
    public override async void _Ready() {
        try {
            var dir=System.Environment.GetEnvironmentVariable("STS2_CHOICE_GAME_DLL_DIR")!;
            AssemblyLoadContext.GetLoadContext(typeof(Probe).Assembly)!.Resolving += (ctx,name) => {
                var p=Path.Combine(dir,name.Name+".dll"); return File.Exists(p) ? ctx.LoadFromAssemblyPath(p) : null;
            };
            var a=AssemblyLoadContext.GetLoadContext(typeof(Probe).Assembly)!.LoadFromAssemblyPath(Path.Combine(dir,"sts2.dll"));
            GD.Print("PROBE context ",AssemblyLoadContext.GetLoadContext(a)," ",AssemblyLoadContext.GetLoadContext(typeof(Node).Assembly));
            Godot.Bridge.ScriptManagerBridge.LookupScriptsInAssembly(a);
            GD.Print("PROBE assembly ",a.FullName," Godot ",typeof(Node).Assembly.FullName);
            GD.Print("PROBE pack ",ProjectSettings.LoadResourcePack(System.Environment.GetEnvironmentVariable("STS2_CHOICE_GAME_PACK")!,false));
            var gameType=a.GetType("MegaCrit.Sts2.Core.Nodes.NGame",true)!;
            var game=(Control)Activator.CreateInstance(gameType)!; game.Size=new Vector2(1920,1080);
            gameType.GetProperty("Instance")!.SetValue(null,game);
            object Construct(string name)=>Activator.CreateInstance(a.GetType(name,true)!)!;
            gameType.GetProperty("RootSceneContainer")!.SetValue(game,Construct("MegaCrit.Sts2.Core.Nodes.NSceneContainer"));
            var input=Construct("MegaCrit.Sts2.Core.Nodes.CommonUi.NInputManager");
            input.GetType().GetProperty("ControllerManager")!.SetValue(input,Construct("MegaCrit.Sts2.Core.Nodes.CommonUi.NControllerManager"));
            gameType.GetProperty("InputManager")!.SetValue(game,input);
            gameType.GetProperty("HotkeyManager")!.SetValue(game,Construct("MegaCrit.Sts2.Core.Nodes.CommonUi.NHotkeyManager"));
            Type T(string name)=>a.GetType(name,true)!;
            object Get(object o,string name)=>o.GetType().GetProperty(name)!.GetValue(o)!;
            void Set(object o,string name,object value)=>o.GetType().GetProperty(name,BindingFlags.Instance|BindingFlags.Public|BindingFlags.NonPublic)!.SetValue(o,value);
            object? Invoke(object o,string name,params object?[] args)=>o.GetType().GetMethod(name,BindingFlags.Instance|BindingFlags.Public|BindingFlags.NonPublic)!.Invoke(o,args);
            T("MegaCrit.Sts2.Core.TestSupport.TestMode").GetProperty("IsOn")!.SetValue(null,true);
            var save=T("MegaCrit.Sts2.Core.Saves.SaveManager").GetProperty("Instance")!.GetValue(null)!;
            Invoke(save,"InitSettingsDataForTest"); Invoke(save,"InitPrefsDataForTest");
            Set(Get(save,"SettingsSave"),"Language","eng");
            T("MegaCrit.Sts2.Core.Localization.LocManager").GetMethod("Initialize")!.Invoke(null,null);
            var cards=NativeBootstrap.SetUp();
            GD.Print("PROBE native bootstrap cards ",cards.Length);
            var run=Construct("MegaCrit.Sts2.Core.Nodes.NRun");
            Invoke(Get(game,"RootSceneContainer"),"SetCurrentScene",run);
            var roomContainer=Construct("MegaCrit.Sts2.Core.Nodes.NSceneContainer");
            run.GetType().GetField("_roomContainer",BindingFlags.Instance|BindingFlags.NonPublic)!.SetValue(run,roomContainer);
            var room=ResourceLoader.Load<PackedScene>("res://scenes/rooms/combat_room.tscn").Instantiate();
            Invoke(roomContainer,"SetCurrentScene",room);
            var ui=room.GetNode<Control>("%CombatUi"); Set(room,"Ui",ui);
            ui.GetParent().RemoveChild(ui); ui.Owner=null; AddChild(ui);
            var hand=ui.GetNode<Control>("%Hand");
            GD.Print("PROBE native hand ready ",hand.IsNodeReady()," type ",hand.GetType().FullName);
            NativeBootstrap.AddCardViews(hand,cards);
            var caseName=System.Environment.GetEnvironmentVariable("STS2_CHOICE_CASE") ?? "ordered";
            var minimum=caseName=="single" ? 1 : 0;
            var maximum=caseName=="single" ? 1 : caseName=="purity-consumer" ? 3 : 2;
            var observations=new System.Collections.Generic.List<object>();
            void Require(bool condition,string message) { if(!condition)throw new InvalidOperationException(message); }
            var prefsType=T("MegaCrit.Sts2.Core.CardSelection.CardSelectorPrefs");
            var prompt=prefsType.GetProperty("ExhaustSelectionPrompt")!.GetValue(null)!;
            var prefs=Activator.CreateInstance(prefsType,new object[]{prompt,minimum,maximum})!;
            var select=hand.GetType().GetMethod("SelectCards")!;
            var mode=Enum.Parse(select.GetParameters()[3].ParameterType,"SimpleSelect");
            System.Threading.Tasks.Task? effect=null;
            System.Threading.Tasks.Task task;
            if(caseName=="purity-consumer") {
                effect=NativeBootstrap.BeginPurity(cards);
                if(effect.IsFaulted)throw effect.Exception!;
                var completion=hand.GetType().GetField("_selectionCompletionSource",BindingFlags.Instance|BindingFlags.NonPublic)!.GetValue(hand)!;
                task=(System.Threading.Tasks.Task)Get(completion,"Task");
            } else task=(System.Threading.Tasks.Task)select.Invoke(hand,new object?[]{prefs,null,null,mode})!;
            GD.Print("PROBE SelectCards pending ",!task.IsCompleted," faulted ",task.IsFaulted);
            if(task.IsFaulted)throw task.Exception!;
            else {
                Require(!task.IsCompleted,"SelectCards must suspend before input");
                var button=hand.GetNode<Control>("%SelectModeConfirmButton");
                GD.Print("PROBE confirm initialized ",Get(button,"IsEnabled"));
                var selectedField=hand.GetType().GetField("_selectedCards",BindingFlags.Instance|BindingFlags.NonPublic)!;
                void Dump(string label) {
                    var values=new System.Collections.Generic.List<int>();
                    foreach(var c in (System.Collections.IEnumerable)selectedField.GetValue(hand)!)values.Add(Array.FindIndex(cards,x=>ReferenceEquals(x,c)));
                    observations.Add(new {input=label,selected=values.ToArray(),pending=!task.IsCompleted,confirm=(bool)Get(button,"IsEnabled")});
                    Require(!task.IsCompleted,"A partial hand callback unexpectedly submitted");
                    Require((bool)Get(button,"IsEnabled")== (values.Count>=minimum && values.Count<=maximum),"Native confirm availability differs");
                }
                void Choose(int index) {
                    var holder=Invoke(hand,"GetCardHolder",cards[index])!;
                    hand.GetType().GetMethod("SelectCardInSimpleMode",BindingFlags.Instance|BindingFlags.NonPublic)!.Invoke(hand,new[]{holder});
                    Dump("choose"+index);
                }
                Dump("initial");
                if(caseName!="zero") { Choose(0);Choose(1);Choose(2); if(effect!=null)Choose(3); }
                var selectedContainer=hand.GetNode<Node>("%SelectedHandCardContainer");
                if(caseName=="ordered" || caseName=="purity-consumer") {
                    Invoke(selectedContainer,"DeselectCard",cards[0]); Dump("deselect0");
                    Choose(0);
                }

                hand.GetType().GetMethod("OnSelectModeConfirmButtonPressed",BindingFlags.Instance|BindingFlags.NonPublic)!.Invoke(hand,new object[]{button});
                GD.Print("PROBE confirm completed ",task.IsCompleted," faulted ",task.IsFaulted);
                Require(task.IsCompletedSuccessfully,"Native confirmation did not complete successfully");
                var returned=(System.Collections.IEnumerable)task.GetType().GetProperty("Result")!.GetValue(task)!;
                var actual=new System.Collections.Generic.List<int>();
                foreach(var c in returned)actual.Add(Array.FindIndex(cards,x=>ReferenceEquals(x,c)));
                var expected=caseName=="zero" ? new int[]{} : caseName=="single" ? new[]{2} : caseName=="purity-consumer" ? new[]{1,3,0} : new[]{2,0};
                Require(System.Linq.Enumerable.SequenceEqual(actual,expected),"Native returned order differs");
                int[]? exhausted=null;
                if(effect!=null) {
                    await effect;
                    exhausted=NativeBootstrap.ExhaustedIndices(cards);
                    Require(System.Linq.Enumerable.SequenceEqual(exhausted,new[]{1,3,0,-1}),"Native Purity exhaust continuation order differs");
                }
                GD.Print("CHOICE_RESULT ",System.Text.Json.JsonSerializer.Serialize(new {case_name=caseName,observations,result=actual,exhausted,framework=System.Runtime.InteropServices.RuntimeInformation.FrameworkDescription,hand_ready=hand.IsNodeReady(),inside_tree=hand.IsInsideTree(),mode=Get(hand,"CurrentMode").ToString(),assembly_mvid=a.ManifestModule.ModuleVersionId.ToString()}));
            }
            ui.Free(); room.Free();

        } catch(Exception ex){GD.PrintErr("PROBE ERROR ",ex);GetTree().Quit(1);return;}
        GetTree().Quit();
    }
}
