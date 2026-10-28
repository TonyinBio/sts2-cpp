// Immutable model defaults only; never replaces gameplay or native UI callbacks.
using System.Text.Json;
using MegaCrit.Sts2.Core.Models;
using MegaCrit.Sts2.Core.Models.Cards;

namespace Sts2Headless;
public partial class RunSimulator {
    public Dictionary<string, object?> AuditSelectionDefinitions(JsonElement command) {
        var purity = ModelDb.Card<Purity>().ToMutable();
        return new() { ["cards"] = new Dictionary<string, object?> {
            [purity.Id.ToString()] = NativeRunRoundTrip.Capture(purity.ToSerializable())
        } };
    }
}
