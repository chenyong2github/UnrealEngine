// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "InputCoreTypes.h"
#include "UnrealWidget.h"
#include "EditorComponents.h"
#include "EngineGlobals.h"
#include "EditorModeRegistry.h"
#include "Templates/SharedPointer.h"
#include "UObject/Object.h"
#include "Math/Ray.h"
#include "InputState.h"
#include "UObject/SoftObjectPtr.h"
#include "UEdMode.generated.h"


class FCanvas;
class FEditorModeTools;
class FEditorViewportClient;
class FModeTool;
class FModeToolkit;
class FPrimitiveDrawInterface;
class FSceneView;
class FViewport;
class UTexture2D;
struct FConvexVolume;
struct FViewportClick;
class UInteractiveToolsContext;
class UInteractiveToolManager;
class UInteractiveTool;
class IToolsContextQueriesAPI;
class IToolsContextTransactionsAPI;
class IToolsContextAssetAPI;

enum EModeTools : int8;
class FEditorViewportClient;
class HHitProxy;
struct FViewportClick;
class FModeTool;
class FEditorViewportClient;
struct FViewportClick;
class UInteractiveToolBuilder;
class FUICommandInfo;
class FUICommandList;


/** Outcomes when determining whether it's possible to perform an action on the edit modes*/
namespace EEditAction
{
	enum Type
	{
		/** Can't process this action */
		Skip = 0,
		/** Can process this action */
		Process,
		/** Stop evaluating other modes (early out) */
		Halt,
	};
};

/**
 * Base class for all editor modes.
 */
UCLASS(Abstract)
class UNREALED_API UEdMode : public UObject, public FEditorCommonDrawHelper
{
	GENERATED_BODY()

public:
	/** Friends so it can access mode's internals on construction */
	friend class FEditorModeRegistry;

	UEdMode();

	virtual void Initialize() {};

	virtual bool MouseEnter(FEditorViewportClient* ViewportClient, FViewport* Viewport, int32 x, int32 y);

	virtual bool MouseLeave(FEditorViewportClient* ViewportClient, FViewport* Viewport);

	virtual bool MouseMove(FEditorViewportClient* ViewportClient, FViewport* Viewport, int32 x, int32 y);

	virtual bool ReceivedFocus(FEditorViewportClient* ViewportClient, FViewport* Viewport);

	virtual bool LostFocus(FEditorViewportClient* ViewportClient, FViewport* Viewport);

	/**
	 * Called when the mouse is moved while a window input capture is in effect
	 *
	 * @param	InViewportClient	Level editor viewport client that captured the mouse input
	 * @param	InViewport			Viewport that captured the mouse input
	 * @param	InMouseX			New mouse cursor X coordinate
	 * @param	InMouseY			New mouse cursor Y coordinate
	 *
	 * @return	true if input was handled
	 */
	virtual bool CapturedMouseMove(FEditorViewportClient* InViewportClient, FViewport* InViewport, int32 InMouseX, int32 InMouseY);

	/** Process all captured mouse moves that occurred during the current frame */
	virtual bool ProcessCapturedMouseMoves(FEditorViewportClient* InViewportClient, FViewport* InViewport, const TArrayView<FIntPoint>& CapturedMouseMoves) { return false; }

	virtual bool InputKey(FEditorViewportClient* ViewportClient, FViewport* Viewport, FKey Key, EInputEvent Event);
	virtual bool InputAxis(FEditorViewportClient* InViewportClient, FViewport* Viewport, int32 ControllerId, FKey Key, float Delta, float DeltaTime);
	virtual bool InputDelta(FEditorViewportClient* InViewportClient, FViewport* InViewport, FVector& InDrag, FRotator& InRot, FVector& InScale);
	virtual bool StartTracking(FEditorViewportClient* InViewportClient, FViewport* InViewport);
	virtual bool EndTracking(FEditorViewportClient* InViewportClient, FViewport* InViewport);
	// Added for handling EDIT Command...
	virtual EEditAction::Type GetActionEditDuplicate() { return EEditAction::Skip; }
	virtual EEditAction::Type GetActionEditDelete() { return EEditAction::Skip; }
	virtual EEditAction::Type GetActionEditCut() { return EEditAction::Skip; }
	virtual EEditAction::Type GetActionEditCopy() { return EEditAction::Skip; }
	virtual EEditAction::Type GetActionEditPaste() { return EEditAction::Skip; }
	virtual bool ProcessEditDuplicate() { return false; }
	virtual bool ProcessEditDelete();
	virtual bool ProcessEditCut() { return false; }
	virtual bool ProcessEditCopy() { return false; }
	virtual bool ProcessEditPaste() { return false; }

	virtual void Tick(FEditorViewportClient* ViewportClient, float DeltaTime);

	virtual bool IsCompatibleWith(FEditorModeID OtherModeID) const { return false; }
	virtual void ActorMoveNotify() {}
	virtual void ActorsDuplicatedNotify(TArray<AActor*>& PreDuplicateSelection, TArray<AActor*>& PostDuplicateSelection, bool bOffsetLocations) {}
	virtual void ActorSelectionChangeNotify() {};
	virtual void ActorPropChangeNotify() {}
	virtual void MapChangeNotify() {}

	/** If the Edmode is handling its own mouse deltas, it can disable the MouseDeltaTacker */
	virtual bool DisallowMouseDeltaTracking() const { return false; }

	/**
	 * Lets each mode/tool specify a pivot point around which the camera should orbit
	 * @param	OutPivot	The custom pivot point returned by the mode/tool
	 * @return	true if a custom pivot point was specified, false otherwise.
	 */
	virtual bool GetPivotForOrbit(FVector& OutPivot) const { return false; }

	/**
	 * Get a cursor to override the default with, if any.
	 * @return true if the cursor was overridden.
	 */
	virtual bool GetCursor(EMouseCursor::Type& OutCursor) const { return false; }

	/** Get override cursor visibility settings */
	virtual bool GetOverrideCursorVisibility(bool& bWantsOverride, bool& bHardwareCursorVisible, bool bSoftwareCursorVisible) const { return false; }

	/** Called before mouse movement is converted to drag/rot */
	virtual bool PreConvertMouseMovement(FEditorViewportClient* InViewportClient) { return false; }

	/** Called after mouse movement is converted to drag/rot */
	virtual bool PostConvertMouseMovement(FEditorViewportClient* InViewportClient) { return false; }

	virtual bool ShouldDrawBrushWireframe(AActor* InActor) const { return true; }

	/** If Rotation Snap should be enabled for this mode*/
	virtual bool IsSnapRotationEnabled();

	void PostInvalidation();
	void RestoreEditorState();
	/** If this mode should override the snap rotation
	* @param	Rotation		The Rotation Override
	*
	* @return					True if you have overridden the value
	*/
	virtual bool SnapRotatorToGridOverride(FRotator& Rotation) { return false; };


	virtual void UpdateInternalData() {}

	virtual void Enter();

	virtual void RegisterTool(TSharedPtr<FUICommandInfo> UICommand, FString ToolIdentifier, UInteractiveToolBuilder* Builder);


	virtual void Exit();
	virtual UTexture2D* GetVertexTexture() { return GEngine->DefaultBSPVertexTexture; }

	virtual void PostUndo() {}

	/**
	 * Check to see if this UEdMode wants to disallow AutoSave
	 * @return true if AutoSave can be applied right now
	 */
	virtual bool CanAutoSave() const { return true; }

	virtual void SelectNone();
	virtual void SelectionChanged() {}

	virtual bool HandleClick(FEditorViewportClient* InViewportClient, HHitProxy* HitProxy, const FViewportClick& Click);

	/**
	 * Allows an editor mode to override the bounding box used to focus the viewport on a selection
	 *
	 * @param Actor			The selected actor that is being considered for focus
	 * @param PrimitiveComponent	The component in the actor being considered for focus
	 * @param InOutBox		The box that should be computed for the actor and component
	 * @return bool			true if the mode overrides the box and populated InOutBox, false if it did not populate InOutBox
	 */
	virtual bool ComputeBoundingBoxForViewportFocus(AActor* Actor, UPrimitiveComponent* PrimitiveComponent, FBox& InOutBox) const { return false; }

	/** Handling SelectActor */
	virtual bool Select(AActor* InActor, bool bInSelected) { return 0; }

	/** Check to see if an actor can be selected in this mode - no side effects */
	virtual bool IsSelectionAllowed(AActor* InActor, bool bInSelection) const { return true; }

	/** @return True if this mode allows the viewport to use a drag tool */
	virtual bool AllowsViewportDragTool() const { return true; }

	/** Returns the editor mode identifier. */
	FEditorModeID GetID() const { return Info.ID; }

	/** Returns the editor mode information. */
	const FEditorModeInfo& GetModeInfo() const { return Info; }

	/** @name Rendering */
	//@{
	/** Draws translucent polygons on brushes and volumes. */
	virtual void Render(const FSceneView* View, FViewport* Viewport, FPrimitiveDrawInterface* PDI);
	//void DrawGridSection(int32 ViewportLocX,int32 ViewportGridY,FVector* A,FVector* B,float* AX,float* BX,int32 Axis,int32 AlphaCase,FSceneView* View,FPrimitiveDrawInterface* PDI);

	/** Overlays the editor hud (brushes, drag tools, static mesh vertices, etc*. */
	virtual void DrawHUD(FEditorViewportClient* ViewportClient, FViewport* Viewport, const FSceneView* View, FCanvas* Canvas);
	//@}

	/**
	 * Called when the mode wants to draw brackets around selected objects
	 */
	virtual void DrawBrackets(FEditorViewportClient* ViewportClient, FViewport* Viewport, const FSceneView* View, FCanvas* Canvas);

	/** True if this mode uses a toolkit mode (eventually they all should) */
	virtual bool UsesToolkits() const;

	/** Gets the toolkit created by this mode */
	TSharedPtr<FModeToolkit> GetToolkit() { return Toolkit; }

	/** Returns the world this toolkit is editing */
	UWorld* GetWorld() const;

	/** Returns the owning mode manager for this mode */
	class FEditorModeTools* GetModeManager() const;

public:

	/** Request that this mode be deleted at the next convenient opportunity (FEditorModeTools::Tick) */
	void RequestDeletion() { bPendingDeletion = true; }

	/** returns true if this mode is to be deleted at the next convenient opportunity (FEditorModeTools::Tick) */
	bool IsPendingDeletion() const { return bPendingDeletion; }

private:
	/** true if this mode is pending removal from its owner */
	bool bPendingDeletion;

	/** Called whenever a mode type is unregistered */
	void OnModeUnregistered(FEditorModeID ModeID);

	// default behavior is to accept active tool
	virtual void TerminateActiveToolsOnPIEStart();

	// default behavior is to accept active tool
	virtual void TerminateActiveToolsOnSaveWorld();

	FRay GetRayFromMousePos(FEditorViewportClient* ViewportClient, FViewport* Viewport, int MouseX, int MouseY);
	
protected:

	/** Information pertaining to this mode. Assigned by FEditorModeRegistry. */
	FEditorModeInfo Info;

	/** Editor Mode Toolkit that is associated with this toolkit mode */
	TSharedPtr<class FModeToolkit> Toolkit;

	/** Pointer back to the mode tools that we are registered with */
	FEditorModeTools* Owner;

	// Property Widgets
public:

	UPROPERTY()
	UMaterialInterface* StandardVertexColorMaterial;

protected:
	/**
	 * Returns the first selected Actor, or NULL if there is no selection.
	 */
	AActor* GetFirstSelectedActorInstance() const;

	void DeactivateAllActiveTools();

	bool bHaveSavedEditorState;
	bool bSavedAntiAliasingState;

	/** Input event instance used to keep track of various button states, etc, that we cannot directly query on-demand */
	FInputDeviceState CurrentMouseState;

public:

	/**
	 * @return active ToolManager
	 */
	virtual UInteractiveToolManager* GetToolManager() const;

	virtual TMap<FName, TArray<TSharedPtr<FUICommandInfo>>> GetModeCommands() const
	{
		return TMap<FName, TArray<TSharedPtr<FUICommandInfo>>>();
	};

	void SetCurrentPaletteName(FName InName)
	{
		CurrentPaletteName = InName;
		UpdateOnPaletteChange();
	};

protected:
	virtual void CreateToolkit();
	virtual void OnToolStarted(UInteractiveToolManager* Manager, UInteractiveTool* Tool) {};
	virtual void OnToolEnded(UInteractiveToolManager* Manager, UInteractiveTool* Tool) { bCheckIfDefaultToolNeeded = true; };
	virtual void ActivateDefaultTool() {};
	virtual void UpdateOnPaletteChange() {};

protected:

	UPROPERTY()
	UInteractiveToolsContext* ToolsContext;
	TSoftClassPtr<UInteractiveToolsContext> ToolsContextClass;
	/** Command list lives here so that the key bindings on the commands can be processed in the viewport. */
	TSharedPtr<FUICommandList> ToolCommandList;
	bool bInvalidationPending;
	IToolsContextQueriesAPI* QueriesAPI;
	IToolsContextTransactionsAPI* TransactionAPI;
	IToolsContextAssetAPI* AssetAPI;

	// called when PIE is about to start, shuts down active tools
	FDelegateHandle BeginPIEDelegateHandle;
	// called before a Save starts. This currently shuts down active tools.
	FDelegateHandle PreSaveWorldDelegateHandle;

	UPROPERTY()
	TSoftClassPtr<UObject> SettingsClass;

	UPROPERTY(Transient)
	UObject* SettingsObject;

	DECLARE_MULTICAST_DELEGATE_OneParam(FOnModeToolNotification, const FText&);
	FOnModeToolNotification OnToolNotificationMessage;
	FOnModeToolNotification OnToolWarningMessage;

	FName CurrentPaletteName;
	bool bCheckIfDefaultToolNeeded;
};

