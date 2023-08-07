from PIL import Image
import numpy as np
from skimage.metrics import structural_similarity
from skimage.metrics import mean_squared_error
from skimage.metrics import peak_signal_noise_ratio


def loadImage(name):
	path = "../out/build/x64-debug/OpenXRViewer/" + name
	return Image.open(path).convert('L')

imgRay = loadImage("comparison_perfect_raytraced.png")
imgCube = loadImage("comparison_cubemap.png")
imgSphereDefault = loadImage("comparison_sphere_high_def.png")
imgSphereAdjusted = loadImage("comparison_sphere_high_adj.png")

def SSIM(img):
	return structural_similarity(np.asfarray(imgRay), np.asfarray(img), multichannel=True, gaussian_weights=True, sigma=1.5, use_sample_covariance=False, data_range=255)

def MSE(img):
	return mean_squared_error(np.asfarray(imgRay), np.asfarray(img))

def PSNR(img):
	return peak_signal_noise_ratio(np.asfarray(imgRay), np.asfarray(img), data_range=255)

def prepResult(value):
	return str(round(value, 4))

def printResults(func, name):
	print(name + " & " + prepResult(func(imgCube)) + " & " + prepResult(func(imgSphereDefault)) + " & " + prepResult(func(imgSphereAdjusted)) + " \\\\")

print(" & Cubemap & Sphere Default & Sphere Adjusted \\\\")
print("\\midrule")
printResults(SSIM, "SSIM")
printResults(MSE, "MSE")
printResults(PSNR, "PSNR")