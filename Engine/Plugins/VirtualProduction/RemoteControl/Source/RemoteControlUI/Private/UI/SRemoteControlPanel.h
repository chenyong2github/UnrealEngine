// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Engine/EngineTypes.h"
#include "EditorUndoClient.h"
#include "Engine/EngineTypes.h"
#include "Filters/SFilterBar.h"
#include "IDetailTreeNode.h"
#include "Misc/TextFilter.h"
#include "RemoteControlUIModule.h"
#include "Styling/SlateTypes.h"
#include "RemoteControlFieldPath.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

enum class ERCPanels : uint8;
class AActor;
struct EVisibility;
struct FAssetData;
class FExposedEntityDragDrop;
struct FListEntry;
struct FRCPanelDrawerArgs;
class FRCPanelWidgetRegistry;
struct FRCPanelStyle;
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
class SRCPanelDrawer;
class SRCPanelExposedEntitiesList;
class SRCPanelFilter;
class SSearchBox;
class STextBlock;
class URemoteControlPreset;

class URCController;
class URCBehaviour;
class URCAction;
class FRCControllerModel;
class FRCBehaviourModel;

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
	// Remote Control Logic Delegates
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnControllerAdded, const FName& /* InPropertyName */);
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnControllerSelectionChanged, TSharedPtr<FRCControllerModel> /* InControllerItem */);
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnBehaviourAdded, URCBehaviour* /* InBehaviour */);
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnBehaviourSelectionChanged, TSharedPtr<FRCBehaviourModel> /* InBehaviourItem */);
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnActionAdded, URCAction* /* InAction */);

	DECLARE_MULTICAST_DELEGATE(FOnEmptyControllers);
	DECLARE_MULTICAST_DELEGATE(FOnEmptyBehaviours);
	DECLARE_MULTICAST_DELEGATE(FOnEmptyActions);
	
	void Construct(const FArguments& InArgs, URemoteControlPreset* InPreset, TSharedPtr<IToolkitHost> InToolkitHost);
	~SRemoteControlPanel();
	static void Shutdown();

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
	 * @param InArgs extension arguments
	 * @return Whether a property is exposed or not.
	 */
	bool IsExposed(const FRCExposesPropertyArgs& InArgs);

	/**
	 * @param InOuterObjects outer objects to check
	 * @param InPropertyPath full property path
	 * @param bUsingDuplicatesInPath whether duplications like property.property[1] should be exists or just property[1]
	 * @return Whether objects is exposed or not.
	 */
	bool IsAllObjectsExposed(TArray<UObject*> InOuterObjects, const FString& InPropertyPath, bool bUsingDuplicatesInPath);

	/**
	 * Exposes or unexposes a property.
	 * @param InArgs The extension arguments of the property to toggle.
	 */
	void ToggleProperty(const FRCExposesPropertyArgs& InArgs);

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
	void SetEditMode(bool bEditMode)
	{
		bIsInEditMode = bEditMode;
	}

	/**
	 * Get the exposed entity list.
	 */
	TSharedPtr<SRCPanelExposedEntitiesList> GetEntityList() { return EntityList; }

	/** Re-create the sections of the panel. */
	void Refresh();

	/** Adds or removes widgets from the default toolbar in this asset editor */
	void AddToolbarWidget(TSharedRef<SWidget> Widget);
	void RemoveAllToolbarWidgets();

private:

	//~ Remote Control Commands
	void BindRemoteControlCommands();
	void UnbindRemoteControlCommands();

	/** Register editor events needed to handle reloading objects and blueprint libraries. */
	void RegisterEvents();
	/** Unregister editor events */
	void UnregisterEvents();

	/** Register panels to the drawer. */
	void RegisterPanels();
	/** Unregister panels from the drawer. */
	void UnregisterPanels();

	/** Unexpose a field from the preset. */
	void Unexpose(const FRCExposesPropertyArgs& InArgs);

	/** Handle the using toggling the edit mode check box. */
	void OnEditModeCheckboxToggle(ECheckBoxState State);

	/** Handler called when a blueprint is reinstanced. */
	void OnBlueprintReinstanced();

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
	TSharedRef<SWidget> CreateCPUThrottleWarning() const;

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
	
	/** Handle clicking on the setting button. */
	FReply OnClickSettingsButton();

	/** Handle triggers each time when any material has been recompiled */
	void OnMaterialCompiled(class UMaterialInterface* MaterialInterface);
	
	/** Registers default tool bar */
	static void RegisterDefaultToolBar();

	/** Makes a default asset editing toolbar */
	void GenerateToolbar();
	
	/** Registers Auxiliary tool bar */
	static void RegisterAuxiliaryToolBar();

	/** Makes a Auxiliary asset editing toolbar */
	void GenerateAuxiliaryToolbar();

	FText HandlePresetName() const;

	/** Called to test if "Save" should be enabled for this asset */
	bool CanSaveAsset() const;

	/** Called when "Save" is clicked for this asset */
	void SaveAsset_Execute() const;

	/** Called to test if "Find in Content Browser" should be enabled for this asset */
	bool CanFindInContentBrowser() const;

	/** Called when "Find in Content Browser" is clicked for this asset */
	void FindInContentBrowser_Execute() const;

	static bool ShouldForceSmallIcons();

	void ToggleProtocolMappings_Execute();
	bool CanToggleProtocolsMode() const;
	bool IsInProtocolsMode() const;

	void ToggleLogicEditor_Execute();
	bool CanToggleLogicPanel() const;
	bool IsLogicPanelEnabled() const;

	void OnRCPanelToggled(ERCPanels InPanelID);

	/** Called when user attempts to delete a group/exposed entity. */
	void DeleteEntity_Execute();

	/** Called to test if user is able to delete a group/exposed entity. */
	bool CanDeleteEntity() const;
	
	/** Called when user attempts to rename a group/exposed entity. */
	void RenameEntity_Execute() const;

	/** Called to test if user is able to rename a group/exposed entity. */
	bool CanRenameEntity() const;
	
	// Exposed Entities filtering. (Filters the Exposed Entities view)
	void OnSearchTextChanged(const FText& InFilterText);
	void OnSearchTextCommitted(const FText& InFilterText, ETextCommit::Type InCommitType);
	void PopulateSearchStrings(const SRCPanelTreeNode& Item, TArray<FString>& OutSearchStrings) const;

	/** Handler for when a filter in the filter list has changed */
	void OnFilterChanged();

	/** Loads settings from config based on the preset identifier. */
	void LoadSettings(const FGuid& InInstanceId);
	
	/** Saves settings from config based on the preset identifier. */
	void SaveSettings();

	/** Retrieves active logic panel. */
	TSharedPtr<class SRCLogicPanelBase> GetActiveLogicPanel() const;

private:
	static const FName DefaultRemoteControlPanelToolBarName;
	static const FName AuxiliaryRemoteControlPanelToolBarName;
	/** Holds the preset asset. */
	TStrongObjectPtr<URemoteControlPreset> Preset;
	/** Whether the panel is in edit mode. */
	bool bIsInEditMode = true;
	/** Whether the panel is in protocols mode. */
	bool bIsInProtocolsMode = false;
	/** Whether the logic panel is enabled or not. */
	bool bIsLogicPanelEnabled = false;
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
	/** Wrapper widget for entity details view. */
	TSharedPtr<SBorder> WrappedEntityDetailsView;
	/** Helper widget for entity details view and protocol details view. */
	static TSharedPtr<SBox> NoneSelectedWidget;
	/** Holds the field's protocol details. */
	TSharedPtr<SBox> EntityProtocolDetails;
	/** Whether to show the rebind all button. */
	bool bShowRebindButton = false;
	/** Cache of exposed property arguments. */
	TSet<FRCExposesPropertyArgs> CachedExposedPropertyArgs;
	/** Holds a cache of widgets. */
	TSharedPtr<FRCPanelWidgetRegistry> WidgetRegistry;
	/** Holds the handle to a timer set for next tick. Used to not schedule more than once event per frame */
	FTimerHandle NextTickTimerHandle;
	/** The toolkit that hosts this panel. */
	TWeakPtr<IToolkitHost> ToolkitHost;
	/** Asset Editor Default Toolbar */
	TSharedPtr<SWidget> Toolbar;
	/** Asset Editor Auxiliary Toolbar */
	TSharedPtr<SWidget> AuxiliaryToolbar;
	/** The widget that will house the default Toolbar widget */
	TSharedPtr<SBorder> ToolbarWidgetContent;
	/** The widget that will house the Auxiliary Toolbar widget */
	TSharedPtr<SBorder> AuxiliaryToolbarWidgetContent;
	/** Additional widgets to be added to the toolbar */
	TArray<TSharedRef<SWidget>> ToolbarWidgets;
	/** The text box used to search for tags. */
	TSharedPtr<SSearchBox> SearchBoxPtr;
	/** Holds a shared pointer reference to the active entity that is selected. */
	TSharedPtr<SRCPanelTreeNode> SelectedEntity;
	/** Text filter for the search text. */
	TSharedPtr<TTextFilter<const SRCPanelTreeNode&>> SearchTextFilter;
	/** The filter list */
	TSharedPtr<SRCPanelFilter> FilterPtr;
	/** Actively serached term. */
	TSharedPtr<FText> SearchedText;
	/** Panel Drawer widget holds all docked panels. */
	TSharedPtr<SRCPanelDrawer> PanelDrawer;
	/** Map of Opened Drawers. */
	TMap<ERCPanels, TSharedRef<FRCPanelDrawerArgs>> RegisteredDrawers;
	/** Panel Style reference. */
	const FRCPanelStyle* RCPanelStyle;
	/** Stores the active panel that is drawn. */
	ERCPanels ActivePanel;

	// ~ Remote Control Logic Panels ~

	/** Controller panel UI widget for Remote Control Logic*/
	TSharedPtr<class SRCControllerPanel> ControllerPanel;
	/** Behaviour panel UI widget for Remote Control Logic*/
	TSharedPtr<class SRCBehaviourPanel> BehaviourPanel;
	/** Action panel UI widget for Remote Control Logic*/
	TSharedPtr<class SRCActionPanel> ActionPanel;

public:

	static const float MinimumPanelWidth;
	// Global Delegates for Remote Control Logic
	FOnControllerAdded OnControllerAdded;	
	FOnBehaviourAdded OnBehaviourAdded;
	FOnActionAdded OnActionAdded;
	FOnControllerSelectionChanged OnControllerSelectionChanged;
	FOnBehaviourSelectionChanged OnBehaviourSelectionChanged;
	FOnEmptyControllers OnEmptyControllers;
	FOnEmptyBehaviours OnEmptyBehaviours;
	FOnEmptyActions OnEmptyActions;
};