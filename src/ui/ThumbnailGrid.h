#pragma once
#include <wx/wx.h>
#include <wx/scrolwin.h>
#include <vector>

// Scrollable 3-column thumbnail grid.
//
// Usage:
//   auto* grid = new ThumbnailGrid(parent, "Select a folder to see images.");
//   grid->SetImages(paths);   // pass empty vector to show the hint
//
// Automatically recomputes thumb size and rebuilds on window resize.
class ThumbnailGrid : public wxScrolledWindow {
public:
    explicit ThumbnailGrid(wxWindow* parent,
                           const wxString& emptyHint = "No images.");

    // Replace displayed images. Empty vector shows the empty hint.
    void SetImages(const std::vector<wxString>& paths);

    // Change the text shown when images list is empty.
    void SetEmptyHint(const wxString& hint);

private:
    void Rebuild();
    void OnSize(wxSizeEvent& evt);

    std::vector<wxString> m_paths;
    wxString              m_emptyHint;
};