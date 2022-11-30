// Copyright Epic Games, Inc. All Rights Reserved.

#include "RenderUtils.h"
#include "Containers/ResourceArray.h"
#include "Containers/DynamicRHIResourceArray.h"
#include "RenderResource.h"
#include "RHIStaticStates.h"
#include "RenderGraphUtils.h"
#include "PipelineStateCache.h"
#include "Misc/ConfigCacheIni.h"

#if WITH_EDITOR
#include "Misc/CoreMisc.h"
#include "Interfaces/ITargetPlatform.h"
#include "Interfaces/ITargetPlatformManagerModule.h"
#endif

FTextureWithRDG::FTextureWithRDG() = default;
FTextureWithRDG::FTextureWithRDG(const FTextureWithRDG& Other) = default;
FTextureWithRDG& FTextureWithRDG::operator=(const FTextureWithRDG & Other) = default;
FTextureWithRDG::~FTextureWithRDG() = default;

FRDGTexture* FTextureWithRDG::GetRDG(FRDGBuilder& GraphBuilder) const
{
	checkf(RenderTarget, TEXT("InitRDG was not called before use."));
	return GraphBuilder.RegisterExternalTexture(RenderTarget);
}

FRDGTexture* FTextureWithRDG::GetPassthroughRDG() const
{
	checkf(RenderTarget, TEXT("InitRDG was not called before use."));
	return FRDGTexture::GetPassthrough(RenderTarget);
}

void FTextureWithRDG::ReleaseRHI()
{
	RenderTarget = nullptr;
	FTexture::ReleaseRHI();
}

void FTextureWithRDG::InitRDG(const TCHAR* Name)
{
	check(TextureRHI);
	RenderTarget = CreateRenderTarget(TextureRHI, Name);
}

const uint16 GCubeIndices[12*3] =
{
	0, 2, 3,
	0, 3, 1,
	4, 5, 7,
	4, 7, 6,
	0, 1, 5,
	0, 5, 4,
	2, 6, 7,
	2, 7, 3,
	0, 4, 6,
	0, 6, 2,
	1, 3, 7,
	1, 7, 5,
};

TGlobalResource<FCubeIndexBuffer> GCubeIndexBuffer;
TGlobalResource<FTwoTrianglesIndexBuffer> GTwoTrianglesIndexBuffer;
TGlobalResource<FScreenSpaceVertexBuffer> GScreenSpaceVertexBuffer;
TGlobalResource<FTileVertexDeclaration> GTileVertexDeclaration;

//
// FPackedNormal serializer
//
FArchive& operator<<(FArchive& Ar, FDeprecatedSerializedPackedNormal& N)
{
	Ar << N.Vector.Packed;
	return Ar;
}

FArchive& operator<<(FArchive& Ar, FPackedNormal& N)
{
	Ar << N.Vector.Packed;
	return Ar;
}

FArchive& operator<<(FArchive& Ar, FPackedRGBA16N& N)
{
	Ar << N.X;
	Ar << N.Y;
	Ar << N.Z;
	Ar << N.W;
	return Ar;
}

/**
 * Bulk data interface for providing a single black color used to initialize a
 * volume texture.
 */
class FBlackVolumeTextureResourceBulkDataInterface : public FResourceBulkDataInterface
{
public:

	/** Default constructor. */
	FBlackVolumeTextureResourceBulkDataInterface(uint8 Alpha)
		: Color(0, 0, 0, Alpha)
	{
	}

	/** Default constructor. */
	FBlackVolumeTextureResourceBulkDataInterface(FColor InColor)
		: Color(InColor)
	{
	}

	/**
	 * Returns a pointer to the bulk data.
	 */
	virtual const void* GetResourceBulkData() const override
	{
		return &Color;
	}

	/** 
	 * @return size of resource memory
	 */
	virtual uint32 GetResourceBulkDataSize() const override
	{
		return sizeof(Color);
	}

	/**
	 * Free memory after it has been used to initialize RHI resource 
	 */
	virtual void Discard() override
	{
	}

private:

	/** Storage for the color. */
	FColor Color;
};

//
// FWhiteTexture implementation
//

/**
 * A solid-colored 1x1 texture.
 */
template <int32 R, int32 G, int32 B, int32 A, bool bWithUAV = false>
class FColoredTexture : public FTextureWithSRV
{
public:
	// FResource interface.
	virtual void InitRHI() override
	{
		// Create the texture RHI.  		
		FBlackVolumeTextureResourceBulkDataInterface BlackTextureBulkData(FColor(R, G, B, A));
		FRHIResourceCreateInfo CreateInfo(&BlackTextureBulkData);
		CreateInfo.DebugName = TEXT("ColoredTexture");
		ETextureCreateFlags CreateFlags = TexCreate_ShaderResource;
		if(bWithUAV)
		{
			CreateFlags |= TexCreate_UAV;
		}
		// BGRA typed UAV is unsupported per D3D spec, use RGBA here.
		FTexture2DRHIRef Texture2D = RHICreateTexture2D(1, 1, PF_R8G8B8A8, 1, 1, CreateFlags, CreateInfo);
		TextureRHI = Texture2D;

		// Create the sampler state RHI resource.
		FSamplerStateInitializerRHI SamplerStateInitializer(SF_Point,AM_Wrap,AM_Wrap,AM_Wrap);
		SamplerStateRHI = GetOrCreateSamplerState(SamplerStateInitializer);

		// Create a view of the texture
		ShaderResourceViewRHI = RHICreateShaderResourceView(TextureRHI, 0u);
		if(bWithUAV)
		{
			UnorderedAccessViewRHI = RHICreateUnorderedAccessView(TextureRHI, 0u);
		}
	}

	/** Returns the width of the texture in pixels. */
	virtual uint32 GetSizeX() const override
	{
		return 1;
	}

	/** Returns the height of the texture in pixels. */
	virtual uint32 GetSizeY() const override
	{
		return 1;
	}
};

class FEmptyVertexBuffer : public FVertexBufferWithSRV
{
public:
	virtual void InitRHI() override
	{
		// Create the texture RHI.  		
		FRHIResourceCreateInfo CreateInfo(TEXT("EmptyVertexBuffer"));
		
		VertexBufferRHI = RHICreateVertexBuffer(16u, BUF_Static | BUF_ShaderResource | BUF_UnorderedAccess, CreateInfo);

		// Create a view of the buffer
		ShaderResourceViewRHI = RHICreateShaderResourceView(VertexBufferRHI, 4u, PF_R32_UINT);
		UnorderedAccessViewRHI = RHICreateUnorderedAccessView(VertexBufferRHI, PF_R32_UINT);
	}
};

FTextureWithSRV* GWhiteTextureWithSRV = new TGlobalResource<FColoredTexture<255,255,255,255> >;
FTextureWithSRV* GBlackTextureWithSRV = new TGlobalResource<FColoredTexture<0,0,0,255> >;
FTextureWithSRV* GTransparentBlackTextureWithSRV = new TGlobalResource<FColoredTexture<0,0,0,0> >;
FTexture* GWhiteTexture = GWhiteTextureWithSRV;
FTexture* GBlackTexture = GBlackTextureWithSRV;
FTexture* GTransparentBlackTexture = GTransparentBlackTextureWithSRV;
FTextureWithSRV* GBlackTextureWithUAV = new TGlobalResource<FColoredTexture<0,0,0,0,true> >;

FVertexBufferWithSRV* GEmptyVertexBufferWithUAV = new TGlobalResource<FEmptyVertexBuffer>;

class FWhiteVertexBuffer : public FVertexBufferWithSRV
{
public:
	virtual void InitRHI() override
	{
		// Create the texture RHI.  		
		FRHIResourceCreateInfo CreateInfo(TEXT("WhiteVertexBuffer"));

		VertexBufferRHI = RHICreateVertexBuffer(sizeof(FVector4), BUF_Static | BUF_ShaderResource, CreateInfo);

		FVector4* BufferData = (FVector4*)RHILockVertexBuffer(VertexBufferRHI, 0, sizeof(FVector4), RLM_WriteOnly);
		*BufferData = FVector4(1.0f, 1.0f, 1.0f, 1.0f);
		RHIUnlockVertexBuffer(VertexBufferRHI);

		// Create a view of the buffer
		ShaderResourceViewRHI = RHICreateShaderResourceView(VertexBufferRHI, sizeof(FVector4), PF_A32B32G32R32F);
	}
};

FVertexBufferWithSRV* GWhiteVertexBufferWithSRV = new TGlobalResource<FWhiteVertexBuffer>;

/**
 * A class representing a 1x1x1 black volume texture.
 */
template <EPixelFormat PixelFormat, uint8 Alpha>
class FBlackVolumeTexture : public FTextureWithRDG
{
public:
	
	/**
	 * Initialize RHI resources.
	 */
	virtual void InitRHI() override
	{
		const TCHAR* Name = TEXT("BlackVolumeTexture");

		if (GSupportsTexture3D)
		{
			// Create the texture.
			FBlackVolumeTextureResourceBulkDataInterface BlackTextureBulkData(Alpha);
			FRHIResourceCreateInfo CreateInfo(&BlackTextureBulkData);
			CreateInfo.DebugName = Name;
			FTexture3DRHIRef Texture3D = RHICreateTexture3D(1,1,1,PixelFormat,1,TexCreate_ShaderResource,CreateInfo);
			TextureRHI = Texture3D;	
		}
		else
		{
			// Create a texture, even though it's not a volume texture
			FBlackVolumeTextureResourceBulkDataInterface BlackTextureBulkData(Alpha);
			FRHIResourceCreateInfo CreateInfo(&BlackTextureBulkData);
			CreateInfo.DebugName = Name;
			FTexture2DRHIRef Texture2D = RHICreateTexture2D(1, 1, PixelFormat, 1, 1, TexCreate_ShaderResource, CreateInfo);
			TextureRHI = Texture2D;
		}

		// Create the sampler state.
		FSamplerStateInitializerRHI SamplerStateInitializer(SF_Point, AM_Wrap, AM_Wrap, AM_Wrap);
		SamplerStateRHI = GetOrCreateSamplerState(SamplerStateInitializer);

		FTextureWithRDG::InitRDG(Name);
	}

	/**
	 * Return the size of the texture in the X dimension.
	 */
	virtual uint32 GetSizeX() const override
	{
		return 1;
	}

	/**
	 * Return the size of the texture in the Y dimension.
	 */
	virtual uint32 GetSizeY() const override
	{
		return 1;
	}
};

/** Global black volume texture resource. */
FTextureWithRDG* GBlackVolumeTexture = new TGlobalResource<FBlackVolumeTexture<PF_B8G8R8A8, 0>>();
FTextureWithRDG* GBlackAlpha1VolumeTexture = new TGlobalResource<FBlackVolumeTexture<PF_B8G8R8A8, 255>>();

/** Global black volume texture resource. */
FTextureWithRDG* GBlackUintVolumeTexture = new TGlobalResource<FBlackVolumeTexture<PF_R8G8B8A8_UINT, 0>>();

class FBlackArrayTexture : public FTexture
{
public:
	// FResource interface.
	virtual void InitRHI() override
	{
		if (GetFeatureLevel() >= ERHIFeatureLevel::SM5)
		{
			// Create the texture RHI.
			FBlackVolumeTextureResourceBulkDataInterface BlackTextureBulkData(0);
			FRHIResourceCreateInfo CreateInfo(&BlackTextureBulkData);
			CreateInfo.DebugName = TEXT("BlackArrayTexture");
			FTexture2DArrayRHIRef TextureArray = RHICreateTexture2DArray(1, 1, 1, PF_B8G8R8A8, 1, 1, TexCreate_ShaderResource, CreateInfo);
			TextureRHI = TextureArray;

			// Create the sampler state RHI resource.
			FSamplerStateInitializerRHI SamplerStateInitializer(SF_Point,AM_Wrap,AM_Wrap,AM_Wrap);
			SamplerStateRHI = GetOrCreateSamplerState(SamplerStateInitializer);
		}
	}

	/** Returns the width of the texture in pixels. */
	virtual uint32 GetSizeX() const override
	{
		return 1;
	}

	/** Returns the height of the texture in pixels. */
	virtual uint32 GetSizeY() const override
	{
		return 1;
	}
};

FTexture* GBlackArrayTexture = new TGlobalResource<FBlackArrayTexture>;

//
// FMipColorTexture implementation
//

/**
 * A texture that has a different solid color in each mip-level
 */
class FMipColorTexture : public FTexture
{
public:
	enum
	{
		NumMips = 12
	};
	static const FColor MipColors[NumMips];

	// FResource interface.
	virtual void InitRHI() override
	{
		// Create the texture RHI.
		int32 TextureSize = 1 << (NumMips - 1);
		FRHIResourceCreateInfo CreateInfo;
		FTexture2DRHIRef Texture2D = RHICreateTexture2D(TextureSize,TextureSize,PF_B8G8R8A8,NumMips,1,TexCreate_ShaderResource,CreateInfo);
		TextureRHI = Texture2D;

		// Write the contents of the texture.
		uint32 DestStride;
		int32 Size = TextureSize;
		for ( int32 MipIndex=0; MipIndex < NumMips; ++MipIndex )
		{
			FColor* DestBuffer = (FColor*)RHILockTexture2D(Texture2D, MipIndex, RLM_WriteOnly, DestStride, false);
			for ( int32 Y=0; Y < Size; ++Y )
			{
				for ( int32 X=0; X < Size; ++X )
				{
					DestBuffer[X] = MipColors[NumMips - 1 - MipIndex];
				}
				DestBuffer += DestStride / sizeof(FColor);
			}
			RHIUnlockTexture2D(Texture2D, MipIndex, false);
			Size >>= 1;
		}

		// Create the sampler state RHI resource.
		FSamplerStateInitializerRHI SamplerStateInitializer(SF_Point,AM_Wrap,AM_Wrap,AM_Wrap);
		SamplerStateRHI = GetOrCreateSamplerState(SamplerStateInitializer);
	}

	/** Returns the width of the texture in pixels. */
	virtual uint32 GetSizeX() const override
	{
		int32 TextureSize = 1 << (NumMips - 1);
		return TextureSize;
	}

	/** Returns the height of the texture in pixels. */
	// PVS-Studio notices that the implementation of GetSizeX is identical to this one
	// and warns us. In this case, it is intentional, so we disable the warning:
	virtual uint32 GetSizeY() const override //-V524
	{
		int32 TextureSize = 1 << (NumMips - 1);
		return TextureSize;
	}
};

const FColor FMipColorTexture::MipColors[NumMips] =
{
	FColor(  80,  80,  80, 0 ),		// Mip  0: 1x1			(dark grey)
	FColor( 200, 200, 200, 0 ),		// Mip  1: 2x2			(light grey)
	FColor( 200, 200,   0, 0 ),		// Mip  2: 4x4			(medium yellow)
	FColor( 255, 255,   0, 0 ),		// Mip  3: 8x8			(yellow)
	FColor( 160, 255,  40, 0 ),		// Mip  4: 16x16		(light green)
	FColor(   0, 255,   0, 0 ),		// Mip  5: 32x32		(green)
	FColor(   0, 255, 200, 0 ),		// Mip  6: 64x64		(cyan)
	FColor(   0, 170, 170, 0 ),		// Mip  7: 128x128		(light blue)
	FColor(  60,  60, 255, 0 ),		// Mip  8: 256x256		(dark blue)
	FColor( 255,   0, 255, 0 ),		// Mip  9: 512x512		(pink)
	FColor( 255,   0,   0, 0 ),		// Mip 10: 1024x1024	(red)
	FColor( 255, 130,   0, 0 ),		// Mip 11: 2048x2048	(orange)
};

RENDERCORE_API FTexture* GMipColorTexture = new FMipColorTexture;
RENDERCORE_API int32 GMipColorTextureMipLevels = FMipColorTexture::NumMips;

// 4: 8x8 cubemap resolution, shader needs to use the same value as preprocessing
RENDERCORE_API const uint32 GDiffuseConvolveMipLevel = 4;

/** A solid color cube texture. */
class FSolidColorTextureCube : public FTexture
{
public:
	FSolidColorTextureCube(const FColor& InColor)
		: bInitToZero(false)
		, PixelFormat(PF_B8G8R8A8)
		, ColorData(InColor.DWColor())
	{}

	FSolidColorTextureCube(EPixelFormat InPixelFormat)
		: bInitToZero(true)
		, PixelFormat(InPixelFormat)
		, ColorData(0)
	{}

	// FRenderResource interface.
	virtual void InitRHI() override
	{
		// Create the texture RHI.
		FRHIResourceCreateInfo CreateInfo(TEXT("SolidColorCube"));
		FTextureCubeRHIRef TextureCube = RHICreateTextureCube(1, PixelFormat, 1, TexCreate_ShaderResource, CreateInfo);
		TextureRHI = TextureCube;

		// Write the contents of the texture.
		for (uint32 FaceIndex = 0; FaceIndex < 6; FaceIndex++)
		{
			uint32 DestStride;
			void* DestBuffer = RHILockTextureCubeFace(TextureCube, FaceIndex, 0, 0, RLM_WriteOnly, DestStride, false);
			if (bInitToZero)
			{
				FMemory::Memzero(DestBuffer, GPixelFormats[PixelFormat].BlockBytes);
			}
			else
			{
				FMemory::Memcpy(DestBuffer, &ColorData, sizeof(ColorData));
			}
			RHIUnlockTextureCubeFace(TextureCube, FaceIndex, 0, 0, false);
		}

		// Create the sampler state RHI resource.
		FSamplerStateInitializerRHI SamplerStateInitializer(SF_Point, AM_Wrap, AM_Wrap, AM_Wrap);
		SamplerStateRHI = GetOrCreateSamplerState(SamplerStateInitializer);
	}

	/** Returns the width of the texture in pixels. */
	virtual uint32 GetSizeX() const override
	{
		return 1;
	}

	/** Returns the height of the texture in pixels. */
	virtual uint32 GetSizeY() const override
	{
		return 1;
	}

private:
	const bool bInitToZero;
	const EPixelFormat PixelFormat;
	const uint32 ColorData;
};

/** A white cube texture. */
class FWhiteTextureCube : public FSolidColorTextureCube
{
public:
	FWhiteTextureCube() : FSolidColorTextureCube(FColor::White) {}
};
FTexture* GWhiteTextureCube = new TGlobalResource<FWhiteTextureCube>;

/** A black cube texture. */
class FBlackTextureCube : public FSolidColorTextureCube
{
public:
	FBlackTextureCube() : FSolidColorTextureCube(FColor::Black) {}
};
FTexture* GBlackTextureCube = new TGlobalResource<FBlackTextureCube>;

/** A black cube texture. */
class FBlackTextureDepthCube : public FSolidColorTextureCube
{
public:
	FBlackTextureDepthCube() : FSolidColorTextureCube(PF_ShadowDepth) {}
};
FTexture* GBlackTextureDepthCube = new TGlobalResource<FBlackTextureDepthCube>;

class FBlackCubeArrayTexture : public FTexture
{
public:
	// FResource interface.
	virtual void InitRHI() override
	{
		if (SupportsTextureCubeArray(GetFeatureLevel() ))
		{
			// Create the texture RHI.
			FRHIResourceCreateInfo CreateInfo(TEXT("BlackCubeArray"));
			FTextureCubeRHIRef TextureCubeArray = RHICreateTextureCubeArray(1,1,PF_B8G8R8A8,1,TexCreate_ShaderResource,CreateInfo);
			TextureRHI = TextureCubeArray;

			for(uint32 FaceIndex = 0;FaceIndex < 6;FaceIndex++)
			{
				uint32 DestStride;
				FColor* DestBuffer = (FColor*)RHILockTextureCubeFace(TextureCubeArray, FaceIndex, 0, 0, RLM_WriteOnly, DestStride, false);
				// Note: alpha is used by reflection environment to say how much of the foreground texture is visible, so 0 says it is completely invisible
				*DestBuffer = FColor(0, 0, 0, 0);
				RHIUnlockTextureCubeFace(TextureCubeArray, FaceIndex, 0, 0, false);
			}

			// Create the sampler state RHI resource.
			FSamplerStateInitializerRHI SamplerStateInitializer(SF_Point,AM_Wrap,AM_Wrap,AM_Wrap);
			SamplerStateRHI = GetOrCreateSamplerState(SamplerStateInitializer);
		}
	}

	/** Returns the width of the texture in pixels. */
	virtual uint32 GetSizeX() const override
	{
		return 1;
	}

	/** Returns the height of the texture in pixels. */
	virtual uint32 GetSizeY() const override
	{
		return 1;
	}
};
FTexture* GBlackCubeArrayTexture = new TGlobalResource<FBlackCubeArrayTexture>;

/**
 * A UINT 1x1 texture.
 */
template <EPixelFormat Format, uint32 R = 0, uint32 G = 0, uint32 B = 0, uint32 A = 0>
class FUintTexture : public FTextureWithSRV
{
public:
	// FResource interface.
	virtual void InitRHI() override
	{
		// Create the texture RHI.  		
		FRHIResourceCreateInfo CreateInfo(TEXT("UintTexture"));
		FTexture2DRHIRef Texture2D = RHICreateTexture2D(1, 1, Format, 1, 1, TexCreate_ShaderResource, CreateInfo);
		TextureRHI = Texture2D;

		// Write the contents of the texture.
		uint32 DestStride;
		void* DestBuffer = RHILockTexture2D(Texture2D, 0, RLM_WriteOnly, DestStride, false);
		WriteData(DestBuffer);
		RHIUnlockTexture2D(Texture2D, 0, false);

		// Create the sampler state RHI resource.
		FSamplerStateInitializerRHI SamplerStateInitializer(SF_Point, AM_Wrap, AM_Wrap, AM_Wrap);
		SamplerStateRHI = GetOrCreateSamplerState(SamplerStateInitializer);

		// Create a view of the texture
		ShaderResourceViewRHI = RHICreateShaderResourceView(TextureRHI, 0u);
	}

	/** Returns the width of the texture in pixels. */
	virtual uint32 GetSizeX() const override
	{
		return 1;
	}

	/** Returns the height of the texture in pixels. */
	virtual uint32 GetSizeY() const override
	{
		return 1;
	}

protected:
	static int32 GetNumChannels()
	{
		return GPixelFormats[Format].NumComponents;
	}

	static int32 GetBytesPerChannel()
	{
		return GPixelFormats[Format].BlockBytes / GPixelFormats[Format].NumComponents;
	}

	template<typename T>
	static void DoWriteData(T* DataPtr)
	{
		T Values[] = { R, G, B, A };
		for (int32 i = 0; i < GetNumChannels(); ++i)
		{
			DataPtr[i] = Values[i];
		}
	}

	static void WriteData(void* DataPtr)
	{
		switch (GetBytesPerChannel())
		{
		case 1: 
			DoWriteData((uint8*)DataPtr);
			return;
		case 2:
			DoWriteData((uint16*)DataPtr);
			return;
		case 4:
			DoWriteData((uint32*)DataPtr);
			return;
		}
		// Unsupported format
		check(0);
	}
};

FTexture* GBlackUintTexture = new TGlobalResource< FUintTexture<PF_R32G32B32A32_UINT> >;

/*
	3 XYZ packed in 4 bytes. (11:11:10 for X:Y:Z)
*/

/**
*	operator FVector - unpacked to -1 to 1
*/
FPackedPosition::operator FVector() const
{

	return FVector(Vector.X/1023.f, Vector.Y/1023.f, Vector.Z/511.f);
}

/**
* operator VectorRegister
*/
VectorRegister FPackedPosition::GetVectorRegister() const
{
	FVector UnpackedVect = *this;

	VectorRegister VectorToUnpack = VectorLoadFloat3_W0(&UnpackedVect);

	return VectorToUnpack;
}

/**
* Pack this vector(-1 to 1 for XYZ) to 4 bytes XYZ(11:11:10)
*/
void FPackedPosition::Set( const FVector& InVector )
{
	check (FMath::Abs<float>(InVector.X) <= 1.f && FMath::Abs<float>(InVector.Y) <= 1.f &&  FMath::Abs<float>(InVector.Z) <= 1.f);
	
#if !WITH_EDITORONLY_DATA
	// This should not happen in Console - this should happen during Cooking in PC
	check (false);
#else
	// Too confusing to use .5f - wanted to use the last bit!
	// Change to int for easier read
	Vector.X = FMath::Clamp<int32>(FMath::TruncToInt(InVector.X * 1023.0f),-1023,1023);
	Vector.Y = FMath::Clamp<int32>(FMath::TruncToInt(InVector.Y * 1023.0f),-1023,1023);
	Vector.Z = FMath::Clamp<int32>(FMath::TruncToInt(InVector.Z * 511.0f),-511,511);
#endif
}

/**
* operator << serialize
*/
FArchive& operator<<(FArchive& Ar,FPackedPosition& N)
{
	// Save N.Packed
	return Ar << N.Packed;
}

void CalcMipMapExtent3D( uint32 TextureSizeX, uint32 TextureSizeY, uint32 TextureSizeZ, EPixelFormat Format, uint32 MipIndex, uint32& OutXExtent, uint32& OutYExtent, uint32& OutZExtent )
{
	OutXExtent = FMath::Max<uint32>(TextureSizeX >> MipIndex, GPixelFormats[Format].BlockSizeX);
	OutYExtent = FMath::Max<uint32>(TextureSizeY >> MipIndex, GPixelFormats[Format].BlockSizeY);
	OutZExtent = FMath::Max<uint32>(TextureSizeZ >> MipIndex, GPixelFormats[Format].BlockSizeZ);
}

SIZE_T CalcTextureMipMapSize3D( uint32 TextureSizeX, uint32 TextureSizeY, uint32 TextureSizeZ, EPixelFormat Format, uint32 MipIndex )
{
	uint32 XExtent;
	uint32 YExtent;
	uint32 ZExtent;
	CalcMipMapExtent3D(TextureSizeX, TextureSizeY, TextureSizeZ, Format, MipIndex, XExtent, YExtent, ZExtent);

	// Offset MipExtent to round up result
	XExtent += GPixelFormats[Format].BlockSizeX - 1;
	YExtent += GPixelFormats[Format].BlockSizeY - 1;
	ZExtent += GPixelFormats[Format].BlockSizeZ - 1;

	const uint32 XPitch = (XExtent / GPixelFormats[Format].BlockSizeX) * GPixelFormats[Format].BlockBytes;
	const uint32 NumRows = YExtent / GPixelFormats[Format].BlockSizeY;
	const uint32 NumLayers = ZExtent / GPixelFormats[Format].BlockSizeZ;

	return static_cast<SIZE_T>(NumLayers) * NumRows * XPitch;
}

SIZE_T CalcTextureSize3D( uint32 SizeX, uint32 SizeY, uint32 SizeZ, EPixelFormat Format, uint32 MipCount )
{
	SIZE_T Size = 0;
	for ( uint32 MipIndex=0; MipIndex < MipCount; ++MipIndex )
	{
		Size += CalcTextureMipMapSize3D(SizeX,SizeY,SizeZ,Format,MipIndex);
	}
	return Size;
}

FIntPoint CalcMipMapExtent( uint32 TextureSizeX, uint32 TextureSizeY, EPixelFormat Format, uint32 MipIndex )
{
	return FIntPoint(FMath::Max<uint32>(TextureSizeX >> MipIndex, GPixelFormats[Format].BlockSizeX), FMath::Max<uint32>(TextureSizeY >> MipIndex, GPixelFormats[Format].BlockSizeY));
}

SIZE_T CalcTextureMipWidthInBlocks(uint32 TextureSizeX, EPixelFormat Format, uint32 MipIndex)
{
	const uint32 BlockSizeX = GPixelFormats[Format].BlockSizeX;
	const uint32 WidthInTexels = FMath::Max<uint32>(TextureSizeX >> MipIndex, 1);
	const uint32 WidthInBlocks = (WidthInTexels + BlockSizeX - 1) / BlockSizeX;
	return WidthInBlocks;
}

SIZE_T CalcTextureMipHeightInBlocks(uint32 TextureSizeY, EPixelFormat Format, uint32 MipIndex)
{
	const uint32 BlockSizeY = GPixelFormats[Format].BlockSizeY;
	const uint32 HeightInTexels = FMath::Max<uint32>(TextureSizeY >> MipIndex, 1);
	const uint32 HeightInBlocks = (HeightInTexels + BlockSizeY - 1) / BlockSizeY;
	return HeightInBlocks;
}

SIZE_T CalcTextureMipMapSize( uint32 TextureSizeX, uint32 TextureSizeY, EPixelFormat Format, uint32 MipIndex )
{
	const uint32 WidthInBlocks = CalcTextureMipWidthInBlocks(TextureSizeX, Format, MipIndex);
	const uint32 HeightInBlocks = CalcTextureMipHeightInBlocks(TextureSizeY, Format, MipIndex);
	return static_cast<SIZE_T>(WidthInBlocks) * HeightInBlocks * GPixelFormats[Format].BlockBytes;
}

SIZE_T CalcTextureSize( uint32 SizeX, uint32 SizeY, EPixelFormat Format, uint32 MipCount )
{
	SIZE_T Size = 0;
	for ( uint32 MipIndex=0; MipIndex < MipCount; ++MipIndex )
	{
		Size += CalcTextureMipMapSize(SizeX,SizeY,Format,MipIndex);
	}
	return Size;
}

void CopyTextureData2D(const void* Source,void* Dest,uint32 SizeY,EPixelFormat Format,uint32 SourceStride,uint32 DestStride)
{
	const uint32 BlockSizeY = GPixelFormats[Format].BlockSizeY;
	const uint32 NumBlocksY = (SizeY + BlockSizeY - 1) / BlockSizeY;

	// a DestStride of 0 means to use the SourceStride
	if(SourceStride == DestStride || DestStride == 0)
	{
		// If the source and destination have the same stride, copy the data in one block.
		if (ensure(Source))
		{
			FMemory::Memcpy(Dest,Source,NumBlocksY * SourceStride);
		}
		else
		{
			FMemory::Memzero(Dest,NumBlocksY * SourceStride);
		}
	}
	else
	{
		// If the source and destination have different strides, copy each row of blocks separately.
		const uint32 NumBytesPerRow = FMath::Min<uint32>(SourceStride, DestStride);
		for(uint32 BlockY = 0;BlockY < NumBlocksY;++BlockY)
		{
			if (ensure(Source))
			{
				FMemory::Memcpy(
					(uint8*)Dest   + DestStride   * BlockY,
					(uint8*)Source + SourceStride * BlockY,
					NumBytesPerRow
					);
			}
			else
			{
				FMemory::Memzero((uint8*)Dest + DestStride * BlockY, NumBytesPerRow);
			}
		}
	}
}

/** Helper functions for text output of texture properties... */
#ifndef CASE_ENUM_TO_TEXT
#define CASE_ENUM_TO_TEXT(txt) case txt: return TEXT(#txt);
#endif

#ifndef TEXT_TO_ENUM
#define TEXT_TO_ENUM(eVal, txt)		if (FCString::Stricmp(TEXT(#eVal), txt) == 0)	return eVal;
#endif

const TCHAR* GetPixelFormatString(EPixelFormat InPixelFormat)
{
	switch (InPixelFormat)
	{
		FOREACH_ENUM_EPIXELFORMAT(CASE_ENUM_TO_TEXT)
	default:
		return TEXT("PF_Unknown");
	}	
}

EPixelFormat GetPixelFormatFromString(const TCHAR* InPixelFormatStr)
{
#define TEXT_TO_PIXELFORMAT(f) TEXT_TO_ENUM(f, InPixelFormatStr);
	FOREACH_ENUM_EPIXELFORMAT(TEXT_TO_PIXELFORMAT)
#undef TEXT_TO_PIXELFORMAT
	return PF_Unknown;
}


const TCHAR* GetCubeFaceName(ECubeFace Face)
{
	switch(Face)
	{
	case CubeFace_PosX:
		return TEXT("PosX");
	case CubeFace_NegX:
		return TEXT("NegX");
	case CubeFace_PosY:
		return TEXT("PosY");
	case CubeFace_NegY:
		return TEXT("NegY");
	case CubeFace_PosZ:
		return TEXT("PosZ");
	case CubeFace_NegZ:
		return TEXT("NegZ");
	default:
		return TEXT("");
	}
}

ECubeFace GetCubeFaceFromName(const FString& Name)
{
	// not fast but doesn't have to be
	if(Name.EndsWith(TEXT("PosX")))
	{
		return CubeFace_PosX;
	}
	else if(Name.EndsWith(TEXT("NegX")))
	{
		return CubeFace_NegX;
	}
	else if(Name.EndsWith(TEXT("PosY")))
	{
		return CubeFace_PosY;
	}
	else if(Name.EndsWith(TEXT("NegY")))
	{
		return CubeFace_NegY;
	}
	else if(Name.EndsWith(TEXT("PosZ")))
	{
		return CubeFace_PosZ;
	}
	else if(Name.EndsWith(TEXT("NegZ")))
	{
		return CubeFace_NegZ;
	}

	return CubeFace_MAX;
}

class FVector4VertexDeclaration : public FRenderResource
{
public:
	FVertexDeclarationRHIRef VertexDeclarationRHI;
	virtual void InitRHI() override
	{
		FVertexDeclarationElementList Elements;
		Elements.Add(FVertexElement(0, 0, VET_Float4, 0, sizeof(FVector4)));
		VertexDeclarationRHI = PipelineStateCache::GetOrCreateVertexDeclaration(Elements);
	}
	virtual void ReleaseRHI() override
	{
		VertexDeclarationRHI.SafeRelease();
	}
};

TGlobalResource<FVector4VertexDeclaration> GVector4VertexDeclaration;

RENDERCORE_API FVertexDeclarationRHIRef& GetVertexDeclarationFVector4()
{
	return GVector4VertexDeclaration.VertexDeclarationRHI;
}

class FVector3VertexDeclaration : public FRenderResource
{
public:
	FVertexDeclarationRHIRef VertexDeclarationRHI;
	virtual void InitRHI() override
	{
		FVertexDeclarationElementList Elements;
		Elements.Add(FVertexElement(0, 0, VET_Float3, 0, sizeof(FVector)));
		VertexDeclarationRHI = PipelineStateCache::GetOrCreateVertexDeclaration(Elements);
	}
	virtual void ReleaseRHI() override
	{
		VertexDeclarationRHI.SafeRelease();
	}
};

TGlobalResource<FVector3VertexDeclaration> GVector3VertexDeclaration;

RENDERCORE_API FVertexDeclarationRHIRef& GetVertexDeclarationFVector3()
{
	return GVector3VertexDeclaration.VertexDeclarationRHI;
}

class FVector2VertexDeclaration : public FRenderResource
{
public:
	FVertexDeclarationRHIRef VertexDeclarationRHI;
	virtual void InitRHI() override
	{
		FVertexDeclarationElementList Elements;
		Elements.Add(FVertexElement(0, 0, VET_Float2, 0, sizeof(FVector2D)));
		VertexDeclarationRHI = PipelineStateCache::GetOrCreateVertexDeclaration(Elements);
	}
	virtual void ReleaseRHI() override
	{
		VertexDeclarationRHI.SafeRelease();
	}
};

TGlobalResource<FVector2VertexDeclaration> GVector2VertexDeclaration;

RENDERCORE_API FVertexDeclarationRHIRef& GetVertexDeclarationFVector2()
{
	return GVector2VertexDeclaration.VertexDeclarationRHI;
}

RENDERCORE_API bool PlatformSupportsSimpleForwardShading(const FStaticShaderPlatform Platform)
{
	static const auto SupportSimpleForwardShadingCVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.SupportSimpleForwardShading"));
	// Scalability feature only needed / used on PC
	return IsPCPlatform(Platform) && SupportSimpleForwardShadingCVar->GetValueOnAnyThread() != 0;
}

RENDERCORE_API bool IsSimpleForwardShadingEnabled(const FStaticShaderPlatform Platform)
{
	static const auto CVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.SimpleForwardShading"));
	return CVar->GetValueOnAnyThread() != 0 && PlatformSupportsSimpleForwardShading(Platform);
}

RENDERCORE_API bool MobileSupportsGPUScene(const FStaticShaderPlatform Platform)
{
	// make it shader platform setting?
	static TConsoleVariableData<int32>* CVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.Mobile.SupportGPUScene"));
	return (CVar && CVar->GetValueOnAnyThread() != 0) ? true : false;
}

RENDERCORE_API bool IsMobileDeferredShadingEnabled(const FStaticShaderPlatform Platform)
{
	if (IsOpenGLPlatform(Platform))
	{
		// needs MRT framebuffer fetch or PLS
		return false;
	}
	static auto* MobileShadingPathCvar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.Mobile.ShadingPath"));
	return MobileShadingPathCvar->GetValueOnAnyThread() == 1;
}

RENDERCORE_API bool SupportsTextureCubeArray(ERHIFeatureLevel::Type FeatureLevel)
{
	return FeatureLevel == ERHIFeatureLevel::SM5 
		// mobile deferred requries ES3.2 feature set
		|| IsMobileDeferredShadingEnabled(GMaxRHIShaderPlatform);
}

RENDERCORE_API bool GPUSceneUseTexture2D(const FStaticShaderPlatform Platform)
{
	if (IsMobilePlatform(Platform))
	{
		static TConsoleVariableData<int32>* CVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.Mobile.UseGPUSceneTexture"));
		if (Platform == SP_OPENGL_ES3_1_ANDROID)
		{
			return true;
		}
		else
		{
			return (CVar && CVar->GetValueOnAnyThread() != 0) ? true : false;
		}
	}
	return false;
}

RENDERCORE_API bool MaskedInEarlyPass(const FStaticShaderPlatform Platform)
{
	static IConsoleVariable* CVarMobileEarlyZPassOnlyMaterialMasking = IConsoleManager::Get().FindConsoleVariable(TEXT("r.Mobile.EarlyZPassOnlyMaterialMasking"));
	static IConsoleVariable* CVarEarlyZPassOnlyMaterialMasking = IConsoleManager::Get().FindConsoleVariable(TEXT("r.EarlyZPassOnlyMaterialMasking"));
	if (IsMobilePlatform(Platform))
	{
		return (CVarMobileEarlyZPassOnlyMaterialMasking && CVarMobileEarlyZPassOnlyMaterialMasking->GetInt() != 0);
	}
	else
	{
		return (CVarEarlyZPassOnlyMaterialMasking && CVarEarlyZPassOnlyMaterialMasking->GetInt() != 0);
	}
}

RENDERCORE_API bool AllowPixelDepthOffset(const FStaticShaderPlatform Platform)
{
	if (IsMobilePlatform(Platform))
	{
		static const auto CVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.Mobile.AllowPixelDepthOffset"));
		return CVar->GetValueOnAnyThread() != 0;
	}
	return true;
}

RENDERCORE_API bool AllowScreenSpaceReflection(const FStaticShaderPlatform Platform)
{
	if (IsMobilePlatform(Platform))
	{
		static const auto CVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.Mobile.AllowSSR"));
		return CVar->GetValueOnAnyThread() != 0;
	}
	return false;
}

RENDERCORE_API bool MobileSupportVolumetricFog(const FStaticShaderPlatform Platform)
{
	static TConsoleVariableData<int32>* CVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.Mobile.VolumetricFog"));
	return (CVar && CVar->GetValueOnAnyThread() != 0) ? true : false;
}

RENDERCORE_API bool MobileUseStandaloneTAA(const FStaticShaderPlatform Platform)
{
	static TConsoleVariableData<int32>* CVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.Mobile.UseStandaloneTAA"));
	return (CVar && CVar->GetValueOnAnyThread() != 0) ? true : false;
}

RENDERCORE_API int32 GUseForwardShading = 0;
static FAutoConsoleVariableRef CVarForwardShading(
	TEXT("r.ForwardShading"),
	GUseForwardShading,
	TEXT("Whether to use forward shading on desktop platforms - requires Shader Model 5 hardware.\n")
	TEXT("Forward shading has lower constant cost, but fewer features supported. 0:off, 1:on\n")
	TEXT("This rendering path is a work in progress with many unimplemented features, notably only a single reflection capture is applied per object and no translucency dynamic shadow receiving."),
	ECVF_RenderThreadSafe | ECVF_ReadOnly
	); 

static TAutoConsoleVariable<int32> CVarDistanceFields(
	TEXT("r.DistanceFields"),
	1,
	TEXT("Enables distance fields rendering.\n") \
	TEXT(" 0: Disabled.\n") \
	TEXT(" 1: Enabled."),
	ECVF_RenderThreadSafe | ECVF_ReadOnly
	); 


RENDERCORE_API uint64 GForwardShadingPlatformMask = 0;
static_assert(SP_NumPlatforms <= sizeof(GForwardShadingPlatformMask) * 8, "GForwardShadingPlatformMask must be large enough to support all shader platforms");

RENDERCORE_API uint64 GDBufferPlatformMask = 0;
static_assert(SP_NumPlatforms <= sizeof(GDBufferPlatformMask) * 8, "GDBufferPlatformMask must be large enough to support all shader platforms");

RENDERCORE_API uint64 GBasePassVelocityPlatformMask = 0;
static_assert(SP_NumPlatforms <= sizeof(GBasePassVelocityPlatformMask) * 8, "GBasePassVelocityPlatformMask must be large enough to support all shader platforms");

RENDERCORE_API uint64 GSelectiveBasePassOutputsPlatformMask = 0;
static_assert(SP_NumPlatforms <= sizeof(GSelectiveBasePassOutputsPlatformMask) * 8, "GSelectiveBasePassOutputsPlatformMask must be large enough to support all shader platforms");

RENDERCORE_API uint64 GDistanceFieldsPlatformMask = 0;
static_assert(SP_NumPlatforms <= sizeof(GDistanceFieldsPlatformMask) * 8, "GDistanceFieldsPlatformMask must be large enough to support all shader platforms");

RENDERCORE_API uint64 GSimpleSkyDiffusePlatformMask = 0;
static_assert(SP_NumPlatforms <= sizeof(GSimpleSkyDiffusePlatformMask) * 8, "GSimpleSkyDiffusePlatformMask must be large enough to support all shader platforms");

// Specifies whether ray tracing *can* be enabled on a particular platform.
// This takes into account whether RT is globally enabled for the project and specifically enabled on a target platform.
// Safe to use to make cook-time decisions, such as whether to compile ray tracing shaders.
RENDERCORE_API uint64 GRayTracingPlaformMask = 0;
static_assert(SP_NumPlatforms <= sizeof(GRayTracingPlaformMask) * 8, "GRayTracingPlaformMask must be large enough to support all shader platforms");

// Specifies whether ray tracing *is* enabled on the current running system (in current game or editor process).
// This takes into account additional factors, such as concrete current GPU/OS/Driver capability, user-set game graphics options, etc.
// Only safe to make run-time decisions, such as whether to build acceleration structures and render ray tracing effects.
// Value may be queried using IsRayTracingEnabled().
RENDERCORE_API bool GUseRayTracing = false;

RENDERCORE_API void RenderUtilsInit()
{
	checkf(GIsRHIInitialized, TEXT("RenderUtilsInit() may only be called once RHI is initialized."));

	if (GUseForwardShading)
	{
		GForwardShadingPlatformMask = ~0ull;
	}

	static IConsoleVariable* DBufferVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.DBuffer"));
	if (DBufferVar && DBufferVar->GetInt())
	{
		GDBufferPlatformMask = ~0ull;
	}

	static IConsoleVariable* BasePassVelocityCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.BasePassOutputsVelocity"));
	if (BasePassVelocityCVar && BasePassVelocityCVar->GetInt())
	{
		GBasePassVelocityPlatformMask = ~0ull;
	}

	static IConsoleVariable* SelectiveBasePassOutputsCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.SelectiveBasePassOutputs"));
	if (SelectiveBasePassOutputsCVar && SelectiveBasePassOutputsCVar->GetInt())
	{
		GSelectiveBasePassOutputsPlatformMask = ~0ull;
	}

	static IConsoleVariable* DistanceFieldsCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.DistanceFields")); 
	if (DistanceFieldsCVar && DistanceFieldsCVar->GetInt())
	{
		GDistanceFieldsPlatformMask = ~0ull;
	}

	static IConsoleVariable* RayTracingCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.RayTracing"));
	if (RayTracingCVar && RayTracingCVar->GetInt())
	{
		GRayTracingPlaformMask = ~0ull;
	}

#if WITH_EDITOR
	ITargetPlatformManagerModule* TargetPlatformManager = GetTargetPlatformManager();
	if (TargetPlatformManager)
	{
		for (uint32 ShaderPlatformIndex = 0; ShaderPlatformIndex < SP_NumPlatforms; ++ShaderPlatformIndex)
		{
			EShaderPlatform ShaderPlatform = EShaderPlatform(ShaderPlatformIndex);
			FName PlatformName = ShaderPlatformToPlatformName(ShaderPlatform);
			ITargetPlatform* TargetPlatform = TargetPlatformManager->FindTargetPlatform(PlatformName.ToString());
			if (TargetPlatform)
			{
				uint64 Mask = 1ull << ShaderPlatformIndex;

				if (TargetPlatform->UsesForwardShading())
				{
					GForwardShadingPlatformMask |= Mask;
				}
				else
				{
					GForwardShadingPlatformMask &= ~Mask;
				}

				if (TargetPlatform->UsesDBuffer())
				{
					GDBufferPlatformMask |= Mask;
				}
				else
				{
					GDBufferPlatformMask &= ~Mask;
				}

				if (TargetPlatform->UsesBasePassVelocity())
				{
					GBasePassVelocityPlatformMask |= Mask;
				}
				else
				{
					GBasePassVelocityPlatformMask &= ~Mask;
				}

				if (TargetPlatform->UsesSelectiveBasePassOutputs())
				{
					GSelectiveBasePassOutputsPlatformMask |= Mask;
				}
				else
				{
					GSelectiveBasePassOutputsPlatformMask &= ~Mask;
				}

				if (TargetPlatform->UsesDistanceFields())
				{
					GDistanceFieldsPlatformMask |= Mask;
				}
				else
				{
					GDistanceFieldsPlatformMask &= ~Mask;
				}

				if (TargetPlatform->UsesRayTracing())
				{
					GRayTracingPlaformMask |= Mask;
				}
				else
				{
					GRayTracingPlaformMask &= ~Mask;
				}

				if (TargetPlatform->ForcesSimpleSkyDiffuse())
				{
					GSimpleSkyDiffusePlatformMask |= Mask;
				}
				else
				{
					GSimpleSkyDiffusePlatformMask &= ~Mask;
				}
			}
		}
	}
#endif // WITH_EDITOR

	// Run-time ray tracing support depends on the following factors:
	// - Ray tracing must be enabled for the project
	// - Skin cache must be enabled for the project
	// - Current GPU, OS and driver must support ray tracing
	// - User is running the Editor *OR* running the game with ray tracing enabled in graphics options

	// When ray tracing is enabled, we must load additional shaders and build acceleration structures for meshes.
	// For this reason it is only possible to enable RT at startup and changing the state requires restart.
	// This is also the reason why IsRayTracingEnabled() lives in RenderCore module, as it controls creation of 
	// RT pipelines in ShaderPipelineCache.cpp.

	if (RayTracingCVar && RayTracingCVar->GetBool())
	{
		const bool bRayTracingAllowedOnCurrentPlatform = !!(GRayTracingPlaformMask & (1ull << GMaxRHIShaderPlatform));
		if (GRHISupportsRayTracing && bRayTracingAllowedOnCurrentPlatform)
		{
			if (GIsEditor)
			{
				// Ray tracing is enabled for the project and we are running on RT-capable machine,
				// therefore the core ray tracing features are also enabled, so that required shaders
				// are loaded, acceleration structures are built, etc.
				GUseRayTracing = true;

				UE_LOG(LogRendererCore, Log, TEXT("Ray tracing is enabled for the editor. Reason: r.RayTracing=1."));
			}
			else
			{
				// If user preference exists in game settings file, the bRayTracingEnabled will be set based on its value.
				// Otherwise the current value is preserved.
				if (GConfig->GetBool(TEXT("RayTracing"), TEXT("r.RayTracing.EnableInGame"), GUseRayTracing, GGameUserSettingsIni))
				{
					UE_LOG(LogRendererCore, Log, TEXT("Ray tracing is %s for the game. Reason: user setting r.RayTracing.EnableInGame=%d."),
						GUseRayTracing ? TEXT("enabled") : TEXT("disabled"),
						(int)GUseRayTracing);
				}
				else
				{
					GUseRayTracing = true;

					UE_LOG(LogRendererCore, Log, TEXT("Ray tracing is enabled for the game. Reason: r.RayTracing=1 and r.RayTracing.EnableInGame is not present (default true)."));
				}
			}

			// Sanity check: skin cache is *required* for ray tracing.
			// It can be dynamically enabled only when its shaders have been compiled.
			IConsoleVariable* SkinCacheCompileShadersCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.SkinCache.CompileShaders"));
			if (GUseRayTracing && SkinCacheCompileShadersCVar->GetInt() <= 0)
			{
				GUseRayTracing = false;

				UE_LOG(LogRendererCore, Fatal, TEXT("Ray tracing requires skin cache to be enabled. Set r.SkinCache.CompileShaders=1."));
			}

		}
		else
		{
			if (!GRHISupportsRayTracing)
			{
				UE_LOG(LogRendererCore, Log, TEXT("Ray tracing is disabled. Reason: not supported by current RHI."));
			}
			else
			{
				UE_LOG(LogRendererCore, Log, TEXT("Ray tracing is disabled. Reason: disabled on current platform."));
			}
		}
	}
	else
	{
		UE_LOG(LogRendererCore, Log, TEXT("Ray tracing is disabled. Reason: r.RayTracing=0."));
	}
}

class FUnitCubeVertexBuffer : public FVertexBuffer
{
public:
	/**
	* Initialize the RHI for this rendering resource
	*/
	void InitRHI() override
	{
		const int32 NumVerts = 8;
		TResourceArray<FVector4, VERTEXBUFFER_ALIGNMENT> Verts;
		Verts.SetNumUninitialized(NumVerts);

		for (uint32 Z = 0; Z < 2; Z++)
		{
			for (uint32 Y = 0; Y < 2; Y++)
			{
				for (uint32 X = 0; X < 2; X++)
				{
					const FVector4 Vertex = FVector4(
					  (X ? -1 : 1),
					  (Y ? -1 : 1),
					  (Z ? -1 : 1),
					  1.0f
					);

					Verts[GetCubeVertexIndex(X, Y, Z)] = Vertex;
				}
			}
		}

		uint32 Size = Verts.GetResourceDataSize();

		// Create vertex buffer. Fill buffer with initial data upon creation
		FRHIResourceCreateInfo CreateInfo(&Verts);
		VertexBufferRHI = RHICreateVertexBuffer(Size, BUF_Static, CreateInfo);
	}
};

class FUnitCubeIndexBuffer : public FIndexBuffer
{
public:
	/**
	* Initialize the RHI for this rendering resource
	*/
	void InitRHI() override
	{
		TResourceArray<uint16, INDEXBUFFER_ALIGNMENT> Indices;
		
		int32 NumIndices = UE_ARRAY_COUNT(GCubeIndices);
		Indices.AddUninitialized(NumIndices);
		FMemory::Memcpy(Indices.GetData(), GCubeIndices, NumIndices * sizeof(uint16));

		const uint32 Size = Indices.GetResourceDataSize();
		const uint32 Stride = sizeof(uint16);

		// Create index buffer. Fill buffer with initial data upon creation
		FRHIResourceCreateInfo CreateInfo(&Indices);
		IndexBufferRHI = RHICreateIndexBuffer(Stride, Size, BUF_Static, CreateInfo);
	}
};

static TGlobalResource<FUnitCubeVertexBuffer> GUnitCubeVertexBuffer;
static TGlobalResource<FUnitCubeIndexBuffer> GUnitCubeIndexBuffer;

RENDERCORE_API FVertexBufferRHIRef& GetUnitCubeVertexBuffer()
{
	return GUnitCubeVertexBuffer.VertexBufferRHI;
}

RENDERCORE_API FIndexBufferRHIRef& GetUnitCubeIndexBuffer()
{
	return GUnitCubeIndexBuffer.IndexBufferRHI;
}

RENDERCORE_API void QuantizeSceneBufferSize(const FIntPoint& InBufferSize, FIntPoint& OutBufferSize)
{
	// Ensure sizes are dividable by the ideal group size for 2d tiles to make it more convenient.
	const uint32 DividableBy = 4;

	static_assert(DividableBy % 4 == 0, "A lot of graphic algorithms where previously assuming DividableBy == 4");

	const uint32 Mask = ~(DividableBy - 1);
	OutBufferSize.X = (InBufferSize.X + DividableBy - 1) & Mask;
	OutBufferSize.Y = (InBufferSize.Y + DividableBy - 1) & Mask;
}

RENDERCORE_API bool UseVirtualTexturing(const FStaticFeatureLevel InFeatureLevel, const ITargetPlatform* TargetPlatform)
{
#if !PLATFORM_SUPPORTS_VIRTUAL_TEXTURE_STREAMING
	if (GIsEditor == false)
	{
		return false;
	}
	else
#endif
	{
		// does the platform supports it.
#if WITH_EDITOR
		if (GIsEditor && TargetPlatform == nullptr)
		{
			ITargetPlatformManagerModule* TPM = GetTargetPlatformManager();
			if (TPM)
			{
				TargetPlatform = TPM->GetRunningTargetPlatform();
			}
		}

		if (TargetPlatform && TargetPlatform->SupportsFeature(ETargetPlatformFeatures::VirtualTextureStreaming) == false)
		{
			return false;
		}
#endif

		// does the project has it enabled ?
		static const auto CVarVirtualTexture = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.VirtualTextures"));
		check(CVarVirtualTexture);
		if (CVarVirtualTexture->GetValueOnAnyThread() == 0)
		{
			return false;
		}		

		// mobile needs an additional switch to enable VT		
		static const auto CVarMobileVirtualTexture = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.Mobile.VirtualTextures"));
		if (InFeatureLevel == ERHIFeatureLevel::ES3_1 && CVarMobileVirtualTexture->GetValueOnAnyThread() == 0)
		{
			return false;
		}

		return true;
	}
}
