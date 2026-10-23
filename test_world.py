#!/usr/bin/env python3
"""Native geometry/camera tests, independent of Windows and graphics driver."""
from pathlib import Path
import subprocess,tempfile,os
root=Path(__file__).resolve().parent
source=r'''
#include "fps_world.h"
#include <cassert>
#include <cstdio>
int main(){
 auto standard=world::BuildPressure(0),high=world::BuildPressure(1),extreme=world::BuildPressure(2);
 assert(high.faces.size()>standard.faces.size()*2);
 assert(extreme.faces.size()>high.faces.size()*2);
 assert(high.solids.size()==standard.solids.size()&&extreme.solids.size()==standard.solids.size());
 for(const auto &face:extreme.faces)for(const auto &v:face.v)assert(std::isfinite(v.u)&&std::isfinite(v.v));
 std::printf("Pressure geometry quads: %zu / %zu / %zu\n",standard.faces.size(),high.faces.size(),extreme.faces.size());
 auto m=world::Build();assert(m.faces.size()>200);assert(world::Free(m,0,12));
 for(const auto &f:m.faces){
  assert(f.material>=0 && f.material<4);
  for(const auto &v:f.v)assert(std::isfinite(v.x)&&std::isfinite(v.u));
  auto a=f.v[0],b=f.v[1],c=f.v[2];
  double ux=b.x-a.x,uy=b.y-a.y,uz=b.z-a.z,vx=c.x-a.x,vy=c.y-a.y,vz=c.z-a.z;
  assert(std::hypot(uy*vz-uz*vy,std::hypot(uz*vx-ux*vz,ux*vy-uy*vx))>0);
 }
 world::Camera c{};world::Move(m,c,0,1,0,.05);assert(c.z<12&&c.x==0);
 c={};world::Move(m,c,90,1,0,.05);assert(c.x>0&&std::abs(c.z-12)<1e-8);
 c={};world::Move(m,c,0,0,1,.05);assert(c.x>0&&c.z==12);
 c={};world::Move(m,c,0,1,1,.05);assert(std::abs(std::hypot(c.x,c.z-12)-.2)<1e-8);
 c={};for(int i=0;i<10000;++i)world::Move(m,c,90,1,0,.05);assert(c.x<=21.3);
 c.x=4;c.z=8;for(int i=0;i<100;++i)world::Move(m,c,90,1,0,.05);
 assert(c.x<4.25);assert(world::Free(m,c.x,c.z));
 c={};world::Move(m,c,0,1,0,50);assert(std::abs(c.z-11.8)<1e-8);
 // Camera view transform: positive yaw turns right; positive pitch looks down.
 const double a=30*world::kPi/180.;
 assert(std::abs(std::cos(a)*std::sin(a)+std::sin(a)*-std::cos(a))<1e-9);
 assert(std::abs(std::cos(a)*-std::sin(a)-std::sin(a)*-std::cos(a))<1e-9);
 std::printf("PASS: %zu textured quads, camera axes, diagonal speed, collision, frame-gap clamp.\n",m.faces.size());
}'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.cpp').write_text(source)
 subprocess.run(['g++','-std=c++17','-O2','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I',str(root),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True,env={**os.environ,"ASAN_OPTIONS":"detect_leaks=0"})
