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
class FFractureToolContext;

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

	virtual TArray<FFractureToolContext> GetFractureToolContexts() const;
	
protected:
	static bool IsStaticMeshSelected();
	static bool IsGeometryCollectionSelected();
	static void AddSingleRootNodeIfRequired(UGeometryCollection* GeometryCollectionObject);
	static void AddAdditionalAttributesIfRequired(UGeometryCollection* GeometryCollectionObject);
	static void GetSelectedGeometryCollectionComponents(TSet<UGeometryCollectionComponent*>& GeomCompSelection);
	static void Refresh(FFractureToolContext& Context, FFractureEditorModeToolkit* Toolkit);
	static void SetOutlinerComponents(TArray<FFractureToolContext>& InContexts, FFractureEditorModeToolkit* Toolkit);
	static void ClearProximity(FGeometryCollection* GeometryCollection);
	static void GenerateProximityIfNecessary(FGeometryCollection* GeometryCollection);


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

	/** Executes function that generates new geometry. Returns the first new geometry index. */
	virtual int32 ExecuteFracture(const FFractureToolContext& FractureContext) { return INDEX_NONE; }

	/** Draw callback from edmode*/
	virtual void Render(const FSceneView* View, FViewport* Viewport, FPrimitiveDrawInterface* PDI) {}

	virtual void PostEditChangeChainProperty(struct FPropertyChangedChainEvent& PropertyChangedEvent) override;
	virtual void FractureContextChanged() {}

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

