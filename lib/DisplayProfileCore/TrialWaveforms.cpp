#include "TrialWaveforms.h"

namespace displayprofile {

WordWaveform wordWaveform(const ProfileId id, const PanelId panel) {
  const bool depg = panel == PanelId::DEPG0397;
  const bool yrdOpm = panel == PanelId::YRD0426 || panel == PanelId::OPM040B3;
  if (!depg && !yrdOpm) {
    if (id == ProfileId::TRIAL_D && panel == PanelId::SE0400NQW47) {
      return {WordPath::STOCK_UI, nullptr, 0, "lut_picco_se0400_mode3 (SDK UI path)"};
    }
    return {};
  }
  switch (id) {
    case ProfileId::TRIAL_A:
      return depg ? WordWaveform{WordPath::TABLE, LUT_TRIAL_A_DEPG0397, 300, "trial A (DEPG0397)"}
                  : WordWaveform{WordPath::TABLE, LUT_TRIAL_A_YRD0426_OPM040B3, 300, "trial A (YRD0426/OPM040B3)"};
    case ProfileId::TRIAL_B:
      return depg ? WordWaveform{WordPath::TABLE, LUT_TRIAL_B_DEPG0397, 220, "trial B (DEPG0397)"}
                  : WordWaveform{WordPath::TABLE, LUT_TRIAL_B_YRD0426_OPM040B3, 220, "trial B (YRD0426/OPM040B3)"};
    case ProfileId::TRIAL_C:
      return depg ? WordWaveform{WordPath::TABLE, LUT_TRIAL_C_DEPG0397, 160, "trial C (DEPG0397)"}
                  : WordWaveform{WordPath::TABLE, LUT_TRIAL_C_YRD0426_OPM040B3, 160, "trial C (YRD0426/OPM040B3)"};
    case ProfileId::TRIAL_D:
      return depg ? WordWaveform{WordPath::TABLE, LUT_TRIAL_D_DEPG0397, 160, "lut_picco_depg_fast"}
                  : WordWaveform{WordPath::TABLE, LUT_TRIAL_D_YRD0426_OPM040B3, 0, "lut_picco_fast"};
    case ProfileId::STOCK:
    default:
      return {};
  }
}

}  // namespace displayprofile
