// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Object.h"
#include "Textures/SlateIcon.h"
#include "SceneManagement.h"

#include "Framework/Commands/UICommandInfo.h"
#include "FractureEditorCommands.h"
#include "FractureEditorModeToolkit.h"

#include "FractureTool.generated.h"

class UGeometryCollection;
class UFractureModalTool;

template <typename T>
class TManagedArray;

DECLARE_LOG_CATEGORY_EXTERN(LogFractureTool, Log, All);

UCLASS(Abstract, config = EditorPerProjectUserSettings)
class UFractureToolSettings : public UObject
{
	GENERATED_BODY()
public:
	UFractureToolSettings(const FObjectInitializer& ObjInit) : Super(ObjInit) {}

	void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	void PostEditChangeChainProperty(struct FPropertyChangedChainEvent& PropertyChangedEvent) override;

	UPROPERTY()
	UFractureModalTool* OwnerTool;
};


struct FFractureToolContext
{
	AActor* OriginalActor;
	UPrimitiveComponent* OriginalPrimitiveComponent;
	UGeometryCollection* FracturedGeometryCollection;
	FString ParentName;
	FTransform Transform;
	FBox Bounds;
	int32 RandomSeed;
	TArray<int32> SelectedBones;
};


/** Tools derived from this class should require parameter inputs from the user, only the bone selection. */
UCLASS(Abstract)
class UFractureActionTool : public UObject
{
public:
	GENERATED_BODY()

	UFractureActionTool(const FObjectInitializer& ObjInit) : Super(ObjInit) {}

	/** This is the Text that will appear on the tool button to execute the tool **/
	virtual FText GetDisplayText() const { return FText(); }
	virtual FText GetTooltipText() const { return FText(); }

	virtual FSlateIcon GetToolIcon() const { return FSlateIcon(); }

	/** Executes the command.  Derived types need to be implemented in a thread safe way*/
	virtual void Execute(TWeakPtr<FFractureEditorModeToolkit> InToolkit) {}
	virtual bool CanExecute() const;

	/** Gets the UI command info for this command */
	const TSharedPtr<FUICommandInfo>& GetUICommandInfo() const;

	virtual void RegisterUICommand(FFractureEditorCommands* BindingContext) {}

	virtual void GetFractureToolContexts(TArray<FFractureToolContext>& FractureToolContexts) const {}

	
protected:
	static bool IsStaticMeshSelected();
	static bool IsGeometryCollectionSelected();
	static void AddSingleRootNodeIfRequired(UGeometryCollection* GeometryCollectionObject);
	static void AddAdditionalAttributesIfRequired(UGeometryCollection* GeometryCollectionObject);
	static void GetSelectedGeometryCollectionComponents(TSet<UGeometryCollectionComponent*>& GeomCompSelection);

protected:
	TSharedPtr<FUICommandInfo> UICommandInfo;

};


/** Tools derived from this class provide parameter details and operate modally. */
UCLASS(Abstract)
class UFractureModalTool : public UFractureActionTool
{
public:
	GENERATED_BODY()

	UFractureModalTool(const FObjectInitializer& ObjInit) : Super(ObjInit) {}

	virtual TArray<UObject*> GetSettingsObjects() const { return TArray<UObject*>(); }
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) {}

	/** This is the Text that will appear on the button to execute the fracture **/
	virtual FText GetApplyText() const { return FText(); }

	/** Executes the command.  Derived types need to be implemented in a thread safe way*/
	virtual void Execute(TWeakPtr<FFractureEditorModeToolkit> InToolkit) override;
	virtual bool CanExecute() const override;

	virtual void ExecuteFracture(const FFractureToolContext& FractureContext) {}

	/** Draw callback from edmode*/
	virtual void Render(const FSceneView* View, FViewport* Viewport, FPrimitiveDrawInterface* PDI) {}

	virtual void PostEditChangeChainProperty(struct FPropertyChangedChainEvent& PropertyChangedEvent) override;
	virtual void FractureContextChanged() {}

protected:

	virtual void GetFractureContexts(TArray<FFractureToolContext>& FractureContexts) const;
	virtual TArray<int32> FilterBones(const TArray<int32>& SelectedBonesOriginal, const FGeometryCollection* const GeometryCollection) const;

	void GenerateFractureToolContext(
		AActor* InActor,
		UGeometryCollectionComponent* InComponent,
		UGeometryCollection* InGeometryCollection,
		const TArray<int32>& InSelectedBones,
		TMap<int32, FBox>& InBoundsToBone,
		const TManagedArray<int32>& TransformToGeometryIndex,
		int32 RandomSeed,
		TArray<FFractureToolContext>& OutFractureContexts
	) const;

};


/** Tools derived from this class provide parameter details, operate modally and use a viewport manipulator to set certain parameters. */
UCLASS(Abstract)
class UFractureInteractiveTool : public UFractureModalTool
{
public:
	GENERATED_BODY()

	UFractureInteractiveTool(const FObjectInitializer& ObjInit) : Super(ObjInit) {}

	// #todo (bmiller) implement interactive widgets

};

