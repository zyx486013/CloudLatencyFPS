#!/usr/bin/env python3
"""Test actual cached-texture painter. --encode also compares optional libx264 CRF23 clips.
This tests CPU-rendered pixel content, not Windows GDI speed or hardware streaming encoders.
"""
from pathlib import Path
import argparse
import json
import subprocess
import tempfile

root = Path(__file__).resolve().parent
parser = argparse.ArgumentParser()
parser.add_argument('--encode', action='store_true')
args = parser.parse_args()
source = (root / 'fps_signal.inl').read_text()
start = source.index('void PaintFpsTexture(')
end = source.index('void PaintFpsSignal(', start)
painter = source[start:end]
header = r'''
#include "fps_stress.h"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
using HDC=int;
struct RECT {long left,top,right,bottom;};
struct State {int fpsScene=0,textureDc=0;fps::Pose fpsPose{};} g;
constexpr int SRCCOPY=0;
int screenW=0,screenH=0,calls=0;
bool render=false;
std::vector<std::uint32_t> atlas;
std::vector<unsigned char> pixels,covered;
void BitBlt(HDC,int x,int y,int w,int h,HDC,int sx,int sy,int) {
 assert(w>0&&h>0&&x>=0&&y>=0&&x+w<=screenW&&y+h<=screenH);
 const int aw=fps::TextureWidth(g.fpsScene), ah=fps::TextureHeight(g.fpsScene);
 assert(sx>=0&&sy>=0&&sx+w<=aw&&sy+h<=ah);++calls;
 for(int j=0;j<h;++j)for(int i=0;i<w;++i){
  const int target=(y+j)*screenW+x+i;
  if(!render){assert(!covered[target]);covered[target]=1;}
  else {const auto c=atlas[(sy+j)*aw+sx+i];pixels[target*3]=c>>16;pixels[target*3+1]=c>>8;pixels[target*3+2]=c;}
 }
}
'''
body = r'''
int main(int argc,char**argv){
 if(argc==1){
  for(int scene=0;scene<4;++scene){
   g.fpsScene=scene;assert(((fps::Flags(false,scene,1,2,false)>>1)&3)==scene);
   for(int w:{13,560,961,1920})for(int h:{11,340,541,1080})for(double yaw:{-360.0,-.1,0.0,45.0,359.9}){
    screenW=w;screenH=h;covered.assign(w*h,0);g.fpsPose={yaw,-80,1};calls=0;
    PaintFpsTexture(0,{0,0,w,h});assert(std::all_of(covered.begin(),covered.end(),[](auto v){return v==1;}));
    assert(calls<=128);
   }
   for(int col=0;col<8;++col)for(int row=0;row<4;++row){
    const auto a=fps::TileOffset(scene,col,row,17,-12),b=fps::TileOffset(scene,col,row,17,-12);
    assert(a.x==b.x&&a.y==b.y);
    if(scene==3){const auto moving=fps::TileOffset(scene,col,row,17.1,-12);assert(a.x!=moving.x||a.y!=moving.y);}
   }
  }
  std::puts("PASS: all 4 scene flags; actual painter bounds, gap-free coverage, copy budget; moving noise changes and stopped noise freezes.");return 0;
 }
 g.fpsScene=std::atoi(argv[1]);render=true;screenW=960;screenH=540;
 const int aw=fps::TextureWidth(g.fpsScene),ah=fps::TextureHeight(g.fpsScene);
 atlas.resize(aw*ah);for(int y=0;y<ah;++y)for(int x=0;x<aw;++x)atlas[y*aw+x]=fps::ScenePixel(g.fpsScene,x,y);
 pixels.resize(screenW*screenH*3);
 for(int i=0;i<120;++i){g.fpsPose=fps::Automatic(10+i/60.0);PaintFpsTexture(0,{0,0,screenW,screenH});
  if(std::fwrite(pixels.data(),1,pixels.size(),stdout)!=pixels.size())return 2;
 }
}
'''
with tempfile.TemporaryDirectory(prefix='cloudlatency-stress-') as tmp:
    cpp = Path(tmp) / 'stress.cpp'
    exe = Path(tmp) / 'stress'
    cpp.write_text(header + painter + body)
    subprocess.run(['g++', '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-I', str(root), str(cpp), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
    if args.encode:
        sizes = {}
        for scene in (1, 2, 3):
            output = Path(tmp) / f'{scene}.h264'
            producer = subprocess.Popen([str(exe), str(scene)], stdout=subprocess.PIPE)
            encoded = subprocess.run(['ffmpeg', '-hide_banner', '-loglevel', 'error', '-f', 'rawvideo', '-pixel_format', 'rgb24', '-video_size', '960x540', '-framerate', '60', '-i', 'pipe:0', '-frames:v', '120', '-c:v', 'libx264', '-preset', 'veryfast', '-tune', 'zerolatency', '-crf', '23', '-pix_fmt', 'yuv420p', '-threads', '2', '-g', '120', str(output)], stdin=producer.stdout)
            producer.stdout.close()
            if producer.wait() or encoded.returncode:
                raise RuntimeError('Clip generation/encoding failed')
            sizes[scene] = output.stat().st_size
        assert sizes[2] > sizes[1] * 2, sizes
        assert sizes[3] > sizes[2] * 1.5, sizes
        print(json.dumps({'test': '960x540 / 60fps / 2s slow pan; software libx264 veryfast zerolatency CRF23', 'bytes': sizes, 'high_vs_old': round(sizes[2]/sizes[1], 2), 'extreme_vs_old': round(sizes[3]/sizes[1], 2)}, ensure_ascii=False))
