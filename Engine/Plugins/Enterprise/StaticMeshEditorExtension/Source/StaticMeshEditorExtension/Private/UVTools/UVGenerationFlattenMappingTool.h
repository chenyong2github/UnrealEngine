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

//Local actions that can be invoked from this toolbar
class FUVGenerationFlattenMappingCommands : public TCommands<FUVGenerationFlattenMappingCommands>
{
public:
	FUVGenerationFlattenMappingCommands();

	// TCommands<> interface
	virtual void RegisterCommands() override;
	// End of TCommands<> interface

public:
	/** CommandInfo associated with the EditMode button in the toolbar */
	TSharedPtr<FUICommandInfo> UnwrapUV;
};

class FUVGenerationFlattenMappingToolbar : public TSharedFromThis<FUVGenerationFlattenMappingToolbar>
{
public:
	FUVGenerationFlattenMappingToolbar();
	virtual ~FUVGenerationFlattenMappingToolbar();

	/** Add UV unwrapping items to the StaticMesh Editor's toolbar */
	static void CreateToolbar(FToolBarBuilder& ToolbarBuilder, const TSharedRef<FUICommandList> CommandList, UStaticMesh* StaticMesh);

private:
	/** Initialize the toolbar */
	bool Initialize(UStaticMesh* InStaticMesh, const TSharedRef<FUICommandList> CommandList);

	/** Populate the toolbar */
	void PopulateToolbar(FToolBarBuilder& ToolbarBuilder, const TSharedRef<FUICommandList> CommandList);

	/** Registers and bind the commands to the toolbar */
	void BindCommands(const TSharedPtr<FUICommandList> CommandList);

	/** Pointer to the edited static mesh */
	UStaticMesh* StaticMesh;

	/** Pointer to the static mesh editor */
	IStaticMeshEditor* StaticMeshEditor;

	/** Pointer to the command list to which the commands are bound to */
	TSharedPtr<FUICommandList> BoundCommandList;

	TStrongObjectPtr<UUVGenerationFlattenMappingToolbarProxyObject> UVGenerationFlattenMappingToolbarProxyObject;
};