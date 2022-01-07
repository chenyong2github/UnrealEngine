// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_EDITOR

#include "UObject/Package.h"
#include "AssetRegistryModule.h"
#include "Misc/ArchiveMD5.h"
#include "Misc/PackageName.h"
#include "Misc/PackagePath.h"
#include "Delegates/DelegateCombinations.h"

class ENGINE_API FExternalPackageHelper
{
public:

	DECLARE_EVENT_TwoParams(FExternalPackageHelper, FOnObjectPackagingModeChanged, UObject*, bool /* bExternal */);
	static FOnObjectPackagingModeChanged OnObjectPackagingModeChanged;

	/**
	 * Create an external package
	 * @param InObjectOuter the object's outer
	 * @param InObjectPath the fully qualified object path, in the format: 'Outermost.Outer.Name'
	 * @param InFlags the package flags to apply
	 * @return the created package
	 */
	static UPackage* CreateExternalPackage(UObject* InObjectOuter, const FString& InObjectPath, EPackageFlags InFlags);

	/**
	 * Set the object packaging mode.
	 * @param InObject the object on which to change the packaging mode
	 * @param InObjectOuter the object's outer
	 * @param bInIsPackageExternal will set the object packaging mode to external if true, to internal otherwise
	 * @param bInShouldDirty should dirty or not the object's outer package
	 * @param InExternalPackageFlags the flags to apply to the external package if bInIsPackageExternal is true
	 */
	static void SetPackagingMode(UObject* InObject, UObject* InObjectOuter, bool bInIsPackageExternal, bool bInShouldDirty, EPackageFlags InExternalPackageFlags);

	/**
	 * Get the path containing the external objects for this path
	 * @param InOuterPackageName The package name to get the external objects path of
	 * @param InPackageShortName Optional short name to use instead of the package short name
	 * @return the path
	 */
	static FString GetExternalObjectsPath(const FString& InOuterPackageName, const FString& InPackageShortName = FString());

	/**
	 * Get the path containing the external objects for this Outer
	 * @param InPackage The package to get the external objects path of
	 * @param InPackageShortName Optional short name to use instead of the package short name
	 * @return the path
	 */
	static FString GetExternalObjectsPath(UPackage* InPackage, const FString& InPackageShortName = FString(), bool bTryUsingPackageLoadedPath = false);
	
	/**
	 * Loads objects from an external package
	 */
	template<typename T>
	static void LoadObjectsFromExternalPackages(UObject* InOuter, TFunctionRef<void(T*)> Operation);

private:
	/**
	 * Get the external package name for this object
	 * @param InObjectPath the fully qualified object path, in the format: 'Outermost.Outer.Name'
	 * @return the package name
	 */
	static FString GetExternalPackageName(UPackage* InOuterPackage, const FString& InObjectPath);

	/** Get the external object package instance name. */
	static FString GetExternalObjectPackageInstanceName(const FString& OuterPackageName, const FString& ObjectShortPackageName);
};

template<typename T>
void FExternalPackageHelper::LoadObjectsFromExternalPackages(UObject* InOuter, TFunctionRef<void(T*)> Operation)
{
	const FString ExternalObjectsPath = FExternalPackageHelper::GetExternalObjectsPath(InOuter->GetPackage(), FString(), /*bTryUsingPackageLoadedPath*/ true);

	// Do a synchronous scan of the world external objects path.			
	IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	AssetRegistry.ScanPathsSynchronous({ ExternalObjectsPath }, /*bForceRescan*/false, /*bIgnoreDenyListScanFilters*/false);

	FARFilter Filter;
	Filter.bRecursivePaths = true;
	Filter.bIncludeOnlyOnDiskAssets = true;
	Filter.ClassNames.Add(T::StaticClass()->GetFName());
	Filter.PackagePaths.Add(*ExternalObjectsPath);

	TArray<FAssetData> Assets;
	AssetRegistry.GetAssets(Filter, Assets);

	FLinkerInstancingContext InstancingContext;
	TArray<UPackage*> InstancePackages;

	UPackage* OuterPackage = InOuter->GetPackage();
	FName PackageResourceName = OuterPackage->GetLoadedPath().GetPackageFName();
	const bool bInstanced = !PackageResourceName.IsNone() && (PackageResourceName != OuterPackage->GetFName());
	if (bInstanced)
	{
		InstancingContext.AddMapping(PackageResourceName, OuterPackage->GetFName());

		for (const FAssetData& Asset : Assets)
		{
			const FString ObjectPackageName = Asset.PackageName.ToString();
			const FString ShortPackageName = FPackageName::GetShortName(ObjectPackageName);
			const FString InstancedName = GetExternalObjectPackageInstanceName(OuterPackage->GetName(), ShortPackageName);
			InstancingContext.AddMapping(FName(*ObjectPackageName), FName(*InstancedName));

			// Create instance package
			UPackage* InstancePackage = CreatePackage(*InstancedName);
			// Propagate RF_Transient
			if (OuterPackage->HasAnyFlags(RF_Transient))
			{
				InstancePackage->SetFlags(RF_Transient);
			}
			InstancePackages.Add(InstancePackage);
		}
	}

	const ELoadFlags LoadFlags = InOuter->GetPackage()->HasAnyPackageFlags(PKG_PlayInEditor) ? LOAD_PackageForPIE : LOAD_None;
	for (int32 i = 0; i < Assets.Num(); i++)
	{
		if (UPackage* Package = LoadPackage(bInstanced ? InstancePackages[i] : nullptr, *Assets[i].PackageName.ToString(), LoadFlags, nullptr, &InstancingContext))
		{
			T* LoadedObject = nullptr;
			ForEachObjectWithPackage(Package, [&LoadedObject](UObject* Object)
				{
					if (T* TypedObj = Cast<T>(Object))
					{
						LoadedObject = TypedObj;
						return false;
					}
					return true;
				}, true, RF_NoFlags, EInternalObjectFlags::Unreachable);

			if (ensure(LoadedObject))
			{
				Operation(LoadedObject);
			}
		}
	}
}

#endif