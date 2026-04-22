#pragma once
#include <wx/wx.h>
#include <wx/statbmp.h>
#include <wx/timer.h>
#include <wx/scrolwin.h>
#include <wx/wrapsizer.h>
#include <wx/gauge.h>
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
    void HandleKeyUp(wxKeyEvent& evt);  // called by KeyUpFilter

private:
    // Global filter to intercept wxEVT_KEY_UP from any focused window
    class KeyUpFilter : public wxEventFilter {
    public:
        explicit KeyUpFilter(SortPhotosPanel* owner) : m_owner(owner) {}
        int FilterEvent(wxEvent& event) override;
    private:
        SortPhotosPanel* m_owner;
    };
    KeyUpFilter* m_keyUpFilter = nullptr;

    // --- sorting UI (nulled after DestroyChildren) ---
    wxPanel*        m_imageViewport   = nullptr;
    wxStaticBitmap* m_imageBitmap    = nullptr;
    wxButton*       m_openVideoBtn   = nullptr;
    wxGauge*        m_progressBar    = nullptr;
    wxStaticText*   m_progressLabel  = nullptr;
    wxStaticText*   m_imageNameLabel = nullptr;
    wxStaticText*   m_fileMetaLabel  = nullptr;
    wxStaticText*   m_helpLabel      = nullptr;
    wxButton*       m_copyNameBtn   = nullptr;
    wxTimer*        m_copyNameTimer = nullptr;
    wxTimer*        m_copyPathTimer = nullptr;
    wxButton*       m_quitBtn     = nullptr;
    wxButton*       m_folder1Btn    = nullptr;
    wxButton*       m_folder2Btn  = nullptr;
    wxButton*       m_saveBtn     = nullptr;
    wxButton*       m_deleteBtn   = nullptr;
    wxButton*       m_undoBtn     = nullptr;
    wxButton*       m_ruleOfThirdsBtn = nullptr;
    wxButton*       m_zoomInBtn   = nullptr;
    wxButton*       m_zoomOutBtn  = nullptr;
    wxStaticBitmap* m_zoomInShiftHint = nullptr;
    wxStaticBitmap* m_zoomOutShiftHint = nullptr;

    // --- session state ---
    size_t                     m_currentIndex   = 0;
    bool                       m_currentIsVideo = false;
    bool                       m_showRuleOfThirds = false;
    double                     m_imageZoom = 1.0;
    wxPoint2DDouble            m_imageOffset;
    wxString                   m_activeImagePath;
    wxString                   m_cachedImagePath;
    wxSize                     m_cachedImageBounds;
    double                     m_cachedZoomFactor = 1.0;
    wxPoint2DDouble            m_cachedImageOffset;
    wxImage                    m_cachedSourceImage;
    wxBitmap                   m_cachedPlainBitmap;
    wxBitmap                   m_cachedGridBitmap;
    wxRect                     m_visibleImageRect;
    bool                       m_isDraggingImage = false;
    wxPoint                    m_imageDragStart;
    wxPoint2DDouble            m_imageDragStartOffset;
    std::vector<PendingAction> m_actionHistory;
    std::vector<wxString>      m_folder1List;
    std::vector<wxString>      m_folder2List;
    std::vector<wxString>      m_deleteList;

    // --- key/button hold tracking ---
    int        m_heldKey           = 0;  // normalized (WXK_* or uppercase char); 0 = none
    wxLongLong m_keyPressTime      = 0;  // ms timestamp of key-down
    wxLongLong m_btnMousePressTime = 0;  // ms timestamp of button mouse-down

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
    void BuildSortingUI();
    void LoadCurrentImage();
    bool RefreshCurrentImageBitmap();
    void SetButtonsEnabled(bool enabled);
    void UpdateGridToggleButton();
    void UpdateZoomButtons();
    void UpdateImageViewportLayout();
    bool IsAtOriginalZoom() const;
    void ResetImageZoom();
    void ApplyZoomStep(int direction, const wxPoint* anchor = nullptr);
    void EndImageDrag();
    void InvalidateImageRenderCache();
    void ShowDoneState();
    void RecordAction(SortAction type);
    void PersistList(const std::vector<wxString>& list, const wxString& filename,
                     const wxString& destDir = wxEmptyString);

    // --- key/button hold helpers ---
    wxButton* KeyToButton(int key) const;
    void SetButtonVisualPressed(wxButton* btn, bool pressed);
    void ExecuteKeyAction(int key);

    // --- end-of-session ---
    void OnAllImagesActedUpon();
    void AdvanceReviewStep();
    void ShowReviewStep();
    void ExecuteConfirmedActions();
    wxString ExecuteFileMove(const wxString& srcPath, const wxString& destFolder);

    // --- crash recovery ---
    void CheckForPendingSession();

    // --- helpers ---
    void     TryQuitToMenu();
    wxBitmap LoadKeycap(const wxString& filename, int size = 28) const;
    wxImage ApplyRuleOfThirdsOverlay(wxImage img) const;
    wxSize GetImageDisplayBounds() const;
    wxBitmap GetOrCreateImageBitmap(const wxString& path, const wxSize& available, bool withGrid);

    // --- event handlers ---
    void OnFolder1(wxCommandEvent& evt);
    void OnFolder2(wxCommandEvent& evt);
    void OnSave(wxCommandEvent& evt);
    void OnDelete(wxCommandEvent& evt);
    void OnUndo(wxCommandEvent& evt);
    void OnToggleRuleOfThirds(wxCommandEvent& evt);
    void OnZoomIn(wxCommandEvent& evt);
    void OnZoomOut(wxCommandEvent& evt);
    void OnImageMouseWheel(wxMouseEvent& evt);
    void OnImageLeftDown(wxMouseEvent& evt);
    void OnImageLeftUp(wxMouseEvent& evt);
    void OnImageMouseMove(wxMouseEvent& evt);
    void OnImageCaptureLost(wxMouseCaptureLostEvent& evt);
    void OnStepConfirm(wxCommandEvent& evt);
    void OnStepSkip(wxCommandEvent& evt);
    void OnSize(wxSizeEvent& evt);
    void OnKeyDown(wxKeyEvent& evt);
};
