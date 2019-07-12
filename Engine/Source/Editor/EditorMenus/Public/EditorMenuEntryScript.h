// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "UObject/ObjectMacros.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/TextProperty.h"
#include "Textures/SlateIcon.h"
#include "Framework/MultiBox/MultiBoxDefs.h"
#include "Styling/SlateTypes.h"

#include "EditorMenuMisc.h"
#include "EditorMenuContext.h"
#include "EditorMenuSection.h"

#include "EditorMenuEntryScript.generated.h"

struct FEditorMenuEntry;
struct FEditorMenuSection;

USTRUCT(BlueprintType, meta=(HasNativeBreak="EditorMenus.EditorMenuEntryExtensions.BreakScriptSlateIcon", HasNativeMake="EditorMenus.EditorMenuEntryExtensions.MakeScriptSlateIcon"))
struct EDITORMENUS_API FScriptSlateIcon
{
	GENERATED_BODY()

public:
	FScriptSlateIcon();
	FScriptSlateIcon(const FName InStyleSetName, const FName InStyleName);
	FScriptSlateIcon(const FName InStyleSetName, const FName InStyleName, const FName InSmallStyleName);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Editor UI")
	FName StyleSetName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Editor UI")
	FName StyleName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Editor UI")
	FName SmallStyleName;

	operator FSlateIcon() const { return GetSlateIcon(); }

	FSlateIcon GetSlateIcon() const;
};

USTRUCT(BlueprintType)
struct EDITORMENUS_API FEditorMenuEntryScriptDataAdvanced
{
	GENERATED_BODY()

public:

	FEditorMenuEntryScriptDataAdvanced();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Advanced")
	FName TutorialHighlight;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Advanced")
	EMultiBlockType EntryType;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Advanced")
	EUserInterfaceActionType UserInterfaceActionType;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SubMenu")
	bool bIsSubMenu;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SubMenu")
	bool bOpenSubMenuOnClick;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Advanced")
	bool bShouldCloseWindowAfterMenuSelection;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ToolBar")
	bool bSimpleComboBox;
};

USTRUCT(BlueprintType)
struct EDITORMENUS_API FEditorMenuEntryScriptData
{
	GENERATED_BODY()

public:

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Identity")
	FName Menu;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Identity")
	FName Section;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Identity")
	FName Name;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Appearance")
	FText Label;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Appearance")
	FText ToolTip;

	UPROPERTY(EditAnywhere,  BlueprintReadWrite, Category = "Appearance")
	FScriptSlateIcon Icon;

	// Optional identifier used for unregistering a group of menu items
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Advanced")
	FName OwnerName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Identity")
	FEditorMenuInsert InsertPosition;

	UPROPERTY(EditAnywhere,  BlueprintReadWrite, Category = "Advanced")
	FEditorMenuEntryScriptDataAdvanced Advanced;
};

UCLASS(Blueprintable, abstract)
class EDITORMENUS_API UEditorMenuEntryScript : public UObject
{
	GENERATED_BODY()

public:

	UFUNCTION(BlueprintImplementableEvent, Category = "Action")
	void Execute(const FEditorMenuContext& Context);

	UFUNCTION(BlueprintNativeEvent, Category = "Advanced")
	bool CanExecute(const FEditorMenuContext& Context) const;
	virtual bool CanExecute_Implementation(const FEditorMenuContext& Context) const { return true; }

	UFUNCTION(BlueprintNativeEvent, Category = "Advanced")
	ECheckBoxState GetCheckState(const FEditorMenuContext& Context) const;
	virtual ECheckBoxState GetCheckState_Implementation(const FEditorMenuContext& Context) const { return ECheckBoxState::Undetermined; }

	UFUNCTION(BlueprintNativeEvent, Category = "Advanced")
	bool IsVisible(const FEditorMenuContext& Context) const;
	virtual bool IsVisible_Implementation(const FEditorMenuContext& Context) const { return true; }

	UFUNCTION(BlueprintNativeEvent, Category = "Advanced")
	FText GetLabel(const FEditorMenuContext& Context) const;
	virtual FText GetLabel_Implementation(const FEditorMenuContext& Context) const { return Data.Label; }

	UFUNCTION(BlueprintNativeEvent, Category = "Advanced")
	FText GetToolTip(const FEditorMenuContext& Context) const;
	virtual FText GetToolTip_Implementation(const FEditorMenuContext& Context) const { return Data.ToolTip; }

	UFUNCTION(BlueprintNativeEvent, Category = "Advanced")
	FScriptSlateIcon GetIcon(const FEditorMenuContext& Context) const;
	virtual FScriptSlateIcon GetIcon_Implementation(const FEditorMenuContext& Context) const { return Data.Icon; }

	UFUNCTION(BlueprintImplementableEvent, Category = "Advanced")
	void ConstructMenuEntry(UEditorMenu* Menu, const FName SectionName, const FEditorMenuContext& Context);

	UFUNCTION(BlueprintCallable, Category = "Advanced")
	void RegisterMenuEntry();

	UFUNCTION(BlueprintCallable, Category = "Advanced")
	void InitEntry(const FName OwnerName, const FName Menu, const FName Section, const FName Name, const FText& Label = FText(), const FText& ToolTip = FText());

private:

	friend struct FEditorMenuSection;
	friend class UEditorMenuSubsystem;

	TAttribute<FText> CreateLabelAttribute(FEditorMenuContext& Context);

	TAttribute<FText> CreateToolTipAttribute(FEditorMenuContext& Context);

	TAttribute<FSlateIcon> CreateIconAttribute(FEditorMenuContext& Context);

	void ToMenuEntry(FEditorMenuEntry& Output);

	bool IsDynamicConstruct() const;

	FSlateIcon GetSlateIcon(const FEditorMenuContext& Context) const;

public:

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Data")
	FEditorMenuEntryScriptData Data;
};
