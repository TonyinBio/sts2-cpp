// Optional isolated combat fixtures, never included in production recordings.
using System.Text.Json;
using MegaCrit.Sts2.Core.Combat;

namespace Sts2Headless;
public partial class RunSimulator
{
    public Dictionary<string, object?> AuditKernelCombat(JsonElement command)
    {
        var enemy = CombatManager.Instance.DebugOnlyGetState()!.Enemies.Single();
        var hp = command.GetProperty("enemy_hp").GetInt32();
        enemy.SetMaxHpInternal(hp);
        enemy.SetCurrentHpInternal(hp);
        return OracleRunState(command);
    }
}
