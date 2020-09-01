// Copyright Epic Games, Inc. All Rights Reserved.

#include "ContentBrowserAssetDataPayload.h"
#include "Modules/ModuleManager.h"
#include "AssetToolsModule.h"
#include "AssetThumbnail.h"
#include "Engine/Texture2D.h"
#include "Materials/Material.h"

const FString& FContentBrowserAssetFolderItemDataPayload::GetFilename() const
{
	if (!bHasCachedFilename)
	{
		FPackageName::TryConvertLongPackageNameToFilename(InternalPath.ToString() / FString(), CachedFilename);
		bHasCachedFilename = true;
	}
	return CachedFilename;
}


FContentBrowserAssetFileItemDataPayload::FContentBrowserAssetFileItemDataPayload(FAssetData&& InAssetData)
	: AssetData(MoveTemp(InAssetData))
{	
}

FContentBrowserAssetFileItemDataPayload::FContentBrowserAssetFileItemDataPayload(const FAssetData& InAssetData)
	: AssetData(InAssetData)
{
}

UPackage* FContentBrowserAssetFileItemDataPayload::GetPackage(const bool bTryRecacheIfNull) const
{
	if (!bHasCachedPackagePtr || (bTryRecacheIfNull && !CachedPackagePtr.IsValid()))
	{
		if (!AssetData.PackageName.IsNone())
		{
			TStringBuilder<FName::StringBufferSize> PackageNameStr;
			AssetData.PackageName.ToString(PackageNameStr);
			CachedPackagePtr = FindObjectSafe<UPackage>(nullptr, *PackageNameStr, /*bExactClass*/true);
		}
		bHasCachedPackagePtr = true;
	}
	return CachedPackagePtr.Get();
}

UPackage* FContentBrowserAssetFileItemDataPayload::LoadPackage() const
{
	if (!bHasCachedPackagePtr || !CachedPackagePtr.IsValid())
	{
		if (!AssetData.PackageName.IsNone())
		{
			TStringBuilder<FName::StringBufferSize> PackageNameStr;
			AssetData.PackageName.ToString(PackageNameStr);
			CachedPackagePtr = ::LoadPackage(nullptr, *PackageNameStr, LOAD_None);
			(void)GetAsset(/*bTryRecacheIfNull*/true); // Also re-cache the asset pointer
		}
		bHasCachedPackagePtr = true;
	}
	return CachedPackagePtr.Get();
}

UObject* FContentBrowserAssetFileItemDataPayload::GetAsset(const bool bTryRecacheIfNull) const
{
	if (!bHasCachedAssetPtr || (bTryRecacheIfNull && !CachedAssetPtr.IsValid()))
	{
		if (!AssetData.ObjectPath.IsNone())
		{
			TStringBuilder<FName::StringBufferSize> AssetPathStr;
			AssetData.ObjectPath.ToString(AssetPathStr);
			CachedAssetPtr = FindObjectSafe<UObject>(nullptr, *AssetPathStr);
		}
		bHasCachedAssetPtr = true;
	}
	return CachedAssetPtr.Get();
}

UObject* FContentBrowserAssetFileItemDataPayload::LoadAsset() const
{
	if (!bHasCachedAssetPtr || !CachedAssetPtr.IsValid())
	{
		if (!AssetData.ObjectPath.IsNone())
		{
			TStringBuilder<FName::StringBufferSize> AssetPathStr;
			AssetData.ObjectPath.ToString(AssetPathStr);
			CachedAssetPtr = LoadObject<UObject>(nullptr, *AssetPathStr);
			(void)GetPackage(/*bTryRecacheIfNull*/true); // Also re-cache the package pointer
		}
		bHasCachedAssetPtr = true;
	}
	return CachedAssetPtr.Get();
}

TSharedPtr<IAssetTypeActions> FContentBrowserAssetFileItemDataPayload::GetAssetTypeActions() const
{
	if (!bHasCachedAssetTypeActionsPtr)
	{
		if (UClass* AssetClass = AssetData.GetClass())
		{
			static const FName NAME_AssetTools = "AssetTools";
			FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(NAME_AssetTools);
			CachedAssetTypeActionsPtr = AssetToolsModule.Get().GetAssetTypeActionsForClass(AssetClass);
		}
		bHasCachedAssetTypeActionsPtr = true;
	}
	return CachedAssetTypeActionsPtr.Pin();
}

const FString& FContentBrowserAssetFileItemDataPayload::GetFilename() const
{
	if (!bHasCachedFilename)
	{
		const FString PackageNameStr = AssetData.PackageName.ToString();

		// Get the filename by finding it on disk first
		if (!FPackageName::DoesPackageExist(PackageNameStr, nullptr, &CachedFilename))
		{
			if (const UPackage* Package = GetPackage())
			{
				// This is a package in memory that has not yet been saved. Determine the extension and convert to a filename
				const FString* PackageExtension = Package->ContainsMap() ? &FPackageName::GetMapPackageExtension() : &FPackageName::GetAssetPackageExtension();
				FPackageName::TryConvertLongPackageNameToFilename(PackageNameStr, CachedFilename, *PackageExtension);
			}
		}

		bHasCachedFilename = true;
	}
	return CachedFilename;
}

void FContentBrowserAssetFileItemDataPayload::UpdateThumbnail(FAssetThumbnail& InThumbnail) const
{
	if (UObject* AssetPtr = GetAsset())
	{
		if (UTexture2D* TextureAssetPtr = Cast<UTexture2D>(AssetPtr))
		{
			TextureAssetPtr->bForceMiplevelsToBeResident = true;
		}
		else if (UMaterial* MaterialAssetPtr = Cast<UMaterial>(AssetPtr))
		{
			MaterialAssetPtr->SetForceMipLevelsToBeResident(true, true, -1.0f);
		}
	}

	InThumbnail.SetAsset(AssetData);
}


FContentBrowserAssetFileItemDataPayload_Creation::FContentBrowserAssetFileItemDataPayload_Creation(FAssetData&& InAssetData, UClass* InAssetClass, UFactory* InFactory)
	: FContentBrowserAssetFileItemDataPayload(MoveTemp(InAssetData))
	, AssetClass(InAssetClass)
	, Factory(InFactory)
{
}


FContentBrowserAssetFileItemDataPayload_Duplication::FContentBrowserAssetFileItemDataPayload_Duplication(FAssetData&& InAssetData, TWeakObjectPtr<UObject> InSourceObject)
	: FContentBrowserAssetFileItemDataPayload(MoveTemp(InAssetData))
	, SourceObject(InSourceObject)
{
}
