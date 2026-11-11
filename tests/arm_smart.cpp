#include "SmartHarmony.h"
#include "MpkMidiPipeline.h"
static mpk225::MpkMidiPipeline g_pipeline;
extern "C" uint32_t query_smart_and_play(uint32_t root) {
    mpk225::HarmonyContext ctx{};
    mpk225::HarmonySuggestion best[4]{};
    const auto n = mpk225::SmartHarmony::top(ctx, best, 4);
    if (n == 0 || root > 127) return 0;
    g_pipeline.setChordMode(true);
    g_pipeline.setChordType(best[0].type);
    if (!g_pipeline.consume({0xFF, 91, root, 0})) return 0;
    return g_pipeline.takeUsbFrame().length;
}
