// Included after the ordinary decoder and cadence helpers.
DecoderResult DecodeFpsBundle(const std::uint8_t *pixels, int width, int height) {
    DecoderResult result{};
    if (width < 256 || height < 64)
        return result;
    double confidence = 1;
    for (int i = 0; i < fps::kFields; ++i) {
        const int x = (i % 4) * width / 4, y = (i / 4) * height / 2;
        const auto tile =
            DecodeMachineCode(pixels + (y * width + x) * 4, width / 4, height / 2, width * 4);
        if (!tile.valid || tile.mode != TestMode::FpsView)
            return result;
        result.fields[i] = tile.sequence;
        confidence = std::min(confidence, tile.confidence);
    }
    const int tag = result.fields[4] >> 10;
    if (fps::BundleCheck(result.fields) != result.fields[7] || tag < 2 || tag > 4 ||
        (tag > 2 && ((result.fields[4] >> 1) & 3) >= 2))
        return DecoderResult{};
    result.valid = result.telemetry = true;
    result.mode = TestMode::FpsView;
    result.sequence = result.fields[0];
    result.confidence = confidence;
    return result;
}
void UpdateTrackingLocked(LONGLONG now) {
    if (now < g.analyzer.nextTrackingAt)
        return;
    g.analyzer.nextTrackingAt = now + g.qpcFrequency.QuadPart;
    fps::Point local[fps::kHistory]{}, remote[512]{};
    int nl = 0, nr = 0;
    const double cutoff = TicksToMilliseconds(now - g.processEpoch) - 3500;
    for (int n = 0; n < g.analyzer.localCount; ++n) {
        const int i =
            (g.analyzer.localWrite - g.analyzer.localCount + n + fps::kHistory) % fps::kHistory;
        if (g.analyzer.localTrace[i].time >= cutoff)
            local[nl++] = g.analyzer.localTrace[i];
    }
    for (int n = 0; n < g.analyzer.remoteCount; ++n) {
        const int i = (g.analyzer.remoteWrite - g.analyzer.remoteCount + n + 512) % 512;
        if (g.analyzer.remoteTrace[i].time >= cutoff + 500)
            remote[nr++] = g.analyzer.remoteTrace[i];
    }
    g.analyzer.tracking = fps::MatchTracking(local, nl, remote, nr);
}
bool ProcessFpsDecodedLocked(const DecoderResult &d, LONGLONG now, char *row,
                             std::size_t capacity) {
    if (!d.telemetry)
        return false;
    const auto *fields = d.fields;
    const int flags = fields[4];
    const int previousPhase = (g.analyzer.fpsFlags >> 3) & 7;
    const double yaw = fps::DecodeYaw(fields[2]), pitch = fps::DecodePitch(fields[3]);
    const double sourceInterval = fields[5] / 10.0, sourcePaint = fields[6] / 10.0;
    const bool baseline = g.analyzer.frameBaseline;
    if (!baseline) {
        g.analyzer.frameBaseline = true;
        g.analyzer.lastFrameSequence = d.sequence;
        g.analyzer.fpsInputSequence = fields[1];
        g.analyzer.fpsYaw = yaw;
        g.analyzer.fpsPitch = pitch;
        g.analyzer.fpsUnwrappedYaw = yaw;
        g.analyzer.lastTransitionAt = now;
        g.analyzer.fpsFlags = flags;
        g.analyzer.runStartedAt = now;
        g.analyzer.runLastFrameAt = now;
        g.analyzer.decodeGap = false;
        return false;
    }
    if (d.sequence == g.analyzer.lastFrameSequence) {
        // A valid repeat is different from an undecodable capture.
        if (!g.analyzer.decodeGap)
            g.analyzer.frameLongestMs = std::max(
                g.analyzer.frameLongestMs, TicksToMilliseconds(now - g.analyzer.lastTransitionAt));
        UpdateRollingFrameStatsLocked(now);
        return false;
    }
    const auto delta = static_cast<std::uint16_t>((d.sequence - g.analyzer.lastFrameSequence) &
                                                  kStreamSequenceMask);
    if (delta > 1000) {
        g.analyzer.frameBaseline = false;
        return false;
    }
    const bool intervalValid = !g.analyzer.decodeGap;
    const double interval = TicksToMilliseconds(now - g.analyzer.lastTransitionAt);
    const double step = fps::YawDelta(g.analyzer.fpsYaw, yaw);
    g.analyzer.fpsUnwrappedYaw += step;
    g.analyzer.maxYawStep = std::max(g.analyzer.maxYawStep, std::fabs(step));
    g.analyzer.fpsYaw = yaw;
    g.analyzer.fpsPitch = pitch;
    g.analyzer.sourceIntervalMs = sourceInterval;
    g.analyzer.sourcePaintMs = sourcePaint;
    g.analyzer.sourceMaxPaintMs = std::max(g.analyzer.sourceMaxPaintMs, sourcePaint);
    g.analyzer.sourceMaxIntervalMs = std::max(g.analyzer.sourceMaxIntervalMs, sourceInterval);
    // A change in scene/control/target rate invalidates comparability of this run.
    if ((flags & 0x1dc7) != (g.analyzer.fpsFlags & 0x1dc7))
        g.analyzer.sourceLimited = true;
    g.analyzer.fpsFlags = flags;
    const int rateIndex = (flags >> 6) & 7;
    const double targetMs = rateIndex > 0 ? 1000.0 / kTargetRates[rateIndex] : 1000.0 / 60;
    if (sourcePaint > targetMs * .8 || (sourceInterval > targetMs * 1.8 && rateIndex > 0))
        g.analyzer.sourceLimited = true;
    const bool manual = (flags & 1) != 0, captured = (flags & 512) != 0;
    if (manual && captured && g.analyzer.pendingMotion &&
        fields[1] != g.analyzer.fpsInputSequence) {
        RecordMotionLatencyLocked(TicksToMilliseconds(now - g.analyzer.motionStartedAt));
        g.analyzer.pendingMotion = false;
    }
    g.analyzer.fpsInputSequence = fields[1];
    const int trace = g.analyzer.remoteWrite;
    g.analyzer.remoteTrace[trace] = {TicksToMilliseconds(now - g.processEpoch),
                                     g.analyzer.fpsUnwrappedYaw};
    g.analyzer.remoteWrite = (trace + 1) % 512;
    g.analyzer.remoteCount = std::min(g.analyzer.remoteCount + 1, 512);
    if (manual && captured)
        UpdateTrackingLocked(now);
    else
        g.analyzer.tracking = {};
    if (intervalValid) {
        const int bin = std::min(static_cast<int>(std::lround(interval * 4)), 4095);
        ++g.analyzer.runHistogram[bin];
        ++g.analyzer.runIntervalTotal;
        g.analyzer.runIntervalSum += interval;
        g.analyzer.runIntervalMax = std::max(g.analyzer.runIntervalMax, interval);
        if (interval > targetMs * 1.5)
            ++g.analyzer.runHitches;
        g.analyzer.renderedNotObserved += delta - 1;
    } else
        ++g.analyzer.unknownIntervals;
    ++g.analyzer.runFrames;
    g.analyzer.runLastFrameAt = now;
    g.analyzer.intervalValid[g.analyzer.frameHistoryWrite] = intervalValid;
    AddFrameTransitionLocked({now, delta});
    g.analyzer.lastFrameSequence = d.sequence;
    g.analyzer.decodeGap = false;
    std::snprintf(
        row, capacity,
        "fps_frame,%.3f,%u,%u,%.3f,%.3f,%lld,%d,%d,%d,%d,%u,%.4f,%.4f,%.3f,%.3f,%.3f,%.5f,%d,%d\n",
        TicksToMilliseconds(now - g.processEpoch), d.sequence, delta, interval, d.confidence,
        g.runId.load(), g.bitrateMbps.load(), (flags >> 1) & 3, manual ? 1 : 0, (flags >> 3) & 7,
        fields[1], yaw, pitch, sourceInterval, sourcePaint,
        g.analyzer.tracking.valid ? g.analyzer.tracking.latency : -1,
        g.analyzer.tracking.correlation, intervalValid ? 1 : 0, (flags >> 10)-2);
    if (!(flags & 1) && previousPhase == 3 && ((flags >> 3) & 7) == 4) {
        g.recording.store(false);
        PostMessageW(g.hwnd, kMessageAutoStop, 0, 0);
    }
    return true;
}
double RunPercentile(const AnalyzerStats &stats, double fraction) {
    if (stats.runIntervalTotal == 0)
        return 0;
    const auto target = static_cast<std::uint64_t>(std::ceil(stats.runIntervalTotal * fraction));
    std::uint64_t count = 0;
    for (int i = 0; i < 4096; ++i) {
        count += stats.runHistogram[i];
        if (count >= target)
            return i / 4.0;
    }
    return 1023.75;
}
void SaveFpsRun() {
    AnalyzerStats stats{};
    CopyAnalyzerStats(&stats);
    if (stats.decodedMode != ModeIndex(TestMode::FpsView) || stats.runFrames == 0)
        return;
    if (g.runCount == 16) {
        std::move(g.runs + 1, g.runs + 16, g.runs);
        g.runCount = 15;
    }
    auto &run = g.runs[g.runCount++];
    run = {};
    run.id = g.runId.load();
    run.bitrate = g.bitrateMbps.load();
    run.flags = stats.fpsFlags;
    run.duration =
        TicksToMilliseconds((g.runEndedAt ? g.runEndedAt : QpcNow()) - stats.runStartedAt) / 1000;
    run.fps = run.duration > 0 ? stats.runFrames / run.duration : 0;
    run.p95 = RunPercentile(stats, .95);
    run.p99 = RunPercentile(stats, .99);
    run.longest = stats.runIntervalMax;
    run.unknown = stats.runUnknownMs;
    if (stats.gapStartedAt > 0)
        run.unknown += TicksToMilliseconds(QpcNow() - stats.gapStartedAt);
    run.tracking = stats.tracking.valid ? stats.tracking.latency : -1;
    run.confidence = stats.tracking.correlation;
    run.frames = stats.runFrames;
    run.hitches = stats.runHitches;
    run.missing = stats.renderedNotObserved;
    run.uncertain = stats.unknownIntervals;
    run.sourcePaint = stats.sourceMaxPaintMs;
    run.sourceInterval = stats.sourceMaxIntervalMs;
    run.sourceLimited = stats.sourceLimited;
    char row[512]{};
    std::snprintf(row, sizeof(row),
                  "fps_summary,%.3f,0,0,%.3f,%.3f,%lld,%d,%d,%d,%d,0,0,0,%.3f,%.3f,%.3f,%.5f,0,%d\n",
                  TicksToMilliseconds(QpcNow() - g.processEpoch), run.p95, run.unknown, run.id,
                  run.bitrate, (run.flags >> 1) & 3, run.flags & 1, (run.flags >> 3) & 7,
                  run.sourceInterval, run.sourcePaint, run.tracking, run.confidence, (run.flags>>10)-2);
    QueueLog(row);
    WriteComparison(false);
}
void WriteComparison(bool openReport) {
    const char *pressureNames[]={"标准","高压","极限"};
    const char *sceneNames[] = {"3D 网格房间", "3D 纹理房间", "高压细节", "极限噪声（合成）"};
    if (!g.runCount) {
        MessageBoxW(g.hwnd,
                    L"先完成一轮 FPS 转视角记录并点击停止。建议按低→高→低码率重复相同场景与动作。",
                    kAppTitle, MB_OK);
        return;
    }
    std::FILE *file = _wfopen(g.comparisonPath, L"wb");
    if (!file) {
        MessageBoxW(g.hwnd, L"无法写入对比报告，请把程序放到可写文件夹。", kAppTitle,
                    MB_OK | MB_ICONERROR);
        return;
    }
    std::fputs(
        "<!doctype html><meta charset='utf-8'><title>FPS 转视角 · "
        "码率对比</title><style>body{font:16px "
        "system-ui;background:#11161f;color:#e7edf5;max-width:1400px;margin:40px auto;padding:0 "
        "24px}table{border-collapse:collapse;width:100%;font-variant-numeric:tabular-nums}td,th{"
        "padding:12px 8px;border-bottom:1px solid "
        "#334155;text-align:left}p{line-height:1.8;color:#b7c4d8}.bad{color:#ffc26c}</"
        "style><h1>FPS 转视角 · "
        "码率对比</"
        "h1><p>"
        "码率为你填写的设置标签，不是实测流量。对比前请固定分辨率、帧率、编码格式、场景、控制方式和"
        "测试时长。推荐低→高→低，每档3轮。观察更新率受到本地刷新、截图和识别限制；无法单凭本报告定"
        "位网络、编码或解码瓶颈。</p><table><tr><th>轮次</th><th>码率标签</th><th>场景 / "
        "压力 / 控制</th><th>记录秒</th><th>观察更新/s</th><th>P95 / P99 ms</th><th>最长有效间隔 "
        "ms</th><th>长间隔/分钟</th><th>识别未知 ms</th><th>未观察帧 / "
        "不确定间隔</th><th>云端绘制最大 ms</th><th>跟踪估计 ms</th></tr>",
        file);
    for (int i = 0; i < g.runCount; ++i) {
        const auto &r = g.runs[i];
        std::fprintf(
            file,
            "<tr><td>%d%s</td><td>%d Mbps</td><td>%s / %s / %s</td><td>%.1f</td><td>%.1f</td><td>%.2f / "
            "%.2f</td><td>%.2f</td><td>%.1f</td><td>%.1f</td><td>%llu / %llu</td><td>%.1f</td><td>",
            i + 1, r.sourceLimited ? " ⚠" : "", r.bitrate, sceneNames[(r.flags >> 1) & 3], pressureNames[std::clamp((r.flags >> 10)-2,0,2)],
            (r.flags & 1) ? "鼠标" : "自动", r.duration, r.fps, r.p95, r.p99, r.longest,
            r.duration > 0 ? r.hitches * 60.0 / r.duration : 0, r.unknown, r.missing, r.uncertain,
            r.sourcePaint);
        if (r.tracking >= 0)
            std::fprintf(file, "%.0f (r=%.3f)", r.tracking, r.confidence);
        else
            std::fputs("证据不足 / 不适用", file);
        if (!(r.flags & 1) && r.duration < 58)
            std::fputs("<br><span class='bad'>未覆盖完整60秒</span>", file);
        std::fputs("</td></tr>", file);
    }
    std::fputs(
        "</table><p>⚠ "
        "表示云端绘制耗时较大、已选择目标帧率下云端帧间隔明显变长，或记录中更改了场景/控制/"
        "目标帧率。这些轮次需结合云端 source CSV 检查。P95/P99 "
        "使用整轮有效间隔直方图，分辨率0.25ms，超过1023.75ms进入末桶。长间隔阈值为目标周期的1."
        "5倍；目标设为自动时暂按60Hz计算，因此对比建议明确选择60/"
        "120Hz。识别失败跨越的间隔排除出帧时间和未观察帧统计，单独计为不确定。跟踪估计需手动左右变"
        "速与反向，匀速/静止或相关峰不明确时不输出数值；它不包含显示器扫描和像素响应。</p>",
        file);
    if (std::fclose(file) != 0) {
        MessageBoxW(g.hwnd, L"对比报告保存失败。", kAppTitle, MB_OK | MB_ICONERROR);
        return;
    }
    if (openReport)
        ShellExecuteW(g.hwnd, L"open", g.comparisonPath, nullptr, nullptr, SW_SHOWNORMAL);
}
