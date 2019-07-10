// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/UObjectGlobals.h"
#include "EditorSubsystem.h"
#include "Templates/SubclassOf.h"
#include "Textures/SlateIcon.h"
#include "Misc/Attribute.h"
#include "Editor.h"

#include "IEditorMenusModule.h"
#include "EditorMenu.h"
#include "EditorMenuContext.h"
#include "EditorMenuDelegates.h"
#include "EditorMenuEntry.h"
#include "EditorMenuEntryScript.h"
#include "EditorMenuOwner.h"
#include "EditorMenuSection.h"
#include "EditorMenuMisc.h"

#include "EditorMenuSubsystem.generated.h"

class FMenuBuilder;
class FMultiBox;
class SWidget;

class UEditorMenu;
class UEditorMenuEntryScript;
struct FEditorMenuEntry;
struct FEditorMenuSection;

USTRUCT()
struct FCustomizedEditorMenuSection
{
	GENERATED_BODY()

	UPROPERTY()
	FName Name;

	UPROPERTY()
	TArray<FName> Items;
};

USTRUCT()
struct FCustomizedEditorMenu
{
	GENERATED_BODY()

	UPROPERTY()
	FName Name;

	UPROPERTY()
	TArray<FCustomizedEditorMenuSection> Sections;

	UPROPERTY()
	TArray<FName> HiddenItems;

	UPROPERTY()
	TArray<FName> HiddenSections;
};

USTRUCT()
struct FGeneratedEditorMenuWidget
{
	GENERATED_BODY()

	UPROPERTY()
	UEditorMenu* GeneratedMenu;

	TWeakPtr<SWidget> Widget;
};

USTRUCT()
struct FGeneratedEditorMenuWidgets
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<FGeneratedEditorMenuWidget> Instances;
};

UCLASS(config = EditorLayout)
class EDITORMENUS_API UEditorMenuSubsystem : public UEditorSubsystem
{
	GENERATED_BODY()

public:

	UEditorMenuSubsystem();

	static inline UEditorMenuSubsystem* Get()
	{
		FModuleManager::LoadModuleChecked<IEditorMenusModule>("EditorMenus");
		return GEditor->GetEditorSubsystem<UEditorMenuSubsystem>();
	}

	/** Try to get EditorMenuSubsystem if GEditor is valid without forcing EditorMenus module to load. */
	static inline UEditorMenuSubsystem* TryGet()
	{
		if (GEditor && IEditorMenusModule::IsAvailable())
		{
			return GEditor->GetEditorSubsystem<UEditorMenuSubsystem>();
		}

		return nullptr;
	}

	/** Unregister everything associated with the given owner without forcing EditorMenus module to load. */
	static inline void UnregisterOwner(FEditorMenuOwner Owner)
	{
		if (UEditorMenuSubsystem* EditorMenus = UEditorMenuSubsystem::TryGet())
		{
			EditorMenus->UnregisterOwnerInternal(Owner);
		}
	}

	/**
	 * Returns true if slate initialized and editor GUI is being used.
	 * The application should have been initialized before this method is called.
	 *
	 * @return	True if slate initialized and editor GUI is being used.
	 */
	static bool IsRunningEditorUI();

	// UEditorSubsystem implementation
	virtual void Initialize(FSubsystemCollectionBase& Collection);
	virtual void Deinitialize();


	/**
	 * Registers a menu by name
	 * @param	Parent	Optional name of a menu to layer on top of.
	 * @param	Type	Type of menu that will be generated such as: ToolBar, VerticalToolBar, etc..
	 * @return	EditorMenu	Menu object
	 */
	UFUNCTION(BlueprintCallable, Category = "Editor UI")
	UEditorMenu* RegisterMenu(FName Name, const FName Parent = NAME_None, EMultiBoxType Type = EMultiBoxType::Menu);

	/**
	 * Extends a menu without registering the menu or claiming ownership of it. Ok to call even if menu does not exist yet.
	 * @param	Name	Name of the menu to extend
	 * @return	EditorMenu	Menu object
	 */
	UFUNCTION(BlueprintCallable, Category = "Editor UI")
	UEditorMenu* ExtendMenu(const FName Name);

	/**
	 * Generate widget from a registered menu. Most common function used to generate new menu widgets.
	 * @param	Name	Registered menu's name that widget will be generated from
	 * @param	Context	Additional information specific to the menu being generated
	 * @return	Widget to display
	 */
	TSharedRef<SWidget> GenerateWidget(const FName Name, FEditorMenuContext& InMenuContext);




	/**
	 * Finds an existing menu that has been registered or extended.
	 * @param	Name	Name of the menu to find.
	 * @return	EditorMenu	Menu object. Returns null if not found.
	 */
	UFUNCTION(BlueprintCallable, Category = "Editor UI")
	UEditorMenu* FindMenu(const FName Name);

	/**
	 * Determines if a menu has already been registered.
	 * @param	Name	Name of the menu to find.
	 * @return	bool	True if menu has already been registered.
	 */
	UFUNCTION(BlueprintCallable, Category = "Editor UI")
	bool IsMenuRegistered(const FName Name) const;

	/** Rebuilds all widgets generated from a specific menu. */
	UFUNCTION(BlueprintCallable, Category = "Editor UI")
	bool RefreshMenuWidget(const FName Name);

	/** Rebuilds all currently generated widgets next tick. */
	UFUNCTION(BlueprintCallable, Category = "Editor UI")
	void RefreshAllWidgets();

	/** Registers menu entry object from blueprint/script */
	UFUNCTION(BlueprintCallable, Category = "Editor UI")
	static bool AddMenuEntryObject(UEditorMenuEntryScript* MenuEntryObject);

	/** Removes all entries that were registered under a specific owner name */
	UFUNCTION(BlueprintCallable, Category = "Editor UI")
	void UnregisterOwnerByName(FName InOwnerName);

	/** Sets a section's displayed label text. */
	UFUNCTION(BlueprintCallable, Category = "Editor UI")
	void SetSectionLabel(const FName MenuName, const FName SectionName, const FText Label);

	/** Sets where to insert a section into a menu when generating relative to other section names. */
	UFUNCTION(BlueprintCallable, Category = "Editor UI")
	void SetSectionPosition(const FName MenuName, const FName SectionName, const FName OtherSectionName, const EEditorMenuInsertType PositionType);

	/** Registers a section for a menu */
	void AddSection(const FName MenuName, const FName SectionName, const TAttribute< FText >& InLabel, const FEditorMenuInsert InPosition = FEditorMenuInsert());

	/** Registers an entry for a menu's section */
	void AddEntry(const FName MenuName, const FName SectionName, const FEditorMenuEntry& Entry);

	/** Removes a menu entry from a given menu and section */
	UFUNCTION(BlueprintCallable, Category = "Editor UI")
	void RemoveEntry(const FName MenuName, const FName Section, const FName Name);

	/** Removes a section from a given menu */
	UFUNCTION(BlueprintCallable, Category = "Editor UI")
	void RemoveSection(const FName MenuName, const FName Section);

	/** Unregisters a menu by name */
	UFUNCTION(BlueprintCallable, Category = "Editor UI")
	void RemoveMenu(const FName MenuName);

	/** Finds a context object of a given class if it exists */
	UFUNCTION(BlueprintCallable, Category = "Editor UI")
	static UObject* FindContext(const FEditorMenuContext& InContext, UClass* InClass);

	/**
	 * Generate widget from a hierarchy of menus. For advanced specialized use cases.
	 * @param	Hierarchy	Array of menus to combine into a final widget
	 * @param	Context	Additional information specific to the menu being generated
	 * @return	Widget to display
	 */
	TSharedRef<SWidget> GenerateWidget(const TArray<UEditorMenu*>& Hierarchy, FEditorMenuContext& InMenuContext);

	/**
	 * Generate widget from a final collapsed menu. For advanced specialized use cases.
	 * @param	GeneratedMenu	Combined final menu to generate final widget from
	 * @return	Widget to display
	 */
	TSharedRef<SWidget> GenerateWidget(UEditorMenu* GeneratedMenu);
	
	/** Create a finalized menu that combines all parents used to generate a widget. Advanced special use cases only. */
	UEditorMenu* GenerateMenu(const FName Name, FEditorMenuContext& InMenuContext);

	/** Create a finalized menu that combines given hierarchy array that will generate a widget. Advanced special use cases only. */
	UEditorMenu* GenerateMenu(const TArray<UEditorMenu*>& Hierarchy, FEditorMenuContext& InMenuContext);

	/** Create a finalized menu based on a custom crafted menu. Advanced special use cases only. */
	UEditorMenu* GenerateMenuAsBuilder(const UEditorMenu* InMenu, FEditorMenuContext& InMenuContext);

	/** For advanced use cases */
	void AssembleMenuByName(UEditorMenu* GeneratedMenu, const FName Name);

	/** For advanced use cases */
	void AssembleMenuHierarchy(UEditorMenu* GeneratedMenu, const TArray<UEditorMenu*>& Hierarchy);

	/** For advanced use cases */
	FEditorMenuOwner CurrentOwner() const;

	/** Registers a new type of string based command handler. */
	void RegisterStringCommandHandler(const FName InName, const FEditorMenuExecuteString& InDelegate);

	/** Removes a string based command handler. */
	void UnregisterStringCommandHandler(const FName InName);

	friend struct FEditorMenuOwnerScoped;
	friend struct FEditorMenuStringCommand;

	//~ Begin UObject Interface
	static void AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector);
	//~ End UObject Interface

private:

	/** Sets a timer to be called next engine tick so that multiple repeated actions can be combined together. */
	void SetNextTickTimer();

	/** Timer function used to consolidate multiple duplicate requests into a single frame. */
	void HandleNextTick();

	/** Release references to UObjects of widgets that have been deleted. Combines multiple requests in one frame together for improved performance. */
	void CleanupStaleWidgetsNextTick();

	/** Release references to UObjects of widgets that have been deleted */
	void CleanupStaleWidgets();

	/** Re-creates widget that is active */
	bool RefreshMenuWidget(const FName Name, FGeneratedEditorMenuWidget& GeneratedMenuWidget);

	/** Sets the current temporary menu owner to avoid needing to supply owner for each menu entry being registered. Used by FEditorMenuEntryScoped */
	void PushOwner(const FEditorMenuOwner InOwner);

	/** Sets the current temporary menu owner. Used by FEditorMenuEntryScoped */
	void PopOwner(const FEditorMenuOwner InOwner);

	UEditorMenu* FindSubMenuToGenerateWith(const FName InParentName, const FName InChildName);

	TArray<UEditorMenu*> CollectHierarchy(const FName Name);

	void FillMenu(FMenuBuilder& MenuBuilder, FName InMenuName, FEditorMenuContext InMenuContext);
	void FillMenuBarDropDown(FMenuBuilder& MenuBuilder, FName InParentName, FName InChildName, FEditorMenuContext InMenuContext);
	void PopulateMenuBuilder(FMenuBuilder& MenuBuilder, UEditorMenu* MenuData);
	void PopulateMenuBarBuilder(FMenuBarBuilder& MenuBarBuilder, UEditorMenu* MenuData);
	void PopulateToolBarBuilder(FToolBarBuilder& ToolBarBuilder, UEditorMenu* MenuData);

	TSharedRef<SWidget> GenerateToolbarComboButtonMenu(const FName SubMenuFullName, FEditorMenuContext InContext);

	FOnGetContent ConvertWidgetChoice(const FNewEditorMenuWidgetChoice& Choice, const FEditorMenuContext& Context) const;

	/** Converts a string command to a FUIAction */
	static FUIAction ConvertUIAction(const FEditorMenuEntry& Block, const FEditorMenuContext& Context);
	static FUIAction ConvertUIAction(const FEditorUIActionChoice& Choice, const FEditorMenuContext& Context);
	static FUIAction ConvertUIAction(const FEditorUIAction& Actions, const FEditorMenuContext& Context);
	static FUIAction ConvertUIAction(const FEditorDynamicUIAction& Actions, const FEditorMenuContext& Context);
	static FUIAction ConvertScriptObjectToUIAction(UEditorMenuEntryScript* ScriptObject, const FEditorMenuContext& Context);

	static void ExecuteStringCommand(const FEditorMenuStringCommand StringCommand, const FEditorMenuContext Context);

	void FillMenuDynamic(FMenuBuilder& Builder, FNewEditorMenuDelegate InConstructMenu);

	void ListAllParents(const FName Name, TArray<FName>& AllParents);

	void AssembleMenu(UEditorMenu* GeneratedMenu, const UEditorMenu* Other);
	void AssembleMenuSection(UEditorMenu* GeneratedMenu, const UEditorMenu* Other, FEditorMenuSection* DestSection, const FEditorMenuSection& OtherSection);

	void CopyMenuSettings(UEditorMenu* GeneratedMenu, const UEditorMenu* Other);

	void AddReferencedContextObjects(const TSharedRef<FMultiBox>& InMultiBox, const FEditorMenuContext& InMenuContext);

	void ApplyCustomization(UEditorMenu* GeneratedMenu);

	FCustomizedEditorMenu* FindCustomizedMenu(const FName InName);
	int32 FindCustomizedMenuIndex(const FName InName);

	void UnregisterOwnerInternal(FEditorMenuOwner Owner);

	static FName JoinMenuPaths(const FName Base, const FName Child);

	static bool GetDisplayUIExtensionPoints();

private:

	UPROPERTY(config, EditAnywhere, Category = Misc)
	TArray<FCustomizedEditorMenu> CustomizedMenus;

	UPROPERTY()
	TMap<FName, UEditorMenu*> Menus;

	UPROPERTY()
	TMap<FName, FGeneratedEditorMenuWidgets> GeneratedMenuWidgets;

	TMap<TWeakPtr<FMultiBox>, TArray<UObject*>> WidgetObjectReferences;

	TArray<FEditorMenuOwner> OwnerStack;

	TMap<FName, FEditorMenuExecuteString> StringCommandHandlers;

	bool bNextTickTimerIsSet;
	bool bRefreshWidgetsNextTick;
	bool bCleanupStaleWidgetsNextTick;
};

struct FEditorMenuOwnerScoped
{
	FEditorMenuOwnerScoped(const FEditorMenuOwner InOwner) : Owner(InOwner)
	{
		UEditorMenuSubsystem::Get()->PushOwner(InOwner);
	}

	~FEditorMenuOwnerScoped()
	{
		UEditorMenuSubsystem::Get()->PopOwner(Owner);
	}

	FEditorMenuOwner GetOwner() const { return Owner; }

private:

	FEditorMenuOwner Owner;
};
