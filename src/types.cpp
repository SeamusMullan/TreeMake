#include "types.h"

const char* KindLabel(PresetKind k) {
    switch (k) {
        case PresetKind::Configure: return "Configure";
        case PresetKind::Build:     return "Build";
        case PresetKind::Test:      return "Test";
        case PresetKind::Package:   return "Package";
        case PresetKind::Workflow:  return "Workflow";
    }
    return "?";
}

ImU32 KindColor(PresetKind k) {
    switch (k) {
        case PresetKind::Configure: return IM_COL32(100, 160, 255, 255);
        case PresetKind::Build:     return IM_COL32(100, 220, 130, 255);
        case PresetKind::Test:      return IM_COL32(240, 180,  80, 255);
        case PresetKind::Package:   return IM_COL32(220, 120, 220, 255);
        case PresetKind::Workflow:  return IM_COL32(220, 100, 100, 255);
    }
    return IM_COL32(180, 180, 180, 255);
}
