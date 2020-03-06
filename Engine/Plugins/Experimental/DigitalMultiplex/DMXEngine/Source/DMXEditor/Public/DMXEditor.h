// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "WorkflowOrientedApp/WorkflowCentricApplication.h"
#include "Misc/NotifyHook.h"

class UDMXLibrary;
class SDockTab;
class UFactory;
class SDMXControllers;
class SDMXInputConsole;
class SDMXFixturePatch;
class SDMXFixtureTypes;
class SDMXEntityEditor;
class FDMXEditorToolbar;
class UDMXEntity;

// Used to enable Entity creator code to inject a base name before the entity creation
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnGetBaseNameForNewEntity, TSubclassOf<UDMXEntity>, FString&);
// Used to enable Entity creator code to set values in a newly created entity
DECLARE_MULTICAST_DELEGATE_OneParam(FOnSetupNewEntity, UDMXEntity*);

class DMXEDITOR_API FDMXEditor
	: public FWorkflowCentricApplication	// Allow Add ApplicationModes
	, public FNotifyHook
{
public:
	FDMXEditor();

	virtual ~FDMXEditor();

	//~ Begin IToolkit implementation
	virtual FName GetToolkitFName() const override;
	virtual FText GetBaseToolkitName() const override;
	virtual FString GetWorldCentricTabPrefix() const override;
	virtual FLinearColor GetWorldCentricTabColorScale() const override;
	//~ End IToolkit implementationFExtensibilityManager

	/** Adds a new Entity to this DMX Library */
	void OnAddNewEntity(TSubclassOf<UDMXEntity> InEntityClass);

	/** Activate the editor tab suited to edit Entities of type InEntityClass */
	bool InvokeEditorTabFromEntityType(TSubclassOf<UDMXEntity> InEntityClass);

	FReply OnAddNewEntity_OnClick(TSubclassOf<UDMXEntity> InEntityClass) { OnAddNewEntity(InEntityClass); return FReply::Handled(); }
	/** Checks if adding a new Entity is allowed in the current list */
	bool CanAddNewEntity(TSubclassOf<UDMXEntity> InEntityClass) const;
	bool NewEntity_IsVisibleForType(TSubclassOf<UDMXEntity> InEntityClass) const;

	/** Utility function to handle all steps required to rename a newly added Entity */
	void RenameNewlyAddedEntity(UDMXEntity* InEntity, TSubclassOf<UDMXEntity> InEntityClass);

	/** Gets the content widget for the tab that edits Entities from InEntityClass */
	TSharedPtr<SDMXEntityEditor> GetEditorWidgetForEntityType(TSubclassOf<UDMXEntity> InEntityClass) const;

	/** Switch to the correct tab to select an Entity for editing */
	void SelectEntityInItsTypeTab(UDMXEntity* InEntity, ESelectInfo::Type InSelectionType = ESelectInfo::Type::Direct);
	/** Switch to the correct tab for the first Entity's type and select the Entities */
	void SelectEntitiesInTypeTab(const TArray<UDMXEntity*>& InEntities, ESelectInfo::Type InSelectionType = ESelectInfo::Type::Direct);
	/** Get the selected entities from the tab that stores the passed in type */
	TArray<UDMXEntity*> GetSelectedEntitiesFromTypeTab(TSubclassOf<UDMXEntity> InEntityClass) const;

	//~ Getters for the various DMX widgets
	TSharedRef<SDMXControllers> GetControllersTab() const { return ControllersWidget.ToSharedRef(); }
	TSharedRef<SDMXFixturePatch> GetFixturePatchTab() const { return FixturePatchWidget.ToSharedRef(); }
	TSharedRef<SDMXFixtureTypes> GetFixtureTypesTab() const { return FixtureTypesWidget.ToSharedRef(); }
	TSharedRef<SDMXInputConsole> GetInputConsoleTab() const { return InputConsoleWidget.ToSharedRef(); }
	TSharedRef<SWidget> GetOutputConsoleTab() const { return OutputConsoleWidget.ToSharedRef(); }

	//~ Getters for event dispatchers
	/**
	 * Called before a new Entity creation to set its base name.
	 * It's highly advisable to only bind to this event right before needing it
	 * and unbinding right after it's called. To avoid getting calls for every new Entity creation
	 * (unless that's the desired behavior).
	 */
	FOnGetBaseNameForNewEntity& GetOnGetBaseNameForNewEntity() { return OnGetBaseNameForNewEntity; }
	/**
	 * Called right after a new Entity is created, to set its values up before it gets selected and renamed.
	 * It's highly advisable to only bind to this event right before needing it
	 * and unbinding right after it's called. To avoid getting calls for every new Entity creation
	 * (unless that's the desired behavior).
	 */
	FOnSetupNewEntity& GetOnSetupNewEntity() { return OnSetupNewEntity; }

	/** Edits the specified DMX library */
	virtual void InitEditor(const EToolkitMode::Type Mode, const TSharedPtr< class IToolkitHost >& InitToolkitHost, UDMXLibrary* DMXLibrary);

	/** Should be called when initializing */
	void CommonInitialization(UDMXLibrary* DMXLibrary);

	/** Get the DMX library being edited */
	UDMXLibrary* GetDMXLibrary() const;

	TSharedPtr<FDMXEditorToolbar> GetToolbarBuilder() { return Toolbar; }

	void RegisterToolbarTab(const TSharedRef<class FTabManager>& TabManager);
	
protected:
	/** Creates the widgets that go into the tabs (note: does not create the tabs themselves) **/
	virtual void CreateDefaultTabContents(UDMXLibrary* DMXLibrary);

	/** Create Default Commands **/
	virtual void CreateDefaultCommands();

	/** Called during initialization of the DMX editor to register commands and extenders. */
	virtual void InitalizeExtenders();

	/** Called during initialization of the DMX editor to register any application modes. */
	virtual void RegisterApplicationModes(UDMXLibrary* DMXLibrary, bool bShouldOpenInDefaultsMode, bool bNewlyCreated = false);

private:
	UDMXLibrary* GetEditableDMXLibrary() const;

	//~ Generate Editor widgets for tabs
	TSharedRef<SDMXInputConsole> CreateInputConsoleWidget();
	TSharedRef<SWidget> CreateOutputConsoleWidget();
	TSharedRef<SDMXControllers> CreateControllersWidget();
	TSharedRef<SDMXFixtureTypes> CreateFixtureTypesWidget();
	TSharedRef<SDMXFixturePatch> CreateFixturePatchWidget();

private:
	/** The toolbar builder class */
	TSharedPtr<FDMXEditorToolbar> Toolbar;

	/** The name given to all instances of this type of editor */
	static const FName ToolkitFName;

	/** UI for the "DMX Monitor" tab */
	TSharedPtr<SDMXInputConsole> InputConsoleWidget;

	/** UI for the "DMX Output Console" tab */
	TSharedPtr<SWidget> OutputConsoleWidget;

	// TODO. Should sold object for particular DMX object, like actor components in blueprint editor
	/** UI for the "DMX Controllers" tab */
	TSharedPtr<SDMXControllers> ControllersWidget;

	/** UI for the "DMX Fixture Types" tab */
	TSharedPtr<SDMXFixtureTypes> FixtureTypesWidget;

	/** UI for the "DMX Fixture Patch" tab */
	TSharedPtr<SDMXFixturePatch> FixturePatchWidget;

	//~ Event dispatchers
	FOnGetBaseNameForNewEntity OnGetBaseNameForNewEntity;
	FOnSetupNewEntity OnSetupNewEntity;
};
