#include "File.h"

#include <assert.h>
#include <fstream>
#include <filesystem>

bool PrepareFileWrite(const std::string& filePath)
{
	std::filesystem::path dir{ filePath };
	dir.remove_filename();
	if (std::filesystem::exists(dir))
	{
		bool isDir = std::filesystem::is_directory(dir);
		assert(isDir && "Path is not a directory!");
		return isDir;
	}
	else
	{
		std::filesystem::create_directories(dir);
		return true;
	}
}

void WriteDDS(const std::string& filePath, uint32_t width, uint32_t height, uint32_t mipCount, const uint8_t* imageData, size_t imageDataSize)
{
	bool prepared = PrepareFileWrite(filePath);
	assert(prepared);
	if (!prepared) return;

	DDSFileHeader header{};
	header.dwMagic = DirectX::DDS_MAGIC;
	header.header.size = sizeof(DirectX::DDS_HEADER);
	header.header.flags = DDS_HEADER_FLAGS_TEXTURE | DDS_HEADER_FLAGS_MIPMAP;
	header.header.height = height;
	header.header.width = width;
	header.header.pitchOrLinearSize = (width * 4 + 7) / 8;
	header.header.mipMapCount = mipCount;
	header.header.ddspf.size = sizeof(DirectX::DDS_PIXELFORMAT);
	header.header.ddspf.flags = DDS_FOURCC;
	header.header.ddspf.fourCC = MAKEFOURCC('D', 'X', '1', '0');
	header.header.ddspf.RGBBitCount = 32;
	header.header.ddspf.RBitMask = 0x000000ff;
	header.header.ddspf.GBitMask = 0x0000ff00;
	header.header.ddspf.BBitMask = 0x00ff0000;
	header.header.ddspf.ABitMask = 0xff000000;
	header.header.caps = DDS_SURFACE_FLAGS_TEXTURE | DDS_SURFACE_FLAGS_MIPMAP;

	header.header10.dxgiFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
	header.header10.resourceDimension = DirectX::DDS_DIMENSION_TEXTURE2D;
	header.header10.arraySize = 1;

	std::fstream writeStream{ filePath, std::ios::out | std::ios::binary };
	assert(!writeStream.bad());
	writeStream.write((char*)&header, sizeof(header));
	writeStream.write((char*)imageData, imageDataSize);
	writeStream.close();
}
