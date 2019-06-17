// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/UObjectGlobals.h"
#include "EditorSubsystem.h"
#include "Templates/SubclassOf.h"
#include "Textures/SlateIcon.h"
#include "Framework/Commands/UIAction.h"
#include "Framework/MultiBox/MultiBox.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/MultiBox/MultiBoxExtender.h"
#include "Framework/MultiBox/MultiBoxDefs.h"
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
class FUICommandInfo;
class FMultiBox;
class SWidget;

class UEditorMenu;
class UEditorMenuEntryScript;
struct FEditorMenuEntry;
struct FEditorMenuSection;
//struct FEditorMenuOwnerScoped;

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
		if (GEditor)
		{
			FModuleManager::LoadModuleChecked<IEditorMenusModule>("EditorMenus");
			return GEditor->GetEditorSubsystem<UEditorMenuSubsystem>();
		}

		return nullptr;
	}

	static inline UEditorMenuSubsystem* GetIfLoaded()
	{
		if (GEditor && IEditorMenusModule::IsAvailable())
		{
			return GEditor->GetEditorSubsystem<UEditorMenuSubsystem>();
		}

		return nullptr;
	}

	// Unregister all entries with a specific owner
	static inline void UnregisterOwner(FEditorMenuOwner Owner)
	{
		if (UEditorMenuSubsystem* EditorMenus = UEditorMenuSubsystem::GetIfLoaded())
		{
			EditorMenus->UnregisterOwnerInternal(Owner);
		}
	}

	// UEditorSubsystem implementation
	virtual void Initialize(FSubsystemCollectionBase& Collection);
	virtual void Deinitialize();


	UEditorMenu* GenerateMenu(const FName Name, FEditorMenuContext& InMenuContext);
	UEditorMenu* GenerateMenu(const TArray<UEditorMenu*>& Hierarchy, FEditorMenuContext& InMenuContext);
	UEditorMenu* GenerateMenuAsBuilder(const UEditorMenu* InMenu, FEditorMenuContext& InMenuContext);

	// Generate switch based on registered name
	TSharedRef<SWidget> GenerateWidget(const FName Name, FEditorMenuContext& InMenuContext);
	TSharedRef<SWidget> GenerateWidget(const TArray<UEditorMenu*>& Hierarchy, FEditorMenuContext& InMenuContext);
	TSharedRef<SWidget> GenerateWidget(UEditorMenu* GeneratedMenu);

	UFUNCTION(BlueprintCallable, Category = "Editor UI")
	UEditorMenu* RegisterMenu(FName Name, const FName Parent = NAME_None, EMultiBoxType Type = EMultiBoxType::Menu);

	UFUNCTION(BlueprintCallable, Category = "Editor UI")
	UEditorMenu* ExtendMenu(const FName Name);

	UFUNCTION(BlueprintCallable, Category = "Editor UI")
	UEditorMenu* FindMenu(const FName Name);

	// Rebuilds all widgets generated from a specific menu
	UFUNCTION(BlueprintCallable, Category = "Editor UI")
	bool RefreshMenuWidget(const FName Name);
	
	// Rebuilds all generated widgets next tick
	UFUNCTION(BlueprintCallable, Category = "Editor UI")
	void RefreshAllWidgets();

	UFUNCTION(BlueprintCallable, Category = "Editor UI")
	static bool AddMenuEntryObject(UEditorMenuEntryScript* MenuEntryObject);

	UFUNCTION(BlueprintCallable, Category = "Editor UI")
	void UnregisterOwnerByName(FName InOwnerName);

	UFUNCTION(BlueprintCallable, Category = "Editor UI")
	void SetSectionLabel(const FName MenuName, const FName SectionName, const FText Label);

	UFUNCTION(BlueprintCallable, Category = "Editor UI")
	void SetSectionPosition(const FName MenuName, const FName SectionName, const FName PositionName, const EEditorMenuInsertType PositionType);

	void AddSection(const FName MenuName, const FName SectionName, const TAttribute< FText >& InLabel, const FEditorMenuInsert InPosition = FEditorMenuInsert());

	void AddEntry(const FName MenuName, const FName SectionName, const FEditorMenuEntry& Entry);

	UFUNCTION(BlueprintCallable, Category = "Editor UI")
	void RemoveEntry(const FName MenuName, const FName Section, const FName Name);

	UFUNCTION(BlueprintCallable, Category = "Editor UI")
	void RemoveSection(const FName MenuName, const FName Section);

	UFUNCTION(BlueprintCallable, Category = "Editor UI")
	void RemoveMenu(const FName MenuName);

	UFUNCTION(BlueprintCallable, Category = "Editor UI")
	static UObject* FindContext(const FEditorMenuContext& InContext, UClass* InClass);

	void AssembleMenuByName(UEditorMenu* GeneratedMenu, const FName Name);
	void AssembleMenuHierarchy(UEditorMenu* GeneratedMenu, const TArray<UEditorMenu*>& Hierarchy);

	FEditorMenuOwner CurrentOwner() const;

	void RegisterStringCommandHandler(const FName InName, const FEditorMenuExecuteString& InDelegate);
	void UnregisterStringCommandHandler(const FName InName);

	friend struct FEditorMenuOwnerScoped;

	//~ Begin UObject Interface
	static void AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector);
	//~ End UObject Interface

private:

	void HandleRefreshAllWidgetsNextTick();

	bool RefreshMenuWidget(const FName Name, FGeneratedEditorMenuWidget& GeneratedMenuWidget);
	
	void CleanupStaleGeneratedMenus();

	TArray<UEditorMenu*> CollectHierarchy(const FName Name);

	void PushOwner(const FEditorMenuOwner InOwner);

	void PopOwner(const FEditorMenuOwner InOwner);

	UEditorMenu* FindSubMenuToGenerateWith(const FName InParentName, const FName InChildName);

	void FillMenu(FMenuBuilder& MenuBuilder, FName InMenuName, FEditorMenuContext InMenuContext);
	void FillMenuBarDropDown(FMenuBuilder& MenuBuilder, FName InParentName, FName InChildName, FEditorMenuContext InMenuContext);
	void PopulateMenuBuilder(FMenuBuilder& MenuBuilder, UEditorMenu* MenuData);
	void PopulateMenuBarBuilder(FMenuBarBuilder& MenuBarBuilder, UEditorMenu* MenuData);
	void PopulateToolBarBuilder(FToolBarBuilder& ToolBarBuilder, UEditorMenu* MenuData);

	TSharedRef<SWidget> GenerateToolbarComboButtonMenu(const FName SubMenuFullName, FEditorMenuContext InContext);

	FUIAction ConvertUIAction(const FEditorMenuStringCommand& StringCommand, const FEditorMenuContext& Context) const;
	static FUIAction ConvertUIAction(FEditorUIActionChoice& Actions, FEditorMenuContext& Context);
	static FUIAction ConvertUIAction(FEditorUIAction& Actions, FEditorMenuContext& Context);
	static FUIAction ConvertUIAction(FEditorDynamicUIAction& Actions, FEditorMenuContext& Context);
	static FUIAction ConvertUIAction(UEditorMenuEntryScript* ScriptObject, FEditorMenuContext& Context);

	void ExecuteStringCommand(const FEditorMenuStringCommand StringCommand, const FEditorMenuContext Context);

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

public:

	UPROPERTY(config, EditAnywhere, Category = Misc)
	TArray<FCustomizedEditorMenu> CustomizedMenus;

private:

	UPROPERTY()
	TMap<FName, UEditorMenu*> Menus;

	UPROPERTY()
	TMap<FName, FGeneratedEditorMenuWidgets> GeneratedMenuWidgets;

	TMap<TWeakPtr<FMultiBox>, TArray<UObject*>> WidgetObjectReferences;

	TArray<FEditorMenuOwner> OwnerStack;

	TMap<FName, FEditorMenuExecuteString> StringCommandHandlers;

	bool bRefreshWidgetsNextTick;
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
