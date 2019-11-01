// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Engine/Texture2D.h"
#include "Texture2DArray.generated.h"

#define MAX_ARRAY_SLICES 512

UCLASS(HideCategories = Object, MinimalAPI, BlueprintType)
class UTexture2DArray : public UTexture
{
	GENERATED_UCLASS_BODY()

public:
	/** Platform data. */
	FTexturePlatformData* PlatformData;
	TMap<FString, FTexturePlatformData*> CookedPlatformData;

	/** Creates and initializes a new Texture2D with the requested settings */
	ENGINE_API int32 CalculateMipZSize(int32 Mip);
	/*
	* Initialize texture source from textures in SourceArray.
	* @param bUpdateSourceSettings Set to false to prevent overriding current texture settings.
	*/
	/** Trivial accessors. */
	FORCEINLINE int32 GetSizeX() const
	{
		return PlatformData ? PlatformData->SizeX : 0;
	}
	FORCEINLINE int32 GetSizeY() const
	{
		return PlatformData ? PlatformData->SizeY : 0;
	}
	FORCEINLINE int32 GetNumSlices() const
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
	virtual void Serialize(FArchive& Ar) override;
	virtual void PostLoad() override;
	virtual void GetAssetRegistryTags(TArray<FAssetRegistryTag>& OutTags) const override;
	virtual FString GetDesc() override;
	virtual void GetResourceSizeEx(FResourceSizeEx& CumulativeResourceSize) override;
	virtual float GetSurfaceWidth() const override { return GetSizeX(); }
	virtual float GetSurfaceHeight() const override { return GetSizeY(); }
	virtual FTextureResource* CreateResource() override;
#if WITH_EDITOR
	ENGINE_API virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	ENGINE_API bool UpdateSourceFromSourceTextures(bool bCreatingNewTexture = true);
	ENGINE_API void InvadiateTextureSource();
	ENGINE_API bool CheckArrayTexturesCompatibility();
#endif // WITH_EDITOR
	virtual void UpdateResource() override;
	virtual EMaterialValueType GetMaterialType() const override { return MCT_Texture2DArray; }
	virtual FTexturePlatformData** GetRunningPlatformData() override { return &PlatformData; }
	virtual TMap<FString, FTexturePlatformData*> *GetCookedPlatformData() override { return &CookedPlatformData; }
	//~ End UTexture Interface

#if WITH_EDITORONLY_DATA
	/** The addressing mode to use for the X axis.*/
	UPROPERTY(EditAnywhere, Category = Source2D, meta = (DisplayName = "Address X"))
	TEnumAsByte<enum TextureAddress> AddressX;

	/** The addressing mode to use for the Y axis.*/
	UPROPERTY(EditAnywhere, Category = Source2D, meta = (DisplayName = "Address Y"))
	TEnumAsByte<enum TextureAddress> AddressY;

	/** The addressing mode to use for the Z axis.*/
	UPROPERTY(EditAnywhere, Category = Source2D, meta = (DisplayName = "Address Z"))
	TEnumAsByte<enum TextureAddress> AddressZ;
	
	/** Add Textures*/
	UPROPERTY(EditAnywhere, Category = Source2D, meta = (DisplayName = "Source Textures"))
	TArray<UTexture2D*> SourceTextures;

#endif

	/**
	 * Calculates the size of this texture in bytes if it had MipCount mip-levels streamed in.
	 *
	 * @param	MipCount Number of mips to calculate size for, counting from the smallest 1x1 mip-level and up.
	 * @return	Size of MipCount mips in bytes
	 */
	uint32 CalcTextureMemorySize(int32 MipCount) const;

	/**
	* Calculates the size of this texture if it had MipCount mip levels streamed in.
	*
	* @param	Enum which mips to calculate size for.
	* @return	Total size of all specified mips, in bytes
	*/
	virtual uint32 CalcTextureMemorySizeEnum(ETextureMipCount Enum) const override;

#if WITH_EDITOR
	/**
	* Return maximum dimension for this texture type.
	*/
	virtual uint32 GetMaximumDimension() const override;

#endif

	ENGINE_API static bool ShaderPlatformSupportsCompression(EShaderPlatform ShaderPlatform);

protected:

#if WITH_EDITOR
	void UpdateMipGenSettings();
#endif
};