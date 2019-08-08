// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "ToolMenuSection.h"
#include "ToolMenus.h"
#include "IToolMenusModule.h"

#include "Textures/SlateIcon.h"
#include "Framework/Commands/UIAction.h"
#include "Framework/MultiBox/MultiBox.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Internationalization/Internationalization.h"

FToolMenuSection::FToolMenuSection() :
	ToolMenuSectionDynamic(nullptr)
{

}


void FToolMenuSection::InitSection(const FName InName, const TAttribute< FText >& InLabel, const FToolMenuInsert InPosition)
{
	Name = InName;
	Label = InLabel;
	InsertPosition = InPosition;
}

void FToolMenuSection::InitGeneratedSectionCopy(const FToolMenuSection& Source, FToolMenuContext& InContext)
{
	Name = Source.Name;
	Label = Source.Label;
	InsertPosition = Source.InsertPosition;
	Construct = Source.Construct;
	Context = InContext;
}

FToolMenuEntry& FToolMenuSection::AddEntry(const FToolMenuEntry& Args)
{
	if (Args.Name == NAME_None)
	{
		return Blocks.Add_GetRef(Args);
	}

	int32 BlockIndex = IndexOfBlock(Args.Name);
	if (BlockIndex != INDEX_NONE)
	{
		Blocks[BlockIndex] = Args;
		return Blocks[BlockIndex];
	}
	else
	{
		return Blocks.Add_GetRef(Args);
	}
}

FToolMenuEntry& FToolMenuSection::AddEntryObject(UToolMenuEntryScript* InObject)
{
	// Avoid modifying objects that are saved as content on disk
	UToolMenuEntryScript* DestObject = InObject;
	if (DestObject->IsAsset())
	{
		DestObject = DuplicateObject<UToolMenuEntryScript>(InObject, UToolMenus::Get());
	}

	// Refresh widgets next tick so that toolbars and menu bars are updated
	UToolMenus::Get()->RefreshAllWidgets();

	FToolMenuEntry Args;
	DestObject->ToMenuEntry(Args);
	return AddEntry(Args);
}

FToolMenuEntry& FToolMenuSection::AddMenuEntry(const FName InName, const TAttribute<FText>& InLabel, const TAttribute<FText>& InToolTip, const TAttribute<FSlateIcon>& InIcon, const FToolUIActionChoice& InAction, const EUserInterfaceActionType InUserInterfaceActionType, const FName InTutorialHighlightName)
{
	return AddEntry(FToolMenuEntry::InitMenuEntry(InName, InLabel, InToolTip, InIcon, InAction, InUserInterfaceActionType, InTutorialHighlightName));
}

FToolMenuEntry& FToolMenuSection::AddMenuEntry(const TSharedPtr< const FUICommandInfo >& InCommand, const TAttribute<FText>& InLabelOverride, const TAttribute<FText>& InToolTipOverride, const TAttribute<FSlateIcon>& InIconOverride, const FName InTutorialHighlightName, const FName InNameOverride)
{
	return AddEntry(FToolMenuEntry::InitMenuEntry(InCommand, InLabelOverride, InToolTipOverride, InIconOverride, InTutorialHighlightName, InNameOverride));
}

FToolMenuEntry& FToolMenuSection::AddMenuEntryWithCommandList(const TSharedPtr< const FUICommandInfo >& InCommand, const TSharedPtr< const FUICommandList >& InCommandList, const TAttribute<FText>& InLabelOverride, const TAttribute<FText>& InToolTipOverride, const TAttribute<FSlateIcon>& InIconOverride, const FName InTutorialHighlightName, const FName InNameOverride)
{
	return AddEntry(FToolMenuEntry::InitMenuEntryWithCommandList(InCommand, InCommandList, InLabelOverride, InToolTipOverride, InIconOverride, InTutorialHighlightName, InNameOverride));
}

FToolMenuEntry& FToolMenuSection::AddDynamicEntry(const FName InName, const FNewToolMenuSectionDelegate& InConstruct)
{
	FToolMenuEntry& Entry = AddEntry(FToolMenuEntry(UToolMenus::Get()->CurrentOwner(), InName, EMultiBlockType::MenuEntry));
	Entry.Construct = InConstruct;
	return Entry;
}

FToolMenuEntry& FToolMenuSection::AddDynamicEntry(const FName InName, const FNewToolMenuDelegateLegacy& InConstruct)
{
	FToolMenuEntry& Entry = AddEntry(FToolMenuEntry(UToolMenus::Get()->CurrentOwner(), InName, EMultiBlockType::MenuEntry));
	Entry.ConstructLegacy = InConstruct;
	return Entry;
}

FToolMenuEntry& FToolMenuSection::AddMenuSeparator(const FName InName)
{
	return AddEntry(FToolMenuEntry::InitMenuSeparator(InName));
}

FToolMenuEntry& FToolMenuSection::AddSubMenu(const FName InName, const TAttribute<FText>& InLabel, const TAttribute<FText>& InToolTip, const FNewToolMenuChoice& InMakeMenu, const FToolUIActionChoice& InAction, const EUserInterfaceActionType InUserInterfaceActionType, bool bInOpenSubMenuOnClick, const TAttribute<FSlateIcon>& InIcon, const bool bInShouldCloseWindowAfterMenuSelection)
{
	return AddEntry(FToolMenuEntry::InitSubMenu(InName, InLabel, InToolTip, InMakeMenu, InAction, InUserInterfaceActionType, bInOpenSubMenuOnClick, InIcon, bInShouldCloseWindowAfterMenuSelection));
}

FToolMenuEntry& FToolMenuSection::AddSubMenu(const FName InName, const TAttribute<FText>& InLabel, const TAttribute<FText>& InToolTip, const FNewToolMenuChoice& InMakeMenu, bool bInOpenSubMenuOnClick, const TAttribute<FSlateIcon>& InIcon, const bool bShouldCloseWindowAfterMenuSelection)
{
	return AddEntry(FToolMenuEntry::InitSubMenu(InName, InLabel, InToolTip, InMakeMenu, bInOpenSubMenuOnClick, InIcon, bShouldCloseWindowAfterMenuSelection));
}

int32 FToolMenuSection::IndexOfBlock(const FName InName) const
{
	for (int32 i=0; i < Blocks.Num(); ++i)
	{
		if (Blocks[i].Name == InName)
		{
			return i;
		}
	}

	return INDEX_NONE;
}

bool FToolMenuSection::IsNonLegacyDynamic() const
{
	return ToolMenuSectionDynamic || Construct.NewToolMenuDelegate.IsBound();
}

void FToolMenuSection::AssembleBlock(const FToolMenuEntry& Block)
{
	const EToolMenuInsertType Position = Block.InsertPosition.Position;

	int32 ExistingIndex = IndexOfBlock(Block.Name);
	if (ExistingIndex != INDEX_NONE)
	{
		Blocks[ExistingIndex] = Block;
	}
	else if (Position == EToolMenuInsertType::Before || Position == EToolMenuInsertType::After)
	{
		int32 DestIndex = IndexOfBlock(Block.InsertPosition.Name);
		if (DestIndex != INDEX_NONE)
		{
			if (Position == EToolMenuInsertType::After)
			{
				++DestIndex;
			}

			Blocks.Insert(Block, DestIndex);
		}
	}
	else if (Position == EToolMenuInsertType::First)
	{
		Blocks.Insert(Block, 0);
	}
	else
	{
		Blocks.Add(Block);
	}
}

int32 FToolMenuSection::RemoveEntry(const FName InName)
{
	return Blocks.RemoveAll([InName](const FToolMenuEntry& Block) { return Block.Name == InName; });
}

int32 FToolMenuSection::RemoveEntriesByOwner(const FToolMenuOwner InOwner)
{
	if (InOwner != FToolMenuOwner())
	{
		return Blocks.RemoveAll([InOwner](const FToolMenuEntry& Block) { return Block.Owner == InOwner; });
	}

	return 0;
}

int32 FToolMenuSection::FindBlockInsertIndex(const FToolMenuEntry& InBlock) const
{
	const FToolMenuInsert InPosition = InBlock.InsertPosition;

	if (InPosition.IsDefault())
	{
		return Blocks.Num();
	}

	if (InPosition.Position == EToolMenuInsertType::First)
	{
		for (int32 i = 0; i < Blocks.Num(); ++i)
		{
			if (Blocks[i].InsertPosition != InPosition)
			{
				return i;
			}
		}

		return Blocks.Num();
	}

	int32 DestIndex = IndexOfBlock(InPosition.Name);
	if (DestIndex == INDEX_NONE)
	{
		return INDEX_NONE;
	}

	if (InsertPosition.Position == EToolMenuInsertType::After)
	{
		++DestIndex;
	}

	for (int32 i = DestIndex; i < Blocks.Num(); ++i)
	{
		if (Blocks[i].InsertPosition != InPosition)
		{
			return i;
		}
	}

	return Blocks.Num();
}
