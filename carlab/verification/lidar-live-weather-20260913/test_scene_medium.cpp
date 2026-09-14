#include "PhysicalLidarModel.h"
#include "LidarSceneWeather.h"
#include <cassert>
#include <iostream>
using namespace CarlaPhysicalLidar;
int main() {
  Receiver p;p.shot_noise=false;p.false_alarms=false;p.range_noise_floor_m=0;
  for(double density : {0.,25.,50.,75.,100.}) {
    const double sigma=CarlaLidarSceneWeather::fog_extinction(density);
    const double ue=CarlaLidarSceneWeather::unreal_fog_density(density);
    assert(std::abs(std::exp(-sigma*20)-std::exp2(-ue/1000*2000))<1e-12);
  }
  double previous=1;
  for(double density : {0.,25.,50.,75.,100.}) {
    const double sigma=CarlaLidarSceneWeather::fog_extinction(density);
    std::vector<Echo> echoes{{20,.3,Surface,1}};Random weather(42),receiver(42);
    propagate_medium(echoes,{{0,80,sigma,0}},80,1,weather);
    assert(echoes[0].response<=previous);previous=echoes[0].response;
    auto detected=detect(echoes,p,receiver);
    std::cout<<density<<" sigma="<<sigma<<" fraction="<<echoes[0].response<<" surface_returns="<<detected.size()<<"\n";
    if(density==0)assert(detected.size()==1);
    if(density==100)assert(detected.empty());
  }
  std::vector<Echo> clear{{20,.3,Surface,1}},distant=clear;Random r(123);
  propagate_medium(distant,{{100,150,2,.3}},80,1,r);
  assert(distant.size()==1&&distant[0].response==clear[0].response);
  int fog_returns=0;
  for(int i=0;i<1000;++i) {
    std::vector<Echo> echoes;Random weather(i),receiver(i);
    propagate_medium(echoes,{{0,80,2,.3}},80,1,weather);
    for(const auto& hit:detect(echoes,p,receiver)){assert(hit.flags&Atmosphere);++fog_returns;}
  }
  assert(fog_returns>0);
  assert(CarlaLidarSceneWeather::rain_extinction(CarlaLidarSceneWeather::rain_mm_h(100))>0);
  assert(CarlaLidarSceneWeather::fog_extinction(-10)==0);
  assert(CarlaLidarSceneWeather::fog_extinction(200)==CarlaLidarSceneWeather::fog_extinction(100));
  std::cout<<"PASS clear/dense/monotonic/fog-start/backscatter/rain/bounds; fog returns "<<fog_returns<<"\n";
}
