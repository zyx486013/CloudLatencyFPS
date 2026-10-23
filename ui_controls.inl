// Included inside the application's anonymous namespace.
void SavePreferences() {
    if (!g.configPath[0])
        return;
    wchar_t ratio[16]{};
    std::swprintf(ratio, 16, L"%d", g.codeRatio);
    if (IsAnalyzer())
        WritePrivateProfileStringW(L"UI", L"AnalyzerTopmost", g.analyzerTopmost ? L"1" : L"0",
                                   g.configPath);
    else
        WritePrivateProfileStringW(L"UI", L"CodeRatio", ratio, g.configPath);
}
void LoadPreferences() {
    wchar_t folder[MAX_PATH]{};
    const DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", folder, MAX_PATH);
    if (n > 0 && n < MAX_PATH - 40) {
        lstrcatW(folder, L"\\CloudLatencyFPS");
        CreateDirectoryW(folder, nullptr);
        std::swprintf(g.configPath, MAX_PATH, L"%ls\\settings.ini", folder);
        g.codeRatio = std::clamp(
            static_cast<int>(GetPrivateProfileIntW(L"UI", L"CodeRatio", 28, g.configPath)), 20, 90);
        g.analyzerTopmost = GetPrivateProfileIntW(L"UI", L"AnalyzerTopmost", 0, g.configPath) != 0;
    }
}
HWND AddControl(HWND parent, const wchar_t *type, const wchar_t *label, int id, DWORD extra) {
    HWND child = CreateWindowExW(0, type, label, WS_CHILD | WS_VISIBLE | WS_TABSTOP | extra, 0, 0,
                                 10, 10, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                                 g.instance, nullptr);
    for (HWND &slot : g.controls)
        if (!slot) {
            slot = child;
            break;
        }
    return child;
}
void CreateControls(HWND hwnd) {
    if (IsAnalyzer()) {
        g.recordButton = AddControl(hwnd, L"BUTTON", L"开始记录", kRecord, BS_PUSHBUTTON);
        g.roiButton = AddControl(hwnd, L"BUTTON", L"重新框选", kRoiReset, BS_PUSHBUTTON);
        g.topmostButton = AddControl(hwnd, L"BUTTON", L"保持置顶", kTopmost, BS_AUTOCHECKBOX);
        g.resetButton = AddControl(hwnd, L"BUTTON", L"清空统计", kAnalyzerReset, BS_PUSHBUTTON);
        g.detailsButton = AddControl(hwnd, L"BUTTON", L"更多统计", kDetails, BS_PUSHBUTTON);
        g.openButton = AddControl(hwnd, L"BUTTON", L"打开 CSV", kOpenCsv, BS_PUSHBUTTON);
        g.bitrateEdit =
            AddControl(hwnd, L"EDIT", L"20", kBitrateLabel, ES_NUMBER | ES_AUTOHSCROLL | WS_BORDER);
        SendMessageW(g.bitrateEdit, EM_SETLIMITTEXT, 4, 0);
        g.comparisonButton = AddControl(hwnd, L"BUTTON", L"码率对比", kComparison, BS_PUSHBUTTON);
    } else {
        g.modeCombo = AddControl(hwnd, L"COMBOBOX", L"", kModeCombo, CBS_DROPDOWNLIST | WS_VSCROLL);
        for (int i = 0; i < 5; ++i)
            SendMessageW(g.modeCombo, CB_ADDSTRING, 0,
                         reinterpret_cast<LPARAM>(ModeName(static_cast<TestMode>(i))));
        g.rateCombo = AddControl(hwnd, L"COMBOBOX", L"", kRateCombo, CBS_DROPDOWNLIST | WS_VSCROLL);
        for (int i = 0; i < kTargetRateCount; ++i) {
            wchar_t rate[24]{};
            if (i == 0)
                lstrcpyW(rate, L"自动");
            else
                std::swprintf(rate, 24, L"%d Hz", kTargetRates[i]);
            SendMessageW(g.rateCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(rate));
        }
        g.displayCombo = AddControl(hwnd, L"COMBOBOX", L"", kDisplayCombo, CBS_DROPDOWNLIST);
        for (const wchar_t *label : {L"窗口化", L"紧凑跟随", L"全屏"})
            SendMessageW(g.displayCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label));
        g.ratioSlider =
            AddControl(hwnd, TRACKBAR_CLASSW, L"识别码宽度", kRatioSlider, TBS_HORZ | TBS_NOTICKS);
        SendMessageW(g.ratioSlider, TBM_SETRANGE, TRUE, MAKELPARAM(20, 90));
        SendMessageW(g.ratioSlider, TBM_SETPAGESIZE, 0, 5);
        g.lockButton = AddControl(hwnd, L"BUTTON", L"锁定布局", kLockLayout, BS_AUTOCHECKBOX);
        g.fullExit = AddControl(hwnd, L"BUTTON", L"返回窗口 · Esc", kFullscreenExit, BS_PUSHBUTTON);
        g.fpsControlCombo = AddControl(hwnd, L"COMBOBOX", L"", kFpsControl, CBS_DROPDOWNLIST);
        for (const wchar_t *label : {L"自动 60 秒", L"相对鼠标"})
            SendMessageW(g.fpsControlCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label));
        g.fpsSceneCombo = AddControl(hwnd, L"COMBOBOX", L"", kFpsScene, CBS_DROPDOWNLIST);
        for (const wchar_t *label : {L"3D 网格房间", L"3D 纹理房间", L"高压细节", L"极限噪声"})
            SendMessageW(g.fpsSceneCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label));
        g.fpsPressureCombo=AddControl(hwnd,L"COMBOBOX",L"",kFpsPressure,CBS_DROPDOWNLIST);
        for(const wchar_t *label:{L"标准",L"高压",L"极限"})
            SendMessageW(g.fpsPressureCombo,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label));
        g.fpsRestartButton =
            AddControl(hwnd, L"BUTTON", L"重置", kFpsRestart, BS_PUSHBUTTON);
    }
    SyncControls();
    LayoutControls(hwnd);
}
void PlaceControl(HWND control, int x, int y, int w, int h) {
    if (control)
        SetWindowPos(control, nullptr, Scale(x), Scale(y), Scale(w), Scale(h),
                     SWP_NOZORDER | SWP_NOACTIVATE);
}
void LayoutControls(HWND hwnd) {
    RECT client{};
    GetClientRect(hwnd, &client);
    if (IsAnalyzer()) {
        PlaceControl(g.recordButton, 16, 43, 98, 30);
        PlaceControl(g.roiButton, 123, 43, 98, 30);
        PlaceControl(g.topmostButton, 236, 43, 104, 30);
        PlaceControl(g.resetButton, 351, 43, 98, 30);
        PlaceControl(g.detailsButton, 460, 43, 98, 30);
        PlaceControl(g.openButton, 567, 43, 98, 30);
        PlaceControl(g.bitrateEdit, 95, 85, 82, 30);
        SetWindowPos(g.comparisonButton, nullptr, client.right - Scale(135), Scale(85), Scale(119),
                     Scale(30), SWP_NOZORDER | SWP_NOACTIVATE);
    } else {
        const bool ordinary = !IsFollowSignal() && !g.fullscreen;
        for (HWND control : {g.fpsControlCombo, g.fpsSceneCombo, g.fpsPressureCombo, g.fpsRestartButton})
            if (control)
                ShowWindow(control,
                           ordinary && g.testMode == TestMode::FpsView ? SW_SHOW : SW_HIDE);
        PlaceControl(g.fpsControlCombo, 56, 88, 150, 150);
        PlaceControl(g.fpsSceneCombo, 221, 88, 140, 150);
        PlaceControl(g.fpsPressureCombo, 367, 88, 80, 150);
        PlaceControl(g.fpsRestartButton, 453, 88, 80, 30);
        for (HWND control : {g.modeCombo, g.rateCombo, g.displayCombo, g.ratioSlider, g.lockButton})
            if (control)
                ShowWindow(control, ordinary ? SW_SHOW : SW_HIDE);
        if (g.fullExit)
            ShowWindow(g.fullExit, g.fullscreen ? SW_SHOW : SW_HIDE);
        PlaceControl(g.modeCombo, 56, 10, 150, 180);
        PlaceControl(g.rateCombo, 261, 10, 100, 240);
        PlaceControl(g.displayCombo, 378, 10, 132, 160);
        PlaceControl(g.ratioSlider, 129, 52, 228, 32);
        PlaceControl(g.lockButton, 378, 53, 132, 32);
        if (g.fullExit)
            SetWindowPos(g.fullExit, nullptr, client.right - Scale(146), Scale(6), Scale(136),
                         Scale(30), SWP_NOZORDER | SWP_NOACTIVATE);
    }
}
void SyncControls() {
    if (IsAnalyzer()) {
        if (g.topmostButton)
            SendMessageW(g.topmostButton, BM_SETCHECK,
                         g.analyzerTopmost ? BST_CHECKED : BST_UNCHECKED, 0);
        if (g.bitrateEdit)
            EnableWindow(g.bitrateEdit, !g.recording.load());
        if (g.recordButton)
            SetWindowTextW(g.recordButton, g.recording.load() ? L"停止并保存" : L"开始记录");
    } else {
        if (g.fpsControlCombo)
            SendMessageW(g.fpsControlCombo, CB_SETCURSEL, g.fpsManual ? 1 : 0, 0);
        if (g.fpsSceneCombo)
            SendMessageW(g.fpsSceneCombo, CB_SETCURSEL, g.fpsScene, 0);
        if(g.fpsPressureCombo) {
            SendMessageW(g.fpsPressureCombo,CB_SETCURSEL,g.fpsPressure,0);
            EnableWindow(g.fpsPressureCombo,g.fpsScene<2);
        }
        if (g.modeCombo) {
            SendMessageW(g.modeCombo, CB_SETCURSEL, ModeIndex(g.testMode), 0);
            EnableWindow(g.modeCombo, !g.layoutLocked);
        }
        if (g.rateCombo)
            SendMessageW(g.rateCombo, CB_SETCURSEL, g.targetRateIndex, 0);
        if (g.displayCombo)
            SendMessageW(g.displayCombo, CB_SETCURSEL,
                         g.fullscreen ? 2 : (IsFollowSignal() ? 1 : 0), 0);
        if (g.ratioSlider) {
            SendMessageW(g.ratioSlider, TBM_SETPOS, TRUE, g.codeRatio);
            EnableWindow(g.ratioSlider, !g.layoutLocked);
        }
        if (g.displayCombo)
            EnableWindow(g.displayCombo, !g.layoutLocked);
        if (g.lockButton)
            SendMessageW(g.lockButton, BM_SETCHECK, g.layoutLocked ? BST_CHECKED : BST_UNCHECKED,
                         0);
    }
}
void SetPresentation(int mode) {
    if (IsAnalyzer() || (g.layoutLocked && !g.fullscreen))
        return;
    ReleaseFpsCapture();
    if (mode == 1 && (g.testMode == TestMode::MouseMotion || g.testMode == TestMode::FpsView))
        mode = 0;
    if (!g.fullscreen && !IsFollowSignal())
        GetWindowPlacement(g.hwnd, &g.normalPlacement);
    if (g.fullscreen)
        g.layoutLocked = false;
    g.fullscreen = mode == 2;
    g.launchMode = mode == 1 ? LaunchMode::FollowSignal : LaunchMode::FixedSignal;
    const DWORD style = (mode == 0 ? kFixedStyle : WS_POPUP | WS_CLIPCHILDREN);
    const DWORD ex = mode == 1 ? kFollowExStyle : kFixedExStyle;
    SetWindowLongPtrW(g.hwnd, GWL_STYLE, style);
    SetWindowLongPtrW(g.hwnd, GWL_EXSTYLE, ex);
    if (mode == 2) {
        MONITORINFO mi{};
        mi.cbSize = sizeof(mi);
        GetMonitorInfoW(MonitorFromWindow(g.hwnd, MONITOR_DEFAULTTONEAREST), &mi);
        SetWindowPos(g.hwnd, HWND_TOPMOST, mi.rcMonitor.left, mi.rcMonitor.top,
                     mi.rcMonitor.right - mi.rcMonitor.left, mi.rcMonitor.bottom - mi.rcMonitor.top,
                     SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    } else if (mode == 1) {
        SetLayeredWindowAttributes(g.hwnd, 0, 255, LWA_ALPHA);
        g.lastFollowPosition = {-10000, -10000};
        UpdateFollowPosition(g.hwnd);
        SetWindowPos(g.hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    } else {
        SetWindowPlacement(g.hwnd, &g.normalPlacement);
        SetWindowPos(g.hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    }
    LayoutControls(g.hwnd);
    RecreateFonts(g.hwnd);
    SyncControls();
    RefreshSignalRate();
    InvalidateRect(g.hwnd, nullptr, FALSE);
}
void ToggleRecording() {
    if (!g.recording.load()) {
        AnalyzerStats stats{};
        CopyAnalyzerStats(&stats);
        if (!stats.roiValid) {
            MessageBoxW(g.hwnd, L"请先 F2/F3 框选识别码。FPS模式需框住整组4:1黑白码。", kAppTitle,
                        MB_OK);
            return;
        }
        BOOL translated = FALSE;
        const UINT bitrate = GetDlgItemInt(g.hwnd, kBitrateLabel, &translated, FALSE);
        if (!translated || bitrate < 1 || bitrate > 9999) {
            MessageBoxW(g.hwnd, L"码率标签请输入1～9999 Mbps；它仅用于比较，不会修改串流设置。",
                        kAppTitle, MB_OK);
            return;
        }
        EnterCriticalSection(&g.statsLock);
        g.bitrateMbps.store(static_cast<int>(bitrate));
        g.runId.store(QpcNow());
        LeaveCriticalSection(&g.statsLock);
        g.runEndedAt = 0;
        ResetAnalyzerStats();
        g.recording.store(true, std::memory_order_release);
    } else {
        g.recording.store(false, std::memory_order_release);
        g.runEndedAt = QpcNow();
        SaveFpsRun();
        EnterCriticalSection(&g.statsLock);
        g.analyzer.pendingClick = false;
        g.analyzer.pendingMotion = false;
        LeaveCriticalSection(&g.statsLock);
        SetEvent(g.logEvent);
    }
    SyncControls();
    InvalidateRect(g.hwnd, nullptr, FALSE);
}
void ResetRoi() {
    if (g.recording.load())
        ToggleRecording();
    g.recording.store(false, std::memory_order_release);
    EnterCriticalSection(&g.statsLock);
    g.analyzer = AnalyzerStats{};
    g.hasPreview = false;
    ++g.roiGeneration;
    LeaveCriticalSection(&g.statsLock);
    SyncControls();
    InvalidateRect(g.hwnd, nullptr, FALSE);
}
