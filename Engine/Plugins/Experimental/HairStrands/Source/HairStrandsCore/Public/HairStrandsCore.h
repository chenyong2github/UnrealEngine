// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"

//typedef UTexture2D* (TCreateTextureHelper*)(FName Package, const FIntPoint& Resolution);
typedef void (*TTextureAllocation)(UTexture2D* Out, const FIntPoint& Resolution, uint32 MipCount);

struct FHairAssetHelper
{
	/* Generate a unique asset & package name */
	typedef void (*TCreateFilename)(const FString& InAssetName, const FString& Suffix, FString& OutPackageName, FString& OutAssetName);

	/* Register a texture asset */
	typedef void (*TRegisterAsset)(UObject* Out);

	/* Save an object within a package */
	typedef void (*TSaveAsset)(UObject* Object);

	TCreateFilename CreateFilename;
	TRegisterAsset RegisterAsset;
	TSaveAsset SaveAsset;
};


/** Implements the HairStrands module  */
class FHairStrandsCore : public IModuleInterface
{
public:

	//~ IModuleInterface interface

	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	virtual bool SupportsDynamicReloading() override
	{
		return true;
	}

	HAIRSTRANDSCORE_API static void RegisterAssetHelper(const FHairAssetHelper& Helper);

#if WITH_EDITOR
	static UTexture2D* CreateTexture(const FString& PackgeName, const FIntPoint& Resolution, const FString& Suffix, TTextureAllocation TextureAllocation);
	static void ResizeTexture(UTexture2D* InTexture, const FIntPoint& Resolution, TTextureAllocation TextureAllocation);
	static UStaticMesh* CreateStaticMesh(const FString& InPackageName, const FString& Suffix);
	static void SaveAsset(UObject* Object);
#endif
};
