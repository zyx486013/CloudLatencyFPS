// Included inside the application namespace after the marker painter.
const wchar_t *FpsPhaseName(int phase) {
    const wchar_t *names[] = {L"静止 10s", L"慢转 20s",       L"快转 20s",
                              L"急停 10s", L"完成 · F7 重跑", L"手动转视角"};
    return names[std::clamp(phase, 0, 5)];
}
void ReleaseTexture() {
    if (g.textureDc) {
        if (g.textureOriginal)
            SelectObject(g.textureDc, g.textureOriginal);
        if (g.textureBitmap)
            DeleteObject(g.textureBitmap);
        DeleteDC(g.textureDc);
    }
    g.textureDc = nullptr;
    g.textureBitmap = nullptr;
    g.textureOriginal = nullptr;
    g.textureScene = -1;
}
const wchar_t *FpsSceneName(int scene) {
    const wchar_t *names[] = {L"3D 网格房间", L"3D 纹理房间", L"高压细节", L"极限噪声"};
    return names[std::clamp(scene, 0, fps::kSceneCount - 1)];
}
const wchar_t *FpsPressureName() {
    const wchar_t *names[]={L"标准",L"高压",L"极限"};
    return g.fpsScene<2?names[std::clamp(g.fpsPressure,0,2)]:L"2D合成";
}
bool EnsureTexture(HDC dc) {
    if (g.fpsScene < 2) {
        RECT client{};GetClientRect(g.hwnd,&client);
        RECT scene{0,SignalHeaderHeight(),client.right,client.bottom-SignalFooterHeight()};
        if(!gpu::Ensure(scene)) return false;
        gpu::Build(g.fpsScene);
        return true;
    }
    if (g.textureDc && g.textureScene == g.fpsScene)
        return true;
    ReleaseTexture();
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    const int width = fps::TextureWidth(g.fpsScene), height = fps::TextureHeight(g.fpsScene);
    bi.bmiHeader.biWidth = width;
    bi.bmiHeader.biHeight = -height;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    std::uint32_t *pixels = nullptr;
    g.textureDc = CreateCompatibleDC(dc);
    g.textureBitmap =
        CreateDIBSection(dc, &bi, DIB_RGB_COLORS, reinterpret_cast<void **>(&pixels), nullptr, 0);
    if (!g.textureDc || !g.textureBitmap || !pixels) {
        ReleaseTexture();
        return false;
    }
    g.textureOriginal = static_cast<HBITMAP>(SelectObject(g.textureDc, g.textureBitmap));
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
            pixels[y * width + x] = fps::ScenePixel(g.fpsScene, x, y);
        }
    g.textureScene = g.fpsScene;
    return true;
}
RECT FpsCodeRect(const RECT &client) {
    // Eight 8x4 markers in a 4x2 bundle, physical cells remain square.
    const int availableW = std::max(static_cast<int>(client.right) - Scale(32), 32);
    const int availableH = std::max(static_cast<int>(client.bottom) - SignalHeaderHeight() -
                                        SignalFooterHeight() - Scale(36),
                                    8);
    int cell =
        std::min(std::max(availableW * g.codeRatio / 100, std::min(availableW, Scale(224))) / 32,
                 availableH / 8);
    cell = std::max(cell, 1);
    const int bottom = static_cast<int>(client.bottom) - SignalFooterHeight() - Scale(10);
    return {Scale(16), bottom - cell * 8, Scale(16) + cell * 32, bottom};
}
void PaintFpsTexture(HDC dc, const RECT &scene) {
    const int width = fps::TextureWidth(g.fpsScene), height = fps::TextureHeight(g.fpsScene);
    // One coherent panorama for baseline scenes; 8x4 independent patches for stress scenes.
    const int columns = g.fpsScene >= 2 ? 8 : 1, rows = g.fpsScene >= 2 ? 4 : 1;
    for (int row = 0; row < rows; ++row) {
        for (int col = 0; col < columns; ++col) {
            const RECT tile{scene.left + (scene.right - scene.left) * col / columns,
                            scene.top + (scene.bottom - scene.top) * row / rows,
                            scene.left + (scene.right - scene.left) * (col + 1) / columns,
                            scene.top + (scene.bottom - scene.top) * (row + 1) / rows};
            const auto offset =
                g.fpsScene >= 2
                    ? fps::TileOffset(g.fpsScene, col, row, g.fpsPose.yaw, g.fpsPose.pitch)
                    : fps::StressOffset{
                          fps::WrapOffset(
                              static_cast<int>(std::lround(g.fpsPose.yaw * width / 360.0)), width),
                          fps::WrapOffset(static_cast<int>(std::lround(g.fpsPose.pitch * 6)),
                                          height)};
            for (int y = tile.top; y < tile.bottom;) {
                const int sy = fps::WrapOffset(offset.y + y - tile.top, height);
                const int h = std::min(height - sy, static_cast<int>(tile.bottom) - y);
                for (int x = tile.left; x < tile.right;) {
                    const int sx = fps::WrapOffset(offset.x + x - tile.left, width);
                    const int w = std::min(width - sx, static_cast<int>(tile.right) - x);
                    BitBlt(dc, x, y, w, h, g.textureDc, sx, sy, SRCCOPY);
                    x += w;
                }
                y += h;
            }
        }
    }
}
void PaintFpsSignal(HDC dc, const RECT &client) {
    const LONGLONG started = QpcNow();
    FillSolid(dc, RECT{0, 0, client.right, SignalHeaderHeight()}, kBackground);
    FillSolid(dc, RECT{0, client.bottom - SignalFooterHeight(), client.right, client.bottom},
              kBackground);
    const RECT scene{0, SignalHeaderHeight(), client.right, client.bottom - SignalFooterHeight()};
    if (g.fpsScene < 2) {
        FillSolid(dc, scene, kBackground);
    } else if (EnsureTexture(dc)) {
        PaintFpsTexture(dc, scene);
    } else {
        FillSolid(dc, scene, kBackground);
        DrawCenteredText(dc, L"纹理缓冲创建失败", scene, g.statusFont, kText);
    }
    const int x = static_cast<int>(client.right) / 2, y = (scene.top + scene.bottom) / 2;
    const int arm = Scale(14), thick = Scale(1);
    FillSolid(dc, RECT{x - arm - 2, y - thick - 2, x + arm + 3, y + thick + 3}, RGB(0, 0, 0));
    FillSolid(dc, RECT{x - thick - 2, y - arm - 2, x + thick + 3, y + arm + 3}, RGB(0, 0, 0));
    FillSolid(dc, RECT{x - arm, y - thick, x + arm + 1, y + thick + 1}, RGB(255, 255, 255));
    FillSolid(dc, RECT{x - thick, y - arm, x + thick + 1, y + arm + 1}, RGB(255, 255, 255));
    wchar_t text[256]{};
    if (!g.fullscreen) {
        DrawLeftText(dc, L"测试", RECT{Scale(16), Scale(8), Scale(55), Scale(40)}, g.smallFont,
                     kMuted);
        DrawLeftText(dc, L"频率", RECT{Scale(221), Scale(8), Scale(260), Scale(40)}, g.smallFont,
                     kMuted);
        std::swprintf(text, 256, L"识别码 %d%%", g.codeRatio);
        DrawLeftText(dc, text, RECT{Scale(16), Scale(54), Scale(130), Scale(87)}, g.smallFont,
                     kMuted);
        DrawLeftText(dc, L"控制", RECT{Scale(16), Scale(90), Scale(55), Scale(122)}, g.smallFont,
                     kMuted);
        std::swprintf(text, 256, L"%ls / %ls · %ls · %ls · 输入 %llu · CPU提交 %.1f ms",
                      FpsSceneName(g.fpsScene), FpsPressureName(), FpsPhaseName(g.fpsPose.phase),
                      g.fpsManual ? (g.fpsCaptured ? L"已捕获" : L"点击场景捕获鼠标") : L"自动",
                      static_cast<unsigned long long>(g.fpsRawEvents), g.fpsLastPaintMs);
        DrawLeftText(dc, text, RECT{Scale(16), Scale(124), client.right - Scale(16), Scale(144)},
                     g.smallFont, kMuted);
    } else {
        std::swprintf(text, 256, L"%ls / %ls · %ls · %ls · %d Hz · CPU提交 %.1f ms",
                      FpsSceneName(g.fpsScene), FpsPressureName(), g.fpsManual ? L"鼠标" : L"自动",
                      FpsPhaseName(g.fpsPose.phase), EffectiveFrameUpper(), g.fpsLastPaintMs);
        DrawLeftText(dc, text, RECT{Scale(12), 0, client.right - Scale(146), SignalHeaderHeight()},
                     g.smallFont, kText);
    }
    const RECT code = FpsCodeRect(client);
    RECT caption{code.left, code.top - Scale(25), std::min(client.right, code.right + Scale(170)),
                 code.top};
    FillSolid(dc, caption, kBackground);
    DrawLeftText(dc, L"F2 / F3 框住下方整组 4:1 黑白码（不含本行）", caption, g.smallFont, kText);
    if (g.fpsAdvancePaint) {
        g.machineSequence =
            static_cast<std::uint16_t>((g.machineSequence + 1) & kStreamSequenceMask);
        g.fpsFields[0] = g.machineSequence;
        g.fpsFields[1] = g.mouseMoveSequence;
        g.fpsFields[2] = fps::EncodeYaw(g.fpsPose.yaw);
        g.fpsFields[3] = fps::EncodePitch(g.fpsPose.pitch);
        g.fpsFields[4] =
            fps::Flags(g.fpsManual, g.fpsScene, g.fpsPose.phase, g.targetRateIndex, g.fpsCaptured, g.fpsPressure);
        const double interval =
            g.fpsLastPaintAt ? TicksToMilliseconds(started - g.fpsLastPaintAt) : 0;
        g.fpsActualIntervalMs = interval;
        g.fpsFields[5] =
            static_cast<std::uint16_t>(std::clamp(std::lround(interval * 10), 0L, 8191L));
        g.fpsFields[6] =
            static_cast<std::uint16_t>(std::clamp(std::lround(g.fpsLastPaintMs * 10), 0L, 8191L));
        g.fpsFields[7] = fps::BundleCheck(
            g.fpsFields); // Check all seven fields together, in addition to each tile CRC.
        g.fpsLastPaintAt = started;
    }
    if (g.fpsScene < 2) {
        const bool ok = gpu::Paint(scene, code);
        if (!ok) {
            gpu::Hide();
            g.fpsAdvancePaint = false;
            FillSolid(dc, scene, kBackground);
            DrawCenteredText(dc, L"3D 初始化或呈现失败，请检查显卡驱动；F5 可切换压力场景",
                             scene, g.statusFont, kText);
        }
    }
    for (int i = 0; i < fps::kFields && g.fpsScene >= 2; ++i) {
        const int col = i % 4, row = i / 4;
        RECT tile{code.left + (code.right - code.left) * col / 4,
                  code.top + (code.bottom - code.top) * row / 2,
                  code.left + (code.right - code.left) * (col + 1) / 4,
                  code.top + (code.bottom - code.top) * (row + 1) / 2};
        DrawMachineCode(dc, tile, TestMode::FpsView, g.fpsFields[i]);
    }
    std::swprintf(text, 256, L"相对输入 %llu 次 · 手动背景不动时请检查串流客户端是否启用相对鼠标",
                  static_cast<unsigned long long>(g.fpsRawEvents));
    if (g.fpsManual && g.fpsScene >= 2) {
        RECT note{Scale(16), scene.top + Scale(8), client.right - Scale(16), scene.top + Scale(34)};
        FillSolid(dc, note, kBackground);
        DrawLeftText(dc, text, note, g.smallFont, kText);
    }
    const wchar_t *foot =
        !g.rawInputOk
            ? L"Raw Input 注册失败：相对鼠标不可用 · 自动测试仍可用"
            : (g.fpsManual ? L"点击捕获 · WASD · Esc 释放 · F4 自动 · F10 压力 · F7 重置 · "
                             L"Ctrl+Alt+F12 退出"
                           : L"F4 鼠标 · F5 场景 · F10 压力 · F7 重跑 · F11 全屏 · Ctrl+Alt+F12 退出");
    DrawCenteredText(dc, foot,
                     RECT{0, client.bottom - SignalFooterHeight(), client.right, client.bottom},
                     g.smallFont, kMuted);
    if (g.fpsScene < 2 && gpu::state.software) {
        FillSolid(dc, RECT{0, client.bottom-SignalFooterHeight(), client.right,client.bottom}, kBackground);
        DrawCenteredText(dc, L"当前使用软件 OpenGL：请安装或启用云端显卡驱动后测量",
            RECT{0, client.bottom-SignalFooterHeight(), client.right,client.bottom}, g.smallFont, RGB(255,190,80));
    }
    // Full paint timing is completed after the back buffer reaches the window DC.
}
void RecordFpsSourcePaint() {
    char row[512]{};
    std::snprintf(row, sizeof(row),
                  "fps_source,%.3f,%u,1,%.3f,1,%lld,0,%d,%d,%d,%u,%.4f,%.4f,%.3f,%.3f,,,1,%d\n",
                  TicksToMilliseconds(g.fpsLastPaintAt - g.processEpoch), g.machineSequence,
                  g.fpsActualIntervalMs, g.runId.load(), g.fpsScene, g.fpsManual ? 1 : 0,
                  g.fpsPose.phase, g.mouseMoveSequence, fps::DecodeYaw(g.fpsFields[2]),
                  fps::DecodePitch(g.fpsFields[3]), g.fpsActualIntervalMs, g.fpsLastPaintMs, g.fpsScene<2?g.fpsPressure:0);
    QueueLog(row);
}
void ReleaseFpsCapture() {
    if (!g.fpsCaptured)
        return;
    g.fpsCaptured = false;
    ClipCursor(nullptr);
    SetCursor(LoadCursorW(nullptr, IDC_ARROW));
    g.signalDirty = true;
}
void CaptureFpsMouse() {
    if (!g.rawInputOk || !g.fpsManual || g.testMode != TestMode::FpsView)
        return;
    RECT client{};
    GetClientRect(g.hwnd, &client);
    POINT cursor{};
    GetCursorPos(&cursor);
    ScreenToClient(g.hwnd, &cursor);
    if (cursor.y < SignalHeaderHeight() || cursor.y >= client.bottom - SignalFooterHeight())
        return;
    POINT center{client.right / 2,
                 (SignalHeaderHeight() + client.bottom - SignalFooterHeight()) / 2};
    ClientToScreen(g.hwnd, &center);
    SetCursorPos(center.x, center.y);
    RECT clip{center.x, center.y, center.x + 1, center.y + 1};
    if (ClipCursor(&clip)) {
        g.fpsCaptured = true;
        SetCursor(nullptr);
    }
}
void RestartFpsScenario() {
    if (IsAnalyzer() || g.testMode != TestMode::FpsView)
        return;
    ReleaseFpsCapture();
    g.frameEpoch = QpcNow();
    g.runId.store(g.frameEpoch);
    g.fpsRawEvents = 0;
    g.fpsYaw = g.fpsPitch = 0;
    gpu::ResetCamera();
    g.fpsPose = {};
    // Frame and input sequences remain monotonic across scenario restarts.
    g.fpsLastPaintAt = 0;
    g.fpsLastPaintMs = 0;
    g.signalDirty = true;
    RefreshSignalRate();
    InvalidateRect(g.hwnd, nullptr, FALSE);
}
void BeginFpsMode() {
    RECT client{};
    GetClientRect(g.hwnd, &client);
    HDC dc = GetDC(g.hwnd);
    EnsureTexture(dc);
    ReleaseDC(g.hwnd, dc);
    RestartFpsScenario();
}
void CycleFpsControl() {
    if (IsAnalyzer() || g.testMode != TestMode::FpsView)
        return;
    g.fpsManual = !g.fpsManual;
    RestartFpsScenario();
    SyncControls();
}
void CycleFpsScene() {
    if (IsAnalyzer() || g.testMode != TestMode::FpsView)
        return;
    g.fpsScene = (g.fpsScene + 1) % fps::kSceneCount;
    HDC dc = GetDC(g.hwnd);
    EnsureTexture(dc);
    ReleaseDC(g.hwnd, dc);
    RestartFpsScenario();
    SyncControls();
}

void CycleFpsPressure() {
    if(IsAnalyzer() || g.testMode!=TestMode::FpsView || g.fpsScene>=2) return;
    g.fpsPressure=(g.fpsPressure+1)%3;
    HDC dc=GetDC(g.hwnd);EnsureTexture(dc);ReleaseDC(g.hwnd,dc);
    RestartFpsScenario();SyncControls();
}
