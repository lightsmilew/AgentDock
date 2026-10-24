#pragma once
#include "core.hpp"
#include "text_scroll.hpp"
#include <condition_variable>
#include <d2d1_3.h>
#include <d3d11.h>
#include <dwrite.h>
#include <dxgi1_2.h>
#include <set>
#include <thread>
#include <wrl/client.h>

namespace ad {
using Microsoft::WRL::ComPtr;
using Rect = D2D1_RECT_F;
inline Rect box(float x, float y, float w, float h) {
    return D2D1::RectF(x, y, x + w, y + h);
}
inline D2D1_COLOR_F color(unsigned rgb, float alpha = 1) {
    return D2D1::ColorF(rgb, alpha);
}
inline constexpr unsigned INK = 0x182B46, BODY = 0x46617F, MUTED = 0x7B91AD, BLUE = 0x3478F6, SOFT = 0xEAF3FF,
                          BG = 0xF5F9FF, LINE = 0xE1EBF7, WHITE = 0xFFFFFF, GREEN = 0x179D87, RED = 0xDD5B6C;
struct Choice {
    std::string id, name, note, brand;
};
class Window {
  public:
    HWND hwnd = nullptr, owner = nullptr;
    float width = 1100, height = 780, dpi = 96, scroll = 0, scrollMax = 0;
    Rect view{};
    std::string title;
    bool modal = false, closing = false;
    std::function<void()> onClose;
    virtual ~Window();
    void open(const std::string &, int w, int h, HWND parent = nullptr, bool modal = false);
    void invalidate();
    void close();
    void shutdown();
    void guard(std::function<void()>);
    void error(const std::string &);
    void notice(const std::string &);
    bool confirm(const std::string &, const std::string &);
    void rect(Rect, unsigned, float radius = 0, unsigned border = 0);
    void line(float, float, float, float, unsigned, float weight = 1.5f);
    void text(const std::string &, Rect, float size = 14, unsigned c = INK, bool bold = false,
              bool wrap = false, int align = 0);
    void icon(const std::string &, float x, float y, float size = 20, unsigned c = BLUE);
    void brand(const std::string &, Rect);
    void button(const std::string &, Rect, std::function<void()>, int style = 0,
                const std::string &glyph = "", bool enabled = true);
    void hit(Rect, std::function<void()>, bool enabled = true);
    void caption(bool main = false);
    void beginScroll(Rect);
    void endScroll(float contentHeight);
    void clipboard(const std::string &);
    fs::path choose(bool folder = false, bool save = false, const std::string &filename = "");
    void browse(const std::string &);
    HWND edit(int id, Rect, const std::string &value = "", bool multi = false, bool secret = false,
              bool readonly = false, const std::string &placeholder = "", bool search = false);
    void activity(Rect);
    void popup(std::vector<Choice>, std::function<void(std::string)>, Rect anchor = {});
    void dismissPopup();
    std::string value(int id);
    void value(int id, const std::string &);
    void hideEdits();
    void removeEdit(int);
    void job(std::function<J()> work, std::function<void(J)> done, const std::string &label);
    bool busy() const {
        return worker.joinable();
    }
    std::function<void()> cancelJob;
    virtual void draw() = 0;
    virtual void changed(int) {}
    virtual void key(WPARAM) {}
    virtual bool beforeClose() {
        return true;
    }
    virtual void resized() {}
    virtual void backgroundReady() {}
    virtual void framePresented() {}
    std::vector<std::unique_ptr<Window>> children;

  protected:
    std::map<int, HWND> edits;
    std::set<HWND> paintedEdits;
    ComPtr<ID2D1DeviceContext5> dc;
    Rect lastAction{};

  private:
    struct Hit {
        Rect r;
        std::function<void()> fn;
        bool enabled;
    };
    struct JobResult {
        J data;
        std::string error;
        std::function<void(J)> done;
    };
    std::vector<Hit> hits;
    std::map<std::string, ComPtr<ID2D1SvgDocument>> svgs;
    std::map<std::string, ComPtr<ID2D1Bitmap>> bitmaps;
    std::map<std::string, ComPtr<IDWriteTextFormat>> formats;
    ComPtr<ID3D11Device> d3d;
    ComPtr<ID2D1Factory1> factory;
    ComPtr<IDWriteFactory> dw;
    ComPtr<ID2D1Device> device;
    ComPtr<IDXGISwapChain1> swap;
    ComPtr<ID2D1Bitmap1> targetBitmap;
    ComPtr<ID2D1SolidColorBrush> brush;
    HFONT editFont = nullptr;
    POINT mouse{-999, -999};
    int hover = -1, pressed = -1, focus = -1;
    bool clipping = false, dragScroll = false;
    bool readyReported = false;
    bool keyboardFocus = false;
    struct TextArea {
        Rect bounds, visible;
        int wheelRemainder = 0;
    };
    std::map<HWND, TextArea> textAreas;
    HWND draggingText = nullptr;
    float textGrab = 0;
    TextScroll textMetrics(HWND);
    void textScrollTo(HWND, int);
    bool textWheel(POINT, int);
    std::map<HWND, std::wstring> placeholders;
    uint64_t jobStarted = 0;
    float dragY = 0, dragOffset = 0;
    LONG minWidth = 520, minHeight = 380;
    std::string toast, jobLabel;
    uint64_t toastUntil = 0;
    std::thread worker;
    std::atomic<bool> stop{false};
    std::vector<Choice> popupItems;
    std::vector<int> popupFiltered;
    std::function<void(std::string)> popupSelected;
    Rect popupAnchor{}, popupBounds{};
    int popupOffset = 0, popupFocus = 0;
    void drawPopup();
    bool popupKey(WPARAM);
    void initGraphics();
    void resizeGraphics();
    void paint();
    static LRESULT CALLBACK proc(HWND, UINT, WPARAM, LPARAM);
    static LRESULT CALLBACK editProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
    LRESULT message(UINT, WPARAM, LPARAM);
};
struct Field {
    std::string key, label, initial, placeholder;
    bool multi = false, secret = false, readonly = false;
    float height = 36;
    std::vector<std::pair<std::string, std::string>> options;
    bool fullWidth = false;
    int section = 0;
    bool modelTable = false;
    bool checkbox = false;
    bool directory = false;
};
class Form : public Window {
  public:
    std::string subtitle, help;
    std::string brandName;
    bool importChoices = false;
    int columns = 1;
    int section = 0;
    std::vector<std::string> sections;
    std::vector<Field> fields;
    struct Action {
        std::string label;
        std::function<void(Form &)> fn;
        int style;
        bool enabled = true;
    };
    std::vector<Action> actions;
    std::function<void(Form &, const std::string &)> onChange;
    std::function<bool(Form &)> canClose;
    bool dirty = false, ready = false;
    std::string get(const std::string &);
    void set(const std::string &, const std::string &);
    void draw() override;
    void changed(int) override;
    void key(WPARAM) override;
    bool beforeClose() override;
    void drawModelTable(Rect);
    void initModelRows();
    struct ModelRow {
        int control;
        std::string value;
    };
    std::vector<ModelRow> modelRows;
    int nextModelControl = 700, modelTablePage = 0, pendingModelFocus = 0;
    bool modelRowsReady = false;
};
class App : public Window {
  public:
    Store store;
    Models models;
    Adapters adapters;
    explicit App(fs::path root) : store(root), models(store), adapters(store, models) {}
    ~App() override {
        stopMaintenance();
        shutdown();
        dialogs.clear();
    }
    void start();
    void draw() override;
    void changed(int) override;
    void key(WPARAM) override;
    bool beforeClose() override;
    void refresh();
    void collect();
    void backgroundReady() override;
    void framePresented() override;

  private:
    int page = 0, offset = 0, scope = 0;
    bool detectedOnly = false;
    std::string category = "all", selection, query, presetGroup = "all", agentQuery, modelQuery;
    J agents, providers, presets, stats, skillRows, recentRows, backupRows;
    std::string projectPath;
    std::string globalQuery;
    J globalSkills = J::array();
    std::function<void()> firstSearchResult;
    std::string draft, prompt, genProvider;
    J generationOptions = J::object();
    std::string expandedAgent, agentModel, agentProvider, agentDefaultInput;
    std::string installingAgent;
    bool cancellingInstall = false;
    J agentState, agentPreview;
    int agentCandidateOffset = 0, agentConfiguredOffset = 0;
    std::vector<std::unique_ptr<Window>> dialogs;
    std::thread maintenance;
    std::mutex maintenanceMutex;
    std::condition_variable maintenanceWake;
    std::atomic<bool> maintenanceStop{false}, searchStop{false}, searchingDisks{false};
    bool refreshPending = false, diskSearchPending = false, backgroundHasResult = false;
    J backgroundResult;
    std::string configPending, navigationTrace;
    LARGE_INTEGER navigationStart{};
    double navigationHandlerMs = 0;
    bool navigationPaintPending = false;
    void requestRefresh(bool searchDisks = false);
    void stopMaintenance();
    void detectionMenu();
    void locateAgent(const std::string &);
    void loadAgentState(const std::string &);
    void navigate(int);
    void overview(float, float, float);
    void skills(float, float, float);
    void agentsPage(float, float, float);
    float agentPanel(float, float, float);
    void modelsPage(float, float, float);
    void generatePage(float, float, float);
    void settings(float, float, float);
    void importDialog();
    void skillDialog(const std::string &id = "", const std::string &content = "");
    void deployDialog(const std::string &);
    void exportSkill(const std::string &, Window *parent = nullptr);
    void agentDialog(const std::string &);
    void installationAction(const std::string &, bool remove = false);
    void providerDialog(const std::string &vendor = "custom", const std::string &id = "");
    void configDialog(const std::string &, const std::string &);
    void commandPalette();
    void searchResults(float, float, float);
    void deploymentDialog();
    void scan();
    void runGenerate(bool improve = false);
    void generationSettings();
    void qualityDialog();
    void generationMenu();
    Form &form(const std::string &, int, int, std::vector<Field>, HWND parent = nullptr);
    void picker(const std::string &, std::vector<Choice>, std::function<void(std::string)>,
                HWND parent = nullptr);
};
int run_app(fs::path, const std::string &initialPage = "");
} // namespace ad
