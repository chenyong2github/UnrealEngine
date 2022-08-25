// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AssetTypeCategories.h"
#include "Containers/Array.h"
#include "ContentBrowserDelegates.h"
#include "CoreMinimal.h"
#include "HAL/Platform.h"
#include "HAL/PlatformCrt.h"
#include "Templates/SharedPointer.h"
#include "UObject/NameTypes.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"
#include "UObject/ObjectPtr.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/WeakObjectPtr.h"
#include "UObject/WeakObjectPtrTemplates.h"

#include "ContentBrowserMenuContexts.generated.h"

class FAssetContextMenu;
class IAssetTypeActions;
class SAssetView;
class SContentBrowser;
class SFilterList;
class UClass;
struct FFrame;

UCLASS()
class CONTENTBROWSER_API UContentBrowserAssetContextMenuContext : public UObject
{
	GENERATED_BODY()

public:

	TWeakPtr<FAssetContextMenu> AssetContextMenu;

	TWeakPtr<IAssetTypeActions> CommonAssetTypeActions;
	
	UPROPERTY()
	TArray<TWeakObjectPtr<UObject>> SelectedObjects;

	UPROPERTY()
	TObjectPtr<UClass> CommonClass;

	UPROPERTY()
	bool bCanBeModified;

	UFUNCTION(BlueprintCallable, Category="Tool Menus")
	TArray<UObject*> GetSelectedObjects() const
	{
		TArray<UObject*> Result;
		Result.Reserve(SelectedObjects.Num());
		for (const TWeakObjectPtr<UObject>& Object : SelectedObjects)
		{
			Result.Add(Object.Get());
		}
		return Result;
	}
};

UCLASS()
class CONTENTBROWSER_API UContentBrowserAssetViewContextMenuContext : public UObject
{
	GENERATED_BODY()

public:
	TWeakPtr<SContentBrowser> OwningContentBrowser;
	TWeakPtr<SAssetView> AssetView;
};

UCLASS()
class CONTENTBROWSER_API UContentBrowserMenuContext : public UObject
{
	GENERATED_BODY()

public:

	TWeakPtr<SContentBrowser> ContentBrowser;
};

UCLASS()
class CONTENTBROWSER_API UContentBrowserFolderContext : public UContentBrowserMenuContext
{
	GENERATED_BODY()

public:

	UPROPERTY()
	bool bCanBeModified;

	UPROPERTY()
	bool bNoFolderOnDisk;

	UPROPERTY()
	int32 NumAssetPaths;

	UPROPERTY()
	int32 NumClassPaths;

	FOnCreateNewFolder OnCreateNewFolder;
};

UCLASS()
class CONTENTBROWSER_API UContentBrowserFilterListContext : public UObject
{
	GENERATED_BODY()

public:

	TWeakPtr<SFilterList> FilterList;

	EAssetTypeCategories::Type MenuExpansion;
};

UCLASS()
class CONTENTBROWSER_API UContentBrowserAddNewContextMenuContext : public UObject
{
	GENERATED_BODY()

public:

	TWeakPtr<SContentBrowser> ContentBrowser;
};

UCLASS()
class CONTENTBROWSER_API UContentBrowserToolbarMenuContext : public UObject
{
	GENERATED_BODY()

public:
	FName GetCurrentPath() const;

	bool CanWriteToCurrentPath() const;

	TWeakPtr<SContentBrowser> ContentBrowser;
};
