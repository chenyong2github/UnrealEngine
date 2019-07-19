// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "EditorMenuDelegates.h"
#include "EditorMenuEntry.h"
#include "Misc/Attribute.h"

#include "EditorMenuSection.generated.h"

UCLASS(Blueprintable)
class EDITORMENUS_API UEditorMenuSectionDynamic : public UObject
{
	GENERATED_BODY()

public:

	UFUNCTION(BlueprintImplementableEvent, Category = "Editor UI")
	void ConstructSections(UEditorMenu* Menu, const FEditorMenuContext& Context);
};

USTRUCT(BlueprintType)
struct EDITORMENUS_API FEditorMenuSection
{
	GENERATED_BODY()

public:

	FEditorMenuSection();

	void InitSection(const FName InName, const TAttribute< FText >& InLabel, const FEditorMenuInsert InPosition);

	FEditorMenuEntry& AddEntryObject(UEditorMenuEntryScript* InObject);
	FEditorMenuEntry& AddEntry(const FEditorMenuEntry& Args);
	FEditorMenuEntry& AddMenuEntry(const FName InName, const TAttribute<FText>& InLabel, const TAttribute<FText>& InToolTip, const TAttribute<FSlateIcon>& InIcon, const FEditorUIActionChoice& InAction, const EUserInterfaceActionType UserInterfaceActionType = EUserInterfaceActionType::Button, const FName InTutorialHighlightName = NAME_None);
	FEditorMenuEntry& AddMenuEntry(const TSharedPtr< const FUICommandInfo >& InCommand, const TAttribute<FText>& InLabelOverride = TAttribute<FText>(), const TAttribute<FText>& InToolTipOverride = TAttribute<FText>(), const TAttribute<FSlateIcon>& InIconOverride = TAttribute<FSlateIcon>(), const FName InTutorialHighlightName = NAME_None, const FName InNameOverride = NAME_None);

	FEditorMenuEntry& AddDynamicEntry(const FName InName, const FNewEditorMenuSectionDelegate& InConstruct);
	FEditorMenuEntry& AddDynamicEntry(const FName InName, const FNewEditorMenuDelegateLegacy& InConstruct);
	FEditorMenuEntry& AddMenuSeparator(const FName InName);

	template <typename TContextType>
	TContextType* FindContext() const
	{
		return Context.Find<TContextType>();
	}

private:

	void InitGeneratedSectionCopy(const FEditorMenuSection& Source, FEditorMenuContext& InContext);

	int32 RemoveEntry(const FName InName);
	int32 RemoveEntriesByOwner(const FEditorMenuOwner InOwner);

	int32 IndexOfBlock(const FName InName) const;
	int32 FindBlockInsertIndex(const FEditorMenuEntry& InBlock) const;

	void AssembleBlock(const FEditorMenuEntry& InBlock);

	bool IsNonLegacyDynamic() const;

	friend class UEditorMenuSectionExtensions;
	friend class UEditorMenuSubsystem;
	friend class UEditorMenu;

public:

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Editor UI")
	FName Name;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Editor UI")
	TArray<FEditorMenuEntry> Blocks;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Editor UI")
	FEditorMenuInsert InsertPosition;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Editor UI")
	FEditorMenuContext Context;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Editor UI")
	UEditorMenuSectionDynamic* EditorMenuSectionDynamic;

	TAttribute<FText> Label;

	FNewSectionConstructChoice Construct;
};
