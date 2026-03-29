#pragma once
#include <wx/wx.h>
#include <wx/statbmp.h>
#include <wx/scrolwin.h>
#include <wx/wrapsizer.h>
#include <vector>

enum {
    ID_SORT_FOLDER1      = wxID_HIGHEST + 100,
    ID_SORT_FOLDER2,
    ID_SORT_SAVE,
    ID_SORT_DELETE,
    ID_SORT_UNDO         = wxID_HIGHEST + 104,
    ID_SORT_STEP_CONFIRM = wxID_HIGHEST + 105,
    ID_SORT_STEP_SKIP    = wxID_HIGHEST + 106
};

enum class SortAction { MoveToFolder1, MoveToFolder2, Save, Delete };
struct PendingAction   { wxString imagePath; SortAction type; };
enum class ReviewStep  { Folder1, Folder2, Delete };

class SortPhotosPanel : public wxPanel {
public:
    explicit SortPhotosPanel(wxWindow* parent);
    ~SortPhotosPanel();
    void RefreshData();

private:
    // --- sorting UI (nulled after DestroyChildren) ---
    wxStaticBitmap* m_imageBitmap = nullptr;
    wxStaticText*   m_statusLabel = nullptr;
    wxButton*       m_folder1Btn  = nullptr;
    wxButton*       m_folder2Btn  = nullptr;
    wxButton*       m_saveBtn     = nullptr;
    wxButton*       m_deleteBtn   = nullptr;
    wxButton*       m_undoBtn     = nullptr;

    // --- session state ---
    size_t                     m_currentIndex = 0;
    std::vector<PendingAction> m_actionHistory;
    std::vector<wxString>      m_folder1List;
    std::vector<wxString>      m_folder2List;
    std::vector<wxString>      m_deleteList;

    // --- review state ---
    bool       m_inReview          = false;
    ReviewStep m_currentReviewStep = ReviewStep::Folder1;
    bool       m_folder1Shown      = false;
    bool       m_folder2Shown      = false;
    bool       m_deleteShown       = false;
    bool       m_folder1Confirmed  = false;
    bool       m_folder2Confirmed  = false;
    bool       m_deleteConfirmed   = false;

    // --- sorting helpers ---
    void LoadCurrentImage();
    void SetButtonsEnabled(bool enabled);
    void ShowDoneState();
    void RecordAction(SortAction type);
    void PersistList(const std::vector<wxString>& list, const wxString& filename);

    // --- end-of-session ---
    void OnAllImagesActedUpon();
    void AdvanceReviewStep();
    void ShowReviewStep();
    void ExecuteConfirmedActions();
    void ExecuteFileMove(const wxString& srcPath, const wxString& destFolder);

    // --- crash recovery ---
    void CheckForPendingSession();

    // --- helpers ---
    wxBitmap LoadKeycap(const wxString& filename, int size = 28) const;

    // --- event handlers ---
    void OnFolder1(wxCommandEvent& evt);
    void OnFolder2(wxCommandEvent& evt);
    void OnSave(wxCommandEvent& evt);
    void OnDelete(wxCommandEvent& evt);
    void OnUndo(wxCommandEvent& evt);
    void OnStepConfirm(wxCommandEvent& evt);
    void OnStepSkip(wxCommandEvent& evt);
    void OnSize(wxSizeEvent& evt);
    void OnKeyDown(wxKeyEvent& evt);
};
