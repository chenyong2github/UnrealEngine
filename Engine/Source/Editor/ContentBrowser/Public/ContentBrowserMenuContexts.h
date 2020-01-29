// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "ContentBrowserDelegates.h"

#include "ContentBrowserMenuContexts.generated.h"

class FAssetContextMenu;
class IAssetTypeActions;
class SAssetView;
class SContentBrowser;

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
	UClass* CommonClass;

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

	TWeakPtr<SAssetView> AssetView;
};

UCLASS()
class CONTENTBROWSER_API UContentBrowserFolderContext : public UObject
{
	GENERATED_BODY()

public:

	UPROPERTY()
	bool bCanBeModified;

	UPROPERTY()
	int32 NumAssetPaths;

	UPROPERTY()
	int32 NumClassPaths;

	TWeakPtr<SContentBrowser> ContentBrowser;
	FOnCreateNewFolder OnCreateNewFolder;
};

UCLASS()
class CONTENTBROWSER_API UContentBrowserAddNewContextMenuContext : public UObject
{
	GENERATED_BODY()

public:

	UContentBrowserAddNewContextMenuContext() :
		NumAssetPaths(0),
		bShowGetContent(false),
		bShowImport(false)
	{
	}

	TWeakPtr<SContentBrowser> ContentBrowser;
	int32 NumAssetPaths;
	bool bShowGetContent;
	bool bShowImport;
};

