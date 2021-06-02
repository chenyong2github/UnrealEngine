// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tools/LegacyEdModeWidgetHelpers.h"

#include "InputState.h"
#include "InteractiveToolManager.h"
#include "EdModeInteractiveToolsContext.h"
#include "ModelingToolsActions.h"

#include "ModelingToolsEditorMode.generated.h"

class FEditorComponentSourceFactory;
class FUICommandList;
class FStylusStateTracker;		// for stylus events

UCLASS(Transient)
class UModelingToolsEditorMode : public UBaseLegacyWidgetEdMode
{
	GENERATED_BODY()
public:
	const static FEditorModeID EM_ModelingToolsEditorModeId;

	UModelingToolsEditorMode();
	UModelingToolsEditorMode(FVTableHelper& Helper);
	~UModelingToolsEditorMode();
	////////////////
	// UEdMode interface
	////////////////

	virtual void Tick(FEditorViewportClient* ViewportClient, float DeltaTime) override;

	virtual bool ShouldDrawWidget() const override;
	virtual bool ProcessEditDelete() override;
	virtual bool ProcessEditCut() override;

	virtual bool CanAutoSave() const override;

	virtual bool ComputeBoundingBoxForViewportFocus(AActor* Actor, UPrimitiveComponent* PrimitiveComponent, FBox& InOutBox) const override;

	virtual bool GetPivotForOrbit(FVector& OutPivot) const override;

	/*
	 * focus events
	 */

	// called when we "start" this editor mode (ie switch to this tab)
	virtual void Enter() override;

	// called when we "end" this editor mode (ie switch to another tab)
	virtual void Exit() override;

	//////////////////
	// End of UEdMode interface
	//////////////////

protected:
	virtual void BindCommands() override;
	virtual void CreateToolkit() override;
	virtual void OnToolStarted(UInteractiveToolManager* Manager, UInteractiveTool* Tool) override;
	virtual void OnToolEnded(UInteractiveToolManager* Manager, UInteractiveTool* Tool) override;

	FDelegateHandle MeshCreatedEventHandle;
	FDelegateHandle TextureCreatedEventHandle;

	TUniquePtr<FStylusStateTracker> StylusStateTracker;

	void ModelingModeShortcutRequested(EModelingModeActionCommands Command);
	void FocusCameraAtCursorHotkey();

	void ConfigureRealTimeViewportsOverride(bool bEnable);
};
