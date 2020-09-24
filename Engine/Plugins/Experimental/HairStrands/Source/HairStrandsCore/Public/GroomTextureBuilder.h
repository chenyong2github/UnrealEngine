// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"
#include "GroomSettings.h"
#include "HairStrandsInterface.h"

class UTexture2D;
class UGroomAsset;
class USkeletalMesh;
class UStaticMesh;
class FRHIIndexBuffer;
class FRHIShaderResourceView;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Follicle texture generation

struct FFollicleInfo
{
	enum EChannel { R = 0, G = 1, B = 2, A = 3 };

	UGroomAsset*GroomAsset = nullptr;
	EChannel	Channel = R;
	uint32		KernelSizeInPixels = 0;
	bool		bGPUOnly = false; // Indirect of the texture should be saved on CPU, or if it will only be used directly on GPU
};

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Hair strands texture generation on meshes surface

struct FStrandsTexturesInfo
{
	UGroomAsset* GroomAsset = nullptr;
	USkeletalMesh* SkeletalMesh = nullptr;
	UStaticMesh* StaticMesh = nullptr;
	uint32 Resolution = 2048;
	uint32 SectionIndex = 0;
	uint32 UVChannelIndex = 0;
	float MaxTracingDistance = 1;
};

struct FStrandsTexturesOutput
{
	class UTexture2D* Tangent = nullptr;
	class UTexture2D* Coverage = nullptr;
	class UTexture2D* Attribute = nullptr;
	bool IsValid() const { return Tangent && Coverage && Attribute; }
};

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct FHairAssetHelper
{
	/* Generate a unique asset & package name */
	typedef void (*TCreateFilename)(const FString& InAssetName, const FString& Suffix, FString& OutPackageName, FString& OutAssetName);

	/* Register a texture asset */
	typedef void (*TRegisterTexture)(UTexture2D* Out);

	TCreateFilename CreateFilename;
	TRegisterTexture RegisterTexture;
};

struct HAIRSTRANDSCORE_API FGroomTextureBuilder
{
	// Follicle texture
	static UTexture2D* CreateGroomFollicleMaskTexture(const UGroomAsset* GroomAsset, uint32 Resolution, FHairAssetHelper Helper);
	static void AllocateFollicleTextureResources(UTexture2D* OuTexture);
	static void AllocateFollicleTextureResources(UTexture2D* OuTexture, uint32 Resolution, uint32 MipCount);
	static void BuildFollicleTexture(const TArray<FFollicleInfo>& InInfos, UTexture2D* OutFollicleTexture, bool bUseGPU);

	// Strands textures
	static FStrandsTexturesOutput CreateGroomStrandsTexturesTexture(const UGroomAsset* GroomAsset, uint32 Resolution, FHairAssetHelper Helper);
	static void BuildStrandsTextures(const FStrandsTexturesInfo& InInfo, const FStrandsTexturesOutput& Output);
};

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////