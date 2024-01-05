# OpenXRViewer

A 360° photo viewer for my master's thesis 'Enhancing Stereoscopic Image Quality in Virtual Reality by Means of High-Fidelity Texture Filtering'. It implements different texture sampling methods to compare the effects of sampling equirectangular textures in regards to aliasing effects.

You can read the full thesis [here](https://stuff.theasuro.de/Master_Thesis-final.pdf).

Example with checkerboard texture when looking down in VR:

![checker-default](https://github.com/tillwuebbers/OpenXRViewer/assets/43892883/e2efc89a-d1be-480d-8887-20a236a00f02)

Using the custom texture sampling method:

![checker-adjusted](https://github.com/tillwuebbers/OpenXRViewer/assets/43892883/3cd1154d-d002-4d30-99b1-728f467fa08f)

The impact on real images is measurable, but creates a weaker visual impact than expected. The thesis goes into more detail, and includes other methods of improving visual quality.

## Code

The code is based on the OpenXR SDK demo (https://github.com/KhronosGroup/OpenXR-SDK-Source/tree/main/src/tests/hello_xr). It was created in a rush and is pretty much not documented beyond what was already there.

- OpenXRViewer/xr_demo/graphicsplugin_custom.cpp contains the custom rendering code.
- The EquirectConverter directory contains tools for creating adjusted mip-maps for equirectangular images, as well as other helper functions.
