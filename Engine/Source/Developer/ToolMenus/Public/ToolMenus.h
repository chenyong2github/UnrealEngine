// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/UObjectGlobals.h"
#include "Templates/SubclassOf.h"
#include "Textures/SlateIcon.h"
#include "Misc/Attribute.h"

#include "IToolMenusModule.h"
#include "ToolMenu.h"
#include "ToolMenuContext.h"
#include "ToolMenuDelegates.h"
#include "ToolMenuEntry.h"
#include "ToolMenuEntryScript.h"
#include "ToolMenuOwner.h"
#include "ToolMenuSection.h"
#include "ToolMenuMisc.h"

#include "Misc/CoreDelegates.h"

#include "ToolMenus.generated.h"

class FMenuBuilder;
class FMultiBox;
class SWidget;

class UToolMenu;
class UToolMenuEntryScript;
struct FToolMenuEntry;
struct FToolMenuSection;

USTRUCT()
struct FCustomizedToolMenuSection
{
	GENERATED_BODY()

	UPROPERTY()
	FName Name;

	UPROPERTY()
	TArray<FName> Items;
};

USTRUCT()
struct FCustomizedToolMenu
{
	GENERATED_BODY()

	UPROPERTY()
	FName Name;

	UPROPERTY()
	TArray<FCustomizedToolMenuSection> Sections;

	UPROPERTY()
	TArray<FName> HiddenItems;

	UPROPERTY()
	TArray<FName> HiddenSections;
};

USTRUCT()
struct FGeneratedToolMenuWidget
{
	GENERATED_BODY()

	UPROPERTY()
	UToolMenu* GeneratedMenu;

	TWeakPtr<SWidget> Widget;
};

USTRUCT()
struct FGeneratedToolMenuWidgets
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<FGeneratedToolMenuWidget> Instances;
};

UCLASS()
class TOOLMENUS_API UToolMenus : public UObject
{
	GENERATED_BODY()

public:

	UToolMenus();

	UFUNCTION(BlueprintCallable, Category = "Tool Menus")
	static UToolMenus* Get();

	/** Try to get UToolMenus without forcing ToolMenus module to load. */
	static inline UToolMenus* TryGet()
	{
		if (IToolMenusModule::IsAvailable())
		{
			return Get();
		}

		return nullptr;
	}

	/** Unregister everything associated with the given owner without forcing ToolMenus module to load. */
	static inline void UnregisterOwner(FToolMenuOwner Owner)
	{
		if (UToolMenus* ToolMenus = UToolMenus::TryGet())
		{
			ToolMenus->UnregisterOwnerInternal(Owner);
		}
	}

	/**
	 * Returns true if slate initialized and editor GUI is being used.
	 * The application should have been initialized before this method is called.
	 *
	 * @return	True if slate initialized and editor GUI is being used.
	 */
	static bool IsToolMenuUIEnabled();

	static void RegisterStartupCallback(const FSimpleMulticastDelegate::FDelegate& Delegate)
	{
		if (IsToolMenuUIEnabled())
		{
			if (UToolMenus::TryGet())
			{
				Delegate.Execute();
			}
			else
			{
				// Wait until UToolMenus has been initialized
				FCoreDelegates::OnPostEngineInit.Add(Delegate);
			}
		}
	}

	static void UnRegisterStartupCallback(const void* UserPointer)
	{
		FCoreDelegates::OnPostEngineInit.RemoveAll(UserPointer);
	}

	/**
	 * Registers a menu by name
	 * @param	Parent	Optional name of a menu to layer on top of.
	 * @param	Type	Type of menu that will be generated such as: ToolBar, VerticalToolBar, etc..
	 * @param	bWarnIfAlreadyRegistered	Display warning if already registered
	 * @return	ToolMenu	Menu object
	 */
	UFUNCTION(BlueprintCallable, Category = "Tool Menus")
	UToolMenu* RegisterMenu(FName Name, const FName Parent = NAME_None, EMultiBoxType Type = EMultiBoxType::Menu, bool bWarnIfAlreadyRegistered = true);

	/**
	 * Extends a menu without registering the menu or claiming ownership of it. Ok to call even if menu does not exist yet.
	 * @param	Name	Name of the menu to extend
	 * @return	ToolMenu	Menu object
	 */
	UFUNCTION(BlueprintCallable, Category = "Tool Menus")
	UToolMenu* ExtendMenu(const FName Name);

	/**
	 * Generate widget from a registered menu. Most common function used to generate new menu widgets.
	 * @param	Name	Registered menu's name that widget will be generated from
	 * @param	Context	Additional information specific to the menu being generated
	 * @return	Widget to display
	 */
	TSharedRef<SWidget> GenerateWidget(const FName Name, FToolMenuContext& InMenuContext);




	/**
	 * Finds an existing menu that has been registered or extended.
	 * @param	Name	Name of the menu to find.
	 * @return	ToolMenu	Menu object. Returns null if not found.
	 */
	UFUNCTION(BlueprintCallable, Category = "Tool Menus")
	UToolMenu* FindMenu(const FName Name);

	/**
	 * Determines if a menu has already been registered.
	 * @param	Name	Name of the menu to find.
	 * @return	bool	True if menu has already been registered.
	 */
	UFUNCTION(BlueprintCallable, Category = "Tool Menus")
	bool IsMenuRegistered(const FName Name) const;

	/** Rebuilds all widgets generated from a specific menu. */
	UFUNCTION(BlueprintCallable, Category = "Tool Menus")
	bool RefreshMenuWidget(const FName Name);

	/** Rebuilds all currently generated widgets next tick. */
	UFUNCTION(BlueprintCallable, Category = "Tool Menus")
	void RefreshAllWidgets();

	/** Registers menu entry object from blueprint/script */
	UFUNCTION(BlueprintCallable, Category = "Tool Menus")
	static bool AddMenuEntryObject(UToolMenuEntryScript* MenuEntryObject);

	/** Removes all entries that were registered under a specific owner name */
	UFUNCTION(BlueprintCallable, Category = "Tool Menus")
	void UnregisterOwnerByName(FName InOwnerName);

	/** Sets a section's displayed label text. */
	UFUNCTION(BlueprintCallable, Category = "Tool Menus")
	void SetSectionLabel(const FName MenuName, const FName SectionName, const FText Label);

	/** Sets where to insert a section into a menu when generating relative to other section names. */
	UFUNCTION(BlueprintCallable, Category = "Tool Menus")
	void SetSectionPosition(const FName MenuName, const FName SectionName, const FName OtherSectionName, const EToolMenuInsertType PositionType);

	/** Registers a section for a menu */
	void AddSection(const FName MenuName, const FName SectionName, const TAttribute< FText >& InLabel, const FToolMenuInsert InPosition = FToolMenuInsert());

	/** Registers an entry for a menu's section */
	void AddEntry(const FName MenuName, const FName SectionName, const FToolMenuEntry& Entry);

	/** Removes a menu entry from a given menu and section */
	UFUNCTION(BlueprintCallable, Category = "Tool Menus")
	void RemoveEntry(const FName MenuName, const FName Section, const FName Name);

	/** Removes a section from a given menu */
	UFUNCTION(BlueprintCallable, Category = "Tool Menus")
	void RemoveSection(const FName MenuName, const FName Section);

	/** Unregisters a menu by name */
	UFUNCTION(BlueprintCallable, Category = "Tool Menus")
	void RemoveMenu(const FName MenuName);

	/** Finds a context object of a given class if it exists */
	UFUNCTION(BlueprintCallable, Category = "Tool Menus")
	static UObject* FindContext(const FToolMenuContext& InContext, UClass* InClass);

	/**
	 * Generate widget from a hierarchy of menus. For advanced specialized use cases.
	 * @param	Hierarchy	Array of menus to combine into a final widget
	 * @param	Context	Additional information specific to the menu being generated
	 * @return	Widget to display
	 */
	TSharedRef<SWidget> GenerateWidget(const TArray<UToolMenu*>& Hierarchy, FToolMenuContext& InMenuContext);

	/**
	 * Generate widget from a final collapsed menu. For advanced specialized use cases.
	 * @param	GeneratedMenu	Combined final menu to generate final widget from
	 * @return	Widget to display
	 */
	TSharedRef<SWidget> GenerateWidget(UToolMenu* GeneratedMenu);
	
	/** Create a finalized menu that combines all parents used to generate a widget. Advanced special use cases only. */
	UToolMenu* GenerateMenu(const FName Name, FToolMenuContext& InMenuContext);

	/** Create a finalized menu that combines given hierarchy array that will generate a widget. Advanced special use cases only. */
	UToolMenu* GenerateMenu(const TArray<UToolMenu*>& Hierarchy, FToolMenuContext& InMenuContext);

	/** Create a finalized menu based on a custom crafted menu. Advanced special use cases only. */
	UToolMenu* GenerateMenuAsBuilder(const UToolMenu* InMenu, FToolMenuContext& InMenuContext);

	/** For advanced use cases */
	void AssembleMenuByName(UToolMenu* GeneratedMenu, const FName Name);

	/** For advanced use cases */
	void AssembleMenuHierarchy(UToolMenu* GeneratedMenu, const TArray<UToolMenu*>& Hierarchy);

	/* Returns list of menus starting with root parent */
	TArray<UToolMenu*> CollectHierarchy(const FName Name);

	/** For advanced use cases */
	FToolMenuOwner CurrentOwner() const;

	/** Registers a new type of string based command handler. */
	void RegisterStringCommandHandler(const FName InName, const FToolMenuExecuteString& InDelegate);

	/** Removes a string based command handler. */
	void UnregisterStringCommandHandler(const FName InName);
	
	/** Sets delegate to setup timer for deferred one off ticks */
	void AssignSetTimerForNextTickDelegate(const FSimpleDelegate& InDelegate);

	/** Timer function used to consolidate multiple duplicate requests into a single frame. */
	void HandleNextTick();

	/** Displaying extension points is for debugging menus */
	DECLARE_DELEGATE_RetVal(bool, FShouldDisplayExtensionPoints);
	FShouldDisplayExtensionPoints ShouldDisplayExtensionPoints;

	static FName JoinMenuPaths(const FName Base, const FName Child);

	friend struct FToolMenuOwnerScoped;
	friend struct FToolMenuStringCommand;

	//~ Begin UObject Interface
	static void AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector);
	//~ End UObject Interface

private:

	/** Sets a timer to be called next engine tick so that multiple repeated actions can be combined together. */
	void SetNextTickTimer();

	/** Release references to UObjects of widgets that have been deleted. Combines multiple requests in one frame together for improved performance. */
	void CleanupStaleWidgetsNextTick();

	/** Release references to UObjects of widgets that have been deleted */
	void CleanupStaleWidgets();

	/** Re-creates widget that is active */
	bool RefreshMenuWidget(const FName Name, FGeneratedToolMenuWidget& GeneratedMenuWidget);

	/** Sets the current temporary menu owner to avoid needing to supply owner for each menu entry being registered. Used by FToolMenuEntryScoped */
	void PushOwner(const FToolMenuOwner InOwner);

	/** Sets the current temporary menu owner. Used by FToolMenuEntryScoped */
	void PopOwner(const FToolMenuOwner InOwner);

	UToolMenu* FindSubMenuToGenerateWith(const FName InParentName, const FName InChildName);

	void FillMenu(FMenuBuilder& MenuBuilder, FName InMenuName, FToolMenuContext InMenuContext);
	void FillMenuBarDropDown(FMenuBuilder& MenuBuilder, FName InParentName, FName InChildName, FToolMenuContext InMenuContext);
	void PopulateMenuBuilder(FMenuBuilder& MenuBuilder, UToolMenu* MenuData);
	void PopulateMenuBarBuilder(FMenuBarBuilder& MenuBarBuilder, UToolMenu* MenuData);
	void PopulateToolBarBuilder(FToolBarBuilder& ToolBarBuilder, UToolMenu* MenuData);

	TSharedRef<SWidget> GenerateToolbarComboButtonMenu(const FName SubMenuFullName, FToolMenuContext InContext);

	FOnGetContent ConvertWidgetChoice(const FNewToolMenuWidgetChoice& Choice, const FToolMenuContext& Context) const;

	/** Converts a string command to a FUIAction */
	static FUIAction ConvertUIAction(const FToolMenuEntry& Block, const FToolMenuContext& Context);
	static FUIAction ConvertUIAction(const FToolUIActionChoice& Choice, const FToolMenuContext& Context);
	static FUIAction ConvertUIAction(const FToolUIAction& Actions, const FToolMenuContext& Context);
	static FUIAction ConvertUIAction(const FToolDynamicUIAction& Actions, const FToolMenuContext& Context);
	static FUIAction ConvertScriptObjectToUIAction(UToolMenuEntryScript* ScriptObject, const FToolMenuContext& Context);

	static void ExecuteStringCommand(const FToolMenuStringCommand StringCommand, const FToolMenuContext Context);

	void FillMenuDynamic(FMenuBuilder& Builder, FNewToolMenuDelegate InConstructMenu, const FToolMenuContext Context);

	void ListAllParents(const FName Name, TArray<FName>& AllParents);

	void AssembleMenu(UToolMenu* GeneratedMenu, const UToolMenu* Other);
	void AssembleMenuSection(UToolMenu* GeneratedMenu, const UToolMenu* Other, FToolMenuSection* DestSection, const FToolMenuSection& OtherSection);

	void CopyMenuSettings(UToolMenu* GeneratedMenu, const UToolMenu* Other);

	void AddReferencedContextObjects(const TSharedRef<FMultiBox>& InMultiBox, const FToolMenuContext& InMenuContext);

	void ApplyCustomization(UToolMenu* GeneratedMenu);

	FCustomizedToolMenu* FindCustomizedMenu(const FName InName);
	int32 FindCustomizedMenuIndex(const FName InName);

	void UnregisterOwnerInternal(FToolMenuOwner Owner);

	bool GetDisplayUIExtensionPoints() const;

private:

	UPROPERTY(EditAnywhere, Category = Misc)
	TArray<FCustomizedToolMenu> CustomizedMenus;

	UPROPERTY()
	TMap<FName, UToolMenu*> Menus;

	UPROPERTY()
	TMap<FName, FGeneratedToolMenuWidgets> GeneratedMenuWidgets;

	TMap<TWeakPtr<FMultiBox>, TArray<UObject*>> WidgetObjectReferences;

	TArray<FToolMenuOwner> OwnerStack;

	TMap<FName, FToolMenuExecuteString> StringCommandHandlers;

	FSimpleDelegate SetTimerForNextTickDelegate;

	bool bNextTickTimerIsSet;
	bool bRefreshWidgetsNextTick;
	bool bCleanupStaleWidgetsNextTick;
};

struct FToolMenuOwnerScoped
{
	FToolMenuOwnerScoped(const FToolMenuOwner InOwner) : Owner(InOwner)
	{
		UToolMenus::Get()->PushOwner(InOwner);
	}

	~FToolMenuOwnerScoped()
	{
		UToolMenus::Get()->PopOwner(Owner);
	}

	FToolMenuOwner GetOwner() const { return Owner; }

private:

	FToolMenuOwner Owner;
};
