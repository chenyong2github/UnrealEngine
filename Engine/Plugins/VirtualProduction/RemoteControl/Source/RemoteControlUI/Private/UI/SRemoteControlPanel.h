// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "EditorUndoClient.h"
#include "IDetailTreeNode.h"
#include "Styling/SlateTypes.h"
#include "RemoteControlFieldPath.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

class AActor;
struct EVisibility;
struct FAssetData;
class FExposedEntityDragDrop;
struct FListEntry;
class FRCPanelWidgetRegistry;
struct FRemoteControlEntity;
class FReply;
class IToolkitHost;
class IPropertyRowGenerator;
class IPropertyHandle;
class SBox;
class SClassViewer;
class SComboButton;
struct SRCPanelTreeNode;
class SRCPanelFunctionPicker;
class SRemoteControlPanel;
class SRCPanelExposedEntitiesList;
class STextBlock;
class URemoteControlPreset;


DECLARE_DELEGATE_TwoParams(FOnEditModeChange, TSharedPtr<SRemoteControlPanel> /* Panel */, bool /* bEditModeChange */);

/**
 * UI representation of a remote control preset.
 * Allows a user to expose/unexpose properties and functions from actors and blueprint libraries.
 */
class SRemoteControlPanel : public SCompoundWidget, public FSelfRegisteringEditorUndoClient
{
	SLATE_BEGIN_ARGS(SRemoteControlPanel) {}
		SLATE_EVENT(FOnEditModeChange, OnEditModeChange)
		SLATE_ARGUMENT(bool, AllowGrouping)
	SLATE_END_ARGS()

public:
	void Construct(const FArguments& InArgs, URemoteControlPreset* InPreset, TSharedPtr<IToolkitHost> InToolkitHost);
	~SRemoteControlPanel();

	//~ FEditorUndoClient interface
	virtual void PostUndo(bool bSuccess) override;
	virtual void PostRedo(bool bSuccess) override;

	/**
	 * @return The preset represented by the panel.
	 */
	URemoteControlPreset* GetPreset() { return Preset.Get(); }

	/**
	 * @return The preset represented by the panel.
	 */
	const URemoteControlPreset* GetPreset() const { return Preset.Get(); }

	/**
	 * @param Handle The handle representing the property to check.
	 * @return Whether a property is exposed or not.
	 */
	bool IsExposed(const TSharedPtr<IPropertyHandle>& PropertyHandle);

	/**
	 * Exposes or unexposes a property.
	 * @param Handle The handle of the property to toggle.
	 */
	void ToggleProperty(const TSharedPtr<IPropertyHandle>& PropertyHandle);

	/**
	 * @return Whether or not the panel is in edit mode.
	 */
	bool IsInEditMode() const { return bIsInEditMode; }

	/**
	 * Get the selected group.
	 */
	FGuid GetSelectedGroup() const;

	/**
	 * Set the edit mode of the panel.
	 * @param bEditMode The desired mode.
	 */
	void SetEditMode(bool bEditMode) { bIsInEditMode = bEditMode; }

	/**
	 * Get the exposed entity list.
	 */
	TSharedPtr<SRCPanelExposedEntitiesList> GetEntityList() { return EntityList; }

private:
	/** Register editor events needed to handle reloading objects and blueprint libraries. */
	void RegisterEvents();
	/** Unregister editor events */
	void UnregisterEvents();

	/** Re-create the sections of the panel. */
	void Refresh();

	/** Unexpose a field from the preset. */
	void Unexpose(const TSharedPtr<IPropertyHandle>& Handle);

	/** Handle the using toggling the edit mode check box. */
	void OnEditModeCheckboxToggle(ECheckBoxState State);

	/** Handler called when a blueprint is reinstanced. */
	void OnBlueprintReinstanced();

	/** Handles creating a new group. */
	FReply OnCreateGroup();

	/** Expose a property using its handle. */
	void ExposeProperty(UObject* Object, FRCFieldPathInfo FieldPath);

	/** Exposes a function.  */
	void ExposeFunction(UObject* Object, UFunction* Function);

	/** Handles exposing an actor from asset data. */
	void OnExposeActor(const FAssetData& AssetData);

	/** Handle exposing an actor. */
	void ExposeActor(AActor* Actor);

	/** Opens or closes the entity details view tab. */
	void ToggleDetailsView();

	/** Handles disabling CPU throttling. */
	FReply OnClickDisableUseLessCPU() const;

	/** Creates a widget that warns the user when CPU throttling is enabled.  */
	TSharedRef<SWidget> CreateCPUThrottleButton() const;

	/** Create expose button, allowing to expose blueprints and actor functions. */
	TSharedRef<SWidget> CreateExposeButton();

	/** Create expose by class menu content */
	TSharedRef<SWidget> CreateExposeByClassWidget();

	/** Cache the classes (and parent classes) of all actors in the level. */
	void CacheLevelClasses();
	
	//~ Handlers for various level actor events.
	void OnActorAddedToLevel(AActor* Actor);
	void OnLevelActorsRemoved(AActor* Actor);
	void OnLevelActorListChanged();

	/** Handles caching an actor's class and parent classes. */
	void CacheActorClass(AActor* Actor);

	/** Handles refreshing the class picker when the map is changed. */
	void OnMapChange(uint32);

	/** Create the details view for the entity currently selected. */
	TSharedRef<SWidget> CreateEntityDetailsView();
	
	/** Update the details view following entity selection change.  */
	void UpdateEntityDetailsView(const TSharedPtr<SRCPanelTreeNode>& SelectedNode);

	/** Returns whether the preset has any unbound property or function. */
	void UpdateRebindButtonVisibility();

	/** Handle user clicking on the rebind all button. */
	FReply OnClickRebindAllButton();

	//~ Handlers called in order to clear the exposed property cache.
	void OnEntityExposed(URemoteControlPreset* InPreset, const FGuid& InEntityId);
	void OnEntityUnexposed(URemoteControlPreset* InPreset, const FGuid& InEntityId);
	
	/** Toggles the logging part of UI */
	void OnLogCheckboxToggle(ECheckBoxState State);

	/** Triggers a next frame update of the actor function picker to ensure that added actors are valid. */
	void UpdateActorFunctionPicker();
	
	/** Handle updating the preset name textblock when it's renamed. */
	void OnAssetRenamed(const FAssetData& Asset, const FString&);

	/** Handle clicking on the setting button. */
	FReply OnClickSettingsButton();

private:
	/** Holds the preset asset. */
	TStrongObjectPtr<URemoteControlPreset> Preset;
	/** Whether the panel is in edit mode. */
	bool bIsInEditMode = true;
	/** Whether objects need to be re-resolved because PIE Started or ended. */
	bool bTriggerRefreshForPIE = false;
	/** Delegate called when the edit mode changes. */
	FOnEditModeChange OnEditModeChange;
	/** Holds the blueprint library picker */
	TSharedPtr<SRCPanelFunctionPicker> BlueprintPicker;
	/** Holds the actor function picker */
	TSharedPtr<SRCPanelFunctionPicker> ActorFunctionPicker;
	/** Holds the subsystem function picker. */
	TSharedPtr<SRCPanelFunctionPicker> SubsystemFunctionPicker;
	/** Holds the exposed entity list view. */
	TSharedPtr<SRCPanelExposedEntitiesList> EntityList;
	/** Holds the combo button that allows exposing functions and actors. */
	TSharedPtr<SComboButton> ExposeComboButton;
	/** Caches all the classes of actors in the current level. */
	TSet<TWeakObjectPtr<const UClass>> CachedClassesInLevel;
	/** Holds the class picker used to expose all actors of class. */
	TSharedPtr<SClassViewer> ClassPicker;
	/** Holds the field's details. */
	TSharedPtr<class IStructureDetailsView> EntityDetailsView;
	/** Holds the field's protocol details. */
	TSharedPtr<SBox> EntityProtocolDetails;
	/**
	 * Pointer to the currently selected node.
	 * Used in order to ensure that the generated details view keeps a valid pointer to the selected entity.
	 */
	TSharedPtr<FRemoteControlEntity> SelectedEntity;
	/** Whether to show the rebind all button. */
	bool bShowRebindButton = false;
	/** Cache of exposed properties. */
	TSet<TWeakPtr<IPropertyHandle>> CachedExposedProperties;
	/** Preset name widget. */
	TSharedPtr<STextBlock> PresetNameTextBlock;
	/** Holds a cache of widgets. */
	TSharedPtr<FRCPanelWidgetRegistry> WidgetRegistry;
	
	/** The toolkit that hosts this panel. */
	TSharedPtr<IToolkitHost> ToolkitHost;
};
