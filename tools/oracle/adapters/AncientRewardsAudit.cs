// Optional live audit fixture. Never injected into ordinary recording builds.
using System.Text.Json;
using MegaCrit.Sts2.Core.Runs;

namespace Sts2Headless;

public partial class RunSimulator
{
    public Dictionary<string, object?> AuditNextAct(JsonElement command)
    {
        BeginRunOperation("audit_next_act", null, () => RunManager.Instance.EnterNextAct());
        AwaitRunBoundary();
        var result = OracleRunResponse(command);
        var start = _runState!.Map!.StartingMapPoint;
        result["starting_node"] = new { col = (int)start.coord.col, row = (int)start.coord.row };
        result["first_fight"] = new { col = (int)start.Children.First().coord.col, row = (int)start.Children.First().coord.row };
        return result;
    }
}
