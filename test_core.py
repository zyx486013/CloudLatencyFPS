#!/usr/bin/env python3
"""Run the actual protocol/statistics functions and layout on a native C++ host.
No Windows GUI/driver behavior is emulated by this test.
"""
from pathlib import Path
import subprocess, tempfile

root=Path(__file__).resolve().parent
source=(root/'CloudLatencyFPS.cpp').read_text()+'\n'+(root/'fps_analyzer.inl').read_text()+'\n'+(root/'fps_signal.inl').read_text()
def function(name):
    import re
    match=re.search(r'^\w[\w:<>&* ]*\b'+name+r'\([^;]*?\)\s*\{',source,re.M)
    if not match: raise ValueError(name)
    start=match.start(); pos=source.index('{',match.start()); depth=1; pos+=1
    while depth:
        depth+=(source[pos]=='{')-(source[pos]=='}');pos+=1
    return source[start:pos]
def structure(name):
    a=source.index('struct '+name+' {');b=source.index('\n};',a)+3
    return source[a:b]

header=r'''
#include <algorithm>
#include <atomic>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
#include "layout.h"
#include "fps_model.h"
using LONGLONG=long long;
struct POINT {long x=0,y=0;}; struct RECT{long left=0,top=0,right=0,bottom=0;};
enum class TestMode{Latency,FrameRate,AutoMotion,MouseMotion,FpsView};
constexpr int kMaxLatencySamples=256,kFrameHistorySize=4096,kCodeColumns=8,kCodeRows=4,kPayloadBits=24;
constexpr std::uint16_t kFullSequenceMask=0x7fff,kStreamSequenceMask=0x1fff;
constexpr int kSyncBits[]={1,0,1,0,0,1,0,1};
'''
structs='\n'.join(structure(n) for n in ['DecoderResult','PixelDimensions','CellPosition','FrameTransition','AnalyzerStats'])
stub=r'''
struct State{AnalyzerStats analyzer;int statsLock=0;unsigned roiGeneration=1;std::atomic<bool> recording{true};struct {LONGLONG QuadPart=1000000;} qpcFrequency;LONGLONG processEpoch=0;std::atomic<LONGLONG>runId{1};std::atomic<int>bitrateMbps{20};int hwnd=0;int codeRatio=28;}g;
int testScale=1;
int Scale(int value){return value*testScale;}
int SignalHeaderHeight(){return Scale(144);}
int SignalFooterHeight(){return Scale(28);}
constexpr unsigned kMessageAutoStop=999;
void PostMessageW(int,unsigned,int,int){}
constexpr int kTargetRates[]={0,30,60,90,120,144,165,240};
void EnterCriticalSection(int*){} void LeaveCriticalSection(int*){}
std::vector<std::string> logs;
void QueueLog(const char* line){logs.emplace_back(line);}
'''
names=['FpsCodeRect','ModeIndex','SequenceMask','TicksToMilliseconds','Crc8','GrayToBinary','MakePayload','DecodePayload','SampleCellLuma','DecodeMachineCode','ClearCadenceStatsLocked','RecordLatencyLocked','RecordMotionLatencyLocked','SortedPercentile','UpdateFrameTimingStatsLocked','UpdateRollingFrameStatsLocked','AddFrameTransitionLocked','DecodeFpsBundle','UpdateTrackingLocked','ProcessFpsDecodedLocked','RunPercentile','ProcessDecodedResult']
body=r'''
int main(){
 unsigned protocols=0,layouts=0,fpsLayouts=0;
 // Boundary phases are continuous, stopping freezes the camera, ring math is signed.
 assert(fps::Automatic(9.99).phase==0&&fps::Automatic(10).phase==1);
 assert(fps::Automatic(30).phase==2&&fps::Automatic(50).phase==3&&fps::Automatic(60).phase==4);
 assert(fps::Automatic(50).yaw==0&&fps::Automatic(80).yaw==0);
 assert(fps::WrapOffset(-1,2048)==2047);
 assert(std::abs(fps::YawDelta(359,1)-2)<.01);
 assert(std::abs(fps::YawDelta(1,359)+2)<.01);
 // Known delayed trajectory at an unrelated gain, and deliberately unidentifiable constant motion.
 std::vector<fps::Point> local,remote,straight;
 auto path=[](double t){return std::sin(t/210)+.3*std::sin(t/93)+.15*std::sin(t/47);};
 for(int t=0;t<=4000;t+=4){local.push_back({double(t),path(t)*800});straight.push_back({double(t),double(t)});}
 for(int t=600;t<=3600;t+=17)remote.push_back({double(t),path(t-74)*23+15});
 auto match=fps::MatchTracking(local.data(),local.size(),remote.data(),remote.size());
 assert(match.valid&&std::abs(match.latency-74)<=2&&match.correlation>.99);
 remote.clear();for(int t=600;t<=3600;t+=17)remote.push_back({double(t),double(t-74)*.1});
 assert(!fps::MatchTracking(straight.data(),straight.size(),remote.data(),remote.size()).valid);
 for(int m=0;m<5;++m)for(unsigned seq=0;seq<=SequenceMask(static_cast<TestMode>(m));++seq){
  const auto mode=static_cast<TestMode>(m);const auto payload=MakePayload(mode,static_cast<std::uint16_t>(seq));DecoderResult d;
  assert(DecodePayload(payload,false,&d)||DecodePayload(payload,true,&d));assert(d.valid&&d.mode==mode&&d.sequence==seq);
  for(int bit=0;bit<24;++bit){DecoderResult bad;const auto damaged=payload^(1u<<bit);assert(!DecodePayload(damaged,false,&bad)&&!DecodePayload(damaged,true,&bad));}
  ++protocols;
 }
 for(int mode=0;mode<5;++mode){
  for(unsigned seq:{0u,1u,101u,8191u}){
   constexpr int width=128,height=64;std::vector<std::uint8_t> pixels(width*height*4);
   const auto p=MakePayload(static_cast<TestMode>(mode),static_cast<std::uint16_t>(seq));
   for(int y=0;y<height;++y)for(int x=0;x<width;++x){int row=y/16,col=x/16;bool bit=row==0?kSyncBits[col]:((p>>(23-((row-1)*8+col)))&1);
    const std::uint8_t value=bit?190:38;for(int c=0;c<3;++c)pixels[(y*width+x)*4+c]=value;
   }
   auto d=DecodeMachineCode(pixels.data(),width,height);assert(d.valid&&d.sequence==seq&&static_cast<int>(d.mode)==mode);
   std::fill(pixels.begin(),pixels.end(),100);assert(!DecodeMachineCode(pixels.data(),width,height).valid);
  }
 }
 for(int scale:{1,2,3})for(int width=560*scale;width<=2560*scale;width+=61*scale)for(int height=300*scale;height<=1440*scale;height+=37*scale)for(int ratio=20;ratio<=90;++ratio)for(bool move:{false,true}){
  auto l=layout::Compute(width,height,106*scale,28*scale,16*scale,ratio,move,false);
  assert(l.code.right-l.code.left==2*(l.code.bottom-l.code.top));assert((l.code.right-l.code.left)%8==0);
  assert(l.code.left>=0&&l.code.top>=0&&l.code.right<=width&&l.code.bottom<=height);
  assert(l.value.right<=l.code.left||l.value.bottom<=l.code.top);
  if(move)assert(l.motion.top>=l.code.bottom&&l.motion.top>=l.value.bottom);
  ++layouts;
 }
 // FPS bundle layout fits all supported window/DPI/ratio combinations.
 for(testScale=1;testScale<=3;++testScale)for(int width=560*testScale;width<=3840*testScale;width+=113*testScale)for(int height=340*testScale;height<=2160*testScale;height+=83*testScale)for(g.codeRatio=20;g.codeRatio<=90;++g.codeRatio){
  const RECT client{0,0,width,height};auto r=FpsCodeRect(client);
  assert(r.left>=0&&r.right<=width&&r.top-Scale(25)>=SignalHeaderHeight()&&r.bottom<=height-SignalFooterHeight());
  assert((r.right-r.left)==4*(r.bottom-r.top)&&(r.right-r.left)%32==0&&(r.bottom-r.top)%8==0);++fpsLayouts;
 }
 // Chronological ring traversal, smooth cadence, and a known slow-tail sample.
 g.analyzer=AnalyzerStats{};g.analyzer.decodedMode=1;
 for(int i=0;i<350;++i)AddFrameTransitionLocked({1000000+i*16667LL,1});
 UpdateFrameTimingStatsLocked(1000000+349*16667LL);
 assert(std::fabs(g.analyzer.frameMedianMs-16.667)<0.001);assert(g.analyzer.recentStutters==0);
 assert(std::fabs(g.analyzer.onePercentLowFps-59.9988)<0.01);
 AddFrameTransitionLocked({1000000+349*16667LL+50000,3});UpdateFrameTimingStatsLocked(1000000+349*16667LL+50000);
 assert(g.analyzer.recentStutters==1&&g.analyzer.frameLongestMs==50.0);assert(g.analyzer.onePercentLowFps<59.0);
 for(int i=0;i<5000;++i)AddFrameTransitionLocked({10000000+i*2000LL,1});UpdateFrameTimingStatsLocked(10000000+4999*2000LL);
 assert(g.analyzer.frameMedianMs==2.0);
 // Baseline, latency, stopped recording, and generation invalidation.
 g.analyzer=AnalyzerStats{};g.recording=true;logs.clear();DecoderResult d{true,TestMode::Latency,5,1.0};
 ProcessDecodedResult(d,1000000,1);g.analyzer.pendingClick=true;g.analyzer.clickAt=1010000;d.sequence=6;
 ProcessDecodedResult(d,1060000,1);assert(g.analyzer.lastLatencyMs==50&&g.analyzer.latencyTotal==1&&logs.size()==1);
 g.recording=false;d.sequence=7;ProcessDecodedResult(d,1080000,1);assert(g.analyzer.latencyTotal==1&&logs.size()==1);
 g.recording=true;g.analyzer.pendingClick=true;g.analyzer.clickAt=1090000;d.sequence=8;ProcessDecodedResult(d,1110000,0);assert(g.analyzer.latencyTotal==1);
 DecoderResult invalid{};ProcessDecodedResult(invalid,4000000,1);assert(!g.analyzer.pendingClick&&g.analyzer.timedOutClicks==1);
 // Stream wrap: 8191 -> 0 is one new frame, not a burst of 8192.
 g.analyzer=AnalyzerStats{};d={true,TestMode::FrameRate,8191,1.0};ProcessDecodedResult(d,5000000,1);d.sequence=0;ProcessDecodedResult(d,5016667,1);
 assert(g.analyzer.frameTransitions==1&&g.analyzer.skippedFrameSlots==0);
 // Whole FPS telemetry raster, mismatched bookends, CRC damage and decoder gaps.
 {
  constexpr int width=256,height=64;
  std::vector<std::uint8_t> pixels(width*height*4);
  std::uint16_t fields[8]={8191,77,8000,4096,fps::Flags(true,1,5,2,true),167,30,8191};
  fields[7]=fps::BundleCheck(fields);
  auto raster=[&](){for(int y=0;y<height;++y)for(int x=0;x<width;++x){int tile=x/64+(y/32)*4,row=(y%32)/8,col=(x%64)/8;auto payload=MakePayload(TestMode::FpsView,fields[tile]);bool bit=row==0?kSyncBits[col]:((payload>>(23-((row-1)*8+col)))&1);for(int c=0;c<3;++c)pixels[(y*width+x)*4+c]=bit?200:35;}};
  raster();auto bundle=DecodeFpsBundle(pixels.data(),width,height);assert(bundle.valid&&bundle.telemetry&&bundle.fields[1]==77);
  assert((fields[4]>>10)==2);
  fields[4]=(fields[4]&1023)|1024;fields[7]=fps::BundleCheck(fields);raster();assert(!DecodeFpsBundle(pixels.data(),width,height).valid);
  fields[4]=(fields[4]&1023)|2048;fields[7]=fps::BundleCheck(fields);raster();assert(DecodeFpsBundle(pixels.data(),width,height).valid);
  for(int scene=0;scene<4;++scene)for(int pressure=0;pressure<3;++pressure){
   fields[4]=fps::Flags(true,scene,5,2,true,pressure);fields[7]=fps::BundleCheck(fields);raster();
   auto profile=DecodeFpsBundle(pixels.data(),width,height);assert(profile.valid);
   assert((profile.fields[4]>>10)==2+(scene<2?pressure:0));
  }
  fields[4]=fps::Flags(true,1,5,2,true);fields[7]=fps::BundleCheck(fields);raster();
  fields[1]=78;raster();assert(!DecodeFpsBundle(pixels.data(),width,height).valid);fields[1]=77;raster();
  for(int y=40;y<48;++y)for(int x=0;x<8;++x)for(int c=0;c<3;++c)pixels[(y*width+x)*4+c]=255-pixels[(y*width+x)*4+c];
  assert(!DecodeFpsBundle(pixels.data(),width,height).valid);
  g.analyzer=AnalyzerStats{};g.recording=true;logs.clear();
  ProcessDecodedResult(bundle,1000000,1);
  bundle.sequence=0;bundle.fields[0]=0;bundle.fields[7]=fps::BundleCheck(bundle.fields);ProcessDecodedResult(bundle,1016667,1);
  assert(g.analyzer.runFrames==1&&g.analyzer.renderedNotObserved==0&&g.analyzer.runIntervalTotal==1);
  DecoderResult bad{};ProcessDecodedResult(bad,1020000,1);
  bundle.sequence=2;bundle.fields[0]=2;bundle.fields[7]=fps::BundleCheck(bundle.fields);ProcessDecodedResult(bundle,1050000,1);
  assert(g.analyzer.unknownIntervals==1&&g.analyzer.runIntervalTotal==1&&g.analyzer.renderedNotObserved==0&&g.analyzer.runUnknownMs==30);
  bundle.sequence=3;bundle.fields[0]=3;bundle.fields[7]=fps::BundleCheck(bundle.fields);ProcessDecodedResult(bundle,1066667,1);
  assert(g.analyzer.runIntervalTotal==2);
  // Input event acknowledgement is separate from video frame sequence.
  g.analyzer.pendingMotion=true;g.analyzer.motionStartedAt=1070000;
  bundle.sequence=4;bundle.fields[0]=4;bundle.fields[1]=78;bundle.fields[7]=fps::BundleCheck(bundle.fields);
  ProcessDecodedResult(bundle,1110000,1);
  assert(g.analyzer.lastMotionLatencyMs==40&&g.analyzer.motionLatencyTotal==1);
  // Changing test mode ends the FPS run without overwriting its results.
  auto legacy=DecoderResult{true,TestMode::FrameRate,1,1.0};ProcessDecodedResult(legacy,1120000,1);
  assert(!g.recording.load()&&g.analyzer.decodedMode==4);
  assert(std::abs(RunPercentile(g.analyzer,.95)-43.25)<.5);
  for(const auto &row:logs)if(row.rfind("fps_frame",0)==0)assert(std::count(row.begin(),row.end(),',')==19);
  // Automatic sequence completion stops the run; a repeated final frame cannot append data.
  g.analyzer=AnalyzerStats{};g.recording=true;
  bundle.sequence=1;bundle.fields[0]=1;bundle.fields[4]=fps::Flags(false,1,3,2,false);bundle.fields[7]=fps::BundleCheck(bundle.fields);
  ProcessDecodedResult(bundle,2000000,1);
  bundle.sequence=2;bundle.fields[0]=2;bundle.fields[4]=fps::Flags(false,1,4,2,false);bundle.fields[7]=fps::BundleCheck(bundle.fields);
  ProcessDecodedResult(bundle,2016667,1);
  assert(!g.recording.load()&&g.analyzer.runFrames==1);
  const auto saved=logs.size();ProcessDecodedResult(bundle,2020000,1);assert(logs.size()==saved);
 }
 std::printf("PASS: %u protocol codes + every single-bit error; raster contrast; %u layouts; cadence, ring wrap, recording state and ROI generations.\n",protocols,layouts);
 std::printf("PASS: %u FPS bundle layouts; full telemetry integrity; decode-gap exclusion; 74ms tracking recovery and ambiguous tracking rejection.\n",fpsLayouts);
}
'''
with tempfile.TemporaryDirectory(prefix='cloudlatency-tests-') as tmp:
    cpp=Path(tmp)/'test.cpp';exe=Path(tmp)/'test'
    cpp.write_text(header+structs+stub+'\n'.join(function(n) for n in names)+body)
    subprocess.run(['g++','-std=c++17','-O2','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-I',str(root),str(cpp),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
