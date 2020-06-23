// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ContentBrowserItemData.h"
#include "AssetData.h"
#include "UObject/GCObject.h"

class IAssetTypeActions;
class FAssetThumbnail;
class UFactory;

class CONTENTBROWSERASSETDATASOURCE_API FContentBrowserAssetFolderItemDataPayload : public IContentBrowserItemDataPayload
{
public:
	explicit FContentBrowserAssetFolderItemDataPayload(const FName InInternalPath)
		: InternalPath(InInternalPath)
	{
	}

	FName GetInternalPath() const
	{
		return InternalPath;
	}

	const FString& GetFilename() const;

private:
	FName InternalPath;

	mutable bool bHasCachedFilename = false;
	mutable FString CachedFilename;
};

class CONTENTBROWSERASSETDATASOURCE_API FContentBrowserAssetFileItemDataPayload : public IContentBrowserItemDataPayload
{
public:
	explicit FContentBrowserAssetFileItemDataPayload(FAssetData&& InAssetData);
	explicit FContentBrowserAssetFileItemDataPayload(const FAssetData& InAssetData);

	const FAssetData& GetAssetData() const
	{
		return AssetData;
	}

	UPackage* GetPackage(const bool bTryRecacheIfNull = false) const;

	UPackage* LoadPackage() const;

	UObject* GetAsset(const bool bTryRecacheIfNull = false) const;

	UObject* LoadAsset() const;

	TSharedPtr<IAssetTypeActions> GetAssetTypeActions() const;

	const FString& GetFilename() const;

	void UpdateThumbnail(FAssetThumbnail& InThumbnail) const;

private:
	FAssetData AssetData;

	mutable bool bHasCachedPackagePtr = false;
	mutable TWeakObjectPtr<UPackage> CachedPackagePtr;

	mutable bool bHasCachedAssetPtr = false;
	mutable TWeakObjectPtr<UObject> CachedAssetPtr;

	mutable bool bHasCachedAssetTypeActionsPtr = false;
	mutable TWeakPtr<IAssetTypeActions> CachedAssetTypeActionsPtr;

	mutable bool bHasCachedFilename = false;
	mutable FString CachedFilename;
};

class CONTENTBROWSERASSETDATASOURCE_API FContentBrowserAssetFileItemDataPayload_Creation : public FContentBrowserAssetFileItemDataPayload, public FGCObject
{
public:
	FContentBrowserAssetFileItemDataPayload_Creation(FAssetData&& InAssetData, UClass* InAssetClass, UFactory* InFactory);

	//~ FGCObject interface
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override
	{
		Collector.AddReferencedObject(AssetClass);
		Collector.AddReferencedObject(Factory);
	}

	UClass* GetAssetClass() const
	{
		return AssetClass;
	}

	UFactory* GetFactory() const
	{
		return Factory;
	}

private:
	/** The class to use when creating the asset */
	UClass* AssetClass = nullptr;

	/** The factory to use when creating the asset. */
	UFactory* Factory = nullptr;
};

class CONTENTBROWSERASSETDATASOURCE_API FContentBrowserAssetFileItemDataPayload_Duplication : public FContentBrowserAssetFileItemDataPayload
{
public:
	FContentBrowserAssetFileItemDataPayload_Duplication(FAssetData&& InAssetData, TWeakObjectPtr<UObject> InSourceObject);

	UObject* GetSourceObject() const
	{
		return SourceObject.Get();
	}

private:
	/** The context to use when creating the asset. Used when initializing an asset with another related asset. */
	TWeakObjectPtr<UObject> SourceObject;
};
