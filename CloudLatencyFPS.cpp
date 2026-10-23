#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define _CRT_SECURE_NO_WARNINGS

#include <windows.h>

#include "layout.h"
#include "fps_model.h"
#include "fps_stress.h"
#include "fps_world.h"
#include "fps_detail.h"
#include <GL/gl.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <shellapi.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>

namespace {

constexpr wchar_t kAppClass[] = L"CloudLatencyFPSAppV2";
constexpr wchar_t kChooserClass[] = L"CloudLatencyFPSChooserV2";
constexpr wchar_t kAppTitle[] = L"Cloud Latency & FPS Tester 0.8.1";
constexpr UINT kMessageTick = WM_APP + 1;
constexpr UINT kMessageAutoStop = WM_APP + 2;

constexpr int kHotkeyRate = 10;
constexpr int kHotkeyMode = 11;
constexpr int kHotkeyReset = 12;
constexpr int kHotkeyExit = 13;
constexpr int kHotkeyRoiTopLeft = 14;
constexpr int kHotkeyRoiBottomRight = 15;
constexpr int kChooserFixed = 101;
constexpr int kChooserFollow = 102;
constexpr int kChooserAnalyzer = 103;
constexpr int kAnalyzerReset = 201;
constexpr int kModeCombo = 301, kRateCombo = 302, kDisplayCombo = 303, kRatioSlider = 304;
constexpr int kLockLayout = 305, kFullscreenExit = 306, kTopmost = 307, kRecord = 308;
constexpr int kRoiReset = 309, kDetails = 310, kOpenCsv = 311;
constexpr int kHotkeyFullscreen = 16;
constexpr int kPreviewWidth = 256, kPreviewHeight = 64;
constexpr int kFpsControl = 312, kFpsScene = 313, kFpsRestart = 314;
constexpr int kBitrateLabel = 315, kComparison = 316, kFpsPressure = 317;
constexpr int kHotkeyFpsControl = 17, kHotkeyFpsScene = 18, kHotkeyFpsRestart = 19, kHotkeyFpsPressure = 20;
constexpr int kLogQueueSize = 4096;
constexpr COLORREF kBackground = RGB(17, 22, 31), kPanel = RGB(29, 37, 51);
constexpr COLORREF kText = RGB(231, 237, 245), kMuted = RGB(166, 183, 205);

constexpr DWORD kFixedStyle = WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN;
constexpr DWORD kFollowStyle = WS_POPUP | WS_CLIPCHILDREN;
constexpr DWORD kFixedExStyle = WS_EX_TOPMOST | WS_EX_APPWINDOW;
constexpr DWORD kFollowExStyle =
    WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT | WS_EX_LAYERED;

constexpr int kCodeColumns = 8;
constexpr int kCodeRows = 4;
constexpr int kPayloadBits = 24;
constexpr int kMaxLatencySamples = 256;
constexpr int kFrameHistorySize = 4096;
constexpr int kFixedInitialWidth = 720;
constexpr int kFixedInitialHeight = 420;
constexpr int kFollowInitialWidth = 360;
constexpr int kFollowInitialHeight = 180;

enum class LaunchMode { None, FixedSignal, FollowSignal, Analyzer };
enum class TestMode { Latency, FrameRate, AutoMotion, MouseMotion, FpsView };

struct DecoderResult {
    bool valid = false;
    TestMode mode = TestMode::Latency;
    std::uint16_t sequence = 0;
    double confidence = 0.0;
    bool telemetry = false;
    std::uint16_t fields[fps::kFields]{};
};

struct PixelDimensions {
    int width;
    int height;
    int stride = 0;
};

struct CellPosition {
    int row;
    int column;
};

struct FrameTransition {
    LONGLONG capturedAt;
    std::uint16_t delta;
};

struct AnalyzerStats {
    POINT firstCorner{};
    POINT secondCorner{};
    RECT roi{};
    bool hasFirstCorner = false;
    bool hasSecondCorner = false;
    bool roiValid = false;
    int decodedMode = -1;
    bool decoderLocked = false;
    double confidence = 0.0;
    std::uint64_t capturePolls = 0;
    std::uint64_t invalidPolls = 0;
    bool pendingClick = false;
    LONGLONG clickAt = 0;
    std::uint64_t clickNumber = 0;
    std::uint64_t timedOutClicks = 0;
    bool latencyBaseline = false;
    std::uint16_t lastLatencySequence = 0;
    double lastLatencyMs = 0.0;
    double latencyMedianMs = 0.0;
    double latencyP95Ms = 0.0;
    double latencySamples[kMaxLatencySamples]{};
    int latencyStored = 0;
    int latencyWriteIndex = 0;
    std::uint64_t latencyTotal = 0;
    bool frameBaseline = false;
    std::uint16_t lastFrameSequence = 0;
    std::uint64_t frameTransitions = 0;
    std::uint64_t skippedFrameSlots = 0;
    LONGLONG frameTimes[kFrameHistorySize]{};
    std::uint16_t frameDeltas[kFrameHistorySize]{};
    int frameHistoryCount = 0;
    int frameHistoryWrite = 0;
    double rollingVisibleFps = 0.0;
    double rollingGeneratedFps = 0.0;
    double frameMedianMs = 0.0;
    double frameP95Ms = 0.0;
    double frameP99Ms = 0.0;
    double frameLongestMs = 0.0;
    double frameJitterMadMs = 0.0;
    double onePercentLowFps = 0.0;
    int recentStutters = 0;
    LONGLONG nextTimingStatsAt = 0;
    LONGLONG lastValidAt = 0;
    LONGLONG lastTransitionAt = 0;
    double captureMs = 0.0;
    bool captureFailed = false;
    bool roiObscured = false;
    bool pendingMotion = false;
    LONGLONG motionStartedAt = 0;
    LONGLONG lastLocalMoveAt = 0;
    double lastMotionLatencyMs = 0.0;
    double motionMedianMs = 0.0;
    double motionP95Ms = 0.0;
    double motionLatencySamples[kMaxLatencySamples]{};
    int motionLatencyStored = 0;
    int motionLatencyWriteIndex = 0;
    std::uint64_t motionLatencyTotal = 0;
    std::uint64_t motionGestures = 0;
    std::uint64_t motionTimeouts = 0;
    std::uint64_t localMouseMoves = 0;
    bool decodeGap = false;
    LONGLONG gapStartedAt = 0;
    double decoderUnknownMs = 0;
    bool intervalValid[kFrameHistorySize]{};
    std::uint64_t unknownIntervals = 0;
    std::uint16_t fpsInputSequence = 0;
    bool fpsInputBaseline = false;
    double fpsYaw = 0, fpsPitch = 0, fpsUnwrappedYaw = 0;
    double sourceIntervalMs = 0, sourcePaintMs = 0;
    int fpsFlags = 0;
    fps::Point localTrace[fps::kHistory]{}, remoteTrace[512]{};
    int localCount = 0, localWrite = 0, remoteCount = 0, remoteWrite = 0;
    double localCumulativeX = 0;
    LONGLONG nextTrackingAt = 0;
    fps::Match tracking{};
    std::uint32_t runHistogram[4096]{};
    int runIntervalCount = 0;
    double runIntervalSum = 0, runIntervalMax = 0;
    std::uint64_t runIntervalTotal = 0, runHitches = 0, runFrames = 0;
    std::uint64_t renderedNotObserved = 0;
    LONGLONG runStartedAt = 0, runLastFrameAt = 0;
    double runUnknownMs = 0, sourceMaxPaintMs = 0, sourceMaxIntervalMs = 0;
    double maxYawStep = 0;
    bool sourceLimited = false;
};

struct AppState {
    HINSTANCE instance = nullptr;
    HWND hwnd = nullptr;
    HWND resetButton = nullptr;
    HWND controls[20]{};
    HWND fpsControlCombo = nullptr, fpsSceneCombo = nullptr, fpsRestartButton = nullptr, fpsPressureCombo = nullptr;
    HWND bitrateEdit = nullptr, comparisonButton = nullptr;
    bool fpsManual = false, fpsCaptured = false, rawInputOk = false, fpsAdvancePaint = false;
    std::uint64_t fpsRawEvents = 0;
    LONGLONG runEndedAt = 0;
    int fpsScene = 1, fpsPressure = 2;
    double fpsYaw = 0, fpsPitch = 0;
    fps::Pose fpsPose{};
    HDC textureDc = nullptr;
    HBITMAP textureBitmap = nullptr, textureOriginal = nullptr;
    int textureScene = -1;
    std::uint16_t fpsFields[fps::kFields]{};
    LONGLONG fpsLastPaintAt = 0;
    double fpsLastPaintMs = 0, fpsActualIntervalMs = 0;
    std::atomic<LONGLONG> runId{0};
    std::atomic<int> bitrateMbps{20};
    CRITICAL_SECTION logLock{};
    struct Run {
        LONGLONG id = 0;
        int bitrate = 0, flags = 0;
        double duration = 0, fps = 0, p95 = 0, p99 = 0, longest = 0, unknown = 0, tracking = 0,
               confidence = 0, sourcePaint = 0, sourceInterval = 0;
        unsigned long long frames = 0, hitches = 0, missing = 0, uncertain = 0;
        bool sourceLimited = false;
    } runs[16]{};
    int runCount = 0;
    wchar_t comparisonPath[MAX_PATH]{};
    HWND modeCombo = nullptr, rateCombo = nullptr, displayCombo = nullptr, ratioSlider = nullptr;
    HWND lockButton = nullptr, topmostButton = nullptr, recordButton = nullptr,
         detailsButton = nullptr;
    HWND fullExit = nullptr, roiButton = nullptr, openButton = nullptr;
    int codeRatio = 28;
    bool fullscreen = false, layoutLocked = false, analyzerTopmost = false, showDetails = false;
    bool signalDirty = true, minimized = false;
    bool paintActive = false, paintAgain = false;
    WINDOWPLACEMENT normalPlacement{};
    HDC backDc = nullptr;
    HBITMAP backBitmap = nullptr, backOriginal = nullptr;
    int backWidth = 0, backHeight = 0;
    int mainFontPixels = 0, statusFontPixels = 0, smallFontPixels = 0;
    HANDLE stopEvent = nullptr, logEvent = nullptr, logThread = nullptr;
    std::atomic<bool> recording{false}, logQuit{false};
    std::atomic<unsigned int> logWrite{0}, logRead{0};
    std::atomic<unsigned long long> logDropped{0};
    std::atomic<bool> logError{false};
    char logQueue[kLogQueueSize][512]{};
    std::uint32_t preview[kPreviewWidth * kPreviewHeight]{};
    bool hasPreview = false;
    int previewSampleWidth = 128;
    unsigned int roiGeneration = 0;
    wchar_t configPath[MAX_PATH]{};
    HHOOK mouseHook = nullptr;
    HANDLE workerThread = nullptr;
    LARGE_INTEGER qpcFrequency{};
    LONGLONG processEpoch = 0;
    std::atomic<double> renderRateHz{60.0};
    LONGLONG pressedAt = 0;
    LONGLONG lastHoldTicks = 0;
    LONGLONG frameEpoch = 0;
    double displayRateHz = 60.0;
    LONGLONG nextRefreshCheck = 0;
    HFONT mainFont = nullptr;
    HFONT statusFont = nullptr;
    HFONT smallFont = nullptr;
    CRITICAL_SECTION statsLock{};
    AnalyzerStats analyzer{};
    LaunchMode launchMode = LaunchMode::None;
    TestMode testMode = TestMode::Latency;
    int frameNumber = 1;
    int motionPhase = 0;
    int targetRateIndex = 0;
    UINT dpi = 96;
    POINT lastFollowPosition{-10000, -10000};
    std::uint16_t eventSequence = 0;
    std::uint16_t machineSequence = 0;
    std::uint16_t mouseMoveSequence = 0;
    POINT signalCursorPosition{};
    wchar_t displayName[CCHDEVICENAME] = L"DISPLAY";
    wchar_t resultsPath[MAX_PATH] = L"CloudLatencyFPS_results_v0.8.csv";
    std::atomic<bool> quit{false};
    std::atomic<bool> tickPending{false};
    bool mouseDown = false;
    bool hotkeysOk = true;
    bool statsLockInitialized = false;
};

AppState g;
LaunchMode gChooserResult = LaunchMode::None;
constexpr int kTargetRates[] = {0, 30, 60, 90, 120, 144, 165, 240};
constexpr int kTargetRateCount = static_cast<int>(sizeof(kTargetRates) / sizeof(kTargetRates[0]));
constexpr int kSyncBits[kCodeColumns] = {1, 0, 1, 0, 0, 1, 0, 1};
constexpr std::uint16_t kFullSequenceMask = 0x7FFFU;
constexpr std::uint16_t kStreamSequenceMask = 0x1FFFU;

int ModeIndex(TestMode mode) { return static_cast<int>(mode); }

bool IsCadenceMode(TestMode mode) { return mode != TestMode::Latency; }

std::uint16_t SequenceMask(TestMode mode) {
    return mode == TestMode::Latency ? kFullSequenceMask : kStreamSequenceMask;
}

bool IsAnalyzer() { return g.launchMode == LaunchMode::Analyzer; }

bool IsFollowSignal() { return g.launchMode == LaunchMode::FollowSignal; }

int Scale(int value) { return MulDiv(value, static_cast<int>(g.dpi), 96); }
int SignalHeaderHeight() {
    return IsFollowSignal()
               ? Scale(24)
               : (g.fullscreen ? Scale(42) : Scale(g.testMode == TestMode::FpsView ? 144 : 106));
}
int SignalFooterHeight() { return IsFollowSignal() ? Scale(24) : Scale(28); }
RECT WinRect(layout::Rect r) { return RECT{r.left, r.top, r.right, r.bottom}; }
layout::Signal SignalLayout(const RECT &client) {
    return layout::Compute(
        static_cast<int>(client.right), static_cast<int>(client.bottom), SignalHeaderHeight(),
        SignalFooterHeight(), Scale(IsFollowSignal() ? 8 : 16), g.codeRatio,
        g.testMode == TestMode::AutoMotion || g.testMode == TestMode::MouseMotion,
        IsFollowSignal());
}
void LayoutControls(HWND hwnd);
void SetPresentation(int mode);
void SyncControls();
void RefreshSignalRate();
void QueueLog(const char *line);
void ReleaseFpsCapture();
void SaveFpsRun();
void ToggleRecording();
void WriteComparison(bool openReport = true);
void BeginFpsMode();
void RestartFpsScenario();
void CycleFpsControl();
void CycleFpsScene();
void CycleFpsPressure();
void HandleRawMouse(LPARAM parameter);

LONGLONG QpcNow() {
    LARGE_INTEGER value{};
    QueryPerformanceCounter(&value);
    return value.QuadPart;
}

double TicksToMilliseconds(LONGLONG ticks) {
    return g.qpcFrequency.QuadPart > 0
               ? static_cast<double>(ticks) * 1000.0 / static_cast<double>(g.qpcFrequency.QuadPart)
               : 0.0;
}

void FillSolid(HDC dc, const RECT &rect, COLORREF color) {
    SetDCBrushColor(dc, color);
    FillRect(dc, &rect, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
}

HFONT MakeFont(int pixelHeight, int weight, const wchar_t *face) {
    return CreateFontW(-std::max(pixelHeight, 8), 0, 0, 0, weight, FALSE, FALSE, FALSE,
                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                       DEFAULT_PITCH | FF_DONTCARE, face);
}

void DeleteFonts() {
    if (g.mainFont != nullptr) {
        DeleteObject(g.mainFont);
        g.mainFont = nullptr;
    }
    if (g.statusFont != nullptr) {
        DeleteObject(g.statusFont);
        g.statusFont = nullptr;
    }
    if (g.smallFont != nullptr) {
        DeleteObject(g.smallFont);
        g.smallFont = nullptr;
    }
}

void RecreateFonts(HWND hwnd) {
    RECT client{};
    GetClientRect(hwnd, &client);
    int mainPixels = Scale(34);
    if (!IsAnalyzer()) {
        const auto l = SignalLayout(client);
        const int width = (l.value.right - l.value.left) / (l.stacked ? 2 : 1);
        const int height = l.value.bottom - l.value.top;
        mainPixels = std::clamp(
            std::min(width / 6, std::max(height - Scale(l.stacked ? 4 : 38), 12)), 12, Scale(220));
    }
    const int statusPixels = Scale(IsFollowSignal() ? 14 : 20);
    const int smallPixels = Scale(IsFollowSignal() ? 12 : 14);
    if (mainPixels != g.mainFontPixels || !g.mainFont) {
        if (g.mainFont)
            DeleteObject(g.mainFont);
        g.mainFont = MakeFont(mainPixels, FW_MEDIUM, L"Consolas");
        g.mainFontPixels = mainPixels;
    }
    if (statusPixels != g.statusFontPixels || !g.statusFont) {
        if (g.statusFont)
            DeleteObject(g.statusFont);
        g.statusFont = MakeFont(statusPixels, FW_MEDIUM, L"Microsoft YaHei UI");
        g.statusFontPixels = statusPixels;
    }
    if (smallPixels != g.smallFontPixels || !g.smallFont) {
        if (g.smallFont)
            DeleteObject(g.smallFont);
        g.smallFont = MakeFont(smallPixels, FW_NORMAL, L"Microsoft YaHei UI");
        g.smallFontPixels = smallPixels;
        for (HWND control : g.controls)
            if (control)
                SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(g.smallFont), TRUE);
    }
}

void DrawCenteredText(HDC dc, const wchar_t *text, RECT rect, HFONT font, COLORREF color) {
    HFONT previous = static_cast<HFONT>(SelectObject(dc, font));
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    DrawTextW(dc, text, -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(dc, previous);
}

void DrawLeftText(HDC dc, const wchar_t *text, RECT rect, HFONT font, COLORREF color) {
    HFONT previous = static_cast<HFONT>(SelectObject(dc, font));
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    DrawTextW(dc, text, -1, &rect,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    SelectObject(dc, previous);
}

std::uint8_t Crc8(std::uint16_t data) {
    std::uint8_t crc = 0;
    for (int byteIndex = 1; byteIndex >= 0; --byteIndex) {
        crc ^= static_cast<std::uint8_t>((data >> (byteIndex * 8)) & 0xFFU);
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc & 0x80U) != 0U ? static_cast<std::uint8_t>((crc << 1U) ^ 0x07U)
                                      : static_cast<std::uint8_t>(crc << 1U);
        }
    }
    return crc;
}

std::uint16_t GrayToBinary(std::uint16_t gray) {
    std::uint16_t binary = 0;
    for (gray &= 0x7FFFU; gray != 0; gray >>= 1U) {
        binary ^= gray;
    }
    return static_cast<std::uint16_t>(binary & 0x7FFFU);
}

std::uint32_t MakePayload(TestMode mode, std::uint16_t sequence) {
    std::uint16_t data = 0;
    if (mode == TestMode::Latency) {
        const auto gray =
            static_cast<std::uint16_t>((sequence ^ (sequence >> 1U)) & kFullSequenceMask);
        data = gray;
    } else {
        // Non-latency payload: 1 flag bit, 2 subtype bits and a 13-bit
        // rolling sequence. This keeps the existing 8x4 marker and CRC-8.
        const auto subtype = static_cast<std::uint16_t>(ModeIndex(mode) - 1);
        data = static_cast<std::uint16_t>(0x8000U | ((subtype & 0x3U) << 13U) |
                                          (sequence & kStreamSequenceMask));
    }
    std::uint32_t payload = (static_cast<std::uint32_t>(data) << 8U) | Crc8(data);
    if (mode == TestMode::Latency && (sequence & 1U) != 0U) {
        payload = (~payload) & 0xFFFFFFU;
    }
    return payload;
}

void DrawMachineCode(HDC dc, const RECT &rect, TestMode mode, std::uint16_t sequence) {
    const std::uint32_t payload = MakePayload(mode, sequence);
    for (int row = 0; row < kCodeRows; ++row) {
        for (int column = 0; column < kCodeColumns; ++column) {
            bool white = false;
            if (row == 0) {
                white = kSyncBits[column] != 0;
            } else {
                const int index = (row - 1) * kCodeColumns + column;
                white = ((payload >> (kPayloadBits - 1 - index)) & 1U) != 0U;
            }
            RECT cell{rect.left + (rect.right - rect.left) * column / kCodeColumns,
                      rect.top + (rect.bottom - rect.top) * row / kCodeRows,
                      rect.left + (rect.right - rect.left) * (column + 1) / kCodeColumns,
                      rect.top + (rect.bottom - rect.top) * (row + 1) / kCodeRows};
            FillSolid(dc, cell, white ? RGB(250, 250, 250) : RGB(4, 4, 4));
        }
    }
    FrameRect(dc, &rect, static_cast<HBRUSH>(GetStockObject(GRAY_BRUSH)));
}

int EffectiveFrameUpper() {
    if (g.targetRateIndex == 0) {
        return std::clamp(static_cast<int>(std::lround(g.displayRateHz)), 24, 500);
    }
    return kTargetRates[g.targetRateIndex];
}

void RefreshSignalRate() {
    const double rate =
        g.minimized
            ? 2.0
            : ((g.testMode == TestMode::Latency && !g.mouseDown && !IsFollowSignal())
                   ? 10.0
                   : (IsCadenceMode(g.testMode) ? static_cast<double>(EffectiveFrameUpper())
                                                : std::clamp(g.displayRateHz, 24.0, 240.0)));
    g.renderRateHz.store(rate, std::memory_order_relaxed);
}
void UpdateRenderRate() { RefreshSignalRate(); }

double DetectDisplayRate(HWND hwnd, wchar_t *displayName, int nameLength) {
    HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFOEXW monitorInfo{};
    monitorInfo.cbSize = sizeof(monitorInfo);
    if (GetMonitorInfoW(monitor, &monitorInfo)) {
        lstrcpynW(displayName, monitorInfo.szDevice, nameLength);
    }
    DWM_TIMING_INFO timing{};
    timing.cbSize = sizeof(timing);
    if (SUCCEEDED(DwmGetCompositionTimingInfo(hwnd, &timing)) &&
        timing.rateRefresh.uiNumerator > 0 && timing.rateRefresh.uiDenominator > 0) {
        const double rate = static_cast<double>(timing.rateRefresh.uiNumerator) /
                            static_cast<double>(timing.rateRefresh.uiDenominator);
        if (rate >= 20.0 && rate <= 1000.0) {
            return rate;
        }
    }
    DEVMODEW mode{};
    mode.dmSize = sizeof(mode);
    if (EnumDisplaySettingsExW(displayName, ENUM_CURRENT_SETTINGS, &mode, 0) &&
        mode.dmDisplayFrequency >= 20 && mode.dmDisplayFrequency <= 1000) {
        return static_cast<double>(mode.dmDisplayFrequency);
    }
    return 60.0;
}

void RefreshDisplayInfo(HWND hwnd, bool force) {
    wchar_t name[CCHDEVICENAME] = L"DISPLAY";
    const double detected = DetectDisplayRate(hwnd, name, CCHDEVICENAME);
    if (force || std::fabs(detected - g.displayRateHz) > 0.001 ||
        lstrcmpW(name, g.displayName) != 0) {
        g.displayRateHz = detected;
        lstrcpynW(g.displayName, name, CCHDEVICENAME);
        UpdateRenderRate();
        InvalidateRect(hwnd, nullptr, FALSE);
    }
}

RECT GetCodeRect(const RECT &client) { return WinRect(SignalLayout(client).code); }
const wchar_t *ModeName(TestMode mode) {
    const wchar_t *names[] = {L"点击延迟", L"帧率", L"自动运动", L"鼠标运动", L"FPS 转视角"};
    return names[std::clamp(ModeIndex(mode), 0, 4)];
}
void DrawMotionTarget(HDC dc, const RECT &track, bool automatic) {
    if (track.right - track.left < 16 || track.bottom - track.top < 16)
        return;
    FillSolid(dc, track, RGB(12, 23, 38));
    for (int division = 1; division < 12; ++division) {
        const int x = track.left + (track.right - track.left) * division / 12;
        FillSolid(dc, RECT{x, track.top, x + 1, track.bottom}, RGB(39, 52, 70));
    }
    const int half = Scale(19), thick = std::max(Scale(2), 1);
    const int inset =
        automatic ? Scale(5)
                  : std::min(half + Scale(3), static_cast<int>(std::min(track.right - track.left,
                                                                        track.bottom - track.top)) /
                                                  3);
    const int w = std::max(static_cast<int>(track.right - track.left) - inset * 2, 1);
    const int h = std::max(static_cast<int>(track.bottom - track.top) - inset * 2, 1);
    int x = track.left + inset;
    if (automatic) {
        const int upper = std::max(EffectiveFrameUpper(), 2), cycle = std::max(upper * 2 - 2, 2);
        const int phase = g.motionPhase % cycle, forward = phase < upper ? phase : cycle - phase;
        x += w * forward / (upper - 1);
        FillSolid(dc, RECT{x - Scale(3), track.top + 4, x + Scale(4), track.bottom - 4},
                  RGB(250, 250, 250));
    } else {
        POINT pt = g.signalCursorPosition;
        ScreenToClient(g.hwnd, &pt);
        x = std::clamp(static_cast<int>(pt.x), static_cast<int>(track.left) + inset,
                       static_cast<int>(track.left) + inset + w);
        const int y = std::clamp(static_cast<int>(pt.y), static_cast<int>(track.top) + inset,
                                 static_cast<int>(track.top) + inset + h);
        // White cross with black edging; the crossing is the received cursor point.
        const int arm = std::min(half, inset - 2);
        FillSolid(dc, RECT{x - arm - 2, y - thick - 2, x + arm + 3, y + thick + 3}, RGB(0, 0, 0));
        FillSolid(dc, RECT{x - thick - 2, y - arm - 2, x + thick + 3, y + arm + 3}, RGB(0, 0, 0));
        FillSolid(dc, RECT{x - arm, y - thick, x + arm + 1, y + thick + 1}, RGB(255, 255, 255));
        FillSolid(dc, RECT{x - thick, y - arm, x + thick + 1, y + arm + 1}, RGB(255, 255, 255));
    }
}

#include "fps_3d.inl"
#include "fps_signal.inl"

void PaintSignalWindow(HDC dc, const RECT &client) {
    if (g.testMode != TestMode::FpsView || g.fpsScene >= 2) gpu::Hide();
    if (g.testMode == TestMode::FpsView) {
        PaintFpsSignal(dc, client);
        return;
    }
    FillSolid(dc, client, kBackground);
    const auto l = SignalLayout(client);
    const RECT code = GetCodeRect(client), panel = WinRect(l.value);
    wchar_t text[192]{};
    if (!IsFollowSignal() && !g.fullscreen) {
        DrawLeftText(dc, L"测试", RECT{Scale(16), Scale(8), Scale(55), Scale(40)}, g.smallFont,
                     kMuted);
        DrawLeftText(dc, L"频率", RECT{Scale(221), Scale(8), Scale(260), Scale(40)}, g.smallFont,
                     kMuted);
        std::swprintf(text, 192, L"识别码 %d%%", g.codeRatio);
        DrawLeftText(dc, text, RECT{Scale(16), Scale(54), Scale(130), Scale(87)}, g.smallFont,
                     kMuted);
        std::swprintf(text, 192, L"%ls · %.2f Hz · %ls", ModeName(g.testMode), g.displayRateHz,
                      g.layoutLocked ? L"布局已锁定" : L"缩放或全屏后重新框选");
        DrawLeftText(dc, text, RECT{Scale(16), Scale(87), client.right - Scale(16), Scale(106)},
                     g.smallFont, kMuted);
    } else {
        std::swprintf(text, 192, L"%ls · %d Hz%ls", ModeName(g.testMode), EffectiveFrameUpper(),
                      g.fullscreen ? L" · 全屏" : L"");
        DrawLeftText(
            dc, text,
            RECT{Scale(10), 0, client.right - Scale(g.fullscreen ? 145 : 10), SignalHeaderHeight()},
            g.smallFont, kMuted);
    }
    COLORREF panelColor = kPanel, ink = kText;
    const wchar_t *caption = L"按住时长 · ms";
    wchar_t value[64] = L"READY";
    if (g.testMode == TestMode::Latency && g.mouseDown) {
        panelColor = RGB(55, 224, 112);
        ink = RGB(4, 24, 10);
        std::swprintf(value, 64, L"%.3f",
                      TicksToMilliseconds(std::max(QpcNow() - g.pressedAt, 0LL)));
    } else if (g.testMode == TestMode::FrameRate) {
        caption = L"生成帧序号";
        std::swprintf(value, 64, L"%d", g.frameNumber);
    } else if (g.testMode == TestMode::AutoMotion) {
        caption = L"匀速往返 · 目标 Hz";
        std::swprintf(value, 64, L"%d", EffectiveFrameUpper());
    } else if (g.testMode == TestMode::MouseMotion) {
        caption = L"鼠标移动事件号 · 十字中心对应收到的位置";
        std::swprintf(value, 64, L"%u", static_cast<unsigned>(g.mouseMoveSequence));
    }
    FillSolid(dc, panel, panelColor);
    if (l.stacked) {
        RECT label = panel;
        label.right = panel.left + (panel.right - panel.left) / 2;
        RECT val = panel;
        val.left = label.right;
        DrawLeftText(dc, caption, label, g.smallFont, ink);
        DrawCenteredText(dc, value, val, g.mainFont, ink);
    } else {
        RECT label = panel;
        label.bottom = std::min(panel.bottom, panel.top + Scale(30));
        DrawCenteredText(dc, caption, label, g.smallFont, ink);
        RECT val = panel;
        val.top = label.bottom;
        DrawCenteredText(dc, value, val, g.mainFont, ink);
    }
    if (l.motion.bottom > l.motion.top)
        DrawMotionTarget(dc, WinRect(l.motion), g.testMode == TestMode::AutoMotion);
    DrawMachineCode(dc, code, g.testMode, g.machineSequence);
    const wchar_t *foot = IsFollowSignal()
                              ? L"F8 模式 · F11 全屏 · Ctrl+Alt+F12 退出"
                              : L"F6 频率 · F8 模式 · F9 复位 · F11 全屏 · Ctrl+Alt+F12 退出";
    DrawCenteredText(dc, foot,
                     RECT{8, client.bottom - SignalFooterHeight(), client.right - 8, client.bottom},
                     g.smallFont, kMuted);
}

void CopyAnalyzerStats(AnalyzerStats *destination) {
    EnterCriticalSection(&g.statsLock);
    *destination = g.analyzer;
    LeaveCriticalSection(&g.statsLock);
}

__declspec(noinline) void PaintAnalyzerWindow(HDC dc, const RECT &client) {
    AnalyzerStats stats{};
    CopyAnalyzerStats(&stats);
    const int width = static_cast<int>(client.right), height = static_cast<int>(client.bottom),
              m = Scale(16);
    FillSolid(dc, client, kBackground);
    DrawLeftText(dc, L"自动识别与结果", RECT{m, Scale(6), width - m, Scale(36)}, g.statusFont,
                 kText);
    const bool recording = g.recording.load(std::memory_order_relaxed);
    DrawLeftText(dc, L"码率标签", RECT{m, Scale(86), Scale(90), Scale(118)}, g.smallFont, kMuted);
    DrawLeftText(dc, L"Mbps · 填串流设置值 · 低→高→低重复相同动作",
                 RECT{Scale(185), Scale(86), width - Scale(142), Scale(118)}, g.smallFont, kMuted);
    wchar_t text[320]{};
    const int previewTop = Scale(128), previewW = Scale(128), previewH = Scale(64);
    RECT previewRect{m, previewTop, m + previewW, previewTop + previewH};
    FillSolid(dc, previewRect, kPanel);
    std::uint32_t preview[kPreviewWidth * kPreviewHeight];
    bool hasPreview;
    int sampleWidth;
    EnterCriticalSection(&g.statsLock);
    hasPreview = g.hasPreview;
    sampleWidth = g.previewSampleWidth;
    if (hasPreview)
        std::copy(std::begin(g.preview), std::end(g.preview), preview);
    LeaveCriticalSection(&g.statsLock);
    if (hasPreview) {
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = kPreviewWidth;
        info.bmiHeader.biHeight = -kPreviewHeight;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        StretchDIBits(dc, m, previewTop, previewW, sampleWidth == 256 ? previewH / 2 : previewH, 0,
                      0, sampleWidth, kPreviewHeight, preview, &info, DIB_RGB_COLORS, SRCCOPY);
    }
    const wchar_t *state =
        !stats.roiValid       ? L"等待框选：F2 左上角 → F3 右下角"
        : stats.roiObscured   ? L"选区被识别器遮挡，请移开本窗口"
        : stats.captureFailed ? L"截图失败，请检查云电脑显示方式"
        : (!stats.decoderLocked || QpcNow() - stats.lastValidAt > g.qpcFrequency.QuadPart / 2)
            ? L"暂无有效识别码，请检查选区"
            : L"识别码有效";
    DrawLeftText(dc, state,
                 RECT{m + previewW + Scale(14), previewTop, width - m, previewTop + Scale(25)},
                 g.smallFont, kText);
    std::swprintf(text, 320, L"%ls · %ld×%ld px · %ls",
                  stats.decodedMode >= 0 ? ModeName(static_cast<TestMode>(stats.decodedMode))
                                         : L"模式自动识别",
                  stats.roi.right - stats.roi.left, stats.roi.bottom - stats.roi.top,
                  recording ? L"正在记录" : L"预览 / 已停止");
    DrawLeftText(
        dc, text,
        RECT{m + previewW + Scale(14), previewTop + Scale(28), width - m, previewTop + Scale(54)},
        g.smallFont, kMuted);
    const int cardsTop = Scale(212), cardsBottom = Scale(322), gap = Scale(12),
              cw = (width - m * 2 - gap * 2) / 3;
    const wchar_t *labels[3] = {L"最近响应延迟", L"中位数", L"P95"};
    double values[3] = {stats.lastLatencyMs, stats.latencyMedianMs, stats.latencyP95Ms};
    const wchar_t *units[3] = {L"ms", L"ms", L"ms"};
    bool hasData = stats.latencyStored > 0;
    if (stats.decodedMode == ModeIndex(TestMode::FpsView)) {
        labels[0] = L"观察画面更新率";
        labels[1] = L"有效帧间隔 P95";
        labels[2] = L"最长有效间隔";
        values[0] = stats.rollingVisibleFps;
        values[1] = stats.frameP95Ms;
        values[2] = stats.frameLongestMs;
        units[0] = L"次/s";
        hasData = stats.runFrames > 1;
    } else if (stats.decodedMode == ModeIndex(TestMode::MouseMotion)) {
        labels[0] = L"最近移动起始延迟";
        labels[1] = L"移动码更新率";
        labels[2] = L"最长更新间隔";
        values[0] = stats.lastMotionLatencyMs;
        values[1] = stats.rollingVisibleFps;
        values[2] = stats.frameLongestMs;
        units[1] = L"次/s";
        hasData = stats.motionLatencyStored > 0;
    } else if (stats.decodedMode > 0) {
        labels[0] = L"可见更新率";
        labels[1] = L"帧间隔 P95";
        labels[2] = L"最长更新间隔";
        values[0] = stats.rollingVisibleFps;
        values[1] = stats.frameP95Ms;
        values[2] = stats.frameLongestMs;
        units[0] = L"次/s";
        hasData = stats.frameHistoryCount > 1;
    }
    for (int i = 0; i < 3; ++i) {
        RECT card{m + i * (cw + gap), cardsTop, m + i * (cw + gap) + cw, cardsBottom};
        FillSolid(dc, card, kPanel);
        DrawLeftText(dc, labels[i],
                     RECT{card.left + Scale(10), card.top + Scale(7), card.right - Scale(6),
                          card.top + Scale(32)},
                     g.smallFont, kMuted);
        if (hasData)
            std::swprintf(text, 320, L"%.1f", values[i]);
        else
            lstrcpyW(text, L"—");
        DrawLeftText(dc, text,
                     RECT{card.left + Scale(10), card.top + Scale(34), card.right - Scale(6),
                          card.top + Scale(79)},
                     g.mainFont, kText);
        DrawLeftText(
            dc, units[i],
            RECT{card.left + Scale(10), card.top + Scale(80), card.right - Scale(6), card.bottom},
            g.smallFont, kMuted);
    }
    const bool latency = stats.decodedMode <= 0;
    DrawLeftText(dc, latency ? L"最近响应延迟 · 最多 120 次" : L"最近更新间隔 · 最多 120 次",
                 RECT{m, Scale(332), width - m, Scale(358)}, g.smallFont, kText);
    double series[120]{};
    int count = 0;
    if (latency) {
        count = std::min(stats.latencyStored, 120);
        for (int i = 0; i < count; ++i) {
            const int index =
                (stats.latencyWriteIndex - count + i + kMaxLatencySamples) % kMaxLatencySamples;
            series[i] = stats.latencySamples[index];
        }
    } else {
        const int points = std::min(stats.frameHistoryCount, 121);
        LONGLONG previous = 0;
        for (int i = 0; i < points; ++i) {
            const int index =
                (stats.frameHistoryWrite - points + i + kFrameHistorySize) % kFrameHistorySize;
            const LONGLONG t = stats.frameTimes[index];
            if (i > 0 &&
                (stats.decodedMode != ModeIndex(TestMode::FpsView) || stats.intervalValid[index]))
                series[count++] = TicksToMilliseconds(t - previous);
            previous = t;
        }
    }
    const int graphTop = Scale(378),
              graphBottom =
                  std::max(graphTop + Scale(50), height - Scale(g.showDetails ? 218 : 148));
    const int left = m + Scale(44), right = width - m;
    double maximum = 40.0;
    for (int i = 0; i < count; ++i)
        maximum = std::max(maximum, series[i] * 1.15);
    HPEN oldPen = static_cast<HPEN>(SelectObject(dc, GetStockObject(DC_PEN)));
    SetDCPenColor(dc, RGB(62, 77, 98));
    MoveToEx(dc, left, graphTop, nullptr);
    LineTo(dc, left, graphBottom);
    LineTo(dc, right, graphBottom);
    std::swprintf(text, 320, L"%.0f", maximum);
    DrawLeftText(dc, text, RECT{m, graphTop - Scale(10), left - 4, graphTop + Scale(12)},
                 g.smallFont, kMuted);
    DrawLeftText(dc, L"0", RECT{m, graphBottom - Scale(14), left - 4, graphBottom + Scale(6)},
                 g.smallFont, kMuted);
    DrawLeftText(dc, L"ms", RECT{m, graphTop - Scale(31), left - 4, graphTop - Scale(10)},
                 g.smallFont, kMuted);
    if (count > 1) {
        POINT points[120]{};
        for (int i = 0; i < count; ++i) {
            points[i].x = left + (right - left) * i / (count - 1);
            points[i].y =
                graphBottom - static_cast<int>((graphBottom - graphTop) * series[i] / maximum);
        }
        SetDCPenColor(dc, RGB(132, 179, 255));
        Polyline(dc, points, count);
    } else
        DrawCenteredText(dc, L"记录后显示趋势", RECT{left, graphTop, right, graphBottom},
                         g.smallFont, kMuted);
    SelectObject(dc, oldPen);
    DrawLeftText(dc, L"较早",
                 RECT{left, graphBottom + 2, left + Scale(80), graphBottom + Scale(24)},
                 g.smallFont, kMuted);
    DrawLeftText(dc, L"最近",
                 RECT{right - Scale(45), graphBottom + 2, right, graphBottom + Scale(24)},
                 g.smallFont, kMuted);
    if (stats.decodedMode == ModeIndex(TestMode::FpsView))
        std::swprintf(text, 320,
                      L"整轮长间隔 %llu · 未观察帧 %llu · 不确定间隔 %llu · 识别未知 %.0f ms",
                      static_cast<unsigned long long>(stats.runHitches),
                      static_cast<unsigned long long>(stats.renderedNotObserved),
                      static_cast<unsigned long long>(stats.unknownIntervals), stats.runUnknownMs);
    else if (latency)
        std::swprintf(text, 320, L"有效点击 %llu   超时 %llu   无效码采样 %llu",
                      static_cast<unsigned long long>(stats.latencyTotal),
                      static_cast<unsigned long long>(stats.timedOutClicks),
                      static_cast<unsigned long long>(stats.invalidPolls));
    else
        std::swprintf(text, 320, L"近 5 秒卡顿 %d   未观察到序号 %llu   无效码采样 %llu",
                      stats.recentStutters,
                      static_cast<unsigned long long>(stats.skippedFrameSlots),
                      static_cast<unsigned long long>(stats.invalidPolls));
    DrawLeftText(dc, text, RECT{m, graphBottom + Scale(26), width - m, graphBottom + Scale(51)},
                 g.smallFont, kMuted);
    if (g.showDetails) {
        std::swprintf(text, 320, L"间隔中位 %.2f ms   P99 %.2f ms   1%% Low %.1f FPS   MAD %.2f ms",
                      stats.frameMedianMs, stats.frameP99Ms, stats.onePercentLowFps,
                      stats.frameJitterMadMs);
        DrawLeftText(dc, text, RECT{m, graphBottom + Scale(54), width - m, graphBottom + Scale(79)},
                     g.smallFont, kMuted);
        std::swprintf(text, 320, L"采集 %.2f ms   对比度指标 %.0f%%   CSV 丢弃 %llu%ls",
                      stats.captureMs, stats.confidence * 100.0,
                      g.logDropped.load(std::memory_order_relaxed),
                      g.logError.load() ? L" · CSV 写入失败" : L"");
        DrawLeftText(dc, text,
                     RECT{m, graphBottom + Scale(82), width - m, graphBottom + Scale(107)},
                     g.smallFont, kMuted);
        std::swprintf(text, 320, L"移动起始：中位 %.2f ms / P95 %.2f ms / 样本 %llu",
                      stats.motionMedianMs, stats.motionP95Ms,
                      static_cast<unsigned long long>(stats.motionLatencyTotal));
        DrawLeftText(dc, text,
                     RECT{m, graphBottom + Scale(110), width - m, graphBottom + Scale(135)},
                     g.smallFont, kMuted);
    }
    if (stats.decodedMode == ModeIndex(TestMode::FpsView)) {
        if (stats.tracking.valid)
            std::swprintf(
                text, 320,
                L"跟踪估计 %.0f ms (r=%.3f) · 起始 %.1f ms · 云端绘制 %.1f ms / 间隔 %.1f ms",
                stats.tracking.latency, stats.tracking.correlation, stats.lastMotionLatencyMs,
                stats.sourcePaintMs, stats.sourceIntervalMs);
        else
            std::swprintf(
                text, 320, L"跟踪：%ls · 云端绘制 %.1f ms · 云端间隔 %.1f ms · 已保存 %d 轮",
                (stats.fpsFlags & 1) ? L"请左右变速、反向；证据不足" : L"自动模式不测输入延迟",
                stats.sourcePaintMs, stats.sourceIntervalMs, g.runCount);
        DrawLeftText(dc, text, RECT{m, height - Scale(69), width - m, height - Scale(42)},
                     g.smallFont, kMuted);
    }
    DrawLeftText(dc, g.logError.load() ? L"CSV 无法写入，请将程序放到可写文件夹" : g.resultsPath,
                 RECT{m, height - Scale(37), width - m, height - Scale(8)}, g.smallFont, kMuted);
}

void ReleaseBackBuffer() {
    if (g.backDc) {
        if (g.backOriginal)
            SelectObject(g.backDc, g.backOriginal);
        if (g.backBitmap)
            DeleteObject(g.backBitmap);
        DeleteDC(g.backDc);
    }
    g.backDc = nullptr;
    g.backBitmap = nullptr;
    g.backOriginal = nullptr;
    g.backWidth = 0;
    g.backHeight = 0;
}
__declspec(noinline) void PaintWindow(HWND hwnd) {
    const LONGLONG paintStarted = QpcNow();
    PAINTSTRUCT paint{};
    HDC windowDc = BeginPaint(hwnd, &paint);
    RECT client{};
    GetClientRect(hwnd, &client);
    const int width = std::max(static_cast<int>(client.right), 1),
              height = std::max(static_cast<int>(client.bottom), 1);
    if (!g.backDc || width != g.backWidth || height != g.backHeight) {
        ReleaseBackBuffer();
        g.backDc = CreateCompatibleDC(windowDc);
        g.backBitmap = CreateCompatibleBitmap(windowDc, width, height);
        if (g.backDc && g.backBitmap) {
            g.backOriginal = static_cast<HBITMAP>(SelectObject(g.backDc, g.backBitmap));
            g.backWidth = width;
            g.backHeight = height;
        }
    }
    HDC dc = g.backDc && g.backBitmap ? g.backDc : windowDc;
    g.fpsAdvancePaint = !IsAnalyzer() && g.testMode == TestMode::FpsView &&
                        paint.rcPaint.left == 0 && paint.rcPaint.top == 0 &&
                        paint.rcPaint.right >= client.right &&
                        paint.rcPaint.bottom >= client.bottom;
    const int saved = SaveDC(dc);
    IntersectClipRect(dc, paint.rcPaint.left, paint.rcPaint.top, paint.rcPaint.right,
                      paint.rcPaint.bottom);
    if (IsAnalyzer())
        PaintAnalyzerWindow(dc, client);
    else
        PaintSignalWindow(dc, client);
    RestoreDC(dc, saved);
    if (dc != windowDc)
        BitBlt(windowDc, paint.rcPaint.left, paint.rcPaint.top,
               paint.rcPaint.right - paint.rcPaint.left, paint.rcPaint.bottom - paint.rcPaint.top,
               dc, paint.rcPaint.left, paint.rcPaint.top, SRCCOPY);
    EndPaint(hwnd, &paint);
    if (g.fpsAdvancePaint) {
        GdiFlush();
        g.fpsLastPaintMs = TicksToMilliseconds(QpcNow() - paintStarted);
        RecordFpsSourcePaint();
    }
}

void DispatchPaint(HWND hwnd) {
    // Keep USER32 callbacks shallow and protect the active GDI/OpenGL frame
    // from synchronous WM_PAINT caused by a nested control/window operation.
    if (g.paintActive) {
        g.paintAgain = true;
        ValidateRect(hwnd, nullptr);
        return;
    }
    g.paintActive = true;
    PaintWindow(hwnd);
    g.paintActive = false;
    if (g.paintAgain) {
        g.paintAgain = false;
        InvalidateRect(hwnd, nullptr, FALSE);
    }
}

void UpdateFollowPosition(HWND hwnd) {
    if (!IsFollowSignal()) {
        return;
    }
    POINT cursor{};
    if (!GetCursorPos(&cursor)) {
        return;
    }
    const int width = MulDiv(kFollowInitialWidth, static_cast<int>(g.dpi), 96);
    const int height = MulDiv(kFollowInitialHeight, static_cast<int>(g.dpi), 96);
    const int offset = MulDiv(20, static_cast<int>(g.dpi), 96);
    HMONITOR monitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST);
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    GetMonitorInfoW(monitor, &info);
    const int left = static_cast<int>(info.rcWork.left);
    const int top = static_cast<int>(info.rcWork.top);
    const int right = static_cast<int>(info.rcWork.right);
    const int bottom = static_cast<int>(info.rcWork.bottom);
    int x = cursor.x + offset;
    int y = cursor.y + offset;
    if (x + width > right) {
        x = cursor.x - width - offset;
    }
    if (y + height > bottom) {
        y = cursor.y - height - offset;
    }
    x = std::clamp(x, left, std::max(right - width, left));
    y = std::clamp(y, top, std::max(bottom - height, top));
    if (x != g.lastFollowPosition.x || y != g.lastFollowPosition.y) {
        SetWindowPos(hwnd, HWND_TOPMOST, x, y, width, height, SWP_NOACTIVATE | SWP_SHOWWINDOW);
        g.lastFollowPosition = {x, y};
    }
}

void ResetSignalTest() {
    if (g.testMode == TestMode::FpsView) {
        RestartFpsScenario();
        return;
    }
    g.mouseDown = false;
    g.pressedAt = 0;
    g.lastHoldTicks = 0;
    g.eventSequence = 0;
    g.machineSequence = 0;
    g.mouseMoveSequence = 0;
    g.frameEpoch = QpcNow();
    g.frameNumber = 1;
    g.motionPhase = 0;
    if (g.testMode == TestMode::Latency) {
        g.machineSequence = g.eventSequence;
    }
    InvalidateRect(g.hwnd, nullptr, FALSE);
    UpdateWindow(g.hwnd);
}

void ClearCadenceStatsLocked() {
    g.analyzer.frameTransitions = 0;
    g.analyzer.skippedFrameSlots = 0;
    g.analyzer.frameHistoryCount = 0;
    g.analyzer.frameHistoryWrite = 0;
    g.analyzer.rollingVisibleFps = 0.0;
    g.analyzer.rollingGeneratedFps = 0.0;
    g.analyzer.frameMedianMs = 0.0;
    g.analyzer.frameP95Ms = 0.0;
    g.analyzer.frameP99Ms = 0.0;
    g.analyzer.frameLongestMs = 0.0;
    g.analyzer.frameJitterMadMs = 0.0;
    g.analyzer.onePercentLowFps = 0.0;
    g.analyzer.recentStutters = 0;
    g.analyzer.nextTimingStatsAt = 0;
    g.analyzer.lastTransitionAt = 0;
}

void ResetAnalyzerStats() {
    EnterCriticalSection(&g.statsLock);
    const AnalyzerStats old = g.analyzer;
    g.analyzer = AnalyzerStats{};
    ++g.roiGeneration;
    g.analyzer.firstCorner = old.firstCorner;
    g.analyzer.secondCorner = old.secondCorner;
    g.analyzer.roi = old.roi;
    g.analyzer.hasFirstCorner = old.hasFirstCorner;
    g.analyzer.hasSecondCorner = old.hasSecondCorner;
    g.analyzer.roiValid = old.roiValid;
    LeaveCriticalSection(&g.statsLock);
    InvalidateRect(g.hwnd, nullptr, FALSE);
}

void ToggleSignalMode() {
    if (g.layoutLocked)
        return;
    switch (g.testMode) {
    case TestMode::Latency:
        g.testMode = TestMode::FrameRate;
        break;
    case TestMode::FrameRate:
        g.testMode = TestMode::AutoMotion;
        break;
    case TestMode::AutoMotion:
        g.testMode = TestMode::MouseMotion;
        break;
    case TestMode::MouseMotion:
        g.testMode = TestMode::FpsView;
        break;
    case TestMode::FpsView:
        ReleaseFpsCapture();
        g.testMode = TestMode::Latency;
        g.lastFollowPosition = {-10000, -10000};
        break;
    }
    g.frameEpoch = QpcNow();
    g.frameNumber = 1;
    g.motionPhase = 0;
    if (g.testMode == TestMode::Latency) {
        g.machineSequence = g.eventSequence;
    } else if (g.testMode == TestMode::MouseMotion) {
        g.machineSequence = static_cast<std::uint16_t>(g.mouseMoveSequence & kStreamSequenceMask);
    } else {
        g.machineSequence = 0;
    }
    if (IsFollowSignal() &&
        (g.testMode == TestMode::MouseMotion || g.testMode == TestMode::FpsView))
        SetPresentation(0);
    if (g.testMode == TestMode::FpsView)
        BeginFpsMode();
    LayoutControls(g.hwnd);
    RecreateFonts(g.hwnd);
    SyncControls();
    g.signalDirty = true;
    UpdateRenderRate();
    InvalidateRect(g.hwnd, nullptr, FALSE);
    UpdateWindow(g.hwnd);
}

void CycleTargetRate() {
    g.targetRateIndex = (g.targetRateIndex + 1) % kTargetRateCount;
    g.frameEpoch = QpcNow();
    g.frameNumber = 1;
    g.motionPhase = 0;
    if (g.testMode == TestMode::FpsView) {
        RestartFpsScenario();
    } else if (g.testMode == TestMode::FrameRate || g.testMode == TestMode::AutoMotion) {
        g.machineSequence = 0;
    }
    SyncControls();
    UpdateRenderRate();
    InvalidateRect(g.hwnd, nullptr, FALSE);
}

void SetRoiCorner(bool firstCorner) {
    if (g.recording.load())
        ToggleRecording();
    POINT cursor{};
    GetCursorPos(&cursor);
    EnterCriticalSection(&g.statsLock);
    ++g.roiGeneration;
    g.analyzer.pendingClick = false;
    g.analyzer.pendingMotion = false;
    if (firstCorner) {
        g.analyzer.hasSecondCorner = false;
        g.analyzer.roiValid = false;
        g.analyzer.firstCorner = cursor;
        g.analyzer.hasFirstCorner = true;
    } else {
        g.analyzer.secondCorner = cursor;
        g.analyzer.hasSecondCorner = true;
    }
    if (g.analyzer.hasFirstCorner && g.analyzer.hasSecondCorner) {
        g.analyzer.roi.left = std::min(g.analyzer.firstCorner.x, g.analyzer.secondCorner.x);
        g.analyzer.roi.top = std::min(g.analyzer.firstCorner.y, g.analyzer.secondCorner.y);
        g.analyzer.roi.right = std::max(g.analyzer.firstCorner.x, g.analyzer.secondCorner.x);
        g.analyzer.roi.bottom = std::max(g.analyzer.firstCorner.y, g.analyzer.secondCorner.y);
        g.analyzer.roiValid = g.analyzer.roi.right - g.analyzer.roi.left >= 64 &&
                              g.analyzer.roi.bottom - g.analyzer.roi.top >= 32;
        g.analyzer.decoderLocked = false;
        g.analyzer.decodedMode = -1;
        g.analyzer.latencyBaseline = false;
        g.analyzer.frameBaseline = false;
        ClearCadenceStatsLocked();
        g.analyzer.pendingMotion = false;
    }
    LeaveCriticalSection(&g.statsLock);
    InvalidateRect(g.hwnd, nullptr, FALSE);
}

bool PointIsInsideAnalyzer(POINT point) {
    HWND hit = WindowFromPoint(point);
    return hit == g.hwnd || (hit != nullptr && IsChild(g.hwnd, hit));
}

void HandleSignalMouse(WPARAM message, const MSLLHOOKSTRUCT *information) {
    if (g.testMode == TestMode::FpsView)
        return;
    if (message == WM_MOUSEMOVE && information != nullptr) {
        if (information->pt.x != g.signalCursorPosition.x ||
            information->pt.y != g.signalCursorPosition.y) {
            g.signalCursorPosition = information->pt;
            g.mouseMoveSequence = static_cast<std::uint16_t>(
                (static_cast<unsigned int>(g.mouseMoveSequence) + 1U) & kStreamSequenceMask);
            if (g.testMode == TestMode::MouseMotion) {
                g.machineSequence = g.mouseMoveSequence;
                g.signalDirty = true;
            }
        }
        return;
    }
    if (message == WM_LBUTTONDOWN && information) {
        HWND hit = WindowFromPoint(information->pt);
        if (hit != g.hwnd && IsChild(g.hwnd, hit))
            return;
    }
    if (message == WM_LBUTTONDOWN && !g.mouseDown) {
        g.mouseDown = true;
        g.pressedAt = QpcNow();
        g.eventSequence =
            static_cast<std::uint16_t>((static_cast<unsigned int>(g.eventSequence) + 1U) & 0x7FFFU);
        if (g.testMode == TestMode::Latency) {
            g.machineSequence = g.eventSequence;
        }
        InvalidateRect(g.hwnd, nullptr, FALSE);
        RefreshSignalRate();
        if (!g.tickPending.exchange(true))
            PostMessageW(g.hwnd, kMessageTick, 0, 0);
    } else if (message == WM_LBUTTONUP && g.mouseDown) {
        g.lastHoldTicks = std::max(QpcNow() - g.pressedAt, 0LL);
        g.mouseDown = false;
        InvalidateRect(g.hwnd, nullptr, FALSE);
        RefreshSignalRate();
        if (!g.tickPending.exchange(true))
            PostMessageW(g.hwnd, kMessageTick, 0, 0);
    }
}

void HandleAnalyzerMouse(WPARAM message, const MSLLHOOKSTRUCT *information) {
    if (!g.recording.load(std::memory_order_acquire))
        return;
    if ((message != WM_LBUTTONDOWN && message != WM_MOUSEMOVE) ||
        PointIsInsideAnalyzer(information->pt)) {
        return;
    }
    const LONGLONG now = QpcNow();
    bool updated = false;
    EnterCriticalSection(&g.statsLock);
    if (message == WM_LBUTTONDOWN && g.analyzer.roiValid && g.analyzer.decoderLocked &&
        now - g.analyzer.lastValidAt < g.qpcFrequency.QuadPart / 2 &&
        g.analyzer.decodedMode == ModeIndex(TestMode::Latency)) {
        g.analyzer.pendingClick = true;
        g.analyzer.clickAt = now;
        ++g.analyzer.clickNumber;
        updated = true;
    } else if (message == WM_MOUSEMOVE && g.analyzer.roiValid && g.analyzer.decoderLocked &&
               now - g.analyzer.lastValidAt < g.qpcFrequency.QuadPart / 2 &&
               g.analyzer.decodedMode == ModeIndex(TestMode::MouseMotion)) {
        const LONGLONG idleThreshold = g.qpcFrequency.QuadPart / 5;
        const bool beginsGesture =
            g.analyzer.lastLocalMoveAt == 0 || now - g.analyzer.lastLocalMoveAt > idleThreshold;
        g.analyzer.lastLocalMoveAt = now;
        ++g.analyzer.localMouseMoves;
        if (beginsGesture) {
            g.analyzer.pendingMotion = true;
            g.analyzer.motionStartedAt = now;
            ++g.analyzer.motionGestures;
            ClearCadenceStatsLocked();
            updated = true;
        }
    }
    LeaveCriticalSection(&g.statsLock);
    if (updated) {
        InvalidateRect(g.hwnd, nullptr, FALSE);
    }
}

LRESULT CALLBACK LowLevelMouseProc(int code, WPARAM message, LPARAM parameter) {
    if (code == HC_ACTION) {
        const auto *information = reinterpret_cast<const MSLLHOOKSTRUCT *>(parameter);
        if (IsAnalyzer()) {
            HandleAnalyzerMouse(message, information);
        } else {
            HandleSignalMouse(message, information);
        }
    }
    return CallNextHookEx(g.mouseHook, code, message, parameter);
}

void RecordLatencyLocked(double latencyMs) {
    g.analyzer.lastLatencyMs = latencyMs;
    g.analyzer.latencySamples[g.analyzer.latencyWriteIndex] = latencyMs;
    g.analyzer.latencyWriteIndex = (g.analyzer.latencyWriteIndex + 1) % kMaxLatencySamples;
    g.analyzer.latencyStored = std::min(g.analyzer.latencyStored + 1, kMaxLatencySamples);
    ++g.analyzer.latencyTotal;
    double sorted[kMaxLatencySamples]{};
    const int count = std::clamp(g.analyzer.latencyStored, 1, kMaxLatencySamples);
    for (int index = 0; index < count; ++index) {
        sorted[index] = g.analyzer.latencySamples[index];
    }
    std::sort(sorted, sorted + count);
    g.analyzer.latencyMedianMs =
        (count & 1) == 0 ? (sorted[count / 2 - 1] + sorted[count / 2]) / 2.0 : sorted[count / 2];
    const int p95Index = std::clamp(
        static_cast<int>(std::ceil(static_cast<double>(count) * 0.95)) - 1, 0, count - 1);
    g.analyzer.latencyP95Ms = sorted[p95Index];
}

void RecordMotionLatencyLocked(double latencyMs) {
    g.analyzer.lastMotionLatencyMs = latencyMs;
    g.analyzer.motionLatencySamples[g.analyzer.motionLatencyWriteIndex] = latencyMs;
    g.analyzer.motionLatencyWriteIndex =
        (g.analyzer.motionLatencyWriteIndex + 1) % kMaxLatencySamples;
    g.analyzer.motionLatencyStored =
        std::min(g.analyzer.motionLatencyStored + 1, kMaxLatencySamples);
    ++g.analyzer.motionLatencyTotal;
    double sorted[kMaxLatencySamples]{};
    const int count = std::clamp(g.analyzer.motionLatencyStored, 1, kMaxLatencySamples);
    for (int index = 0; index < count; ++index) {
        sorted[index] = g.analyzer.motionLatencySamples[index];
    }
    std::sort(sorted, sorted + count);
    g.analyzer.motionMedianMs =
        (count & 1) == 0 ? (sorted[count / 2 - 1] + sorted[count / 2]) / 2.0 : sorted[count / 2];
    const int p95Index = std::clamp(
        static_cast<int>(std::ceil(static_cast<double>(count) * 0.95)) - 1, 0, count - 1);
    g.analyzer.motionP95Ms = sorted[p95Index];
}

double SortedPercentile(const double *sorted, int count, double percentile) {
    if (count <= 0) {
        return 0.0;
    }
    const int index = std::clamp(
        static_cast<int>(std::ceil(static_cast<double>(count) * percentile)) - 1, 0, count - 1);
    return sorted[index];
}

void UpdateFrameTimingStatsLocked(LONGLONG now) {
    const LONGLONG cutoff = now - g.qpcFrequency.QuadPart * 5;
    LONGLONG times[kFrameHistorySize]{};
    int indices[kFrameHistorySize]{};
    int timeCount = 0;
    for (int n = 0; n < g.analyzer.frameHistoryCount; ++n) {
        const int index =
            (g.analyzer.frameHistoryWrite - g.analyzer.frameHistoryCount + n + kFrameHistorySize) %
            kFrameHistorySize;
        if (g.analyzer.frameTimes[index] >= cutoff) {
            indices[timeCount] = index;
            times[timeCount++] = g.analyzer.frameTimes[index];
        }
    }
    double intervals[kFrameHistorySize]{};
    int intervalCount = 0;
    for (int index = 1; index < timeCount; ++index) {
        if (g.analyzer.decodedMode == ModeIndex(TestMode::FpsView) &&
            !g.analyzer.intervalValid[indices[index]])
            continue;
        intervals[intervalCount++] = TicksToMilliseconds(times[index] - times[index - 1]);
    }
    if (intervalCount <= 0) {
        g.analyzer.frameMedianMs = 0.0;
        g.analyzer.frameP95Ms = 0.0;
        g.analyzer.frameP99Ms = 0.0;
        g.analyzer.frameLongestMs =
            g.analyzer.decodedMode != ModeIndex(TestMode::MouseMotion) &&
                    g.analyzer.lastTransitionAt > 0 && !g.analyzer.decodeGap
                ? TicksToMilliseconds(std::max(now - g.analyzer.lastTransitionAt, 0LL))
                : 0.0;
        g.analyzer.frameJitterMadMs = 0.0;
        g.analyzer.onePercentLowFps = 0.0;
        g.analyzer.recentStutters = 0;
        return;
    }
    std::sort(intervals, intervals + intervalCount);
    const double median =
        (intervalCount & 1) == 0
            ? (intervals[intervalCount / 2 - 1] + intervals[intervalCount / 2]) / 2.0
            : intervals[intervalCount / 2];
    const double p95 = SortedPercentile(intervals, intervalCount, 0.95);
    const double p99 = SortedPercentile(intervals, intervalCount, 0.99);
    double longest = intervals[intervalCount - 1];
    if (g.analyzer.decodedMode != ModeIndex(TestMode::MouseMotion) && timeCount > 0 &&
        !g.analyzer.decodeGap) {
        longest = std::max(longest, TicksToMilliseconds(std::max(now - times[timeCount - 1], 0LL)));
    }
    double deviations[kFrameHistorySize]{};
    int stutters = 0;
    const double stutterThreshold = std::max(median * 1.5, median + 4.0);
    for (int index = 0; index < intervalCount; ++index) {
        deviations[index] = std::fabs(intervals[index] - median);
        if (intervals[index] > stutterThreshold) {
            ++stutters;
        }
    }
    std::sort(deviations, deviations + intervalCount);
    g.analyzer.frameMedianMs = median;
    g.analyzer.frameP95Ms = p95;
    g.analyzer.frameP99Ms = p99;
    g.analyzer.frameLongestMs = longest;
    g.analyzer.frameJitterMadMs =
        (intervalCount & 1) == 0
            ? (deviations[intervalCount / 2 - 1] + deviations[intervalCount / 2]) / 2.0
            : deviations[intervalCount / 2];
    const int lowCount = std::max(1, static_cast<int>(std::ceil(intervalCount * 0.01)));
    double slowSum = 0.0;
    for (int i = intervalCount - lowCount; i < intervalCount; ++i)
        slowSum += intervals[i];
    g.analyzer.onePercentLowFps = slowSum > 0 ? 1000.0 * lowCount / slowSum : 0.0;
    g.analyzer.recentStutters = stutters;
}

void UpdateRollingFrameStatsLocked(LONGLONG now) {
    if (now < g.analyzer.nextTimingStatsAt)
        return;
    const LONGLONG cutoff = now - g.qpcFrequency.QuadPart;
    int visible = 0;
    int generated = 0;
    for (int index = 0; index < g.analyzer.frameHistoryCount; ++index) {
        if (g.analyzer.frameTimes[index] >= cutoff) {
            ++visible;
            generated += g.analyzer.frameDeltas[index];
        }
    }
    g.analyzer.rollingVisibleFps = static_cast<double>(visible);
    g.analyzer.rollingGeneratedFps = static_cast<double>(generated);
    if (now >= g.analyzer.nextTimingStatsAt) {
        UpdateFrameTimingStatsLocked(now);
        g.analyzer.nextTimingStatsAt = now + g.qpcFrequency.QuadPart / 10;
    }
}

void AddFrameTransitionLocked(const FrameTransition &transition) {
    const int index = g.analyzer.frameHistoryWrite;
    g.analyzer.frameTimes[index] = transition.capturedAt;
    g.analyzer.frameDeltas[index] = transition.delta;
    g.analyzer.frameHistoryWrite = (index + 1) % kFrameHistorySize;
    g.analyzer.frameHistoryCount = std::min(g.analyzer.frameHistoryCount + 1, kFrameHistorySize);
    ++g.analyzer.frameTransitions;
    g.analyzer.lastTransitionAt = transition.capturedAt;
    if (transition.delta > 1) {
        g.analyzer.skippedFrameSlots += static_cast<std::uint64_t>(transition.delta - 1);
    }
    UpdateRollingFrameStatsLocked(transition.capturedAt);
}

bool DecodePayload(std::uint32_t payload, bool inverted, DecoderResult *result) {
    const std::uint32_t recovered = inverted ? ((~payload) & 0xFFFFFFU) : payload;
    const auto data = static_cast<std::uint16_t>((recovered >> 8U) & 0xFFFFU);
    if (Crc8(data) != static_cast<std::uint8_t>(recovered & 0xFFU)) {
        return false;
    }
    const bool streamMode = (data & 0x8000U) != 0U;
    const auto codedSequence = static_cast<std::uint16_t>(data & 0x7FFFU);
    TestMode mode = TestMode::Latency;
    std::uint16_t sequence = 0;
    if (streamMode) {
        if (inverted) {
            return false;
        }
        const auto subtype = static_cast<unsigned int>(codedSequence >> 13U);
        if (subtype > 3U) {
            return false;
        }
        mode = static_cast<TestMode>(subtype + 1U);
        sequence = static_cast<std::uint16_t>(codedSequence & kStreamSequenceMask);
    } else {
        sequence = GrayToBinary(codedSequence);
        if ((((sequence & 1U) != 0U) != inverted)) {
            return false;
        }
    }
    result->valid = true;
    result->mode = mode;
    result->sequence = sequence;
    return true;
}

double SampleCellLuma(const std::uint8_t *pixels, const PixelDimensions &dimensions,
                      const CellPosition &cell) {
    const int cellLeft = dimensions.width * cell.column / kCodeColumns;
    const int cellRight = dimensions.width * (cell.column + 1) / kCodeColumns;
    const int cellTop = dimensions.height * cell.row / kCodeRows;
    const int cellBottom = dimensions.height * (cell.row + 1) / kCodeRows;
    const int insetX = std::max((cellRight - cellLeft) / 4, 1);
    const int insetY = std::max((cellBottom - cellTop) / 4, 1);
    const int left = std::min(cellLeft + insetX, cellRight - 1);
    const int right = std::max(cellRight - insetX, left + 1);
    const int top = std::min(cellTop + insetY, cellBottom - 1);
    const int bottom = std::max(cellBottom - insetY, top + 1);
    std::uint64_t sum = 0;
    std::uint64_t count = 0;
    for (int y = top; y < bottom; ++y) {
        for (int x = left; x < right; ++x) {
            const std::size_t offset =
                static_cast<std::size_t>(y) *
                    (dimensions.stride > 0 ? dimensions.stride : dimensions.width * 4) +
                static_cast<std::size_t>(x) * 4U;
            const std::uint8_t *pixel = pixels + offset;
            sum += (static_cast<unsigned int>(pixel[2]) * 77U +
                    static_cast<unsigned int>(pixel[1]) * 150U +
                    static_cast<unsigned int>(pixel[0]) * 29U) >>
                   8U;
            ++count;
        }
    }
    return count > 0 ? static_cast<double>(sum) / static_cast<double>(count) : 0.0;
}

DecoderResult DecodeMachineCode(const std::uint8_t *pixels, int width, int height, int stride = 0) {
    DecoderResult result{};
    if (width < 64 || height < 32) {
        return result;
    }
    double blackSum = 0.0;
    double whiteSum = 0.0;
    int blackCount = 0;
    int whiteCount = 0;
    double syncLuma[kCodeColumns]{};
    const PixelDimensions dimensions{width, height, stride};
    for (int column = 0; column < kCodeColumns; ++column) {
        syncLuma[column] = SampleCellLuma(pixels, dimensions, CellPosition{0, column});
        if (kSyncBits[column] != 0) {
            whiteSum += syncLuma[column];
            ++whiteCount;
        } else {
            blackSum += syncLuma[column];
            ++blackCount;
        }
    }
    const double white = whiteSum / static_cast<double>(whiteCount);
    const double black = blackSum / static_cast<double>(blackCount);
    const double contrast = white - black;
    if (contrast < 22.0) {
        return result;
    }
    const double threshold = (white + black) / 2.0;
    int syncErrors = 0;
    for (int column = 0; column < kCodeColumns; ++column) {
        if ((syncLuma[column] > threshold) != (kSyncBits[column] != 0)) {
            ++syncErrors;
        }
    }
    if (syncErrors > 1) {
        return result;
    }
    std::uint32_t payload = 0;
    for (int row = 1; row < kCodeRows; ++row) {
        for (int column = 0; column < kCodeColumns; ++column) {
            payload <<= 1U;
            if (SampleCellLuma(pixels, dimensions, CellPosition{row, column}) > threshold) {
                payload |= 1U;
            }
        }
    }
    DecoderResult decoded{};
    if (!DecodePayload(payload, false, &decoded) && !DecodePayload(payload, true, &decoded)) {
        return result;
    }
    decoded.confidence =
        std::clamp((contrast - 20.0) / 160.0, 0.0, 1.0) * (syncErrors == 0 ? 1.0 : 0.75);
    return decoded;
}

void QueueLog(const char *line);
#include "fps_analyzer.inl"

void ProcessDecodedResult(const DecoderResult &decoded, LONGLONG now, unsigned int generation) {
    char rows[3][256]{};
    int rowCount = 0;
    EnterCriticalSection(&g.statsLock);
    if (generation != g.roiGeneration) {
        LeaveCriticalSection(&g.statsLock);
        return;
    }
    const bool active = g.recording.load(std::memory_order_acquire);
    if (active)
        ++g.analyzer.capturePolls;
    if (g.analyzer.pendingClick && now - g.analyzer.clickAt > g.qpcFrequency.QuadPart * 2) {
        g.analyzer.pendingClick = false;
        ++g.analyzer.timedOutClicks;
    }
    if (g.analyzer.pendingMotion &&
        now - g.analyzer.motionStartedAt > g.qpcFrequency.QuadPart * 2) {
        g.analyzer.pendingMotion = false;
        ++g.analyzer.motionTimeouts;
    }
    if (!decoded.valid) {
        if (active) {
            ++g.analyzer.invalidPolls;
            g.analyzer.decodeGap = true;
            if (!g.analyzer.gapStartedAt)
                g.analyzer.gapStartedAt = now;
            UpdateRollingFrameStatsLocked(now);
        }
        if (g.analyzer.pendingClick && now - g.analyzer.clickAt > g.qpcFrequency.QuadPart * 2) {
            g.analyzer.pendingClick = false;
            ++g.analyzer.timedOutClicks;
        }
        if (g.analyzer.pendingMotion &&
            now - g.analyzer.motionStartedAt > g.qpcFrequency.QuadPart * 2) {
            g.analyzer.pendingMotion = false;
            ++g.analyzer.motionTimeouts;
        }
        LeaveCriticalSection(&g.statsLock);
        return;
    }
    if (active && g.analyzer.decodeGap && g.analyzer.gapStartedAt > 0) {
        const double gap = TicksToMilliseconds(now - g.analyzer.gapStartedAt);
        g.analyzer.decoderUnknownMs += gap;
        g.analyzer.runUnknownMs += gap;
        g.analyzer.gapStartedAt = 0;
    }
    g.analyzer.lastValidAt = now;
    const int decodedMode = ModeIndex(decoded.mode);
    if (decoded.mode != TestMode::FpsView)
        g.analyzer.decodeGap = false;
    if (!active) {
        g.analyzer.decoderLocked = true;
        g.analyzer.confidence = decoded.confidence;
        // Preserve the stopped run until a new run starts; preview may still refresh.
        if (g.analyzer.latencyTotal == 0 && g.analyzer.frameTransitions == 0 &&
            g.analyzer.motionLatencyTotal == 0)
            g.analyzer.decodedMode = decodedMode;
        LeaveCriticalSection(&g.statsLock);
        return;
    }
    const bool modeChanged = !g.analyzer.decoderLocked || g.analyzer.decodedMode != decodedMode;
    if (modeChanged && g.analyzer.decoderLocked &&
        g.analyzer.decodedMode == ModeIndex(TestMode::FpsView) && g.analyzer.runFrames > 0) {
        g.recording.store(false);
        LeaveCriticalSection(&g.statsLock);
        PostMessageW(g.hwnd, kMessageAutoStop, 0, 0);
        return;
    }
    if (modeChanged) {
        g.analyzer.decodedMode = decodedMode;
        g.analyzer.latencyBaseline = false;
        g.analyzer.frameBaseline = false;
        g.analyzer.pendingClick = false;
        g.analyzer.pendingMotion = false;
        if (decoded.mode == TestMode::MouseMotion) {
            g.analyzer.lastLocalMoveAt = 0;
        }
        ClearCadenceStatsLocked();
    }
    g.analyzer.decoderLocked = true;
    g.analyzer.confidence = decoded.confidence;

    if (decoded.mode == TestMode::FpsView) {
        char row[512]{};
        const bool logged = ProcessFpsDecodedLocked(decoded, now, row, sizeof(row));
        LeaveCriticalSection(&g.statsLock);
        if (logged)
            QueueLog(row);
        return;
    }
    if (decoded.mode == TestMode::Latency) {
        if (!g.analyzer.latencyBaseline) {
            g.analyzer.latencyBaseline = true;
            g.analyzer.lastLatencySequence = decoded.sequence;
        } else if (decoded.sequence != g.analyzer.lastLatencySequence) {
            g.analyzer.lastLatencySequence = decoded.sequence;
            if (g.analyzer.pendingClick && now >= g.analyzer.clickAt) {
                const double latency = TicksToMilliseconds(std::max(now - g.analyzer.clickAt, 0LL));
                RecordLatencyLocked(latency);
                g.analyzer.pendingClick = false;
                if (rowCount < 3) {
                    std::snprintf(rows[rowCount], 256, "latency,%.3f,%u,1,%.3f,%.3f\n",
                                  TicksToMilliseconds(now - g.processEpoch),
                                  static_cast<unsigned int>(decoded.sequence), latency,
                                  decoded.confidence);
                    ++rowCount;
                }
            }
        }
    } else {
        if (!g.analyzer.frameBaseline) {
            g.analyzer.frameBaseline = true;
            g.analyzer.lastFrameSequence = decoded.sequence;
        } else if (decoded.sequence != g.analyzer.lastFrameSequence) {
            const unsigned int modulus = static_cast<unsigned int>(SequenceMask(decoded.mode)) + 1U;
            const auto delta = static_cast<std::uint16_t>(
                (static_cast<unsigned int>(decoded.sequence) + modulus -
                 static_cast<unsigned int>(g.analyzer.lastFrameSequence)) &
                SequenceMask(decoded.mode));
            g.analyzer.lastFrameSequence = decoded.sequence;
            if (delta > 0 && delta <= 1000) {
                if (decoded.mode == TestMode::MouseMotion && g.analyzer.pendingMotion &&
                    now >= g.analyzer.motionStartedAt) {
                    const double motionLatency =
                        TicksToMilliseconds(std::max(now - g.analyzer.motionStartedAt, 0LL));
                    RecordMotionLatencyLocked(motionLatency);
                    g.analyzer.pendingMotion = false;
                    if (rowCount < 3) {
                        std::snprintf(rows[rowCount], 256, "motion_latency,%.3f,%u,%u,%.3f,%.3f\n",
                                      TicksToMilliseconds(now - g.processEpoch),
                                      static_cast<unsigned int>(decoded.sequence),
                                      static_cast<unsigned int>(delta), motionLatency,
                                      decoded.confidence);
                        ++rowCount;
                    }
                }
                AddFrameTransitionLocked(FrameTransition{now, delta});
                if (rowCount < 3) {
                    const char *rowType =
                        decoded.mode == TestMode::FrameRate
                            ? "frame"
                            : (decoded.mode == TestMode::AutoMotion ? "auto_motion"
                                                                    : "mouse_motion");
                    std::snprintf(rows[rowCount], 256, "%s,%.3f,%u,%u,%.3f,%.3f\n", rowType,
                                  TicksToMilliseconds(now - g.processEpoch),
                                  static_cast<unsigned int>(decoded.sequence),
                                  static_cast<unsigned int>(delta), g.analyzer.rollingVisibleFps,
                                  decoded.confidence);
                    ++rowCount;
                }
            } else {
                g.analyzer.frameBaseline = false;
            }
        } else {
            UpdateRollingFrameStatsLocked(now);
        }
    }
    if (g.analyzer.pendingClick && now - g.analyzer.clickAt > g.qpcFrequency.QuadPart * 2) {
        g.analyzer.pendingClick = false;
        ++g.analyzer.timedOutClicks;
    }
    if (g.analyzer.pendingMotion &&
        now - g.analyzer.motionStartedAt > g.qpcFrequency.QuadPart * 2) {
        g.analyzer.pendingMotion = false;
        ++g.analyzer.motionTimeouts;
    }
    LeaveCriticalSection(&g.statsLock);
    for (int i = 0; i < rowCount; ++i)
        QueueLog(rows[i]);
}

void PrepareResultsPath() {
    wchar_t modulePath[MAX_PATH]{};
    GetModuleFileNameW(nullptr, modulePath, MAX_PATH);
    wchar_t *separator = std::wcsrchr(modulePath, L'\\');
    if (separator != nullptr) {
        *(separator + 1) = L'\0';
        if (std::wcslen(modulePath) + 40 < MAX_PATH)
            std::swprintf(g.resultsPath, MAX_PATH, L"%lsCloudLatencyFPS_%ls_v0.8.csv", modulePath,
                          IsAnalyzer() ? L"results" : L"source");
        std::swprintf(g.comparisonPath, MAX_PATH, L"%lsCloudLatencyFPS_comparison.html",
                      modulePath);
    }
}

void QueueLog(const char *line) {
    EnterCriticalSection(&g.logLock);
    const unsigned int write = g.logWrite.load(std::memory_order_relaxed),
                       read = g.logRead.load(std::memory_order_acquire);
    if (write - read >= kLogQueueSize) {
        g.logDropped.fetch_add(1, std::memory_order_relaxed);
        LeaveCriticalSection(&g.logLock);
        return;
    }
    const int commas = static_cast<int>(std::count(line, line + std::strlen(line), ','));
    if (commas == 5) {
        std::snprintf(g.logQueue[write % kLogQueueSize], 512, "%.*s,%lld,%d,,,,,,,,,,,\n",
                      static_cast<int>(std::strlen(line)) - 1, line, g.runId.load(),
                      g.bitrateMbps.load());
    } else
        std::snprintf(g.logQueue[write % kLogQueueSize], 512, "%s", line);
    g.logWrite.store(write + 1, std::memory_order_release);
    LeaveCriticalSection(&g.logLock);
    if (write == read)
        SetEvent(g.logEvent);
}
DWORD WINAPI LogThreadProc(void *) {
    std::FILE *file = _wfopen(g.resultsPath, L"a+");
    char buffer[65536];
    if (file) {
        std::setvbuf(file, buffer, _IOFBF, sizeof(buffer));
        std::fseek(file, 0, SEEK_END);
        if (std::ftell(file) == 0)
            std::fputs(
                "type,process_time_ms,sequence,delta,measurement,confidence,run_id,bitrate_label_"
                "mbps,scene,control,phase,input_sequence,yaw_deg,pitch_deg,source_interval_ms,"
                "source_paint_ms,tracking_latency_ms,tracking_correlation,interval_valid,pressure_level\n",
                file);
    } else
        g.logError.store(true);
    LONGLONG nextFlush = QpcNow() + g.qpcFrequency.QuadPart;
    for (;;) {
        unsigned int read = g.logRead.load(std::memory_order_relaxed);
        const unsigned int write = g.logWrite.load(std::memory_order_acquire);
        while (read != write) {
            if (file && std::fputs(g.logQueue[read % kLogQueueSize], file) == EOF)
                g.logError.store(true);
            ++read;
            g.logRead.store(read, std::memory_order_release);
        }
        if (file &&
            (QpcNow() >= nextFlush || (IsAnalyzer() && !g.recording.load()) || g.logQuit.load())) {
            if (std::fflush(file) != 0)
                g.logError.store(true);
            nextFlush = QpcNow() + g.qpcFrequency.QuadPart;
        }
        if (g.logQuit.load() && g.logRead.load() == g.logWrite.load())
            break;
        WaitForSingleObject(g.logEvent, 100);
    }
    if (file && std::fclose(file) != 0)
        g.logError.store(true);
    return 0;
}
HANDLE MakeTimer() {
    HANDLE timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
                                          TIMER_ALL_ACCESS);
    if (!timer)
        timer = CreateWaitableTimerW(nullptr, FALSE, nullptr);
    return timer;
}
bool WaitUntil(HANDLE timer, LONGLONG deadline) {
    const LONGLONG remaining = deadline - QpcNow();
    if (remaining <= 0)
        return !g.quit.load();
    if (timer) {
        LARGE_INTEGER due{};
        due.QuadPart = -std::max(remaining * 10000000LL / g.qpcFrequency.QuadPart, 1LL);
        if (SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE)) {
            const HANDLE waits[] = {g.stopEvent, timer};
            return WaitForMultipleObjects(2, waits, FALSE, INFINITE) != WAIT_OBJECT_0;
        }
    }
    const DWORD ms = static_cast<DWORD>(std::max(remaining * 1000 / g.qpcFrequency.QuadPart, 1LL));
    return WaitForSingleObject(g.stopEvent, ms) != WAIT_OBJECT_0;
}
DWORD WINAPI CaptureThreadProc(void *) {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
    HDC screenDc = GetDC(nullptr), memoryDc = CreateCompatibleDC(screenDc);
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = kPreviewWidth;
    bi.bmiHeader.biHeight = -kPreviewHeight;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    std::uint8_t *pixels = nullptr;
    HBITMAP bitmap = CreateDIBSection(screenDc, &bi, DIB_RGB_COLORS,
                                      reinterpret_cast<void **>(&pixels), nullptr, 0);
    HGDIOBJ previous = nullptr;
    if (memoryDc && bitmap) {
        previous = SelectObject(memoryDc, bitmap);
        SetStretchBltMode(memoryDc, COLORONCOLOR);
    }
    HANDLE timer = MakeTimer();
    LONGLONG next = QpcNow(), nextUi = 0;
    while (!g.quit.load(std::memory_order_acquire)) {
        const LONGLONG started = QpcNow();
        RECT roi{};
        bool valid = false;
        unsigned int generation;
        EnterCriticalSection(&g.statsLock);
        roi = g.analyzer.roi;
        valid = g.analyzer.roiValid;
        generation = g.roiGeneration;
        LeaveCriticalSection(&g.statsLock);
        bool captured = false, obscured = false;
        const bool bundle = (roi.right - roi.left) > (roi.bottom - roi.top) * 3;
        const int sampleWidth = bundle ? kPreviewWidth : 128;
        if (valid) {
            RECT own{}, overlap{};
            if (!IsIconic(g.hwnd) && GetWindowRect(g.hwnd, &own))
                obscured = IntersectRect(&overlap, &roi, &own) != FALSE;
        }
        if (valid && !obscured && memoryDc && bitmap && pixels) {
            captured = StretchBlt(memoryDc, 0, 0, sampleWidth, kPreviewHeight, screenDc, roi.left,
                                  roi.top, roi.right - roi.left, roi.bottom - roi.top,
                                  SRCCOPY | CAPTUREBLT) != FALSE;
            // GDI can batch DIB writes; flush before reading its memory and timestamping.
            if (captured)
                captured = GdiFlush() != FALSE;
        }
        const LONGLONG capturedAt = QpcNow();
        DecoderResult result{};
        if (captured)
            result =
                bundle ? DecodeFpsBundle(pixels, kPreviewWidth, kPreviewHeight)
                       : DecodeMachineCode(pixels, sampleWidth, kPreviewHeight, kPreviewWidth * 4);
        if (valid)
            ProcessDecodedResult(result, capturedAt, generation);
        if (started >= nextUi) {
            EnterCriticalSection(&g.statsLock);
            if (generation == g.roiGeneration) {
                g.analyzer.captureMs = TicksToMilliseconds(capturedAt - started);
                g.analyzer.captureFailed = valid && !captured && !obscured;
                g.analyzer.roiObscured = obscured;
                g.hasPreview = captured;
                g.previewSampleWidth = sampleWidth;
                if (captured)
                    std::copy_n(reinterpret_cast<std::uint32_t *>(pixels),
                                kPreviewWidth * kPreviewHeight, g.preview);
            }
            LeaveCriticalSection(&g.statsLock);
            if (!IsIconic(g.hwnd) && !g.tickPending.exchange(true))
                if (!PostMessageW(g.hwnd, kMessageTick, 0, 0))
                    g.tickPending.store(false);
            nextUi = started + g.qpcFrequency.QuadPart / 10;
        }
        const LONGLONG period =
            g.qpcFrequency.QuadPart / ((valid && g.recording.load()) ? 500 : 10);
        next += std::max(period, 1LL);
        const LONGLONG now = QpcNow();
        if (next <= now)
            next = now + std::max(period, 1LL);
        if (!WaitUntil(timer, next))
            break;
    }
    if (previous)
        SelectObject(memoryDc, previous);
    if (bitmap)
        DeleteObject(bitmap);
    if (memoryDc)
        DeleteDC(memoryDc);
    if (screenDc)
        ReleaseDC(nullptr, screenDc);
    if (timer)
        CloseHandle(timer);
    return 0;
}
void OnSignalRenderTick(HWND hwnd) {
    g.tickPending.store(false, std::memory_order_release);
    if (g.minimized)
        return;
    const LONGLONG now = QpcNow();
    if (IsFollowSignal() && g.testMode != TestMode::MouseMotion)
        UpdateFollowPosition(hwnd);
    if (now >= g.nextRefreshCheck) {
        RefreshDisplayInfo(hwnd, false);
        g.nextRefreshCheck = now + g.qpcFrequency.QuadPart * 2;
    }
    bool redraw = g.signalDirty;
    if (g.testMode == TestMode::FpsView) {
        gpu::Move(now);
        if (!g.fpsManual)
            g.fpsPose = fps::Automatic(TicksToMilliseconds(now - g.frameEpoch) / 1000.0);
        else
            g.fpsPose = {g.fpsYaw, g.fpsPitch, 5};
        redraw = true;
    } else if (g.testMode == TestMode::FrameRate || g.testMode == TestMode::AutoMotion) {
        const int upper = EffectiveFrameUpper();
        const double elapsed = static_cast<double>(std::max(now - g.frameEpoch, 0LL)) /
                               static_cast<double>(g.qpcFrequency.QuadPart);
        const auto slots = static_cast<std::uint64_t>(std::floor(elapsed * upper));
        const auto sequence = static_cast<std::uint16_t>(slots & kStreamSequenceMask);
        redraw = redraw || sequence != g.machineSequence;
        g.machineSequence = sequence;
        g.frameNumber = static_cast<int>(slots % static_cast<std::uint64_t>(upper)) + 1;
        g.motionPhase =
            static_cast<int>(slots % static_cast<std::uint64_t>(std::max(upper * 2 - 2, 2)));
    } else if (g.testMode == TestMode::Latency && g.mouseDown)
        redraw = true;
    if (redraw) {
        RECT client{};
        GetClientRect(hwnd, &client);
        const auto l = SignalLayout(client);
        RECT dirty = g.testMode == TestMode::FpsView ? client : WinRect(l.content);
        InvalidateRect(hwnd, &dirty, FALSE);
        UpdateWindow(hwnd);
        g.signalDirty = false;
    }
}
DWORD WINAPI SignalRenderThreadProc(void *) {
    HANDLE timer = MakeTimer();
    LONGLONG next = QpcNow();
    while (!g.quit.load(std::memory_order_acquire)) {
        const double rate = std::clamp(g.renderRateHz.load(std::memory_order_relaxed), 2.0, 500.0);
        const LONGLONG period =
            std::max(static_cast<LONGLONG>(g.qpcFrequency.QuadPart / rate), 1LL);
        next += period;
        const LONGLONG now = QpcNow();
        if (next <= now || next > now + period * 2)
            next = now + period;
        if (!WaitUntil(timer, next))
            break;
        if (!g.quit.load() && !g.tickPending.exchange(true))
            if (!PostMessageW(g.hwnd, kMessageTick, 0, 0))
                g.tickPending.store(false);
    }
    if (timer)
        CloseHandle(timer);
    return 0;
}

bool RegisterModeHotkeys(HWND hwnd) {
    bool ok = true;
    if (IsAnalyzer()) {
        ok = RegisterHotKey(hwnd, kHotkeyRoiTopLeft, MOD_NOREPEAT, VK_F2) != FALSE && ok;
        ok = RegisterHotKey(hwnd, kHotkeyRoiBottomRight, MOD_NOREPEAT, VK_F3) != FALSE && ok;
    } else {
        ok = RegisterHotKey(hwnd, kHotkeyRate, MOD_NOREPEAT, VK_F6) != FALSE && ok;
        ok = RegisterHotKey(hwnd, kHotkeyMode, MOD_NOREPEAT, VK_F8) != FALSE && ok;
        ok = RegisterHotKey(hwnd, kHotkeyReset, MOD_NOREPEAT, VK_F9) != FALSE && ok;
        ok = RegisterHotKey(hwnd, kHotkeyFullscreen, MOD_NOREPEAT, VK_F11) != FALSE && ok;
        ok = RegisterHotKey(hwnd, kHotkeyFpsControl, MOD_NOREPEAT, VK_F4) != FALSE && ok;
        ok = RegisterHotKey(hwnd, kHotkeyFpsScene, MOD_NOREPEAT, VK_F5) != FALSE && ok;
        ok = RegisterHotKey(hwnd, kHotkeyFpsRestart, MOD_NOREPEAT, VK_F7) != FALSE && ok;
        ok = RegisterHotKey(hwnd, kHotkeyFpsPressure, MOD_NOREPEAT, VK_F10) != FALSE && ok;
    }
    if (!IsAnalyzer())
        ok = RegisterHotKey(hwnd, kHotkeyExit, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, VK_F12) !=
                 FALSE &&
             ok;
    return ok;
}

void UnregisterModeHotkeys(HWND hwnd) {
    for (int id = kHotkeyRate; id <= kHotkeyFpsPressure; ++id) {
        UnregisterHotKey(hwnd, id);
    }
}

#include "ui_controls.inl"
#include "fps_input.inl"

LRESULT CALLBACK AppWindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE:
        CreateControls(hwnd);
        return 0;
    case WM_INPUT:
        HandleRawMouse(lParam);
        break; // DefWindowProc releases foreground Raw Input resources.
    case WM_ACTIVATE:
        if (LOWORD(wParam) == WA_INACTIVE && !IsAnalyzer())
            ReleaseFpsCapture();
        break;
    case WM_SETCURSOR:
        if (g.fpsCaptured && LOWORD(lParam) == HTCLIENT) {
            SetCursor(nullptr);
            return TRUE;
        }
        break;
    case WM_LBUTTONDOWN:
        if (!IsAnalyzer() && g.testMode == TestMode::FpsView && g.fpsManual)
            CaptureFpsMouse();
        break;
    case WM_COMMAND: {
        const int id = LOWORD(wParam), notice = HIWORD(wParam);
        if (id == kFpsRestart) {
            RestartFpsScenario();
            return 0;
        }
        if (id == kComparison) {
            WriteComparison();
            return 0;
        }
        if (id == kAnalyzerReset) {
            if (g.recording.load())
                ToggleRecording();
            ResetAnalyzerStats();
            return 0;
        }
        if (id == kTopmost) {
            g.analyzerTopmost = SendMessageW(g.topmostButton, BM_GETCHECK, 0, 0) == BST_CHECKED;
            SetWindowPos(hwnd, g.analyzerTopmost ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            return 0;
        }
        if (id == kRecord) {
            ToggleRecording();
            return 0;
        }
        if (id == kRoiReset) {
            ResetRoi();
            return 0;
        }
        if (id == kDetails) {
            g.showDetails = !g.showDetails;
            SetWindowTextW(g.detailsButton, g.showDetails ? L"收起统计" : L"更多统计");
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        if (id == kOpenCsv) {
            ShellExecuteW(hwnd, L"open", g.resultsPath, nullptr, nullptr, SW_SHOWNORMAL);
            return 0;
        }
        if (id == kFullscreenExit) {
            SetPresentation(0);
            return 0;
        }
        if (id == kLockLayout) {
            g.layoutLocked = SendMessageW(g.lockButton, BM_GETCHECK, 0, 0) == BST_CHECKED;
            SyncControls();
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        if (notice == CBN_SELCHANGE) {
            if (id == kModeCombo) {
                const int selected =
                    static_cast<int>(SendMessageW(g.modeCombo, CB_GETCURSEL, 0, 0));
                while (!g.layoutLocked && ModeIndex(g.testMode) != selected && selected >= 0 &&
                       selected < 5)
                    ToggleSignalMode();
            } else if (id == kRateCombo) {
                const int selected =
                    static_cast<int>(SendMessageW(g.rateCombo, CB_GETCURSEL, 0, 0));
                if (selected >= 0) {
                    g.targetRateIndex = (selected + kTargetRateCount - 1) % kTargetRateCount;
                    CycleTargetRate();
                }
            } else if (id == kFpsControl) {
                const bool manual = SendMessageW(g.fpsControlCombo, CB_GETCURSEL, 0, 0) == 1;
                if (manual != g.fpsManual)
                    CycleFpsControl();
            } else if (id == kFpsPressure) {
                g.fpsPressure=std::clamp(static_cast<int>(SendMessageW(g.fpsPressureCombo,CB_GETCURSEL,0,0)),0,2);
                HDC dc=GetDC(hwnd);EnsureTexture(dc);ReleaseDC(hwnd,dc);
                RestartFpsScenario();
                SyncControls();
            } else if (id == kFpsScene) {
                g.fpsScene = static_cast<int>(SendMessageW(g.fpsSceneCombo, CB_GETCURSEL, 0, 0));
                HDC dc = GetDC(hwnd);
                EnsureTexture(dc);
                ReleaseDC(hwnd, dc);
                RestartFpsScenario();
                SyncControls();
            } else if (id == kDisplayCombo)
                SetPresentation(static_cast<int>(SendMessageW(g.displayCombo, CB_GETCURSEL, 0, 0)));
            return 0;
        }
        break;
    }
    case WM_HSCROLL:
        if (reinterpret_cast<HWND>(lParam) == g.ratioSlider && !g.layoutLocked) {
            g.codeRatio =
                std::clamp(static_cast<int>(SendMessageW(g.ratioSlider, TBM_GETPOS, 0, 0)), 20, 90);
            RecreateFonts(hwnd);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    case WM_GETMINMAXINFO: {
        auto *limits = reinterpret_cast<MINMAXINFO *>(lParam);
        if (!IsFollowSignal() && !g.fullscreen) {
            limits->ptMinTrackSize = {Scale(IsAnalyzer() ? 710 : 560),
                                      Scale(IsAnalyzer() ? 700 : 380)};
            if (g.layoutLocked) {
                RECT r{};
                GetWindowRect(hwnd, &r);
                limits->ptMinTrackSize = {r.right - r.left, r.bottom - r.top};
                limits->ptMaxTrackSize = limits->ptMinTrackSize;
            }
        }
        return 0;
    }
    case WM_WINDOWPOSCHANGING:
        if (g.layoutLocked && !IsAnalyzer() && !g.fullscreen) {
            auto *position = reinterpret_cast<WINDOWPOS *>(lParam);
            position->flags |= SWP_NOMOVE | SWP_NOSIZE;
        }
        break;
    case WM_SIZE:
        g.minimized = wParam == SIZE_MINIMIZED;
        if (g.fpsCaptured)
            ReleaseFpsCapture();
        if (!g.minimized) {
            if (g.hwnd)
                RecreateFonts(hwnd);
            LayoutControls(hwnd);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        if (!IsAnalyzer())
            RefreshSignalRate();
        return 0;
    case WM_DPICHANGED: {
        g.dpi = HIWORD(wParam);
        const RECT *suggested = reinterpret_cast<const RECT *>(lParam);
        SetWindowPos(hwnd, nullptr, suggested->left, suggested->top,
                     suggested->right - suggested->left, suggested->bottom - suggested->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        RecreateFonts(hwnd);
        return 0;
    }
    case WM_DISPLAYCHANGE:
        if (!IsAnalyzer()) {
            RefreshDisplayInfo(hwnd, true);
        }
        return 0;
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN: {
        HDC dc = reinterpret_cast<HDC>(wParam);
        SetTextColor(dc, kText);
        SetBkColor(dc, kBackground);
        SetDCBrushColor(dc, kBackground);
        return reinterpret_cast<LRESULT>(GetStockObject(DC_BRUSH));
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        DispatchPaint(hwnd);
        return 0;
    case WM_HOTKEY:
        switch (static_cast<int>(wParam)) {
        case kHotkeyFpsControl:
            CycleFpsControl();
            break;
        case kHotkeyFpsScene:
            CycleFpsScene();
            break;
        case kHotkeyFpsPressure:
            CycleFpsPressure();
            break;
        case kHotkeyFpsRestart:
            RestartFpsScenario();
            break;
        case kHotkeyFullscreen:
            ReleaseFpsCapture();
            SetPresentation(g.fullscreen ? 0 : 2);
            break;
        case kHotkeyRate:
            CycleTargetRate();
            break;
        case kHotkeyMode:
            ToggleSignalMode();
            break;
        case kHotkeyReset:
            ResetSignalTest();
            break;
        case kHotkeyExit:
            DestroyWindow(hwnd);
            break;
        case kHotkeyRoiTopLeft:
            SetRoiCorner(true);
            break;
        case kHotkeyRoiBottomRight:
            SetRoiCorner(false);
            break;
        default:
            break;
        }
        return 0;
    case WM_KEYDOWN:
        if (!IsAnalyzer() && !IsFollowSignal() && wParam == VK_ESCAPE) {
            if (g.fpsCaptured)
                ReleaseFpsCapture();
            else if (g.fullscreen)
                SetPresentation(0);
            return 0;
        }
        if (IsAnalyzer() && (wParam == L'R' || wParam == VK_DELETE)) {
            ResetAnalyzerStats();
            return 0;
        }
        break;
    case kMessageAutoStop:
        g.runEndedAt = QpcNow();
        SaveFpsRun();
        SyncControls();
        SetEvent(g.logEvent);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case kMessageTick:
        if (IsAnalyzer()) {
            g.tickPending.store(false, std::memory_order_release);
            InvalidateRect(hwnd, nullptr, FALSE);
        } else {
            OnSignalRenderTick(hwnd);
        }
        return 0;
    case WM_NCHITTEST:
        if (IsFollowSignal()) {
            return HTTRANSPARENT;
        }
        break;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        if (IsAnalyzer() && g.recording.load())
            ToggleRecording();
        SavePreferences();
        ReleaseFpsCapture();
        gpu::Release();
        ReleaseTexture();
        g.quit.store(true, std::memory_order_release);
        SetEvent(g.stopEvent);
        if (g.mouseHook != nullptr) {
            UnhookWindowsHookEx(g.mouseHook);
            g.mouseHook = nullptr;
        }
        UnregisterModeHotkeys(hwnd);
        DeleteFonts();
        ReleaseBackBuffer();
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

LRESULT CALLBACK ChooserWindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE: {
        HFONT font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        HWND label = CreateWindowExW(0, L"STATIC", L"请选择这次启动的运行方式：",
                                     WS_CHILD | WS_VISIBLE | SS_CENTER, 20, 20, 420, 32, hwnd,
                                     nullptr, g.instance, nullptr);
        SendMessageW(label, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        const wchar_t *labels[] = {L"固定显示窗口（云电脑）", L"跟随鼠标信号（云电脑）",
                                   L"本地自动识别器"};
        const int ids[] = {kChooserFixed, kChooserFollow, kChooserAnalyzer};
        for (int index = 0; index < 3; ++index) {
            HWND button = CreateWindowExW(
                0, L"BUTTON", labels[index], WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 55,
                65 + index * 58, 350, 42, hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(ids[index])), g.instance, nullptr);
            SendMessageW(button, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        }
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == kChooserFixed) {
            gChooserResult = LaunchMode::FixedSignal;
        } else if (LOWORD(wParam) == kChooserFollow) {
            gChooserResult = LaunchMode::FollowSignal;
        } else if (LOWORD(wParam) == kChooserAnalyzer) {
            gChooserResult = LaunchMode::Analyzer;
        }
        if (gChooserResult != LaunchMode::None) {
            DestroyWindow(hwnd);
        }
        return 0;
    case WM_CLOSE:
        gChooserResult = LaunchMode::None;
        DestroyWindow(hwnd);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

LaunchMode RunStartupChooser() {
    WNDCLASSEXW chooserClass{};
    chooserClass.cbSize = sizeof(chooserClass);
    chooserClass.lpfnWndProc = ChooserWindowProc;
    chooserClass.hInstance = g.instance;
    chooserClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    chooserClass.hbrBackground = reinterpret_cast<HBRUSH>(static_cast<INT_PTR>(COLOR_WINDOW + 1));
    chooserClass.lpszClassName = kChooserClass;
    RegisterClassExW(&chooserClass);
    RECT workArea{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &workArea, 0);
    constexpr int width = 480;
    constexpr int height = 300;
    const int x = static_cast<int>(workArea.left) +
                  std::max(static_cast<int>(workArea.right - workArea.left) - width, 0) / 2;
    const int y = static_cast<int>(workArea.top) +
                  std::max(static_cast<int>(workArea.bottom - workArea.top) - height, 0) / 2;
    HWND chooser = CreateWindowExW(WS_EX_TOPMOST, kChooserClass, L"云电脑延迟与帧率测试器",
                                   WS_CAPTION | WS_SYSMENU, x, y, width, height, nullptr, nullptr,
                                   g.instance, nullptr);
    if (chooser == nullptr) {
        return LaunchMode::None;
    }
    ShowWindow(chooser, SW_SHOW);
    UpdateWindow(chooser);
    MSG message{};
    while (IsWindow(chooser) && GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return gChooserResult;
}

bool RegisterAppWindowClass() {
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = AppWindowProc;
    windowClass.hInstance = g.instance;
    windowClass.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = nullptr;
    windowClass.lpszClassName = kAppClass;
    windowClass.hIconSm = LoadIconW(nullptr, IDI_APPLICATION);
    return RegisterClassExW(&windowClass) != 0;
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    INITCOMMONCONTROLSEX controls{sizeof(INITCOMMONCONTROLSEX), ICC_BAR_CLASSES};
    InitCommonControlsEx(&controls);
    g.instance = instance;
    g.normalPlacement.length = sizeof(WINDOWPLACEMENT);
    g.dpi = GetDpiForSystem();
    LoadPreferences();
    g.stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    g.logEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!g.stopEvent || !g.logEvent) {
        MessageBoxW(nullptr, L"无法创建线程同步事件。", kAppTitle, MB_OK | MB_ICONERROR);
        if (g.stopEvent)
            CloseHandle(g.stopEvent);
        if (g.logEvent)
            CloseHandle(g.logEvent);
        return 1;
    }
    GetCursorPos(&g.signalCursorPosition);
    QueryPerformanceFrequency(&g.qpcFrequency);
    if (g.qpcFrequency.QuadPart <= 0) {
        MessageBoxW(nullptr, L"QueryPerformanceCounter 不可用。", kAppTitle, MB_OK | MB_ICONERROR);
        return 1;
    }
    g.processEpoch = QpcNow();
    InitializeCriticalSection(&g.statsLock);
    InitializeCriticalSection(&g.logLock);
    g.runId.store(g.processEpoch);
    g.statsLockInitialized = true;
    g.launchMode = RunStartupChooser();
    if (g.launchMode == LaunchMode::None) {
        DeleteCriticalSection(&g.statsLock);
        return 0;
    }
    if (!RegisterAppWindowClass()) {
        MessageBoxW(nullptr, L"窗口类注册失败。", kAppTitle, MB_OK | MB_ICONERROR);
        DeleteCriticalSection(&g.statsLock);
        return 1;
    }
    RECT workArea{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &workArea, 0);
    const int initialWidth =
        Scale(IsAnalyzer() ? 820 : (IsFollowSignal() ? kFollowInitialWidth : kFixedInitialWidth));
    const int initialHeight =
        Scale(IsAnalyzer() ? 760 : (IsFollowSignal() ? kFollowInitialHeight : kFixedInitialHeight));
    const int initialX =
        static_cast<int>(workArea.left) +
        std::max(static_cast<int>(workArea.right - workArea.left) - initialWidth, 0) / 2;
    const int initialY =
        static_cast<int>(workArea.top) +
        std::max(static_cast<int>(workArea.bottom - workArea.top) - initialHeight, 0) / 2;
    const DWORD style = IsFollowSignal() ? kFollowStyle : kFixedStyle;
    const DWORD exStyle =
        IsFollowSignal() ? kFollowExStyle : (IsAnalyzer() ? WS_EX_APPWINDOW : kFixedExStyle);
    const wchar_t *title =
        IsAnalyzer() ? L"本地自动识别器 - Cloud Latency & FPS Tester" : kAppTitle;
    g.hwnd = CreateWindowExW(exStyle, kAppClass, title, style, initialX, initialY, initialWidth,
                             initialHeight, nullptr, nullptr, instance, nullptr);
    if (g.hwnd == nullptr) {
        MessageBoxW(nullptr, L"主窗口创建失败。", kAppTitle, MB_OK | MB_ICONERROR);
        DeleteCriticalSection(&g.statsLock);
        return 1;
    }
    if (IsFollowSignal()) {
        SetLayeredWindowAttributes(g.hwnd, 0, 255, LWA_ALPHA);
    }
    g.dpi = GetDpiForWindow(g.hwnd);
    RecreateFonts(g.hwnd);
    LayoutControls(g.hwnd);
    GetWindowPlacement(g.hwnd, &g.normalPlacement);
    if (IsFollowSignal()) {
        g.normalPlacement.rcNormalPosition = {initialX, initialY,
                                              initialX + Scale(kFixedInitialWidth),
                                              initialY + Scale(kFixedInitialHeight)};
    }
    if (IsAnalyzer() && g.analyzerTopmost)
        SetWindowPos(g.hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    if (!IsAnalyzer()) {
        RefreshDisplayInfo(g.hwnd, true);
        g.frameEpoch = QpcNow();
        g.nextRefreshCheck = g.frameEpoch + g.qpcFrequency.QuadPart;
        UpdateRenderRate();
    }
    PrepareResultsPath();
    RAWINPUTDEVICE raw{0x01, 0x02, static_cast<DWORD>(IsAnalyzer() ? RIDEV_INPUTSINK : 0), g.hwnd};
    g.rawInputOk = RegisterRawInputDevices(&raw, 1, sizeof(raw)) != FALSE;
    g.hotkeysOk = RegisterModeHotkeys(g.hwnd);
    g.mouseHook = SetWindowsHookExW(WH_MOUSE_LL, LowLevelMouseProc, GetModuleHandleW(nullptr), 0);
    if (g.mouseHook == nullptr) {
        MessageBoxW(g.hwnd, L"无法安装全局鼠标钩子。", kAppTitle, MB_OK | MB_ICONERROR);
        DestroyWindow(g.hwnd);
        DeleteCriticalSection(&g.statsLock);
        return 1;
    }
    ShowWindow(g.hwnd, IsFollowSignal() ? SW_SHOWNOACTIVATE
                                        : (showCommand == SW_HIDE ? SW_SHOWNORMAL : showCommand));
    if (IsFollowSignal()) {
        UpdateFollowPosition(g.hwnd);
    }
    UpdateWindow(g.hwnd);
    {
        g.logThread = CreateThread(nullptr, 0, LogThreadProc, nullptr, 0, nullptr);
        if (!g.logThread)
            g.logError.store(true);
    }
    g.workerThread = CreateThread(
        nullptr, 0, IsAnalyzer() ? CaptureThreadProc : SignalRenderThreadProc, nullptr, 0, nullptr);
    if (g.workerThread == nullptr) {
        MessageBoxW(g.hwnd, L"工作线程创建失败。", kAppTitle, MB_OK | MB_ICONERROR);
        DestroyWindow(g.hwnd);
        g.logQuit.store(true);
        SetEvent(g.logEvent);
        if (g.logThread) {
            WaitForSingleObject(g.logThread, INFINITE);
            CloseHandle(g.logThread);
        }
        CloseHandle(g.stopEvent);
        CloseHandle(g.logEvent);
        DeleteCriticalSection(&g.statsLock);
        return 1;
    }
    if (!g.hotkeysOk) {
        MessageBoxW(g.hwnd, L"部分全局热键被其他程序占用。", kAppTitle, MB_OK | MB_ICONWARNING);
    }
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (message.message == WM_KEYDOWN && message.wParam == VK_ESCAPE &&
            (g.fullscreen || g.fpsCaptured)) {
            if (g.fpsCaptured)
                ReleaseFpsCapture();
            else
                SetPresentation(0);
            continue;
        }
        if (IsDialogMessageW(g.hwnd, &message))
            continue;
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    g.quit.store(true, std::memory_order_release);
    if (g.workerThread != nullptr) {
        SetEvent(g.stopEvent);
        WaitForSingleObject(g.workerThread, INFINITE);
        CloseHandle(g.workerThread);
        g.workerThread = nullptr;
    }
    g.logQuit.store(true);
    SetEvent(g.logEvent);
    if (g.logThread) {
        WaitForSingleObject(g.logThread, INFINITE);
        CloseHandle(g.logThread);
    }
    CloseHandle(g.stopEvent);
    CloseHandle(g.logEvent);
    if (g.statsLockInitialized) {
        DeleteCriticalSection(&g.statsLock);
        DeleteCriticalSection(&g.logLock);
        g.statsLockInitialized = false;
    }
    return static_cast<int>(message.wParam);
}
