// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "ToolMenuOwner.h"
#include "ToolMenuDelegates.h"
#include "ToolMenuSection.h"
#include "ToolMenuContext.h"
#include "UObject/Object.h"
#include "Framework/MultiBox/ToolMenuBase.h"

#include "ToolMenu.generated.h"

UCLASS(BlueprintType)
class TOOLMENUS_API UToolMenu : public UToolMenuBase
{
	GENERATED_BODY()

public:

	UToolMenu();

	UFUNCTION(BlueprintCallable, Category = "Tool Menus")
	void InitMenu(const FToolMenuOwner Owner, FName Name, FName Parent = NAME_None, EMultiBoxType Type = EMultiBoxType::Menu);

	UFUNCTION(BlueprintCallable, Category = "Tool Menus", meta = ( ScriptName = "AddSection", DisplayName = "Add Section", AutoCreateRefTerm = "Label", AdvancedDisplay = "InsertName,InsertType" ))
	void AddSectionScript(const FName SectionName, const FText& Label = FText(), const FName InsertName = NAME_None, const EToolMenuInsertType InsertType = EToolMenuInsertType::Default);

	UFUNCTION(BlueprintCallable, Category = "Tool Menus", meta = ( ScriptName = "AddDynamicSection", DisplayName = "Add Dynamic Section" ))
	void AddDynamicSectionScript(const FName SectionName, UToolMenuSectionDynamic* Object);

	UFUNCTION(BlueprintCallable, Category = "Tool Menus")
	void AddMenuEntry(const FName SectionName, const FToolMenuEntry& Args);

	UFUNCTION(BlueprintCallable, Category = "Tool Menus")
	void AddMenuEntryObject(UToolMenuEntryScript* InObject);

	UFUNCTION(BlueprintCallable, Category = "Tool Menus", meta = ( ScriptName = "AddSubMenu", AutoCreateRefTerm = "Label,ToolTip" ))
	UToolMenu* AddSubMenuScript(const FName Owner, const FName SectionName, const FName Name, const FText& Label, const FText& ToolTip = FText());

	UToolMenu* AddSubMenu(const FToolMenuOwner Owner, const FName SectionName, const FName Name, const FText& Label, const FText& ToolTip = FText());

	void RemoveSection(const FName SectionName);

	FToolMenuSection& AddSection(const FName SectionName, const TAttribute< FText >& InLabel = TAttribute<FText>(), const FToolMenuInsert InPosition = FToolMenuInsert());

	FToolMenuSection& AddDynamicSection(const FName SectionName, const FNewSectionConstructChoice& InConstruct, const FToolMenuInsert InPosition = FToolMenuInsert());

	FToolMenuSection* FindSection(const FName SectionName);

	FToolMenuSection& FindOrAddSection(const FName SectionName);

	FName GetMenuName() const { return MenuName; }

	bool IsRegistered() const { return bRegistered; }

	/** returns array [Menu, Menu.SubMenuA, Menu.SubMenuB] for Menu.SubMenuB.SubMenuB */
	TArray<const UToolMenu*> GetSubMenuChain() const;

	/** returns "SubMenuC.SubMenuD" for menu "ModuleA.MenuB.SubMenuC.SubMenuD" */
	FString GetSubMenuNamePath() const;

	//~ Begin UToolMenuBase Interface
	virtual bool IsEditing() const override;
	virtual FName GetSectionName(const FName InEntryName) const override;
	virtual bool ContainsSection(const FName InName) const override;
	virtual bool ContainsEntry(const FName InName) const override;
	virtual FCustomizedToolMenu* FindMenuCustomization() const override;
	virtual FCustomizedToolMenu* AddMenuCustomization() const override;
	virtual FCustomizedToolMenuHierarchy GetMenuCustomizationHierarchy() const override;
	virtual void UpdateMenuCustomizationFromMultibox(const TSharedRef<const FMultiBox>& InMultiBox) override;
	//~ End UToolMenuBase Interface

	TArray<FName> GetMenuHierarchyNames(bool bIncludeSubMenuRoot) const;
	void SetMaxHeight(uint32 InMaxHeight)
	{
		MaxHeight = InMaxHeight;
	}

	template <typename TContextType>
	TContextType* FindContext() const
	{
		return Context.Find<TContextType>();
	}

	//~ Begin UObject Interface
	virtual bool IsDestructionThreadSafe() const { return false; }
	//~ End UObject Interface

	friend class UToolMenus;

private:

	void InitGeneratedCopy(const UToolMenu* Source, const FName InMenuName, const FToolMenuContext* InContext = nullptr);

	bool FindEntry(const FName EntryName, int32& SectionIndex, int32& EntryIndex) const;

	FToolMenuEntry* FindEntry(const FName EntryName);

	const FToolMenuEntry* FindEntry(const FName EntryName) const;

	int32 IndexOfSection(const FName SectionName) const;

	int32 FindInsertIndex(const FToolMenuSection& InSection) const;

public:

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tool Menus")
	FName MenuName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tool Menus")
	FName MenuParent;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tool Menus")
	FName StyleName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tool Menus")
	FName TutorialHighlightName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tool Menus")
	EMultiBoxType MenuType;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tool Menus")
	bool bShouldCloseWindowAfterMenuSelection;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tool Menus")
	bool bCloseSelfOnly;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tool Menus")
	bool bSearchable;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ToolBar")
	bool bToolBarIsFocusable;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ToolBar")
	bool bToolBarForceSmallIcons;

	/** Prevent menu from being customized */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ToolBar")
	bool bPreventCustomization;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tool Menus")
	FToolMenuOwner MenuOwner;
	
	UPROPERTY()
	FToolMenuContext Context;

	UPROPERTY()
	TArray<FToolMenuSection> Sections;

	UPROPERTY()
	const UToolMenu* SubMenuParent;

	UPROPERTY()
	FName SubMenuSourceEntryName;

	FMultiBox::FOnModifyBlockWidgetAfterMake ModifyBlockWidgetAfterMake;

private:

	bool bRegistered;

	const ISlateStyle* StyleSet;

	uint32 MaxHeight;
};
