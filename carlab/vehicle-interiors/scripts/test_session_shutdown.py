"""Gracefully close only the isolated runtime when its owner requests shutdown."""
import unreal as u
from pathlib import Path
request=Path('/mnt/simulations/vehicle-interiors/.test-session-2230-stop')
def tick(delta):
 if request.exists():
  request.unlink()
  world=u.find_object(None,'/Game/Carla/Maps/Town10HD_Opt.Town10HD_Opt')
  if world:u.SystemLibrary.execute_console_command(world,'quit')
handle=u.register_slate_post_tick_callback(tick)
