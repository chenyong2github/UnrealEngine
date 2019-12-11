// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EdMode.h"

#include "InputState.h"
#include "InteractiveToolManager.h"
#include "EdModeInteractiveToolsContext.h"


class FEditorComponentSourceFactory;
class FEditorToolAssetAPI;
class FUICommandList;

class FModelingToolsEditorMode : public FEdMode
{
public:
	const static FEditorModeID EM_ModelingToolsEditorModeId;
public:
	FModelingToolsEditorMode();
	virtual ~FModelingToolsEditorMode();

	////////////////
	// FEdMode interface
	////////////////

	virtual void Tick(FEditorViewportClient* ViewportClient, float DeltaTime) override;
	virtual void Render(const FSceneView* View, FViewport* Viewport, FPrimitiveDrawInterface* PDI) override;
	virtual bool UsesToolkits() const override;

	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;

	// these disable the standard gizmo, which is probably want we want in
	// these tools as we can't hit-test the standard gizmo...
	virtual bool AllowWidgetMove() override;
	virtual bool ShouldDrawWidget() const override;
	virtual bool UsesTransformWidget() const override;

	virtual void ActorSelectionChangeNotify() override;

	virtual bool ProcessEditDelete();

	virtual bool CanAutoSave() const override;

	/*
	 * focus events
	 */

	// called when we "start" this editor mode (ie switch to this tab)
	virtual void Enter() override;

	// called when we "end" this editor mode (ie switch to another tab)
	virtual void Exit() override;

	// called when viewport window is focused
	virtual bool ReceivedFocus(FEditorViewportClient* ViewportClient, FViewport* Viewport) override;

	// called when viewport window loses focus (ie when some other window is focused)
	// *not* called when editor is backgrounded, but is called when editor is minimized
	virtual bool LostFocus(FEditorViewportClient* ViewportClient, FViewport* Viewport) override;


	/*
	 * Mouse position events - These are called when no mouse button is down
	 */

	// called when mouse moves over viewport window
	virtual bool MouseEnter(FEditorViewportClient* ViewportClient, FViewport* Viewport, int32 x, int32 y) override;

	// called when mouse leaves viewport window
	virtual bool MouseLeave(FEditorViewportClient* ViewportClient, FViewport* Viewport) override;

	// called on any mouse-move event. *not* called during tracking/capturing, eg if any button is down
	virtual bool MouseMove(FEditorViewportClient* ViewportClient, FViewport* Viewport, int32 x, int32 y) override;


	/*
	 * Input Button/Axis Events & Mouse Capture
	 *
	 *   event sequence for left mouse down/capture/up is:
	 *     - InputKey( EKeys::LeftMouseButton, IE_PRESSED )
	 *     - StartTracking()
	 *     - CapturedMouseMove() and InputAxis() (repeated for each mouse-move)
	 *     - Inputkey( EKeys::LeftMouseButton, IE_RELEASED )
	 *     - EndTracking()
	 *
	 *   for doubleclick, we get one of the above sequence, then a second
	 *   where instead of the first IE_PRESSED we get a IE_DoubleClick
	 *
	 *   for mouse wheel, we get following sequence
	 *     - InputKey ( EKeys::MouseScrollUp / Down, IE_PRESSED )
	 *     - InputAxis() for wheel move
	 *     - InputKey ( EKeys::MouseScrollUp / Down, IE_RELEASED )
	 *   it appears that there will only ever be *one* InputAxis() between the pressed/released sequenece
	 *
	 *   Note that this wheel event sequence can happen *during* a 
	 *   middle-mouse tracking sequence on a wheel-button (nice!)
	 */


	// This is not just called for keyboard keys, it is also called for mouse down/up events!
	// Eg for left-press we get Key = EKeys::LeftMouseButton and Event = IE_Pressed
	// Return value indicates "handled'. If we return true for mouse press events then
	// the StartTracking/CapturedMouseMove/EndTracking sequence is *not* called (but we
	// also don't get MouseMove(), so the mouse-movement events appear to be lost?)
	virtual bool InputKey(FEditorViewportClient* ViewportClient, FViewport* Viewport, FKey Key, EInputEvent Event) override;


	// Called for 1D Axis movements - EKeys::MouseX, EKeys::MouseY, and EKeys::MouseWheelAxis
	// Called even if we return true from InputKey which otherwise blocks tracking. 
	// Return value indicates "handled" but has no effect on CapturedMouseMove events (ie we still get them)
	virtual bool InputAxis(FEditorViewportClient* InViewportClient, FViewport* Viewport, int32 ControllerId, FKey Key, float Delta, float DeltaTime);


	// called on mouse-down. return value indicates whether this was "handled" but
	// does not mean we get exclusive capture events!
	virtual bool StartTracking(FEditorViewportClient* InViewportClient, FViewport* InViewport) override;

	// called during mouse-down mouse-move. Always called, return value is used to indicate whether
	// we "handled" this mouse move (see FEditorModeTools::CapturedMouseMove) but does not pre-empt
	// other editor modes from seeing this move event...
	virtual bool CapturedMouseMove(FEditorViewportClient* InViewportClient, FViewport* InViewport, int32 InMouseX, int32 InMouseY) override;

	// always called on mouse-up
	virtual bool EndTracking(FEditorViewportClient* InViewportClient, FViewport* InViewport) override;


	//////////////////
	// End of FEdMode interface
	//////////////////



public:
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnModelingModeToolNotification, const FText&);
	FOnModelingModeToolNotification OnToolNotificationMessage;
	FOnModelingModeToolNotification OnToolWarningMessage;


public:
	virtual UEdModeInteractiveToolsContext* GetToolsContext() const
	{
		return ToolsContext;
	}

	virtual UInteractiveToolManager* GetToolManager() const
	{ 
		return ToolsContext->ToolManager;
	}

protected:

	UEdModeInteractiveToolsContext* ToolsContext;

	/** Command list lives here so that the key bindings on the commands can be processed in the viewport. */
	TSharedPtr<FUICommandList> UICommandList;


public:
	/** Cached pointer to the viewport world interaction object we're using to interact with mesh elements */
	class UViewportWorldInteraction* ViewportWorldInteraction;

};
