// Raw input handler: never suppresses other apps' mouse messages.
void HandleRawMouse(LPARAM parameter) {
    RAWINPUT input{};
    UINT size = sizeof(input);
    if (GetRawInputData(reinterpret_cast<HRAWINPUT>(parameter), RID_INPUT, &input, &size,
                        sizeof(RAWINPUTHEADER)) == static_cast<UINT>(-1) ||
        input.header.dwType != RIM_TYPEMOUSE)
        return;
    const auto &mouse = input.data.mouse;
    if ((mouse.usFlags & MOUSE_MOVE_ABSOLUTE) || (mouse.lLastX == 0 && mouse.lLastY == 0))
        return;
    const LONGLONG now = QpcNow();
    if (!IsAnalyzer()) {
        if (g.testMode != TestMode::FpsView || !g.fpsManual || !g.fpsCaptured)
            return;
        ++g.fpsRawEvents;
        g.fpsYaw += mouse.lLastX * .1;
        g.fpsPitch = std::clamp(g.fpsPitch + mouse.lLastY * .1, -80.0, 80.0);
        g.mouseMoveSequence =
            static_cast<std::uint16_t>((g.mouseMoveSequence + 1) & kStreamSequenceMask);
        g.signalDirty = true;
        return;
    }
    if (!g.recording.load())
        return;
    POINT cursor{};
    GetCursorPos(&cursor);
    if (PointIsInsideAnalyzer(cursor))
        return;
    EnterCriticalSection(&g.statsLock);
    if (g.analyzer.decodedMode == ModeIndex(TestMode::FpsView) && g.analyzer.decoderLocked &&
        (g.analyzer.fpsFlags & 513) == 513 &&
        now - g.analyzer.lastValidAt < g.qpcFrequency.QuadPart / 2) {
        if (g.analyzer.lastLocalMoveAt == 0 ||
            now - g.analyzer.lastLocalMoveAt > g.qpcFrequency.QuadPart / 5) {
            g.analyzer.pendingMotion = true;
            g.analyzer.motionStartedAt = now;
            ++g.analyzer.motionGestures;
        }
        g.analyzer.lastLocalMoveAt = now;
        ++g.analyzer.localMouseMoves;
        g.analyzer.localCumulativeX += mouse.lLastX;
        const int i = g.analyzer.localWrite;
        g.analyzer.localTrace[i] = {TicksToMilliseconds(now - g.processEpoch),
                                    g.analyzer.localCumulativeX};
        g.analyzer.localWrite = (i + 1) % fps::kHistory;
        g.analyzer.localCount = std::min(g.analyzer.localCount + 1, fps::kHistory);
    }
    LeaveCriticalSection(&g.statsLock);
}
