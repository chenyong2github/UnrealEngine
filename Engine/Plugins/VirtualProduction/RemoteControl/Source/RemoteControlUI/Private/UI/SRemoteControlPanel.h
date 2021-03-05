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
class FExposedEntityDragDrop;
struct EVisibility;
struct FListEntry;
struct FAssetData;
class FReply;
class IPropertyRowGenerator;
class IPropertyHandle;
struct SRCPanelTreeNode;
class SRCPanelFunctionPicker;
class SRemoteControlPanel;
class SRCPanelExposedEntitiesList;
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
	void Construct(const FArguments& InArgs, URemoteControlPreset* InPreset);
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

	/** Handles exposing an actor. */
	void OnExposeActor(const FAssetData& AssetData);


	/** Handles disbabling CPU throttling. */
	FReply OnClickDisableUseLessCPU() const;

	/** Creates a widget that warns the user when CPU throttling is enabled.  */
	TSharedRef<SWidget> CreateCPUThrottleButton() const;
private:
	/** Holds the preset asset. */
	TStrongObjectPtr<URemoteControlPreset> Preset;
	/** Holds all the field groups. */
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
};
