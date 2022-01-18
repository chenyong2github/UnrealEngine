// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Engine/TextureCube.h"
#include "TextureCubeArray.generated.h"

UCLASS(HideCategories = Object, MinimalAPI, BlueprintType)
class UTextureCubeArray : public UTexture
{
	GENERATED_UCLASS_BODY()

public:
	/** Platform data. */
	FTexturePlatformData* PlatformData;
	TMap<FString, FTexturePlatformData*> CookedPlatformData;

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
	virtual float GetSurfaceWidth() const override { return static_cast<float>(GetSizeX()); }
	virtual float GetSurfaceHeight() const override { return static_cast<float>(GetSizeY()); }
	virtual float GetSurfaceDepth() const override { return 0.0f; }
	virtual uint32 GetSurfaceArraySize() const override { return GetNumSlices(); }
	virtual FTextureResource* CreateResource() override;
#if WITH_EDITOR
	ENGINE_API virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	ENGINE_API bool UpdateSourceFromSourceTextures(bool bCreatingNewTexture = true);
	ENGINE_API void InvadiateTextureSource();
	ENGINE_API bool CheckArrayTexturesCompatibility();
#endif // WITH_EDITOR
	virtual void UpdateResource() override;
	virtual EMaterialValueType GetMaterialType() const override { return MCT_TextureCubeArray; }
	virtual FTexturePlatformData** GetRunningPlatformData() override { return &PlatformData; }
	virtual TMap<FString, FTexturePlatformData*>* GetCookedPlatformData() override { return &CookedPlatformData; }
	//~ End UTexture Interface

#if WITH_EDITORONLY_DATA
	/** Add Textures*/
	UPROPERTY(EditAnywhere, Category = SourceCube, meta = (DisplayName = "Source Textures"))
	TArray<TObjectPtr<UTextureCube>> SourceTextures;
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
};