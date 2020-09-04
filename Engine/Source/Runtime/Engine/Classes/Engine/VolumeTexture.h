// Copyright Epic Games, Inc. All Rights Reserved.


#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Engine/Texture2D.h"
#include "VolumeTexture.generated.h"

extern bool GSupportsVolumeTextureStreaming;

class FTextureResource;

UCLASS(hidecategories=(Object, Compositing, ImportSettings), MinimalAPI)
class UVolumeTexture : public UTexture
{
	GENERATED_UCLASS_BODY()

public:
	/** Platform data. */
	FTexturePlatformData* PlatformData;
	TMap<FString, FTexturePlatformData*> CookedPlatformData;

#if WITH_EDITORONLY_DATA
	/** A (optional) reference texture from which the volume texture was built */
	UPROPERTY(EditAnywhere, Category=Source2D, meta=(DisplayName="Source Texture"))
	UTexture2D* Source2DTexture;
	/** The lighting Guid of the source 2D texture, used to trigger rebuild when the source changes. */
	UPROPERTY()
	FGuid SourceLightingGuid;
	UPROPERTY(EditAnywhere, Category=Source2D, meta=(DisplayName="Tile Size X"))
	/** The reference texture tile size X */
	int32 Source2DTileSizeX;
	/** The reference texture tile size Y */
	UPROPERTY(EditAnywhere, Category=Source2D, meta=(DisplayName="Tile Size Y"))
	int32 Source2DTileSizeY;
#endif

	ENGINE_API bool UpdateSourceFromSourceTexture();
	
	/**
	 * Updates a volume texture from a user function, which allows for arbitrary UVolumeTexture objects to be filled.
	 * The pointer passed to the user function is owned by UVolumeTexture and should be cast to the required type
	 * before being filled up with the new voxel data.
	 * @param	Func	A function taking the x, y, z position as input and returning the texture data for this position.
	 * @param	SizeX	The width of the volume texture.
	 * @param	SizeY	The height of the volume texture.
	 * @param	SizeZ	The depth of the volume texture.
	 * @param	Format	The input volume texture format.
	 */
	ENGINE_API bool UpdateSourceFromFunction(TFunction<void(int32 /*PosX*/, int32 /*PosY*/, int32 /*PosZ*/, void* /*OutValue*/)> Func, int32 SizeX, int32 SizeY, int32 SizeZ, ETextureSourceFormat Format);

	//~ Begin UObject Interface.
	virtual void Serialize(FArchive& Ar) override;
	virtual void PostLoad() override;
	virtual void GetAssetRegistryTags(TArray<FAssetRegistryTag>& OutTags) const override;
	virtual FString GetDesc() override;
	virtual void GetResourceSizeEx(FResourceSizeEx& CumulativeResourceSize) override;
	//~ End UObject Interface.

	/** Trivial accessors. */
	FORCEINLINE int32 GetSizeX() const
	{
		return PlatformData ? PlatformData->SizeX : 0;
	}
	FORCEINLINE int32 GetSizeY() const
	{
		return PlatformData ? PlatformData->SizeY : 0;
	}
	FORCEINLINE int32 GetSizeZ() const
	{
		return PlatformData ? PlatformData->GetNumSlices() : 0;
	}
	FORCEINLINE int32 GetNumMips() const
	{
		return PlatformData ? PlatformData->Mips.Num() : 0;
	}
	FORCEINLINE EPixelFormat GetPixelFormat() const
	{
		return PlatformData ? PlatformData->PixelFormat : PF_Unknown;
	}

	//~ Begin UTexture Interface
	virtual float GetSurfaceWidth() const override { return GetSizeX(); }
	virtual float GetSurfaceHeight() const override { return GetSizeY(); }
	virtual FTextureResource* CreateResource() override;
#if WITH_EDITOR
	ENGINE_API void SetDefaultSource2DTileSize();
	ENGINE_API virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif // WITH_EDITOR
	virtual void UpdateResource() override;
	virtual EMaterialValueType GetMaterialType() const override { return MCT_VolumeTexture; }
	virtual FTexturePlatformData** GetRunningPlatformData() override { return &PlatformData; }
	virtual TMap<FString, FTexturePlatformData*> *GetCookedPlatformData() override { return &CookedPlatformData; }
	//~ End UTexture Interface
	
	/**
	 * Calculates the size of this texture in bytes if it had MipCount miplevels streamed in.
	 *
	 * @param	MipCount	Number of mips to calculate size for, counting from the smallest 1x1 mip-level and up.
	 * @return	Size of MipCount mips in bytes
	 */
	uint32 CalcTextureMemorySize(int32 MipCount) const;

	/**
	 * Calculates the size of this texture if it had MipCount miplevels streamed in.
	 *
	 * @param	Enum	Which mips to calculate size for.
	 * @return	Total size of all specified mips, in bytes
	 */
	virtual uint32 CalcTextureMemorySizeEnum( ETextureMipCount Enum ) const override;

#if WITH_EDITOR
	/**
	* Return maximum dimension for this texture type.
	*/
	virtual uint32 GetMaximumDimension() const override;

#endif

	ENGINE_API static bool ShaderPlatformSupportsCompression(FStaticShaderPlatform ShaderPlatform);


	//~ Begin UStreamableRenderAsset Interface
	virtual int32 CalcCumulativeLODSize(int32 NumLODs) const final override { return CalcTextureMemorySize(NumLODs); }
	virtual bool StreamOut(int32 NewMipCount) final override;
	virtual bool StreamIn(int32 NewMipCount, bool bHighPrio) final override;
	//~ End UStreamableRenderAsset Interface

protected:

#if WITH_EDITOR
	void UpdateMipGenSettings();
#endif
};



