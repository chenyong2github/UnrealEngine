// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EdMode.h"
#include "InteractiveToolsContext.h"
#include "Delegates/Delegate.h"
#include "EdModeInteractiveToolsContext.generated.h"

/**
 * EdModeInteractiveToolsContext is an extension/adapter of an InteractiveToolsContext which 
 * allows it to be easily embedded inside an FEdMode. A set of functions are provided which can be
 * called from the FEdMode functions of the same name. These will handle the data type
 * conversions and forwarding calls necessary to operate the ToolsContext
 */
UCLASS(Transient)
class EDITORINTERACTIVETOOLSFRAMEWORK_API UEdModeInteractiveToolsContext : public UInteractiveToolsContext
{
	GENERATED_BODY()

public:
	UEdModeInteractiveToolsContext();


	virtual void InitializeContextFromEdMode(FEdMode* EditorMode);
	virtual void ShutdownContext();

	// default behavior is to accept active tool
	virtual void TerminateActiveToolsOnPIEStart();

	// default behavior is to accept active tool
	virtual void TerminateActiveToolsOnSaveWorld();

	// default behavior is to accept active tool
	virtual void TerminateActiveToolsOnWorldTearDown();

	IToolsContextQueriesAPI* GetQueriesAPI() const { return QueriesAPI; }
	IToolsContextTransactionsAPI* GetTransactionAPI() const { return TransactionAPI; }
	IToolsContextAssetAPI* GetAssetAPI() const { return AssetAPI; }

	virtual void PostInvalidation();


	// call these from your FEdMode functions of the same name

	virtual void Tick(FEditorViewportClient* ViewportClient, float DeltaTime);
	virtual void Render(const FSceneView* View, FViewport* Viewport, FPrimitiveDrawInterface* PDI);

	virtual bool ProcessEditDelete();

	virtual bool InputKey(FEditorViewportClient* ViewportClient, FViewport* Viewport, FKey Key, EInputEvent Event);

	virtual bool MouseEnter(FEditorViewportClient* ViewportClient, FViewport* Viewport, int32 x, int32 y);
	virtual bool MouseLeave(FEditorViewportClient* ViewportClient, FViewport* Viewport);
	virtual bool MouseMove(FEditorViewportClient* ViewportClient, FViewport* Viewport, int32 x, int32 y);

	virtual bool StartTracking(FEditorViewportClient* InViewportClient, FViewport* InViewport);
	virtual bool CapturedMouseMove(FEditorViewportClient* InViewportClient, FViewport* InViewport, int32 InMouseX, int32 InMouseY);
	virtual bool EndTracking(FEditorViewportClient* InViewportClient, FViewport* InViewport);


	//
	// Utility functions useful for hooking up to UICommand/etc
	//

	virtual bool CanStartTool(const FString& ToolTypeIdentifier) const;
	virtual bool ActiveToolHasAccept() const;
	virtual bool CanAcceptActiveTool() const;
	virtual bool CanCancelActiveTool() const;
	virtual bool CanCompleteActiveTool() const;
	virtual void StartTool(const FString& ToolTypeIdentifier);
	virtual void EndTool(EToolShutdownType ShutdownType);

	virtual bool ShouldIgnoreHotkeys() const { return bInFlyMode; }

protected:
	// we hide these 
	virtual void Initialize(IToolsContextQueriesAPI* QueriesAPI, IToolsContextTransactionsAPI* TransactionsAPI) override;
	virtual void Shutdown() override;

	virtual void DeactivateActiveTool(EToolSide WhichSide, EToolShutdownType ShutdownType);
	virtual void DeactivateAllActiveTools();

public:
	UPROPERTY()
	UMaterialInterface* StandardVertexColorMaterial;

protected:
	FEdMode* EditorMode;

	// called when PIE is about to start, shuts down active tools
	FDelegateHandle BeginPIEDelegateHandle;
	// called before a Save starts. This currently shuts down active tools.
	FDelegateHandle PreSaveWorldDelegateHandle;
	// called when a map is changed
	FDelegateHandle WorldTearDownDelegateHandle;

	// EdMode implementation of InteractiveToolFramework APIs - see ToolContextInterfaces.h
	IToolsContextQueriesAPI* QueriesAPI;
	IToolsContextTransactionsAPI* TransactionAPI;
	IToolsContextAssetAPI* AssetAPI;

	// if true, we invalidate the ViewportClient on next tick
	bool bInvalidationPending;

	/** Input event instance used to keep track of various button states, etc, that we cannot directly query on-demand */
	FInputDeviceState CurrentMouseState;

	// Utility function to convert viewport x/y from mouse events (and others?) into scene ray.
	// Copy-pasted from other Editor code, seems kind of expensive?
	static FRay GetRayFromMousePos(FEditorViewportClient* ViewportClient, FViewport* Viewport, int MouseX, int MouseY);

	/** This will be set to true if user is in right-mouse "fly mode", which requires special handling to intercept hotkeys/etc */
	bool bInFlyMode = false;

	// editor UI state that we set before starting tool and when exiting tool
	// Currently disabling anti-aliasing during active Tools because it causes PDI flickering
	bool bHaveSavedEditorState = false;
	FLevelEditorViewportClient* SavedViewportClient;
	void SaveEditorStateAndSetForTool();
	void RestoreEditorState();

};
