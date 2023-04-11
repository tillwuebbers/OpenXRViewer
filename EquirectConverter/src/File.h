#pragma once

#define WIN32_LEAN_AND_MEAN
#include "Windows.h"

#undef min
#undef max

#include <string>
#include <dxgi.h>
#include "../libraries/dds/DDS.h"

bool PrepareFileWrite(const std::string& filePath);
void WriteDDS(const std::string& filePath, uint32_t width, uint32_t height, uint32_t mipCount, const uint8_t* imageData, size_t imageDataSize);

struct DDSFileHeader
{
	DWORD dwMagic;
	DirectX::DDS_HEADER header = {};
	DirectX::DDS_HEADER_DXT10 header10 = {};
};
