// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Widgets/SCompoundWidget.h"
#include "RemoteControlField.h"
#include "Widgets/Views/SListView.h"
#include "RemoteControlPreset.h"
#include "IDetailTreeNode.h"
#include "UObject/SoftObjectPath.h"
#include "Templates/SubclassOf.h"
#include "GameFramework/Actor.h"
#include "UObject/StrongObjectPtr.h"

class SRemoteControlPanel;
class IPropertyRowGenerator;
class IPropertyHandle;
class URemoteControlPreset;
struct SExposedFieldWidget;
class FPanelSection;
struct FListEntry;

DECLARE_DELEGATE_TwoParams(FOnEditModeChange, TSharedPtr<SRemoteControlPanel> /* Panel */, bool /* bEditModeChange */)

/**
 * UI representation of a remote control preset.
 * Allows a user to expose/unexpose properties and functions from actors and blueprint libraries.
 */
class SRemoteControlPanel : public SCompoundWidget
{
	SLATE_BEGIN_ARGS(SRemoteControlPanel) {}
		SLATE_EVENT(FOnEditModeChange, OnEditModeChange)
	SLATE_END_ARGS()

public:
	void Construct(const FArguments& InArgs, URemoteControlPreset* InPreset);

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
	bool IsExposed(const TSharedPtr<IPropertyHandle>& Handle);

	/**
	 * Exposes or unexposes a property.
	 * @param Handle The handle of the property to toggle.
	 */
	void ToggleProperty(const TSharedPtr<IPropertyHandle>& Handle);

	/**
	 * @return Whether or not the panel is in edit mode.
	 */
	bool IsInEditMode() const { return bIsInEditMode; }

	/**
	 * Set the edit mode of the panel.
	 * @param bEditMode The desired mode.
	 */
	void SetEditMode(bool bEditMode) { bIsInEditMode = bEditMode; }

	//~ FWidget interface
	virtual FReply OnMouseButtonUp(const FGeometry&, const FPointerEvent&) override;

private:
	/** Register events needed to handle reloading objets and blueprint libraries. */
	void RegisterEvents();

	/** Re-create the sections of the panel. */
	void Refresh();

	/** Refresh the layout of the panel */
	void RefreshLayout();

	/** Select a section by name */
	void SelectSection(const FString& SectionName);

	/** Clear the section selection. */
	void ClearSelection();

	/** Expose a property using its handle. */
	void Expose(const TSharedPtr<IPropertyHandle>& Handle);

	/** Unexpose a field using it's ID*/
	void Unexpose(const FGuid& FieldId);

	/** Find a property ID using it's handle */
	FGuid FindPropertyId(const TSharedPtr<IPropertyHandle>& Handle);

	/** Create a blueprint library picker widget. */
	TSharedRef<SWidget> CreateBlueprintLibraryPicker();

	/** Handles creating a new table row that contains a section. */
	TSharedRef<ITableRow> OnGenerateRow(TSharedRef<FPanelSection> Section, const TSharedRef<STableViewBase>& OwnerTable);

	/** Select actors in the current level. */
	void SelectActorsInlevel(const TArray<UObject*>& Objects);

	/** Create the section widgets. */
	TArray<TSharedRef<FPanelSection>> CreateObjectSections();

	/** Handle creating the actor picker widget. */
	TSharedRef<SWidget> OnGetActorPickerMenuContent();

	/** Handle selecting an actor to expose. */
	void OnSelectActorToExpose(AActor* Actor);

	/** Handle the actor picker closing. */
	void OnCloseActorPicker();

	/** Handle exposing the currently selected actors in the level. */
	void OnExposeSelectedActor();

	/** Expose an array of objects. */
	void ExposeObjects(const TArray<UObject*>& Objects);

	/** Handle removing a section from the panel. */
	FReply HandleRemoveSection(const FString& Alias);

	/** Handle the section selection changing. */
	void HandleSelectionChanged(TSharedPtr<FPanelSection> InSection, ESelectInfo::Type InSelectInfo);

	/** Handle the using toggling the edit mode check box. */
	void OnEditModeCheckboxToggle(ECheckBoxState State);

	/** Handle exposing a blueprint library. */
	void OnExposeBlueprintLibrary(TSharedPtr<FListEntry> InEntry);

	/** Get the currently selected section. */
	TSharedPtr<FPanelSection> GetSelectedSection();

	/** Rebuild the blueprint library list. */
	void ReloadBlueprintLibraries();
private:
	/** Holds the preset asset. */
	TStrongObjectPtr<URemoteControlPreset> Preset;

	/** Holds the section list view. */
	TSharedPtr<SListView<TSharedRef<FPanelSection>>> ListView;

	/** Holds the section widgets. */
	TArray<TSharedRef<FPanelSection>> SectionList;

	/** Whether the panel is in edit mode. */
	bool bIsInEditMode;

	/** Holds the blueprint library picker widget. */
	TSharedPtr<class SMenuAnchor> BlueprintLibraryPicker;

	/** Holds a list of all the blueprint libraries. */
	TArray<TSharedPtr<FListEntry>> BlueprintLibraries;

	/** Delegate called when the edit mode changes. */
	FOnEditModeChange OnEditModeChange;

	friend FPanelSection;
};
