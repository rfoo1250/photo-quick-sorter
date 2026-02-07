#pragma once
#include <wx/string.h>

// A simple struct-style class to hold folder paths
class FolderLocations {
public:
    wxString baseFolder;
    wxString folder1;
    wxString folder2;

    FolderLocations() = default;

    void Clear() {
        baseFolder.clear();
        folder1.clear();
        folder2.clear();
    }
};
