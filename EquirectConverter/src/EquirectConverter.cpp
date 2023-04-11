#include "EquirectConverter.h"

#include "Constants.h"
#include "Memory.h"
#include "File.h"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <string>
#include <vector>
#include <assert.h>
#include <cmath>
#include <iostream>

inline float clamp01(float x)
{
	return x < 0.f ? 0.f : x > 1.f ? 1.f : x;
}

void TexturePosToSphereCoord(int x, int y, int width, int height, float& theta, float& phi)
{
	const float u = static_cast<float>(x) / static_cast<float>(width);
	const float v = static_cast<float>(y) / static_cast<float>(height);

	theta = u * PI;
	phi = (v * PI / 2.f) - PI / 4.f;
}

void SphereCoordToTexturePos(float phi, float theta, int width, int height, int& x, int& y)
{
	x = static_cast<int>(theta / PI) * width;
	y = static_cast<int>((phi + PI / 4.f) / PI * 2.f) * height;
}

void GenerateEquirectangularCheckerboard(const int resolutionX, const int resolutionY)
{
	MemoryArena arena{};
	uint8_t* pixels = NewArray(arena, uint8_t, resolutionX * resolutionY * 3);

	for (int px = 0; px < resolutionX; px++)
	{
		for (int py = 0; py < resolutionY; py++)
		{
			if (px == 512 && py == 256)
				int a = 0;

			// map texture coordinate on equirectangular texture to sphere
			const float u = static_cast<float>(px) / static_cast<float>(resolutionX);
			const float v = static_cast<float>(py) / static_cast<float>(resolutionY);
			
			const float theta = u * 2.f * PI - PI;
			const float phi   = v *       PI - PI / 2.f;

			const float x = std::cos(theta) * std::cos(phi);
			const float y = std::sin(phi);
			const float z = std::sin(theta) * std::cos(phi);

			const int index = (py * resolutionX + px) * 3;
			float checkerboard = static_cast<int>(floorf(u * 8.f) + floorf(v * 8.f)) % 2 == 0 ? 0.f : 1.f;

			const float r = checkerboard;
			const float g = checkerboard;
			const float b = checkerboard;

			pixels[index + 0] = static_cast<uint8_t>(r * 255.f);
			pixels[index + 1] = static_cast<uint8_t>(g * 255.f);
			pixels[index + 2] = static_cast<uint8_t>(b * 255.f);
		}
	}

	stbi_write_png("checkerboard.png", resolutionX, resolutionY, 3, pixels, resolutionX * 3);
}

enum class ImageShape
{
	Regular,
	Equirect
};

enum class MipGenerationType
{
	Point,
	Box,
	Kaiser
};

// kaiser and bessel functions from
// https://computergraphics.stackexchange.com/questions/6393/kaiser-windowed-sinc-filter-for-mip-mapping
float BesselI0(float x)
{
	float r = 1.f;
	float term = 1.f;
	for (int k = 1; true; k++)
	{
		float f = x / static_cast<float>(k);
		term *= .25f * f * f;
		float new_r = r + term;
		if (new_r == r) break;
		r = new_r;
	}
	return r;
}

float KaiserWindow(float x)
{
	const float alpha = 4.f * PI;
	return BesselI0(alpha * std::sqrtf(1.f - x * x)) / BesselI0(alpha);
}

uint8_t* GenerateConventionalMipLevel(MemoryArena& arena, uint8_t* source, int sourceWidth, int sourceHeight, int channelCount, MipGenerationType type)
{
	int targetWidth = sourceWidth / 2;
	int targetHeight = sourceHeight / 2;

	uint8_t* mipMapData = NewArray(arena, uint8_t, targetWidth * targetHeight * channelCount);

	// Point-Filtering
	if (type == MipGenerationType::Point)
	{
		for (int y = 0; y < sourceHeight; y += 2)
		{
			for (int x = 0; x < sourceWidth; x += 2)
			{
				int sourceIndex = (y * sourceWidth + x) * channelCount;
				int targetIndex = ((y / 2) * targetWidth + (x / 2)) * channelCount;

				for (int c = 0; c < channelCount; c++)
				{
					mipMapData[targetIndex + c] = source[sourceIndex + c];
				}
			}
		}
	}

	// Box-Filtering
	else if (type == MipGenerationType::Box)
	{
		for (int y = 0; y < sourceHeight; y += 2)
		{
			for (int x = 0; x < sourceWidth; x += 2)
			{
				int sourceIndex0 = (y * sourceWidth + x) * channelCount;
				int sourceIndex1 = ((y + 1) * sourceWidth + x) * channelCount;
				int sourceIndex2 = (y * sourceWidth + x + 1) * channelCount;
				int sourceIndex3 = ((y + 1) * sourceWidth + x + 1) * channelCount;
				int targetIndex = ((y / 2) * targetWidth + (x / 2)) * channelCount;

				for (int c = 0; c < channelCount; c++)
				{
					mipMapData[targetIndex + c] = (source[sourceIndex0 + c]
						+ source[sourceIndex1 + c]
						+ source[sourceIndex2 + c]
						+ source[sourceIndex3 + c]) / 4;
				}
			}
		}
	}

	// TODO: Kaiser-Filtering
	else if (type == MipGenerationType::Kaiser)
	{
		
	}

	else { assert(false); return nullptr; }

	return mipMapData;
}

void AverageRect(float minX, float minY, float maxX, float maxY, int sourceWidth, int sourceHeight, int channelCount, uint8_t* sourceBase, uint8_t* target)
{
	if (minX < 0.) minX = 0.;
	if (maxX < 0.) maxX = 0.;
	if (minX >= sourceWidth) minX = sourceWidth - 1;
	if (maxX >= sourceWidth) maxX = sourceWidth - 1;
	if (minY < 0.) minY = 0.;
	if (maxY < 0.) maxY = 0.;
	if (minY >= sourceHeight) minY = sourceHeight - 1;
	if (maxY >= sourceHeight) maxY = sourceHeight - 1;

	for (int c = 0; c < channelCount; c++)
	{
		int channelSum = 0;
		int pixelCount = 0;

		// TODO: this currently floors the area, which isn't perfect if we want to average a 1.5px wide area for example.
		for (int y = static_cast<int>(minY); y < static_cast<int>(maxY); y++)
		{
			for (int x = static_cast<int>(minX); x < static_cast<int>(maxX); x++)
			{
				channelSum += sourceBase[(y * sourceWidth + x) * channelCount + c];
				pixelCount++;
			}
		}

		if (pixelCount == 0)
		{
			target[c] = sourceBase[(static_cast<int>(minY) * sourceWidth + static_cast<int>(minX)) * channelCount + c];
		}
		else
		{
			target[c] = channelSum / pixelCount;
		}
	}
}

uint8_t* GenerateEquirectMipLevel(MemoryArena& arena, uint8_t* source, int sourceWidth, int sourceHeight, int channelCount, MipGenerationType type)
{
	int targetWidth = sourceWidth / 2;
	int targetHeight = sourceHeight / 2;

	uint8_t* mipMapData = NewArray(arena, uint8_t, targetWidth * targetHeight * channelCount);

	// Point-Filtering (no difference)
	if (type == MipGenerationType::Point)
	{
		for (int y = 0; y < sourceHeight; y += 2)
		{
			for (int x = 0; x < sourceWidth; x += 2)
			{
				int sourceIndex = (y * sourceWidth + x) * channelCount;
				int targetIndex = ((y / 2) * targetWidth + (x / 2)) * channelCount;

				for (int c = 0; c < channelCount; c++)
				{
					mipMapData[targetIndex + c] = source[sourceIndex + c];
				}
			}
		}
	}

	// Box-Filtering
	if (type == MipGenerationType::Box)
	{
		for (int y = 0; y < sourceHeight; y++)
		{
			for (int x = 0; x < sourceWidth; x++)
			{
				float theta;
				float phi;
				TexturePosToSphereCoord(x, y, sourceWidth, sourceHeight, theta, phi);
				float circumferenceRatio = std::abs(std::cos(phi * 2.f)); // *2 why?

				float verticalPixels = 1.f;
				float horizontalPixels = std::min(1.f / circumferenceRatio, static_cast<float>(sourceWidth));

				int targetIndex = ((y / 2) * targetWidth + (x / 2)) * channelCount;
				AverageRect(x, y, x + horizontalPixels, y + verticalPixels, sourceWidth, sourceHeight, channelCount, source, &mipMapData[targetIndex]);
			}
		}
	}

	else { assert(false); return nullptr; }

	return mipMapData;
}

void GenerateMipMap(const char* sourcePath, const char* targetPathPrefix, MipGenerationType type, ImageShape shape)
{
	MemoryArena mipMemory{};

	int width;
	int height;
	int originalChannelCount;
	int channelCount = 4;

	uint8_t* imageData = stbi_load(sourcePath, &width, &height, &originalChannelCount, channelCount);
	if (imageData == nullptr)
	{
		std::cout << stbi_failure_reason() << std::endl;
		return;
	}
	size_t imageDataSize = width * height * channelCount;

	uint8_t* level0 = NewArray(mipMemory, uint8_t, imageDataSize);
	memcpy(level0, imageData, imageDataSize);

	std::string pathPrefix{ targetPathPrefix };
	std::string path0 = pathPrefix + "0.png";
	PrepareFileWrite(path0);
	bool writeResult = stbi_write_png(path0.c_str(), width, height, channelCount, level0, 0);
	assert(writeResult && "Failed to write image!");
	
	int mipSourceWidth = width;
	int mipSourceHeight = height;
	uint8_t* mipSource = level0;

	int mipLevel = 0;
	while (mipSourceWidth >= 2 && mipSourceHeight >= 2)
	{
		if (shape == ImageShape::Regular)
		{
			mipSource = GenerateConventionalMipLevel(mipMemory, mipSource, mipSourceWidth, mipSourceHeight, channelCount, type);
		}
		else if (shape == ImageShape::Equirect)
		{
			mipSource = GenerateEquirectMipLevel(mipMemory, mipSource, mipSourceWidth, mipSourceHeight, channelCount, type);
		}
		else { assert(false); return; }

		mipSourceWidth /= 2;
		mipSourceHeight /= 2;
		mipLevel++;
		
		std::string path = pathPrefix + std::to_string(mipLevel) + ".png";
		PrepareFileWrite(path);
		bool writeResult = stbi_write_png(path.c_str(), mipSourceWidth, mipSourceHeight, channelCount, mipSource, 0);
		assert(writeResult && "Failed to write image!");
	}

	std::string ddsPath = pathPrefix + "result.dds";
	WriteDDS(ddsPath, width, height, mipLevel, mipMemory.base, mipMemory.used);
	
	stbi_image_free(imageData);
}

int main(int argc, char* argv[])
{
	//GenerateEquirectangularCheckerboard(1024, 512);
	GenerateMipMap("textures/Wolfstein.jpg", "textures/eq-box/out-eq-box-", MipGenerationType::Box, ImageShape::Equirect);
	return 0;
}