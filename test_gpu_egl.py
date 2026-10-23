import ctypes as C,os,subprocess,re,tempfile,argparse,json,math
parser=argparse.ArgumentParser(description="Render actual Build/Paint using EGL on Linux; Windows/WGL lifecycle is not emulated.")
parser.add_argument("--gl-header",required=True)
parser.add_argument("--output",required=True)
parser.add_argument("--encode",action="store_true")
parser.add_argument("--resize",action="store_true")
args=parser.parse_args()
from pathlib import Path
os.environ['EGL_PLATFORM']='surfaceless'
e=C.CDLL('libEGL.so.1');gl=C.CDLL('libGL.so.1')
def bind(lib,n,rest,args):
 f=getattr(lib,n);f.restype=rest;f.argtypes=args;return f
ptr=C.c_void_p;I=C.c_int
getdisplay=bind(e,'eglGetDisplay',ptr,[ptr]);init=bind(e,'eglInitialize',I,[ptr,C.POINTER(I),C.POINTER(I)])
d=getdisplay(None);major=I();minor=I();assert init(d,C.byref(major),C.byref(minor))
choose=bind(e,'eglChooseConfig',I,[ptr,C.POINTER(I),C.POINTER(ptr),I,C.POINTER(I)])
attrs=(I*15)(0x3033,1,0x3040,8,0x3024,8,0x3023,8,0x3022,8,0x3025,24,0x3038,0,0)
config=ptr();num=I();assert choose(d,attrs,C.byref(config),1,C.byref(num)) and num.value
assert bind(e,'eglBindAPI',I,[I])(0x30A2)
context=bind(e,'eglCreateContext',ptr,[ptr,ptr,ptr,C.POINTER(I)])(d,config,None,(I*1)(0x3038));assert context
surface=bind(e,'eglCreatePbufferSurface',ptr,[ptr,ptr,C.POINTER(I)])(d,config,(I*5)(0x3057,960,0x3056,600,0x3038));assert surface
assert bind(e,'eglMakeCurrent',I,[ptr,ptr,ptr,ptr])(d,surface,surface,context)
root=Path(__file__).resolve().parent
output=Path(args.output).resolve();output.mkdir(parents=True,exist_ok=True)
text=(root/'fps_3d.inl').read_text();build=text[text.index('void Build('):]
build=build[:build.rfind('} // namespace gpu')]
glpath=Path(args.gl_header).resolve()
header='''
#define WINGDIAPI
#define APIENTRY
#define FALSE 0
#include "'''+str(glpath)+'''"
#include "fps_world.h"
#include "fps_stress.h"
#include "fps_detail.h"
struct RECT {long left,top,right,bottom;};
enum class TestMode{FpsView};
struct G {int fpsScene=1,fpsPressure=0;fps::Pose fpsPose;unsigned short fpsFields[8]{};}g;
constexpr int kSyncBits[]={1,0,1,0,0,1,0,1};
int Scale(int a){return a;}
unsigned MakePayload(TestMode,unsigned short s){
 unsigned short data=static_cast<unsigned short>(0xe000 | (s&8191));
 unsigned char crc=0;for(int i=15;i>=0;--i){bool top=(crc&128)!=0;crc<<=1;if(top != ((data>>i)&1))crc^=7;}
 return (static_cast<unsigned>(data)<<8)|crc;
}
namespace gpu {
struct State{int scene=-1,pressure=-1,dc=0;GLuint list=0,textures[4]{};world::Mesh mesh=world::Build();world::Camera camera;}state;
bool Ensure(const RECT&){return true;}
bool SwapBuffers(int){return true;}
'''
footer='''
}
extern "C" int render(double yaw,double pitch,double x,double z,int scene,int pressure){
 g.fpsPose={yaw,pitch,5};g.fpsScene=scene;g.fpsPressure=pressure;gpu::state.camera={x,z};
 return gpu::Paint(RECT{0,0,960,600},RECT{16,496,336,576});
}
extern "C" int render_size(int width,int height){
 return gpu::Paint(RECT{0,0,width,height},RECT{16,height-104,336,height-24});
}
'''
tmp=output/'gl_check.cpp';tmp.write_text(header+build+footer)
subprocess.run(['g++','-std=c++17','-fPIC','-shared','-O2','-I',str(root),str(tmp),'-l:libGL.so.1','-o',str(output/'gl_check.so')],check=True)
lib=C.CDLL(str(output/'gl_check.so'));render=bind(lib,'render',I,[C.c_double]*4+[I,I]);read=bind(gl,'glReadPixels',None,[I,I,I,I,I,I,ptr]);geterr=bind(gl,'glGetError',C.c_uint,[])
from PIL import Image
import numpy as np
images=[]
for i,pose in enumerate([(0,0,0,12,1,0),(25,0,0,12,1,0),(0,20,0,12,1,0),(0,0,2,12,1,0),(0,0,0,12,0,0),(0,0,0,12,0,1),(0,0,0,12,0,2),(25,10,0,12,1,2)]):
 assert render(*pose),hex(geterr())
 pixels=(C.c_ubyte*(960*600*3))();read(0,0,960,600,0x1907,0x1401,pixels);assert geterr()==0
 im=Image.frombytes('RGB',(960,600),bytes(pixels)).transpose(Image.Transpose.FLIP_TOP_BOTTOM)
 im.save(output/f'scene_{i}.png');images.append(np.array(im))
for i in range(1,len(images)):
 difference=np.mean(np.any(images[0][:480]!=images[i][:480],axis=2));assert difference>.25,(i,difference)
# Exact unfiltered marker and visible shaded geometry.
assert len(np.unique(images[0][:480].reshape(-1,3),axis=0))>500
assert np.all(images[0][498:504,18:24]==250)
print('PASS: actual OpenGL Build/Paint on EGL/Mesa; 8 views / 3 pressures, yaw/pitch/translation/grid, exact HUD pixels; no GL errors.')

# A stationary camera must return identical scene pixels at both high profiles.
for pressure in (1,2):
 assert render(15,5,0,12,1,pressure)
 a=(C.c_ubyte*(960*600*3))();read(0,0,960,600,0x1907,0x1401,a)
 assert render(15,5,0,12,1,pressure)
 b=(C.c_ubyte*(960*600*3))();read(0,0,960,600,0x1907,0x1401,b)
 assert bytes(a)==bytes(b)
if args.encode:
 results=[]
 for pressure in (0,1,2):
  path=output/f"pressure_{pressure}.mp4"
  command=['ffmpeg','-v','error','-f','rawvideo','-pixel_format','rgb24','-video_size','960x600','-framerate','60','-i','pipe:0','-an','-c:v','libx264','-preset','veryfast','-tune','zerolatency','-crf','23','-pix_fmt','yuv420p','-threads','2','-y',str(path)]
  proc=subprocess.Popen(command,stdin=subprocess.PIPE)
  for frame in range(120):
   assert render(frame*.35,math.sin(frame/30)*3,0,12,0,pressure)
   pixels=(C.c_ubyte*(960*600*3))();read(0,0,960,600,0x1907,0x1401,pixels)
   proc.stdin.write(bytes(pixels))
  proc.stdin.close();assert proc.wait()==0
  results.append({'pressure':pressure,'bytes':path.stat().st_size})
 (output/'encode_results.json').write_text(json.dumps(results,indent=2))
 print('Same quality encoding:',json.dumps(results))
 assert results[1]['bytes']>results[0]['bytes']*2,results
 assert results[2]['bytes']>results[0]['bytes']*2,results

if args.resize:
 render_size=bind(lib,'render_size',I,[I,I])
 create=bind(e,'eglCreatePbufferSurface',ptr,[ptr,ptr,C.POINTER(I)])
 make=bind(e,'eglMakeCurrent',I,[ptr,ptr,ptr,ptr])
 destroy=bind(e,'eglDestroySurface',I,[ptr,ptr])
 for width,height in [(720,400),(1920,1080),(3840,2160),(1920,1040),(720,400)]*2:
  assert make(d,None,None,None)
  assert destroy(d,surface)
  surface=create(d,config,(I*5)(0x3057,width,0x3056,height,0x3038));assert surface
  assert make(d,surface,surface,context)
  assert render_size(width,height),hex(geterr())
  marker=(C.c_ubyte*3)();read(20,100,1,1,0x1907,0x1401,marker)
  assert geterr()==0
 print('PASS: actual GL surface recreate/rebind and draws at 720x400, 1080p, 4K and restored sizes (10 transitions).')
