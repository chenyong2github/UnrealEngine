// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "EditorMenuOwner.h"
#include "EditorMenuDelegates.h"
#include "EditorMenuSection.h"
#include "EditorMenuContext.h"

#include "Factories/Factory.h"

#include "EditorMenu.generated.h"

UCLASS(BlueprintType)
class EDITORMENUS_API UEditorMenu : public UObject
{
	GENERATED_BODY()

public:

	UEditorMenu();

	UFUNCTION(BlueprintCallable, Category = "Editor UI")
	void InitMenu(const FEditorMenuOwner Owner, FName Name, FName Parent = NAME_None, EMultiBoxType Type = EMultiBoxType::Menu);

	UFUNCTION(BlueprintCallable, Category = "Editor UI", meta = ( ScriptName = "AddSection", DisplayName = "Add Section", AutoCreateRefTerm = "Label", AdvancedDisplay = "InsertName,InsertType" ))
	void AddSectionScript(const FName SectionName, const FText& Label = FText(), const FName InsertName = NAME_None, const EEditorMenuInsertType InsertType = EEditorMenuInsertType::Default);

	UFUNCTION(BlueprintCallable, Category = "Editor UI", meta = ( ScriptName = "AddDynamicSection", DisplayName = "Add Dynamic Section" ))
	void AddDynamicSectionScript(const FName SectionName, UEditorMenuSectionDynamic* Object);

	UFUNCTION(BlueprintCallable, Category = "Editor UI")
	void AddMenuEntry(const FName SectionName, const FEditorMenuEntry& Args);

	UFUNCTION(BlueprintCallable, Category = "Editor UI")
	void AddMenuEntryObject(UEditorMenuEntryScript* InObject);

	UFUNCTION(BlueprintCallable, Category = "Editor UI", meta = ( ScriptName = "AddSubMenu", AutoCreateRefTerm = "Label,ToolTip" ))
	UEditorMenu* AddSubMenuScript(const FName Owner, const FName SectionName, const FName Name, const FText& Label, const FText& ToolTip = FText());

	UEditorMenu* AddSubMenu(const FEditorMenuOwner Owner, const FName SectionName, const FName Name, const FText& Label, const FText& ToolTip = FText());

	void RemoveSection(const FName SectionName);

	FEditorMenuSection& AddSection(const FName SectionName, const TAttribute< FText >& InLabel = TAttribute<FText>(), const FEditorMenuInsert InPosition = FEditorMenuInsert());

	FEditorMenuSection& AddDynamicSection(const FName SectionName, const FNewSectionConstructChoice& InConstruct, const FEditorMenuInsert InPosition = FEditorMenuInsert());

	FEditorMenuSection* FindSection(const FName SectionName);

	FEditorMenuSection& FindOrAddSection(const FName SectionName);

	FName GetMenuName() const { return MenuName; }

	bool IsRegistered() const { return bRegistered; }

	template <typename TContextType>
	TContextType* FindContext() const
	{
		return Context.Find<TContextType>();
	}

	//~ Begin UObject Interface
	virtual bool IsDestructionThreadSafe() const { return false; }
	//~ End UObject Interface

	friend class UEditorMenuSubsystem;

private:

	void InitGeneratedCopy(const UEditorMenu* Source);

	bool FindEntry(const FName EntryName, int32& SectionIndex, int32& EntryIndex) const;

	int32 IndexOfSection(const FName SectionName) const;

	int32 FindInsertIndex(const FEditorMenuSection& InSection) const;

public:

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Editor UI")
	FName MenuName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Editor UI")
	FName MenuParent;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Editor UI")
	FName StyleName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Editor UI")
	FName TutorialHighlightName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Editor UI")
	EMultiBoxType MenuType;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Editor UI")
	bool bShouldCloseWindowAfterMenuSelection;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Editor UI")
	bool bCloseSelfOnly;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Editor UI")
	bool bSearchable;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ToolBar")
	bool bToolBarIsFocusable;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ToolBar")
	bool bToolBarForceSmallIcons;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Editor UI")
	FEditorMenuOwner MenuOwner;
	
	UPROPERTY()
	FEditorMenuContext Context;

private:

	UPROPERTY()
	TArray<FEditorMenuSection> Sections;

	bool bRegistered;

	const ISlateStyle* StyleSet;
};
