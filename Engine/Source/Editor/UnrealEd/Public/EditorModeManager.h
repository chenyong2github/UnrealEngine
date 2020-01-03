// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"
#include "UObject/GCObject.h"
#include "UnrealWidget.h"
#include "Editor.h"
#include "EditorUndoClient.h"
#include "EdMode.h"

class FCanvas;
class FEditorViewportClient;
class FEdMode;
class FModeTool;
class FPrimitiveDrawInterface;
class FSceneView;
class FViewport;
class IToolkitHost;
class USelection;
struct FConvexVolume;
struct FViewportClick;
class UEdMode;

/**
 * A helper class to store the state of the various editor modes.
 */
class UNREALED_API FEditorModeTools : public FGCObject, public FEditorUndoClient
{
public:
	FEditorModeTools();
	virtual ~FEditorModeTools();

	/**
	 * Set the default editor mode for these tools
	 * 
	 * @param	DefaultModeID		The mode ID for the new default mode
	 */
	void SetDefaultMode( const FEditorModeID DefaultModeID );

	/**
	 * Adds a new default mode to this tool's list of default modes.  You can have multiple default modes, but they all must be compatible with each other.
	 * 
	 * @param	DefaultModeID		The mode ID for the new default mode
	 */
	void AddDefaultMode( const FEditorModeID DefaultModeID );

	/**
	 * Removes a default mode
	 * 
	 * @param	DefaultModeID		The mode ID for the default mode to remove
	 */
	void RemoveDefaultMode( const FEditorModeID DefaultModeID );

	/**
	 * Activates the default modes defined by this class.  Note that there can be more than one default mode, and this call will activate them all in sequence.
	 */
	void ActivateDefaultMode();

	/** 
	 * Returns true if the default modes are active.  Note that there can be more than one default mode, and this will only return true if all default modes are active.
	 */
	bool IsDefaultModeActive() const;

	/**
	 * Activates an editor mode. Shuts down all other active modes which cannot run with the passed in mode.
	 * 
	 * @param InID		The ID of the editor mode to activate.
	 * @param bToggle	true if the passed in editor mode should be toggled off if it is already active.
	 */
	void ActivateMode( FEditorModeID InID, bool bToggle = false );

	/**
	 * Deactivates an editor mode. 
	 * 
	 * @param InID		The ID of the editor mode to deactivate.
	 */
	void DeactivateMode(FEditorModeID InID);

	/**
	 * Deactivate the mode and entirely purge it from memory. Used when a mode type is unregistered
	 */
	void DestroyMode(FEditorModeID InID);

	/**
	 * Creates the mode toolbar tab if needed
	 */
	TSharedRef<SDockTab> MakeModeToolbarTab();

	/**
	 * Whether or not the mode toolbar should be shown.  If any active modes generated a toolbar this method will return true
	 */
	bool ShouldShowModeToolbar();

protected:
	
	/** Deactivates the editor mode at the specified index */
	void DeactivateModeAtIndex( int32 InIndex );
	/** Deactivates the editor mode at the specified index */
	void DeactivateScriptableModeAtIndex(int32 InIndex);
		
private:
	void RebuildModeToolBar();
	void SpawnOrUpdateModeToolbar();
public:

	/**
	 * Deactivates all modes, note some modes can never be deactivated.
	 */
	void DeactivateAllModes();

	/** 
	 * Returns the editor mode specified by the passed in ID
	 */
	UE_DEPRECATED(4.24, "Use GetActiveMode instead.")
	FEdMode* FindMode( FEditorModeID InID );

	UEdMode* GetActiveScriptableMode(FEditorModeID InID);

	/**
	 * Returns true if the current mode is not the specified ModeID.  Also optionally warns the user.
	 *
	 * @param	ModeID			The editor mode to query.
	 * @param	ErrorMsg		If specified, inform the user the reason why this is a problem
	 * @param	bNotifyUser		If true, display the error as a notification, instead of a dialog
	 * @return					true if the current mode is not the specified mode.
	 */
	bool EnsureNotInMode(FEditorModeID ModeID, const FText& ErrorMsg = FText::GetEmpty(), bool bNotifyUser = false) const;

	FMatrix GetCustomDrawingCoordinateSystem();
	FMatrix GetCustomInputCoordinateSystem();
	FMatrix GetLocalCoordinateSystem();
	
	/** 
	 * Returns true if the passed in editor mode is active 
	 */
	bool IsModeActive( FEditorModeID InID ) const;

	/**
	 * Returns a pointer to an active mode specified by the passed in ID
	 * If the editor mode is not active, NULL is returned
	 */
	FEdMode* GetActiveMode( FEditorModeID InID );
	const FEdMode* GetActiveMode( FEditorModeID InID ) const;

	template <typename SpecificModeType>
	SpecificModeType* GetActiveModeTyped( FEditorModeID InID )
	{
		return static_cast<SpecificModeType*>(GetActiveMode(InID));
	}

	template <typename SpecificModeType>
	const SpecificModeType* GetActiveModeTyped( FEditorModeID InID ) const
	{
		return static_cast<SpecificModeType*>(GetActiveMode(InID));
	}

	/**
	 * Returns the active tool of the passed in editor mode.
	 * If the passed in editor mode is not active or the mode has no active tool, NULL is returned
	 */
	const FModeTool* GetActiveTool( FEditorModeID InID ) const;

	/** 
	 * Returns an array of all active modes
	 */
	UE_DEPRECATED(4.24, "All access to modes now needs to go through the mode manager")
	void GetActiveModes( TArray<FEdMode*>& OutActiveModes );

	void SetShowWidget( bool InShowWidget )	{ bShowWidget = InShowWidget; }
	bool GetShowWidget() const;

	/** Cycle the widget mode, forwarding queries to modes */
	void CycleWidgetMode (void);

	/** Check with modes to see if the widget mode can be cycled */
	bool CanCycleWidgetMode() const;

	/**Save Widget Settings to Ini file*/
	void SaveWidgetSettings();
	/**Load Widget Settings from Ini file*/
	void LoadWidgetSettings();

	/** Gets the widget axis to be drawn */
	EAxisList::Type GetWidgetAxisToDraw( FWidget::EWidgetMode InWidgetMode ) const;

	/** Mouse tracking interface.  Passes tracking messages to all active modes */
	bool StartTracking(FEditorViewportClient* InViewportClient, FViewport* InViewport);
	bool EndTracking(FEditorViewportClient* InViewportClient, FViewport* InViewport);
	bool IsTracking() const { return bIsTracking; }

	bool AllowsViewportDragTool() const;

	/** Notifies all active modes that a map change has occured */
	void MapChangeNotify();

	/** Notifies all active modes to empty their selections */
	void SelectNone();

	/** Notifies all active modes of box selection attempts */
	bool BoxSelect( FBox& InBox, bool InSelect );

	/** Notifies all active modes of frustum selection attempts */
	bool FrustumSelect( const FConvexVolume& InFrustum, FEditorViewportClient* InViewportClient, bool InSelect );

	/** true if any active mode uses a transform widget */
	bool UsesTransformWidget() const;

	/** true if any active mode uses the passed in transform widget */
	bool UsesTransformWidget( FWidget::EWidgetMode CheckMode ) const;

	/** Sets the current widget axis */
	void SetCurrentWidgetAxis( EAxisList::Type NewAxis );

	/** Notifies all active modes of mouse click messages. */
	bool HandleClick(FEditorViewportClient* InViewportClient,  HHitProxy *HitProxy, const FViewportClick &Click );

	/**
	 * Allows editor modes to override the bounding box used to focus the viewport on a selection
	 * 
	 * @param Actor			The selected actor that is being considered for focus
	 * @param PrimitiveComponent	The component in the actor being considered for focus
	 * @param InOutBox		The box that should be computed for the actor and component
	 * @return bool			true if a mode overrides the box and populated InOutBox, false if it did not populate InOutBox
	 */
	bool ComputeBoundingBoxForViewportFocus(AActor* Actor, UPrimitiveComponent* PrimitiveComponent, FBox& InOutBox);

	/** true if the passed in brush actor should be drawn in wireframe */	
	bool ShouldDrawBrushWireframe( AActor* InActor ) const;

	/** true if brush vertices should be drawn */
	bool ShouldDrawBrushVertices() const;

	/** Ticks all active modes */
	void Tick( FEditorViewportClient* ViewportClient, float DeltaTime );

	/** Notifies all active modes of any change in mouse movement */
	bool InputDelta( FEditorViewportClient* InViewportClient,FViewport* InViewport,FVector& InDrag,FRotator& InRot,FVector& InScale );

	/** Notifies all active modes of captured mouse movement */	
	bool CapturedMouseMove( FEditorViewportClient* InViewportClient, FViewport* InViewport, int32 InMouseX, int32 InMouseY );

	/** Notifies all active modes of all captured mouse movement */	
	bool ProcessCapturedMouseMoves( FEditorViewportClient* InViewportClient, FViewport* InViewport, const TArrayView<FIntPoint>& CapturedMouseMoves );

	/** Notifies all active modes of keyboard input */
	bool InputKey( FEditorViewportClient* InViewportClient, FViewport* Viewport, FKey Key, EInputEvent Event);

	/** Notifies all active modes of axis movement */
	bool InputAxis( FEditorViewportClient* InViewportClient, FViewport* Viewport, int32 ControllerId, FKey Key, float Delta, float DeltaTime);

	bool MouseEnter( FEditorViewportClient* InViewportClient, FViewport* Viewport, int32 X, int32 Y );
	
	bool MouseLeave( FEditorViewportClient* InViewportClient, FViewport* Viewport );

	/** Notifies all active modes that the mouse has moved */
	bool MouseMove( FEditorViewportClient* InViewportClient, FViewport* Viewport, int32 X, int32 Y );

	/** Notifies all active modes that a viewport has received focus */
	bool ReceivedFocus( FEditorViewportClient* InViewportClient, FViewport* Viewport );

	/** Notifies all active modes that a viewport has lost focus */
	bool LostFocus( FEditorViewportClient* InViewportClient, FViewport* Viewport );

	/** Draws all active modes */	
	void DrawActiveModes( const FSceneView* InView, FPrimitiveDrawInterface* PDI );

	/** Renders all active modes */
	void Render( const FSceneView* InView, FViewport* Viewport, FPrimitiveDrawInterface* PDI );

	/** Draws the HUD for all active modes */
	void DrawHUD( FEditorViewportClient* InViewportClient,FViewport* Viewport,const FSceneView* View,FCanvas* Canvas );

	/** 
	 * Get a pivot point specified by any active modes around which the camera should orbit
	 * @param	OutPivot	The custom pivot point returned by the mode/tool
	 * @return	true if a custom pivot point was specified, false otherwise.
	 */
	bool GetPivotForOrbit( FVector& OutPivot ) const;

	/** Calls PostUndo on all active modes */
	// Begin FEditorUndoClient
	virtual void PostUndo(bool bSuccess) override;
	virtual void PostRedo(bool bSuccess) override;
	// End of FEditorUndoClient

	/** True if we should allow widget move */
	bool AllowWidgetMove() const;

	/** True if we should disallow mouse delta tracking. */
	bool DisallowMouseDeltaTracking() const;

	/** Get a cursor to override the default with, if any */
	bool GetCursor(EMouseCursor::Type& OutCursor) const;

	/** Get override cursor visibility settings */
	bool GetOverrideCursorVisibility(bool& bWantsOverride, bool& bHardwareCursorVisible, bool bSoftwareCursorVisible) const;

	/** Called before converting mouse movement to drag/rot */
	bool PreConvertMouseMovement(FEditorViewportClient* InViewportClient);

	/** Called after converting mouse movement to drag/rot */
	bool PostConvertMouseMovement(FEditorViewportClient* InViewportClient);

	/**
	 * Returns a good location to draw the widget at.
	 */
	FVector GetWidgetLocation() const;

	/**
	 * Changes the current widget mode.
	 */
	void SetWidgetMode( FWidget::EWidgetMode InWidgetMode );

	/**
	 * Allows you to temporarily override the widget mode.  Call this function again
	 * with WM_None to turn off the override.
	 */
	void SetWidgetModeOverride( FWidget::EWidgetMode InWidgetMode );

	/**
	 * Retrieves the current widget mode, taking overrides into account.
	 */
	FWidget::EWidgetMode GetWidgetMode() const;

	/**
	 * Gets the current state of script editor usage of show friendly names
	 * @ return - If true, replace variable names with sanatized ones
	 */
	bool GetShowFriendlyVariableNames() const;

	/**
	 * Gets the maximum number of bookmarks.
	 *
	 * @param InViewportClient	Level editor viewport client used to reference the world which owns the bookmarks.
	 */
	const uint32 GetMaxNumberOfBookmarks(FEditorViewportClient* InViewportClient) const;

	/**
	 * Compacts the available bookmarks into mapped spaces.
	 * Does nothing if all valid bookmarks are already mapped.
	 *
	 * @param InViewportClient	Level editor viewport client used to reference the world which owns the bookmarks.
	 */
	void CompactBookmarks(FEditorViewportClient* InViewportClient) const;

	/**
	 * Sets a bookmark in the levelinfo file, allocating it if necessary.
	 * 
	 * @param InIndex			Index of the bookmark to set.
	 * @param InViewportClient	Level editor viewport client used to reference the world which owns the bookmark.
	 */
	void SetBookmark( uint32 InIndex, FEditorViewportClient* InViewportClient );

	/**
	 * Checks to see if a bookmark exists at a given index
	 * 
	 * @param InIndex			Index of the bookmark to set.
	 * @param InViewportClient	Level editor viewport client used to reference the world which owns the bookmark.
	 */
	bool CheckBookmark( uint32 InIndex, FEditorViewportClient* InViewportClient );

	/**
	 * Activates a bookmark from the list.
	 * 
	 * @param InIndex			Index of the bookmark to set.
	 * @param					Set to true to restore the visibility of any streaming levels.
	 * @param InViewportClient	Level editor viewport client used to reference the world which owns the bookmark.
	 */
	UE_DEPRECATED(4.21, "Please use the version of JumpToBookmark that accepts a Shared Pointer to FBookmarkBaseJumpToSettings.")
	void JumpToBookmark( uint32 InIndex, bool bShouldRestoreLevelVisibility, FEditorViewportClient* InViewportClient );

	/**
	 * Activates a bookmark from the list.
	 * 
	 * @param InIndex			Index of the bookmark to set.
	 * @param InSettings		Settings to used when jumpting to the bookmark.
	 * @param InViewportClient	Level editor viewport client used to reference the world which owns the bookmark.
	 */
	void JumpToBookmark( uint32 InIndex, TSharedPtr<struct FBookmarkBaseJumpToSettings> InSettings, FEditorViewportClient* InViewportClient );

	/**
	 * Clears a bookmark from the list.
	 * 
	 * @param InIndex			Index of the bookmark to clear.
	 * @param InViewportClient	Level editor viewport client used to reference the world which owns the bookmark.
	 */
	void ClearBookmark( uint32 InIndex, FEditorViewportClient* InViewportClient );

	/**
	 * Clears all book marks
	 * 
	 * @param InViewportClient	Level editor viewport client used to reference the world which owns the bookmarks.
	 */
	void ClearAllBookmarks( FEditorViewportClient* InViewportClient );

	// FGCObject interface
	virtual void AddReferencedObjects( FReferenceCollector& Collector ) override;
	// End of FGCObject interface

	/**
	 * Loads the state that was saved in the INI file
	 */
	void LoadConfig(void);

	/**
	 * Saves the current state to the INI file
	 */
	void SaveConfig(void);

	/** 
	 * Sets the pivot locations
	 * 
	 * @param Location 		The location to set
	 * @param bIncGridBase	Whether or not to also set the GridBase
	 */
	void SetPivotLocation( const FVector& Location, const bool bIncGridBase );

	/**
	 * Multicast delegate for OnModeEntered and OnModeExited callbacks.
	 *
	 * First parameter:  The editor mode that was changed
	 * Second parameter:  True if entering the mode, or false if exiting the mode
	 */
PRAGMA_DISABLE_DEPRECATION_WARNINGS
	DECLARE_EVENT_TwoParams( FEditorModeTools, FEditorModeChangedEvent, FEdMode*, bool );
UE_DEPRECATED(4.24, "Use OnEditorModeIDChanged() instead.")
	FEditorModeChangedEvent& OnEditorModeChanged() { return EditorModeChangedEvent; }
PRAGMA_ENABLE_DEPRECATION_WARNINGS

	DECLARE_EVENT_TwoParams(FEditorModeTools, FEditorModeIDChangedEvent, const FEditorModeID&, bool);
	FEditorModeIDChangedEvent& OnEditorModeIDChanged() { return EditorModeIDChangedEvent; }

	/** delegate type for triggering when widget mode changed */
	DECLARE_EVENT_OneParam( FEditorModeTools, FWidgetModeChangedEvent, FWidget::EWidgetMode );
	FWidgetModeChangedEvent& OnWidgetModeChanged() { return WidgetModeChangedEvent; }

	/**	Broadcasts the WidgetModeChanged event */
	void BroadcastWidgetModeChanged(FWidget::EWidgetMode InWidgetMode) { WidgetModeChangedEvent.Broadcast(InWidgetMode); }

	/**	Broadcasts the EditorModeChanged event */
	UE_DEPRECATED(4.24, "Use BroadcastEditorModeIDChanged() instead.")
	void BroadcastEditorModeChanged(FEdMode* Mode, bool IsEnteringMode) { EditorModeChangedEvent.Broadcast(Mode, IsEnteringMode); }

	/**	Broadcasts the EditorModeIDChanged event */
	void BroadcastEditorModeIDChanged(const FEditorModeID& ModeID, bool IsEnteringMode) { EditorModeIDChangedEvent.Broadcast(ModeID, IsEnteringMode); }

	/**
	 * Returns the current CoordSystem
	 * 
	 * @param bGetRawValue true when you want the actual value of CoordSystem, not the value modified by the state.
	 */
	ECoordSystem GetCoordSystem(bool bGetRawValue = false);

	/** Sets the current CoordSystem */
	void SetCoordSystem(ECoordSystem NewCoordSystem);

	/** Sets the hide viewport UI state */
	void SetHideViewportUI( bool bInHideViewportUI ) { bHideViewportUI = bInHideViewportUI; }

	/** Is the viewport UI hidden? */
	bool IsViewportUIHidden() const { return bHideViewportUI; }

	/** The toolbar tab name that should be used as the tab identifier */
	static const FName EditorModeToolbarTabName;

	bool PivotShown;
	bool Snapping;
	bool SnappedActor;

	FVector CachedLocation;
	FVector PivotLocation;
	FVector SnappedLocation;
	FVector GridBase;

	/** The angle for the translate rotate widget */
	float TranslateRotateXAxisAngle;

	/** The angles for the 2d translate rotate widget */
	float TranslateRotate2DAngle;

	/** Draws in the top level corner of all FEditorViewportClient windows (can be used to relay info to the user). */
	FString InfoString;

	/** Sets the host for toolkits created via modes from this mode manager (can only be called once) */
	void SetToolkitHost(TSharedRef<IToolkitHost> Host);

	/** Returns the host for toolkits created via modes from this mode manager */
	TSharedPtr<IToolkitHost> GetToolkitHost() const;

	/** Check if toolkit host exists */
	bool HasToolkitHost() const;

	/**
	 * Returns the set of selected actors.
	 */
	virtual USelection* GetSelectedActors() const;

	/**
	 * @return the set of selected non-actor objects.
	 */
	virtual USelection* GetSelectedObjects() const;

	/**
	 * Returns the set of selected components.
	 */
	virtual USelection* GetSelectedComponents() const;

	/**
	 * Returns the world that is being edited by this mode manager
	 */ 
	virtual UWorld* GetWorld() const;

	/**
	 * Whether or not the current selection has a scene component selected
 	 */
	bool SelectionHasSceneComponent() const;

	bool IsSelectionAllowed(AActor* InActor, const bool bInSelected) const;

	bool IsSelectionHandled(AActor* InActor, const bool bInSelected) const;

	bool ProcessEditDuplicate();
	bool ProcessEditDelete();
	bool ProcessEditCut();
	bool ProcessEditCopy();
	bool ProcessEditPaste();
	EEditAction::Type  GetActionEditDuplicate();
	EEditAction::Type  GetActionEditDelete();
	EEditAction::Type  GetActionEditCut();
	EEditAction::Type  GetActionEditCopy();
	EEditAction::Type GetActionEditPaste();

	void DeactivateOtherVisibleModes(FEditorModeID InMode);
	bool IsSnapRotationEnabled() const;
	bool SnapRotatorToGridOverride(FRotator& InRotation) const;
	void ActorsDuplicatedNotify(TArray<AActor*>& InPreDuplicateSelection, TArray<AActor*>& InPostDuplicateSelection, const bool bOffsetLocations);
	void ActorMoveNotify();
	void ActorSelectionChangeNotify();
	void ActorPropChangeNotify();
	void UpdateInternalData();
	bool IsOnlyVisibleActiveMode(FEditorModeID InMode) const;

	/** returns true if all active EdModes are OK with an AutoSave happening now  */
	bool CanAutoSave() const;

protected:
	/** 
	 * Delegate handlers
	 **/
	void OnEditorSelectionChanged(UObject* NewSelection);
	void OnEditorSelectNone();

	/** List of default modes for this tool.  These must all be compatible with each other. */
	TArray<FEditorModeID> DefaultModeIDs;

	/** A list of active editor modes. */
	TArray< TSharedPtr<FEdMode> > ActiveModes;

	/** A list of active editor modes. */
	TArray< UEdMode* > ActiveScriptableModes;

	/** The host of the toolkits created by these modes */
	TWeakPtr<IToolkitHost> ToolkitHost;

	/** A list of previously active editor modes that we will potentially recycle */
	TMap< FEditorModeID, TSharedPtr<FEdMode> > RecycledModes;

	/** A list of previously active editor modes that we will potentially recycle */
	TMap< FEditorModeID, UEdMode* > RecycledScriptableModes;

	/** The mode that the editor viewport widget is in. */
	FWidget::EWidgetMode WidgetMode;

	/** If the widget mode is being overridden, this will be != WM_None. */
	FWidget::EWidgetMode OverrideWidgetMode;

	/** If 1, draw the widget and let the user interact with it. */
	bool bShowWidget;

	/** if true, the viewports will hide all UI overlays */
	bool bHideViewportUI;

	/** if true the current selection has a scene component */
	bool bSelectionHasSceneComponent;
private:
	struct FEdModeToolbarRow
	{
		FEdModeToolbarRow(FName InModeID, FName InPaletteName, FText InDisplayName, TSharedRef<SWidget> InToolbarWidget)
			: ModeID(InModeID)
			, PaletteName(InPaletteName)
			, DisplayName(InDisplayName)
			, ToolbarWidget(InToolbarWidget)
		{}
		FName ModeID;
		FName PaletteName;
		FText DisplayName;
		TSharedPtr<SWidget> ToolbarWidget;
	};

	/** All toolbar rows generated by active modes.  There will be one row per active mode that generates a toolbar */
	TArray<FEdModeToolbarRow> ActiveToolBarRows;

	/** The coordinate system the widget is operating within. */
	ECoordSystem CoordSystem;

	/** Multicast delegate that is broadcast when a mode is entered or exited */
	FEditorModeChangedEvent EditorModeChangedEvent;

	/** Multicast delegate that is broadcast when a mode is entered or exited */
	FEditorModeIDChangedEvent EditorModeIDChangedEvent;

	/** Multicast delegate that is broadcast when a widget mode is changed */
	FWidgetModeChangedEvent WidgetModeChangedEvent;

	/** The dock tab for any modes that generate a toolbar */
	TWeakPtr<SDockTab> ModeToolbarTab;

	/** The actual toolbar rows will be placed in this vertical box */
	TWeakPtr<SVerticalBox> ModeToolbarBox;

	/** Flag set between calls to StartTracking() and EndTracking() */
	bool bIsTracking;
};
