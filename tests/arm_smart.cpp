#include "SmartHarmony.h"
#include "VoicingPlanner.h"
#include "MpkMidiPipeline.h"
static mpk225::MpkMidiPipeline g_pipeline;
extern "C" uint32_t query_smart_and_play(uint32_t root) {
    mpk225::HarmonyContext ctx{};
    mpk225::HarmonySuggestion best[4]{};
    const auto n = mpk225::SmartHarmony::top(ctx, best, 4);
    if (n == 0 || root > 127) return 0;
    mpk225::VoicedChord plan{};
    if (!mpk225::VoicingPlanner::plan(best[0].root, best[0].type,
                                      mpk225::VoicedChord{}, 48, 84, plan)) return 0;
    g_pipeline.setChordMode(true);
    if (!g_pipeline.playPlannedVoicing(static_cast<uint8_t>(root), 91,
                                      plan.notes, plan.count)) return 0;
    return g_pipeline.takeUsbFrame().length;
}
