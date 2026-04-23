#include "file_dialog.h"

#include <nfd.h>

void OpenFileDialog(AppState& st) {
    nfdu8char_t* outPath = nullptr;
    nfdu8filteritem_t filters[1] = { { "CMake Presets", "json" } };
    nfdopendialogu8args_t args = {};
    args.filterList = filters;
    args.filterCount = 1;

    nfdresult_t result = NFD_OpenDialogU8_With(&outPath, &args);
    if (result == NFD_OKAY && outPath) {
        st.pendingLoad = outPath;
        NFD_FreePathU8(outPath);
    }
}

void OpenDirectoryDialog(AppState& st) {
    nfdu8char_t* outPath = nullptr;
    nfdresult_t result = NFD_PickFolderU8(&outPath, nullptr);
    if (result == NFD_OKAY && outPath) {
        st.pendingDirLoad = outPath;
        NFD_FreePathU8(outPath);
    }
}
