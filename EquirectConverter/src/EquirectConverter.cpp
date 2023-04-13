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

float clampMinMax(float x, float min, float max)
{
	assert(min <= max);
	if (x < min) return min;
	else if (x > max) return max;
	else return x;
}

int clampMinMax(int x, int min, int max)
{
	assert(min <= max);
	if (x < min) return min;
	else if (x > max) return max;
	else return x;
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
	minX = clampMinMax(minX, 0.f, static_cast<float>(sourceWidth));
	maxX = clampMinMax(maxX, 0.f, static_cast<float>(sourceWidth));
	minY = clampMinMax(minY, 0.f, static_cast<float>(sourceHeight));
	maxY = clampMinMax(maxY, 0.f, static_cast<float>(sourceHeight));

	assert(minX < maxX && minY < maxY);

	int minIntX = static_cast<int>(std::ceilf(minX));
	int minPartX = static_cast<int>(std::floorf(minX));

	int minIntY = static_cast<int>(std::ceilf(minY));
	int minPartY = static_cast<int>(std::floorf(minY));

	int maxIntX = static_cast<int>(std::floorf(maxX));
	int maxPartX = static_cast<int>(std::ceilf(maxX));

	int maxIntY = static_cast<int>(std::floorf(maxY));
	int maxPartY = static_cast<int>(std::ceilf(maxY));

	auto readSource = [&](int x, int y, int c) { return sourceBase[(y * sourceWidth + x) * channelCount + c]; };

	for (int c = 0; c < channelCount; c++)
	{
		float channelSum = 0;

		// Add whole pixels
		for (int y = minIntY; y < maxIntY; y++)
		{
			for (int x = minIntX; x < maxIntX; x++)
			{
				channelSum += readSource(x, y, c);
			}
		}

		// Add partial pixels
		float minPartSizeX = minIntX - minX;
		float minPartSizeY = minIntY - minY;
		float maxPartSizeX = maxX - maxIntX;
		float maxPartSizeY = maxY - maxIntY;

		// Corners
		channelSum += readSource(minPartX, minPartY, c) * minPartSizeX * minPartSizeY;
		channelSum += readSource(maxPartX, minPartY, c) * maxPartSizeX * minPartSizeY;
		channelSum += readSource(minPartX, maxPartY, c) * minPartSizeX * maxPartSizeY;
		channelSum += readSource(maxPartX, maxPartY, c) * maxPartSizeX * maxPartSizeY;

		// Edges
		for (int x = minIntX; x < maxIntX; x++)
		{
			channelSum += readSource(x, minPartY, c) * minPartSizeY;
			channelSum += readSource(x, maxPartY, c) * maxPartSizeY;
		}
		for (int y = minIntY; y < maxIntY; y++)
		{
			channelSum += readSource(minPartX, y, c) * minPartSizeX;
			channelSum += readSource(maxPartX, y, c) * maxPartSizeX;
		}

		// Divide by area to average
		target[c] = channelSum / ((maxX - minX) * (maxY - minY));
	}
}

// for a pixel on the texture, calculate how much area on the sphere it represents
float calcSphereArea(int x, int y, int sourceWidth, int sourceHeight)
{
	float theta;
	float phi;
	TexturePosToSphereCoord(x, y, sourceWidth, sourceHeight, theta, phi);

	float circumferenceRatio = std::abs(std::cos(phi * 2.f)); // *2 why?

	float verticalPixels = 1.f;
	float horizontalPixels = std::min(1.f / circumferenceRatio, static_cast<float>(sourceWidth));

	return verticalPixels * circumferenceRatio;
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

	// Box-Filtering (wrong)
	/*if (type == MipGenerationType::Box)
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
				assert(horizontalPixels >= 1.f);

				int targetIndex = ((y / 2) * targetWidth + (x / 2)) * channelCount;

				AverageRect(x - horizontalPixels / 2.f, y, x + horizontalPixels / 2.f, y + verticalPixels, sourceWidth, sourceHeight, channelCount, source, &mipMapData[targetIndex]);
			}
		}
	}*/

	if (type == MipGenerationType::Box)
	{
		for (int y = 0; y < sourceHeight; y += 2)
		{
			for (int x = 0; x < sourceWidth; x += 2)
			{
				int sourceIndex0 = (y * sourceWidth + x) * channelCount;
				int sourceIndex1 = ((y + 1) * sourceWidth + x) * channelCount;
				int sourceIndex2 = (y * sourceWidth + x + 1) * channelCount;
				int sourceIndex3 = ((y + 1) * sourceWidth + x + 1) * channelCount;

				float sourceArea0 = calcSphereArea(x, y, sourceWidth, sourceHeight);
				float sourceArea1 = calcSphereArea(x, y + 1, sourceWidth, sourceHeight);
				float sourceArea2 = calcSphereArea(x + 1, y, sourceWidth, sourceHeight);
				float sourceArea3 = calcSphereArea(x + 1, y + 1, sourceWidth, sourceHeight);
				float totalArea = sourceArea0 + sourceArea1 + sourceArea2 + sourceArea3;

				int targetIndex = ((y / 2) * targetWidth + (x / 2)) * channelCount;

				for (int c = 0; c < channelCount; c++)
				{
					mipMapData[targetIndex + c] =
						source[sourceIndex0 + c] * sourceArea0 / totalArea
						+ source[sourceIndex1 + c] * sourceArea1 / totalArea
						+ source[sourceIndex2 + c] * sourceArea2 / totalArea
						+ source[sourceIndex3 + c] * sourceArea3 / totalArea;
				}
			}
		}
	}

	else { assert(false); return nullptr; }

	return mipMapData;
}

void WriteImage(std::string& pathPrefix, std::string& shapePrefix, std::string& filterPrefix, uint8_t* mipSource, int w, int h, int channelCount, int mipLevel)
{
	std::string path = pathPrefix + shapePrefix + "-" + filterPrefix + "-" + std::to_string(mipLevel) + ".png";
	PrepareFileWrite(path);
	bool writeResult = stbi_write_png(path.c_str(), w, h, channelCount, mipSource, 0);
	assert(writeResult && "Failed to write image!");
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

	int mipLevel = 0;
	int mipSourceWidth = width;
	int mipSourceHeight = height;
	uint8_t* mipSource = level0;

	std::string shapePrefix = shape == ImageShape::Regular ? "re" : "eq";
	std::string filterPrefix = "";
	switch (type)
	{
	case MipGenerationType::Point:  filterPrefix = "pt"; break;
	case MipGenerationType::Box:    filterPrefix = "bx"; break;
	case MipGenerationType::Kaiser: filterPrefix = "ka"; break;
	}
	std::string pathPrefix{ targetPathPrefix };
	WriteImage(pathPrefix, shapePrefix, filterPrefix, mipSource, mipSourceWidth, mipSourceHeight, channelCount, mipLevel);

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
		
		WriteImage(pathPrefix, shapePrefix, filterPrefix, mipSource, mipSourceWidth, mipSourceHeight, channelCount, mipLevel);
	}

	std::string ddsPath = pathPrefix + shapePrefix + "-" + filterPrefix + ".dds";
	WriteDDS(ddsPath, width, height, mipLevel, mipMemory.base, mipMemory.used);
	
	stbi_image_free(imageData);
}

int main(int argc, char* argv[])
{
	//GenerateEquirectangularCheckerboard(1024, 512);
	GenerateMipMap("textures/Wolfstein.jpg", "textures/gen/", MipGenerationType::Box, ImageShape::Equirect);
	//GenerateMipMap("textures/Wolfstein.jpg", "textures/gen/", MipGenerationType::Box, ImageShape::Regular);
	//GenerateMipMap("textures/Wolfstein.jpg", "textures/gen/", MipGenerationType::Point, ImageShape::Regular);
	return 0;
}