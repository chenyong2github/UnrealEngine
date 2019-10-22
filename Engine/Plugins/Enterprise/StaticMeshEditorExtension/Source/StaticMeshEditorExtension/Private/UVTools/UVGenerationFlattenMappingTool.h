// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Framework/Commands/Commands.h"
#include "Templates/SharedPointer.h"
#include "UObject/Object.h"
#include "UObject/StrongObjectPtr.h"

#include "UVGenerationFlattenMappingTool.generated.h"

class FExtender;
class FMenuBuilder;
class FToolBarBuilder;
class FUICommandList;
class UStaticMesh;
class IStaticMeshEditor;
struct FAssetData;

class FUVGenerationFlattenMappingTool
{
public:
	/*
	 * Called to extend the static mesh editor's secondary toolbar.
	 */
	static TSharedRef<FExtender> OnExtendContentBrowserAssetSelectionMenu(const TArray<FAssetData>& SelectedAssets);

	/*
	 * Opens the Unwrap UV dialog window.
	 */
	static void OpenUnwrapUVWindow(TArray<UStaticMesh*> StaticMeshes);
};

UCLASS()
class UUVGenerationFlattenMappingToolbarProxyObject : public UObject
{
	GENERATED_BODY()

public:

	/** The UV generation flatten mapping toolbar that owns this */
	class FUVGenerationFlattenMappingToolbar* Owner;
};

class FUVGenerationFlattenMappingToolbar : public TSharedFromThis<FUVGenerationFlattenMappingToolbar>
{
public:
	/** Add UV unwrapping menu entry item to the StaticMesh Editor's toolbar */
	static void CreateMenu(FMenuBuilder& ParentMenuBuilder, const TSharedRef<FUICommandList> CommandList, UStaticMesh* InStaticMesh);
};