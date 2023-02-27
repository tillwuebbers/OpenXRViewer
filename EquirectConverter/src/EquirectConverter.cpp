#include "EquirectConverter.h"

#include "Constants.h"
#include "Memory.h"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <vector>
#include <assert.h>
#include <cmath>

inline float clamp01(float x)
{
	return x < 0.f ? 0.f : x > 1.f ? 1.f : x;
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

int main(int argc, char* argv[])
{
	GenerateEquirectangularCheckerboard(1024, 512);
	return 0;
}