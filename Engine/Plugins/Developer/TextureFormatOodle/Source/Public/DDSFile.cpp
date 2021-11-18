// Copyright Epic Games, Inc. All Rights Reserved.

#include "DDSFile.h"
#include "Logging/LogMacros.h"
#include "Serialization/Archive.h"

DEFINE_LOG_CATEGORY_STATIC(LogOodleDDS, Log, All);

namespace OodleDDS
{

constexpr uint32 MakeFOURCC(uint32 a, uint32 b, uint32 c, uint32 d) { return ((a) | ((b) << 8) | ((c) << 16) | ((d) << 24)); }

constexpr uint32 DDSD_CAPS = 0x00000001;
constexpr uint32 DDSD_HEIGHT = 0x00000002;
constexpr uint32 DDSD_WIDTH = 0x00000004;
constexpr uint32 DDSD_PITCH = 0x00000008;
constexpr uint32 DDSD_PIXELFORMAT = 0x00001000;
constexpr uint32 DDSD_MIPMAPCOUNT = 0x00020000;
constexpr uint32 DDSD_DEPTH = 0x00800000;

constexpr uint32 DDPF_ALPHA = 0x00000002;
constexpr uint32 DDPF_FOURCC = 0x00000004;
constexpr uint32 DDPF_RGB = 0x00000040;
constexpr uint32 DDPF_LUMINANCE = 0x00020000;
constexpr uint32 DDPF_BUMPDUDV = 0x00080000;

constexpr uint32 DDSCAPS_COMPLEX = 0x00000008;
constexpr uint32 DDSCAPS_TEXTURE = 0x00001000;
constexpr uint32 DDSCAPS_MIPMAP = 0x00400000;

constexpr uint32 DDSCAPS2_CUBEMAP = 0x00000200;
constexpr uint32 DDSCAPS2_VOLUME = 0x00200000;

constexpr uint32 RESOURCE_DIMENSION_UNKNOWN = 0;
constexpr uint32 RESOURCE_DIMENSION_BUFFER = 1;
constexpr uint32 RESOURCE_DIMENSION_TEXTURE1D = 2;
constexpr uint32 RESOURCE_DIMENSION_TEXTURE2D = 3;
constexpr uint32 RESOURCE_DIMENSION_TEXTURE3D = 4;

constexpr uint32 RESOURCE_MISC_TEXTURECUBE = 0x00000004;

constexpr uint32 DDS_MAGIC = MakeFOURCC('D', 'D', 'S', ' ');
constexpr uint32 DX10_MAGIC = MakeFOURCC('D', 'X', '1', '0');

struct FDDSPixelFormat
{
	uint32 size;
	uint32 flags;
	uint32 fourCC;
	uint32 RGBBitCount;
	uint32 RBitMask;
	uint32 GBitMask;
	uint32 BBitMask;
	uint32 ABitMask;
};

struct FDDSHeader 
{
	uint32 size;
	uint32 flags;
	uint32 height;
	uint32 width;
	uint32 pitchOrLinearSize;
	uint32 depth;	 
	uint32 num_mips;
	uint32 reserved1[11];
	FDDSPixelFormat ddspf;
	uint32 caps;
	uint32 caps2;
	uint32 caps3;
	uint32 caps4;
	uint32 reserved2;
};

struct FDDSHeaderDX10 
{
	uint32 dxgi_format;
	uint32 resource_dimension;
	uint32 misc_flag;	 // see D3D11_RESOURCE_MISC_FLAG
	uint32 array_size;
	uint32 misc_flag2;
};

struct FDXGIFormatName 
{
	EDXGIFormat Format;
	const char* Name;
};

const char* DXGIFormatGetName(EDXGIFormat fmt) 
{
	static const FDXGIFormatName FormatList[] = 
	{
#define RGBFMT(name,id,bypu) { EDXGIFormat::name, #name },
#define BCNFMT(name,id,bypu) { EDXGIFormat::name, #name },
#define ODDFMT(name,id) { EDXGIFormat::name, #name },
		OODLE_DXGI_FORMAT_LIST
#undef RGBFMT
#undef BCNFMT
#undef ODDFMT
	};

	for(size_t i = 0; i < sizeof(FormatList) / sizeof(*FormatList); ++i) 
	{
		if (FormatList[i].Format == fmt) 
		{
			return FormatList[i].Name;
		}
	}
	return FormatList[0].Name; // first entry is "unknown format"
}

// list of non-sRGB / sRGB pixel format pairs: even=UNORM, odd=UNORM_SRGB
// (sorted by DXGI_FORMAT code)
static EDXGIFormat DXGIFormatSRGBTable[] = 
{
	EDXGIFormat::R8G8B8A8_UNORM,		EDXGIFormat::R8G8B8A8_UNORM_SRGB,
	EDXGIFormat::BC1_UNORM,			EDXGIFormat::BC1_UNORM_SRGB,
	EDXGIFormat::BC2_UNORM,			EDXGIFormat::BC2_UNORM_SRGB,
	EDXGIFormat::BC3_UNORM,			EDXGIFormat::BC3_UNORM_SRGB,
	EDXGIFormat::B8G8R8A8_UNORM,		EDXGIFormat::B8G8R8A8_UNORM_SRGB,
	EDXGIFormat::B8G8R8X8_UNORM,		EDXGIFormat::B8G8R8X8_UNORM_SRGB,
	EDXGIFormat::BC7_UNORM,			EDXGIFormat::BC7_UNORM_SRGB,
};

static int DXGIFormatGetIndexInSRGBTable(EDXGIFormat Format) 
{
	for(size_t i = 0; i < sizeof(DXGIFormatSRGBTable) / sizeof(*DXGIFormatSRGBTable); ++i) 
	{
		if (DXGIFormatSRGBTable[i] == Format) 
		{
			return(int)i;
		}
	}
	return -1;
}

bool DXGIFormatIsSRGB(EDXGIFormat Format) 
{
	int idx = DXGIFormatGetIndexInSRGBTable(Format);
	return idx >= 0 && ((idx & 1) == 1);
}

EDXGIFormat DXGIFormatRemoveSRGB(EDXGIFormat fmt) 
{
	int idx = DXGIFormatGetIndexInSRGBTable(fmt);
	if(idx >= 0)
	{
		return DXGIFormatSRGBTable[idx & ~1];
	}
	else 
	{
		return fmt;
	}
}

EDXGIFormat DXGIFormatAddSRGB(EDXGIFormat fmt) 
{
	int idx = DXGIFormatGetIndexInSRGBTable(fmt);
	if(idx >= 0) 
	{
		return DXGIFormatSRGBTable[idx | 1];
	}
	else 
	{
		return fmt;
	}
}

// this is used for trying to map old format specifications to DXGI.
struct FBitmaskToDXGI 
{
	uint32 Flags;
	uint32 Bits;
	uint32 RMask, GMask, BMask, AMask;
	EDXGIFormat Format;
};

// used for mapping fourcc format specifications to DXGI
struct FFOURCCToDXGI 
{
	uint32 fourcc;
	EDXGIFormat Format;
};

struct FDXGIFormatInfo 
{
	EDXGIFormat Format;
	uint32 UnitWidth; // width of a coding unit
	uint32 UnitHeight; // height of a coding unit
	uint32 UnitBytes;
};

static const FDXGIFormatInfo SupportedFormatList[] = 
{
#define RGBFMT(name,id,bypu) { EDXGIFormat::name, 1,1, bypu },
#define BCNFMT(name,id,bypu) { EDXGIFormat::name, 4,4, bypu },
#define ODDFMT(name,id) // these are not supported for reading so they're intentionally not on the list
	OODLE_DXGI_FORMAT_LIST
#undef RGBFMT
#undef BCNFMT
#undef ODDFMT
};

// this is following MS DDSTextureLoader11
static const FBitmaskToDXGI BitmaskToDXGITable[] = 
{
	//flags					bits	r			g			b			a			dxgi
	{ DDPF_RGB,			32,		0x000000ff, 0x0000ff00, 0x00ff0000, 0xff000000, EDXGIFormat::R8G8B8A8_UNORM },
	{ DDPF_RGB,			32,		0x00ff0000, 0x0000ff00, 0x000000ff, 0xff000000, EDXGIFormat::B8G8R8A8_UNORM },
	{ DDPF_RGB,			32,		0x00ff0000, 0x0000ff00, 0x000000ff, 0x00000000, EDXGIFormat::B8G8R8X8_UNORM },
	{ DDPF_RGB,			32,		0x3ff00000, 0x000ffc00, 0x000003ff, 0xc0000000, EDXGIFormat::R10G10B10A2_UNORM }, // yes, this mask is backwards, but that's the standard value to write for R10G10B10A2_UNORM! (see comments in DDSTextureLoader11)
	{ DDPF_RGB,			32,		0x0000ffff, 0xffff0000, 0x00000000, 0x00000000, EDXGIFormat::R16G16_UNORM },
	{ DDPF_RGB,			32,		0xffffffff, 0x00000000, 0x00000000, 0x00000000, EDXGIFormat::R32_FLOAT }, // only 32-bit color channel format in D3D9 was R32F
	{ DDPF_RGB,			16,		0x7c00,		0x03e0,		0x001f,		0x8000,		EDXGIFormat::B5G5R5A1_UNORM },
	{ DDPF_RGB,			16,		0xf800,		0x07e0,		0x001f,		0x0000,		EDXGIFormat::B5G6R5_UNORM },
	{ DDPF_RGB,			16,		0x0f00,		0x00f0,		0x000f,		0xf000,		EDXGIFormat::B4G4R4A4_UNORM },
	{ DDPF_LUMINANCE,	8,		0xff,		0x00,		0x00,		0x00,		EDXGIFormat::R8_UNORM },
	{ DDPF_LUMINANCE,	16,		0xffff,		0x0000,		0x0000,		0x0000,		EDXGIFormat::R16_UNORM },
	{ DDPF_LUMINANCE,	16,		0x00ff,		0x0000,		0x0000,		0xff00,		EDXGIFormat::R8G8_UNORM }, // official way to do it - this must go first!
	{ DDPF_LUMINANCE,	8,		0xff,		0x00,		0x00,		0xff00,		EDXGIFormat::R8G8_UNORM }, // some writers write this instead, ugh.
	{ DDPF_ALPHA,		8,		0x00,		0x00,		0x00,		0xff,		EDXGIFormat::A8_UNORM },
	{ DDPF_BUMPDUDV,	32,		0x000000ff, 0x0000ff00, 0x00ff0000, 0xff000000, EDXGIFormat::R8G8B8A8_SNORM },
	{ DDPF_BUMPDUDV,	2,		0x0000ffff, 0xffff0000, 0x00000000, 0x00000000, EDXGIFormat::R16G16_SNORM },
	{ DDPF_BUMPDUDV,	16,		0x00ff,		0xff00,		0x0000,		0x0000,		EDXGIFormat::R8G8_SNORM },
};

// this is following MS DDSTextureLoader11
// when multiple FOURCCs map to the same DXGI format, put the preferred FOURCC first
static const FFOURCCToDXGI FOURCCToDXGITable[] = 
{
	{ MakeFOURCC('D','X','T','1'),		EDXGIFormat::BC1_UNORM },
	{ MakeFOURCC('D','X','T','2'),		EDXGIFormat::BC2_UNORM },
	{ MakeFOURCC('D','X','T','3'),		EDXGIFormat::BC2_UNORM },
	{ MakeFOURCC('D','X','T','4'),		EDXGIFormat::BC3_UNORM },
	{ MakeFOURCC('D','X','T','5'),		EDXGIFormat::BC3_UNORM },
	{ MakeFOURCC('A','T','I','1'),		EDXGIFormat::BC4_UNORM },
	{ MakeFOURCC('B','C','4','U'),		EDXGIFormat::BC4_UNORM },
	{ MakeFOURCC('B','C','4','S'),		EDXGIFormat::BC4_SNORM },
	{ MakeFOURCC('B','C','5','U'),		EDXGIFormat::BC5_UNORM },
	{ MakeFOURCC('B','C','5','S'),		EDXGIFormat::BC5_SNORM },
	{ MakeFOURCC('A','T','I','2'),		EDXGIFormat::BC5_UNORM }, // NOTE: ATI2 is kind of odd (technically swapped block order), so put it below BC5U
	{ MakeFOURCC('B','C','6','H'),		EDXGIFormat::BC6H_UF16 },
	{ MakeFOURCC('B','C','7','L'),		EDXGIFormat::BC7_UNORM },
	{ MakeFOURCC('B','C','7', 0 ),		EDXGIFormat::BC7_UNORM },
	{ 36,								EDXGIFormat::R16G16B16A16_UNORM }, // D3DFMT_A16B16G16R16
	{ 110,								EDXGIFormat::R16G16B16A16_SNORM }, // D3DFMT_Q16W16V16U16
	{ 111,								EDXGIFormat::R16_FLOAT }, // D3DFMT_R16F
	{ 112,								EDXGIFormat::R16G16_FLOAT }, // D3DFMT_G16R16F
	{ 113,								EDXGIFormat::R16G16B16A16_FLOAT }, // D3DFMT_A16B16G16R16F
	{ 114,								EDXGIFormat::R32_FLOAT }, // D3DFMT_R32F
	{ 115,								EDXGIFormat::R32G32_FLOAT }, // D3DFMT_G32R32F
	{ 116,								EDXGIFormat::R32G32B32A32_FLOAT }, // D3DFMT_G32R32F
};

static const FDXGIFormatInfo* DXGIFormatGetInfo(EDXGIFormat InFormat)
{
	// need to handle this special because UNKNOWN _does_ appear in the master list
	// but we don't want to treat it as legal
	if (InFormat == EDXGIFormat::UNKNOWN)
	{
		return 0;
	}

	for (size_t i = 0; i < sizeof(SupportedFormatList)/sizeof(*SupportedFormatList); ++i)
	{
		if (InFormat == SupportedFormatList[i].Format) 
		{
			return &SupportedFormatList[i];
		}
	}

	return 0;
}

static EDXGIFormat DXGIFormatFromDDS9Header(const FDDSHeader* InDDSHeader)
{
	// The old format can be specified either with FOURCC or with some bit masks, so we use
	// this to determine the corresponding dxgi format.
	const FDDSPixelFormat &ddpf = InDDSHeader->ddspf;
	if (ddpf.flags & DDPF_FOURCC) 
	{
		for (size_t i = 0; i < sizeof(FOURCCToDXGITable)/sizeof(*FOURCCToDXGITable); ++i) 
		{
			if (ddpf.fourCC == FOURCCToDXGITable[i].fourcc) 
			{
				return FOURCCToDXGITable[i].Format;
			}
		}
	}
	else 
	{
		uint32 type_flags = ddpf.flags & (DDPF_RGB | DDPF_LUMINANCE | DDPF_ALPHA | DDPF_BUMPDUDV);
		for (size_t i = 0; i < sizeof(BitmaskToDXGITable)/sizeof(*BitmaskToDXGITable); ++i) 
		{
			const FBitmaskToDXGI *fmt = &BitmaskToDXGITable[i];
			if (type_flags == fmt->Flags && ddpf.RGBBitCount == fmt->Bits &&
				ddpf.RBitMask == fmt->RMask && ddpf.GBitMask == fmt->GMask &&
				ddpf.BBitMask == fmt->BMask && ddpf.ABitMask == fmt->AMask)
			{
				return fmt->Format;
			}
		}
	}

	return EDXGIFormat::UNKNOWN;
}

static uint32 MipDimension(uint32 dim, uint32 level) 
{
	// mip dimensions truncate at every level and bottom out at 1
	uint32 x = dim >> level;
	return x ? x : 1;
}

static void InitMip(FDDSMip* InMip, uint32 InWidth, uint32 InHeight, uint32 InDepth, const FDXGIFormatInfo* InFormatInfo) 
{
	uint32 width_u = (InWidth + InFormatInfo->UnitWidth-1) / InFormatInfo->UnitWidth;
	uint32 height_u = (InHeight + InFormatInfo->UnitHeight-1) / InFormatInfo->UnitHeight;

	InMip->Width = InWidth;
	InMip->Height = InHeight;
	InMip->Depth = InDepth;
	InMip->RowStride = width_u * InFormatInfo->UnitBytes;
	InMip->SliceStride = height_u * InMip->RowStride;
	InMip->DataSize = InDepth * InMip->SliceStride;
	InMip->Data = 0;
}

FDDSFile::~FDDSFile() 
{
	if (Mips) 
	{
		FMemory::Free(Mips);
	}
	if (MipRawPtr)
	{
		FMemory::Free(MipRawPtr);
	}
}

static bool AllocateMips(FDDSFile* InDDS, const FDXGIFormatInfo* InFormatInfo, uint32 InCreateFlags)
{
	InDDS->Mips = (FDDSMip*)FMemory::MallocZeroed(InDDS->MipCount * InDDS->ArraySize * sizeof(FDDSMip));
	if (!InDDS->Mips) 
	{
		return false;
	}

	InDDS->MipDataSize = 0;
	InDDS->MipRawPtr = 0;

	if (!(InCreateFlags & FDDSFile::CREATE_FLAG_NO_MIP_STORAGE_ALLOC)) 
	{
		// Allocate storage for all the mip levels

		// first pass, add up all sizes
		//	then alloc it, second pass hand out all the pointers

		size_t AllMipsSize = 0;
		for (uint32 ArrayIndex = 0; ArrayIndex < InDDS->ArraySize; ++ArrayIndex) 
		{
			for (uint32 MipIndex = 0; MipIndex < InDDS->MipCount; ++MipIndex) 
			{
				FDDSMip* Mip = InDDS->Mips + (ArrayIndex * InDDS->MipCount + MipIndex);
				uint32 MipWidth = MipDimension(InDDS->Width, MipIndex);
				uint32 MipHeight = MipDimension(InDDS->Height, MipIndex);
				uint32 MipDepth = MipDimension(InDDS->Depth, MipIndex);
				InitMip(Mip, MipWidth, MipHeight, MipDepth, InFormatInfo);
				AllMipsSize += Mip->DataSize;
			}
		}

		InDDS->MipDataSize = AllMipsSize;
		InDDS->MipRawPtr = FMemory::MallocZeroed(AllMipsSize);
		if (!InDDS->MipRawPtr)
		{
			return false;
		}

		unsigned char* MipPtr = (unsigned char *) InDDS->MipRawPtr;
		for (uint32 MipIndex = 0; MipIndex < InDDS->ArraySize * InDDS->MipCount; ++MipIndex) 
		{
			FDDSMip* Mip = InDDS->Mips + MipIndex;
			Mip->Data = MipPtr;
			MipPtr += Mip->DataSize;
		}
	}

	return true;
}

/* static */ FDDSFile* FDDSFile::CreateEmpty(int InDimension, uint32 InWidth, uint32 InHeight, uint32 InDepth, uint32 InMipCount, uint32 InArraySize, EDXGIFormat InFormat, uint32 InCreateFlags)
{
	const FDXGIFormatInfo *FormatInfo;

	// Some sanity checks
	if (!InWidth || !InHeight || !InDepth || !InMipCount || !InArraySize)
	{
		return 0;
	}

	// Cube maps must have an array size that's a multiple of 6
	if ((InCreateFlags & CREATE_FLAG_CUBEMAP) && (InArraySize % 6) != 0) 
	{
		UE_LOG(LogOodleDDS, Error, TEXT("Array length must be multple of 6 for cube maps"));
		return 0;
	}

	// Fail if it's not a recognized format
	FormatInfo = DXGIFormatGetInfo(InFormat);
	if (!FormatInfo)
	{
		UE_LOG(LogOodleDDS, Error, TEXT("Unsupported format %d (%s)"), InFormat, DXGIFormatGetName(InFormat));
		return 0;
	}

	// Allocate the struct
	FDDSFile* DDS = new FDDSFile();
	DDS->Dimension = InDimension;
	DDS->Width = InWidth;
	DDS->Height = InHeight;
	DDS->Depth = InDepth;
	DDS->MipCount = InMipCount;
	DDS->ArraySize = InArraySize;
	DDS->DXGIFormat = InFormat;
	DDS->CreateFlags = InCreateFlags & ~CREATE_FLAG_NO_MIP_STORAGE_ALLOC;

	if (!AllocateMips(DDS, FormatInfo, InCreateFlags)) 
	{
		delete DDS;
		return 0;
	}

	return DDS;
}

/* static */ FDDSFile* FDDSFile::CreateEmpty2D(uint32 InWidth, uint32 InHeight, uint32 InMipCount, EDXGIFormat InFormat, uint32 InCreateFlags)
{
	return FDDSFile::CreateEmpty(2, InWidth, InHeight, 1, InMipCount, 1, InFormat, InCreateFlags);
}

static bool ParseHeader(FDDSFile* InDDS, FDDSHeader const* InHeader, FDDSHeaderDX10 const* InDX10Header)
{
	// If the fourCC is "DX10" then we have a secondary header that follows the first header. 
	// This header specifies an dxgi_format explicitly, so we don't have to derive one.
	bool bDX10 = false;
	const FDDSPixelFormat& ddpf = InHeader->ddspf;
	if ((ddpf.flags & DDPF_FOURCC) && ddpf.fourCC == DX10_MAGIC)
	{
		if (InDX10Header->resource_dimension >= RESOURCE_DIMENSION_TEXTURE1D && InDX10Header->resource_dimension <= RESOURCE_DIMENSION_TEXTURE3D)
		{
			InDDS->Dimension = (InDX10Header->resource_dimension - RESOURCE_DIMENSION_TEXTURE1D) + 1;
		}
		else
		{
			UE_LOG(LogOodleDDS, Error, TEXT("D3D10 resource dimension in DDS is not 1D, 2D or 3D texture."));
			return false;
		}
		InDDS->DXGIFormat = (EDXGIFormat)InDX10Header->dxgi_format;
		bDX10 = true;
	}
	else
	{
		// For D3D9-style files, we guess dimension from the caps bits.
		// If the volume cap is set, assume 3D, otherwise 2D.
		InDDS->Dimension = (InHeader->caps2 & DDSCAPS2_VOLUME) ? 3 : 2;
		InDDS->DXGIFormat = DXGIFormatFromDDS9Header(InHeader);
	}

	// Check if the pixel format is supported
	const FDXGIFormatInfo* format_info = DXGIFormatGetInfo(InDDS->DXGIFormat);
	if (!format_info)
	{
		UE_LOG(LogOodleDDS, Error, TEXT("Unsupported DDS pixel format!"));
		return false;
	}

	// More header parsing
	bool bIsCubemap = bDX10 ? (InDX10Header->misc_flag & RESOURCE_MISC_TEXTURECUBE) != 0 : (InHeader->caps2 & DDSCAPS2_CUBEMAP) != 0;
	bool bIsVolume = InDDS->Dimension == 3;

	InDDS->Width = InHeader->width;
	InDDS->Height = InHeader->height;
	InDDS->Depth = bIsVolume ? InHeader->depth : 1;
	InDDS->MipCount = (InHeader->caps & DDSCAPS_MIPMAP) ? InHeader->num_mips : 1;
	InDDS->ArraySize = bDX10 ? InDX10Header->array_size : 1;
	InDDS->CreateFlags = 0;
	if (bIsCubemap)
	{
		InDDS->CreateFlags |= FDDSFile::CREATE_FLAG_CUBEMAP;
		InDDS->ArraySize *= 6;
	}

	// Sanity-check all these values
	if (!InDDS->Width || !InDDS->Height || !InDDS->Depth || !InDDS->MipCount || !InDDS->ArraySize)
	{
		UE_LOG(LogOodleDDS, Error, TEXT("Invalid dimensions in DDS file."));
		return false;
	}

	// TODO: might want to make sure that num_mips <= number of actual possible mips for the resolution.

	// Note: max_mips of 16 means maximum dimension of 64k-1... increase this number if you need to.
	const uint32 MaxDimension = (1 << FDDSFile::MAX_MIPS_SUPPORTED) - 1; // max_dim=0xffff has 16 mip levels, but 0x10000 has 17
	if (InDDS->Width > MaxDimension || InDDS->Height > MaxDimension || InDDS->Depth > MaxDimension || InDDS->MipCount > FDDSFile::MAX_MIPS_SUPPORTED)
	{
		UE_LOG(LogOodleDDS, Error, TEXT("Dimensions of DDS exceed maximum of %u"), MaxDimension);
		return false;
	}

	// Cubemaps need to be square
	if (bIsCubemap && (InDDS->Width != InDDS->Height || InDDS->Depth != 1))
	{
		UE_LOG(LogOodleDDS, Error, TEXT("Cubemap is not square or has non-1 depth!"));
		return false;
	}

	return true;
}


static bool ReadPayload(FDDSFile* InDDS, FArchive* Ar)
{
	const FDXGIFormatInfo* FormatInfo = DXGIFormatGetInfo(InDDS->DXGIFormat);
	if (!AllocateMips(InDDS, FormatInfo, InDDS->CreateFlags))
	{
		UE_LOG(LogOodleDDS, Error, TEXT("Out of memory allocating DDS mip chain"));
		return false;
	}

	for (uint32 SubresourceIndex = 0; SubresourceIndex < InDDS->ArraySize * InDDS->MipCount; ++SubresourceIndex)
	{
		FDDSMip* Mip = InDDS->Mips + SubresourceIndex;

		Ar->Serialize(Mip->Data, Mip->DataSize);
		if (Ar->GetError())
		{
			UE_LOG(LogOodleDDS, Error, TEXT("Corrupt file: texture data truncated."));
			return false;
		}
	}

	return true;
}


/* static */ FDDSFile* FDDSFile::CreateFromArchive(FArchive* Ar)
{
	if (Ar->IsLoading() == false)
	{
		return nullptr;
	}

	uint32 Magic = 0;
	Ar->Serialize(&Magic, 4);
	if (Ar->GetError())
	{
		UE_LOG(LogOodleDDS, Error, TEXT("Not a DDS file."));
		return nullptr;
	}

	if (Magic != ' SDD')
	{
		UE_LOG(LogOodleDDS, Error, TEXT("Not a DDS file."));
		return nullptr;
	}

	FDDSHeader DDSHeader = {};
	FDDSHeaderDX10 DDS10Header = {};

	Ar->Serialize(&DDSHeader, sizeof(DDSHeader));
	if (Ar->GetError())
	{
		UE_LOG(LogOodleDDS, Error, TEXT("Failed to read DDS header"));
		return nullptr;
	}

	// do we need to read a dx10 header?
	const FDDSPixelFormat& ddpf = DDSHeader.ddspf;
	if ((ddpf.flags & DDPF_FOURCC) && ddpf.fourCC == DX10_MAGIC)
	{
		Ar->Serialize(&DDS10Header, sizeof(FDDSHeaderDX10));
		if (Ar->GetError())
		{
			UE_LOG(LogOodleDDS, Error, TEXT("Failed to read DX10 DDS header"));
			return nullptr;
		}
	}

	FDDSFile* DDS = new FDDSFile();

	if (!ParseHeader(DDS, &DDSHeader, &DDS10Header))
	{
		delete DDS;
		return nullptr;
	}

	if (!ReadPayload(DDS, Ar))
	{
		delete DDS;
		return nullptr;
	}

	return DDS;
 }

//
// Write to an archive (i.e. file)
//
bool FDDSFile::SerializeToArchive(FArchive* Ar)
{	
	if (Ar->IsSaving() == false)
	{
		return false;
	}

	// Validate DDS a bit...
	bool bIsCubemap = (this->CreateFlags & CREATE_FLAG_CUBEMAP) != 0;
	if(	   (this->DXGIFormat == EDXGIFormat::UNKNOWN) // unknown format
		|| (bIsCubemap && (this->ArraySize % 6) != 0) // says its a cubemap... but doesn't have a multiple of 6 faces?!
		|| (this->ArraySize <= 0)
		|| (this->MipCount <= 0)
		|| (this->Mips == 0)
		|| (this->Dimension < 1 || this->Dimension > 3)
		)
	{
		return false;
	}

	if((this->Dimension == 3 && this->ArraySize != 1) || // volume textures can't be arrays
	   (this->Dimension < 3 && this->Depth > 1) || // 1D and 2D textures must have depth==1
	   (this->Dimension < 2 && this->Height > 1)) // 1D textures must have height==1
	{
		return false;
	}

	uint32 DepthFlag = (this->Dimension == 3) ? 0x800000 : 0; // DDSD_DEPTH
	uint32 WriteArraySize = bIsCubemap ? this->ArraySize / 6 : this->ArraySize; 

	uint32 Caps2 = 0;
	if (this->Dimension == 3) 
	{
		Caps2 |= DDSCAPS2_VOLUME;
	}
	if (bIsCubemap) 
	{
		Caps2 |= 0xFE00; // DDSCAPS2_CUBEMAP*
	}

	uint32 FourCC = DX10_MAGIC;
	bool bIsDX10 = true;

	FDDSHeader DDSHeader = 
	{
		124, // size value. Required to be 124
		DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH | DDSD_PIXELFORMAT | DDSD_MIPMAPCOUNT | DepthFlag,
		this->Height,
		this->Width,
		0, // pitch or linear size
		this->Depth,
		this->MipCount,
		{}, // reserved U32's
		// DDSPF (DDS PixelFormat)
		{
			32, // size, must be 32
			DDPF_FOURCC, // DDPF_FOURCC
			FourCC, // DX10 header specification
			0,0,0,0,0 // Omit this data as the DX10 header specifies a DXGI format which implicitly defines this information more specifically...
		},
		DDSCAPS_COMPLEX | DDSCAPS_TEXTURE | DDSCAPS_MIPMAP,
		Caps2,
		0,
		0,
		0
	};

	uint32 ResourceDimension = RESOURCE_DIMENSION_TEXTURE1D + (this->Dimension - 1);
	uint32 MiscFlags = bIsCubemap ? RESOURCE_MISC_TEXTURECUBE : 0;
	FDDSHeaderDX10 DX10Header = 
	{
		(uint32)this->DXGIFormat, // DXGI_FORMAT
		ResourceDimension, 
		MiscFlags, 
		WriteArraySize,
		0, // DDS_ALPHA_MODE_UNKNOWN
	};

	// Write the magic identifier and headers...
	uint32 DDSMagic = ' SDD';
	Ar->Serialize(&DDSMagic, 4);
	Ar->Serialize(&DDSHeader, sizeof(DDSHeader));
	if (bIsDX10) 
	{
		Ar->Serialize(&DX10Header, sizeof(DX10Header));
	}
	
	// now go through all subresources in standard order and write them out
	// @@ this could just write mip_data_ptr,mip_data_size
	for(uint32 i = 0; i < this->ArraySize * this->MipCount; ++i) 
	{
		FDDSMip *Mip = this->Mips + i;
		Ar->Serialize(Mip->Data, Mip->DataSize);
	}

	return true;
}

} // end OodleDDS namespace