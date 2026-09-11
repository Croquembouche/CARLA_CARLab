#!/usr/bin/env python3
"""Check changed translation units using this build's real Unreal compiler flags."""
from pathlib import Path
import concurrent.futures
import json
import shlex
import subprocess
import sys
ue=Path('/mnt/simulations/UnrealEngine5_carla/Engine')
plugin=Path('/mnt/simulations/carla/Unreal/CarlaUnreal/Plugins/Carla')
build=plugin/'Intermediate/Build/Linux/x64/UnrealEditor/Development/Carla'
args=shlex.split((build/'Module.Carla.1.cpp.o.rsp').read_text())
out=[]
it=iter(args)
for arg in it:
    if arg == '-include-pch':
        next(it); out.extend(['-include',str(build/'PCH.Carla.h')]); continue
    if arg == '-o': next(it); continue
    if arg in ('-c','-MD','-fpch-validate-input-files-content') or arg.startswith('-MF') or arg.endswith('/Module.Carla.1.cpp'): continue
    out.append(arg)
compiler=ue/'Extras/ThirdPartyNotUE/SDKs/HostLinux/Linux_x64/v23_clang-18.1.0-rockylinux8/x86_64-unknown-linux-gnu/bin/clang++'
files=sys.argv[1:] or ['Sensor/GpuSensorDispatcher.cpp','Sensor/GpuSensorSelfTest.cpp','Sensor/RayCastLidar.cpp',
       'Sensor/RayCastSemanticLidar.cpp','Sensor/Radar.cpp','Sensor/ImageUtil.cpp','Game/CarlaEngine.cpp','Carla.cpp',
       'Sensor/SceneCaptureCamera.cpp','Sensor/DepthCamera.cpp','Sensor/NormalsCamera.cpp',
       'Sensor/SemanticSegmentationCamera.cpp','Sensor/InstanceSegmentationCamera.cpp','Sensor/OpticalFlowCamera.cpp']
def run(name):
    log=Path('/mnt/simulations/carla/carlab/host-setup/logs')/(Path(name).name+'.carla-syntax.log')
    command=[str(compiler),*out,'-fsyntax-only','-fno-color-diagnostics',str(plugin/'Source/Carla'/name)]
    with log.open('w') as handle:
        result=subprocess.run(command,cwd=ue/'Source',stdout=handle,stderr=subprocess.STDOUT)
    print(name,result.returncode,flush=True)
    return {'file':name,'exit_code':result.returncode,'log':str(log)}
with concurrent.futures.ThreadPoolExecutor(max_workers=2) as executor:
    results=list(executor.map(run,files))
report=Path('/mnt/simulations/verification/gpu-cpp-syntax.json')
if sys.argv[1:] and report.exists():
    previous={x['file']:x for x in json.loads(report.read_text())}
    previous.update({x['file']:x for x in results})
    report.write_text(json.dumps(list(previous.values()),indent=2))
else:
    report.write_text(json.dumps(results,indent=2))
raise SystemExit(any(x['exit_code'] for x in results))
