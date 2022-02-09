# simpler-cam
Attempt at the most basic functional example of a camera app based on libcamera.
Based on simple-cam, RaspberryPi's libcamera-apps, with library support from libcamera and libpng.
Differences:
* no event loop - just a simple semaphore-post to have the callback wake a wait-sleeping main thread.
* no Mason / Ninja, just a simple Makefile.
* no command line arguments, edit the code and rebuild to adjust your camera settings.
* no auto WB / auto EG / etc. fixed settings.
* settings set on camera start, tries to be as quick as possible.
* saves a png file. No mjpeg or other compression artifacts.
* just works.

The questions are; could it be more simple? Could it be made faster?
