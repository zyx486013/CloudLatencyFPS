#!/usr/bin/env python3
"""Actual resize/dispatch functions with synchronous Win32 mocks, plus real
Windows x64 compiler stack frames. Does not emulate a Windows graphics driver.
"""
from pathlib import Path
import argparse,subprocess,tempfile,re,os,zipfile
root=Path(__file__).resolve().parent
parser=argparse.ArgumentParser()
parser.add_argument('--toolchain',default=os.environ.get('LLVM_MINGW_DIR'))
parser.add_argument('--baseline',type=Path)
args=parser.parse_args()
def function(text,name):
 m=re.search(r'^\w[\w:<>&* ]*\b'+name+r'\([^;]*?\)\s*\{',text,re.M)
 assert m,name
 a=text.index('{',m.start());i=a+1;depth=1
 while depth:
  depth+=(text[i]=='{')-(text[i]=='}');i+=1
 return text[m.start():i]
code=r'''
#include <cassert>
#include <cstdio>
using HWND=void*;using HDC=void*;using HGLRC=void*;using UINT=unsigned;
constexpr bool FALSE=false;
constexpr UINT SWP_NOACTIVATE=16,SWP_NOZORDER=4,SWP_NOREDRAW=8,SWP_NOCOPYBITS=256,SWP_DEFERERASE=8192,SWP_SHOWWINDOW=64;
struct RECT{long left,top,right,bottom;};struct POINT{long x,y;};
struct App{HWND hwnd=reinterpret_cast<HWND>(1);bool paintActive=false,paintAgain=false;}g;
struct State{HWND window=reinterpret_cast<HWND>(2);HDC dc=reinterpret_cast<HDC>(3);HGLRC context=reinterpret_cast<HGLRC>(4);}state;
RECT rect{0,144,720,400};bool visible=true,bound=true,rectOK=true,releaseOK=true,resizeOK=true,bindOK=true;
int resizeCalls=0,bindCalls=0,renderCalls=0,insidePaint=0,invalidations=0,validations=0;bool injectNested=false;
bool GetWindowRect(HWND,RECT* out){*out=rect;return rectOK;}
bool ScreenToClient(HWND,POINT*){return true;}
bool IsWindowVisible(HWND){return visible;}
bool wglMakeCurrent(HDC dc,HGLRC context){
 if(!dc&&!context){if(!releaseOK)return false;bound=false;return true;}
 ++bindCalls;assert(dc==state.dc&&context==state.context);bound=bindOK;return bindOK;
}
bool SetWindowPos(HWND,void*,long x,long y,long w,long h,UINT flags){
 assert(!bound);assert((flags&(SWP_NOREDRAW|SWP_NOCOPYBITS|SWP_DEFERERASE))==(SWP_NOREDRAW|SWP_NOCOPYBITS|SWP_DEFERERASE));
 ++resizeCalls;if(!resizeOK)return false;rect={x,y,x+w,y+h};if(flags&SWP_SHOWWINDOW)visible=true;return true;
}
void DispatchPaint(HWND);
void ValidateRect(HWND,void*){++validations;}
void InvalidateRect(HWND,void*,bool){++invalidations;}
void PaintWindow(HWND hwnd){
 assert(g.paintActive);assert(++insidePaint==1);++renderCalls;
 if(injectNested){injectNested=false;for(int i=0;i<8;++i)DispatchPaint(hwnd);}
 --insidePaint;
}
'''
code+=function((root/'fps_3d.inl').read_text(),'BindSurface')+'\n'
code+=function((root/'CloudLatencyFPS.cpp').read_text(),'DispatchPaint')+'\n'
code+=r'''
int main(){
 const RECT sizes[]={{0,144,720,400},{0,144,1920,1040},{0,40,1920,1080},{0,80,3840,2160},{0,288,1408,840}};
 for(int cycle=0;cycle<100;++cycle)for(auto r:sizes){
  assert(BindSurface(r));assert(bound);assert(rect.left==r.left&&rect.top==r.top&&rect.right==r.right&&rect.bottom==r.bottom);
 }
 int count=resizeCalls;assert(BindSurface(sizes[4]));assert(resizeCalls==count);
 assert(!BindSurface({0,0,0,0}));assert(!BindSurface({0,200,720,100}));
 releaseOK=false;assert(!BindSurface(sizes[0]));assert(resizeCalls==count);releaseOK=true;
 resizeOK=false;assert(!BindSurface(sizes[0]));resizeOK=true;
 assert(BindSurface(sizes[0]));bindOK=false;assert(!BindSurface(sizes[0]));bindOK=true;
 visible=false;assert(BindSurface(sizes[0]));assert(visible);
 rectOK=false;assert(!BindSurface(sizes[0]));rectOK=true;
 injectNested=true;DispatchPaint(g.hwnd);assert(renderCalls==1&&validations==8&&invalidations==1);
 assert(!g.paintActive&&!g.paintAgain&&insidePaint==0);
 DispatchPaint(g.hwnd);assert(renderCalls==2&&invalidations==1);
 std::puts("PASS: 500 window/maximized/fullscreen/DPI resize transitions; unbind-before-resize, failures, nested paints coalesced.");
}
'''
with tempfile.TemporaryDirectory() as directory:
 tmp=Path(directory);(tmp/'check.cpp').write_text(code)
 subprocess.run(['g++','-std=c++17','-O2','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(tmp/'check.cpp'),'-o',str(tmp/'check')],check=True)
 subprocess.run([str(tmp/'check')],check=True,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0'})
 if args.toolchain:
  compiler=Path(args.toolchain)/'bin/x86_64-w64-mingw32-clang++'
  def frames(source,out):
   subprocess.run([str(compiler),str(source),'-std=c++17','-O2','-DWINVER=0x0A00','-D_WIN32_WINNT=0x0A00','-DUNICODE','-D_UNICODE','-S','-o',str(out)],check=True)
   result={}
   for m in re.finditer(r'\.seh_proc ([^\n]+)(.*?)(?=\.seh_endproc)',out.read_text(),re.S):
    size=re.search(r'\.seh_stackalloc\s+(\d+)',m[2])
    if size:result[m[1]]=int(size[1])
   return result
  fixed=frames(root/'CloudLatencyFPS.cpp',tmp/'fixed.s')
  callback=next(v for k,v in fixed.items() if 'AppWindowProc' in k)
  painter=next(v for k,v in fixed.items() if '11PaintWindow' in k)
  assert callback<=4096 and painter<=4096,(callback,painter)
  print(f'PASS: actual Windows x64 callback stack {callback} bytes; PaintWindow {painter} bytes (budgets 4096).')
  if args.baseline:
   old=tmp/'baseline';old.mkdir()
   with zipfile.ZipFile(args.baseline) as z:
    for n in z.namelist():
     if Path(n).suffix in {'.cpp','.h','.inl'}:(old/Path(n).name).write_bytes(z.read(n))
   baseline=frames(old/'CloudLatencyFPS.cpp',tmp/'old.s')
   before=next(v for k,v in baseline.items() if 'AppWindowProc' in k)
   assert before>65536,before
   print(f'Baseline regression reproduced: old callback {before} bytes exceeds stack budget; fixed {callback}.')
