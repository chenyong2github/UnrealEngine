// Copyright Epic Games, Inc. All Rights Reserved.


#pragma once

#include "CoreMinimal.h"

#include "AssetRegistry/IAssetRegistry.h"
#include "Misc/AssetRegistryInterface.h"
#include "Modules/ModuleManager.h"

namespace AssetRegistryConstants
{
	const FName ModuleName("AssetRegistry");
}

/**
 * Asset registry module
 */
class FAssetRegistryModule : public IModuleInterface, public IAssetRegistryInterface
{

public:

	/** Called right after the module DLL has been loaded and the module object has been created */
	virtual void StartupModule() override;

	virtual IAssetRegistry& Get() const
	{
		return IAssetRegistry::GetChecked();
	}

	static IAssetRegistry& GetRegistry()
	{
		return IAssetRegistry::GetChecked();
	}

	static void TickAssetRegistry(float DeltaTime)
	{
		IAssetRegistry::GetChecked().Tick(DeltaTime);
	}

	static void AssetCreated(UObject* NewAsset)
	{
		IAssetRegistry::GetChecked().AssetCreated(NewAsset);
	}

	static void AssetDeleted(UObject* DeletedAsset)
	{
		IAssetRegistry::GetChecked().AssetDeleted(DeletedAsset);
	}

	static void AssetRenamed(const UObject* RenamedAsset, const FString& OldObjectPath)
	{
		IAssetRegistry::GetChecked().AssetRenamed(RenamedAsset, OldObjectPath);
	}

	static void PackageDeleted(UPackage* DeletedPackage)
	{
		IAssetRegistry::GetChecked().PackageDeleted(DeletedPackage);
	}

	UE_DEPRECATED(4.26, "Use GetDependencies that takes a UE::AssetRegistry::EDependencyCategory instead")
	void GetDependencies(FName InPackageName, TArray<FName>& OutDependencies, EAssetRegistryDependencyType::Type InDependencyType)
	{
		GetDependenciesDeprecated(InPackageName, OutDependencies, InDependencyType);
	}

	/** Access the dependent package names for a given source package */
	virtual void GetDependencies(FName InPackageName, TArray<FName>& OutDependencies, UE::AssetRegistry::EDependencyCategory Category = UE::AssetRegistry::EDependencyCategory::Package, const UE::AssetRegistry::FDependencyQuery& Flags = UE::AssetRegistry::FDependencyQuery()) override
	{
		IAssetRegistry::GetChecked().GetDependencies(InPackageName, OutDependencies, Category, Flags);
	}


protected:
	/* This function is a workaround for platforms that don't support disable of deprecation warnings on override functions*/
	virtual void GetDependenciesDeprecated(FName InPackageName, TArray<FName>& OutDependencies, EAssetRegistryDependencyType::Type InDependencyType) override
	{
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
		IAssetRegistry::GetChecked().GetDependencies(InPackageName, OutDependencies, InDependencyType);
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
	}
};
