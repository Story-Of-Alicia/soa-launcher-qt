## Linux

### The game shows up with missing textures and models:
This primarily happens when the game is running on system wine and is more likely to trigger if you have an NVIDIA GPU as well.
In the launcher do reset installation and then select either GE or UMU proton runtime.
**DW-proton is known to currently cause issues with the game, do not try that.**

### Launcher is stuck on waiting for browser auth:
Currently flatpak browsers cause it to get stuck due to system handler failing to call back. Use a non-flatpak browser from your distro's repository.

## MacOS

### Game crashes instantly on startup:
Open the terminal and paste 
```sh 
env SOA_WINE_DLL_OVERRIDES='winegstreamer=' \
  "/Applications/soa_launcher.app/Contents/MacOS/soa_launcher"
```
The launcher will open and opening the game should start as intended as well.

### Launcher is unable to find the Game Porting Toolkit in the runtime section:

Make sure the Game Porting Toolkit is in ./Applications so that the launcher automatically finds it. Otherwise, point the launcher manually to it.