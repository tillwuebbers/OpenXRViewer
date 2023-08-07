from SSIM_PIL import compare_ssim
from PIL import Image

def loadImage(name):
	path = "../out/build/x64-debug/OpenXRViewer/" + name
	return Image.open(path)

imgRay = loadImage("comparison_perfect_raytraced.png")
imgCube = loadImage("comparison_cubemap.png")
imgSphereDefault = loadImage("comparison_sphere_high_def.png")
imgSphereAdjusted = loadImage("comparison_sphere_high_adj.png")

def printComparison(name, img):
	print(name + ": " + str(compare_ssim(imgRay, img)))

printComparison("Cubemap", imgCube)
printComparison("Sphere Default", imgSphereDefault)
printComparison("Sphere Adjusted", imgSphereAdjusted)