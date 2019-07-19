// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "EditorMenuSection.h"
#include "EditorMenuSubsystem.h"
#include "IEditorMenusModule.h"

#include "Textures/SlateIcon.h"
#include "Framework/Commands/UIAction.h"
#include "Framework/MultiBox/MultiBox.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Internationalization/Internationalization.h"

#include "Editor.h"

FEditorMenuSection::FEditorMenuSection() :
	EditorMenuSectionDynamic(nullptr)
{

}


void FEditorMenuSection::InitSection(const FName InName, const TAttribute< FText >& InLabel, const FEditorMenuInsert InPosition)
{
	Name = InName;
	Label = InLabel;
	InsertPosition = InPosition;
}

void FEditorMenuSection::InitGeneratedSectionCopy(const FEditorMenuSection& Source, FEditorMenuContext& InContext)
{
	Name = Source.Name;
	Label = Source.Label;
	InsertPosition = Source.InsertPosition;
	Construct = Source.Construct;
	Context = InContext;
}

FEditorMenuEntry& FEditorMenuSection::AddEntry(const FEditorMenuEntry& Args)
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

FEditorMenuEntry& FEditorMenuSection::AddEntryObject(UEditorMenuEntryScript* InObject)
{
	// Avoid modifying objects that are saved as content on disk
	UEditorMenuEntryScript* DestObject = InObject;
	if (DestObject->IsAsset())
	{
		DestObject = DuplicateObject<UEditorMenuEntryScript>(InObject, UEditorMenuSubsystem::Get());
	}

	// Refresh widgets next tick so that toolbars and menu bars are updated
	UEditorMenuSubsystem::Get()->RefreshAllWidgets();

	FEditorMenuEntry Args;
	DestObject->ToMenuEntry(Args);
	return AddEntry(Args);
}

FEditorMenuEntry& FEditorMenuSection::AddMenuEntry(const FName InName, const TAttribute<FText>& InLabel, const TAttribute<FText>& InToolTip, const TAttribute<FSlateIcon>& InIcon, const FEditorUIActionChoice& InAction, const EUserInterfaceActionType InUserInterfaceActionType, const FName InTutorialHighlightName)
{
	return AddEntry(FEditorMenuEntry::InitMenuEntry(InName, InLabel, InToolTip, InIcon, InAction, InUserInterfaceActionType, InTutorialHighlightName));
}

FEditorMenuEntry& FEditorMenuSection::AddMenuEntry(const TSharedPtr< const FUICommandInfo >& InCommand, const TAttribute<FText>& InLabelOverride, const TAttribute<FText>& InToolTipOverride, const TAttribute<FSlateIcon>& InIconOverride, const FName InTutorialHighlightName, const FName InNameOverride)
{
	return AddEntry(FEditorMenuEntry::InitMenuEntry(InCommand, InLabelOverride, InToolTipOverride, InIconOverride, InTutorialHighlightName, InNameOverride));
}

FEditorMenuEntry& FEditorMenuSection::AddDynamicEntry(const FName InName, const FNewEditorMenuSectionDelegate& InConstruct)
{
	FEditorMenuEntry& Entry = AddEntry(FEditorMenuEntry(UEditorMenuSubsystem::Get()->CurrentOwner(), InName, EMultiBlockType::MenuEntry));
	Entry.Construct = InConstruct;
	return Entry;
}

FEditorMenuEntry& FEditorMenuSection::AddDynamicEntry(const FName InName, const FNewEditorMenuDelegateLegacy& InConstruct)
{
	FEditorMenuEntry& Entry = AddEntry(FEditorMenuEntry(UEditorMenuSubsystem::Get()->CurrentOwner(), InName, EMultiBlockType::MenuEntry));
	Entry.ConstructLegacy = InConstruct;
	return Entry;
}

FEditorMenuEntry& FEditorMenuSection::AddMenuSeparator(const FName InName)
{
	return AddEntry(FEditorMenuEntry::InitMenuSeparator(InName));
}

int32 FEditorMenuSection::IndexOfBlock(const FName InName) const
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

bool FEditorMenuSection::IsNonLegacyDynamic() const
{
	return EditorMenuSectionDynamic || Construct.NewEditorMenuDelegate.IsBound();
}

void FEditorMenuSection::AssembleBlock(const FEditorMenuEntry& Block)
{
	const EEditorMenuInsertType Position = Block.InsertPosition.Position;

	int32 ExistingIndex = IndexOfBlock(Block.Name);
	if (ExistingIndex != INDEX_NONE)
	{
		Blocks[ExistingIndex] = Block;
	}
	else if (Position == EEditorMenuInsertType::Before || Position == EEditorMenuInsertType::After)
	{
		int32 DestIndex = IndexOfBlock(Block.InsertPosition.Name);
		if (DestIndex != INDEX_NONE)
		{
			if (Position == EEditorMenuInsertType::After)
			{
				++DestIndex;
			}

			Blocks.Insert(Block, DestIndex);
		}
	}
	else if (Position == EEditorMenuInsertType::First)
	{
		Blocks.Insert(Block, 0);
	}
	else
	{
		Blocks.Add(Block);
	}
}

int32 FEditorMenuSection::RemoveEntry(const FName InName)
{
	return Blocks.RemoveAll([InName](const FEditorMenuEntry& Block) { return Block.Name == InName; });
}

int32 FEditorMenuSection::RemoveEntriesByOwner(const FEditorMenuOwner InOwner)
{
	if (InOwner != FEditorMenuOwner())
	{
		return Blocks.RemoveAll([InOwner](const FEditorMenuEntry& Block) { return Block.Owner == InOwner; });
	}

	return 0;
}

int32 FEditorMenuSection::FindBlockInsertIndex(const FEditorMenuEntry& InBlock) const
{
	const FEditorMenuInsert InPosition = InBlock.InsertPosition;

	if (InPosition.IsDefault())
	{
		return Blocks.Num();
	}

	if (InPosition.Position == EEditorMenuInsertType::First)
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

	if (InsertPosition.Position == EEditorMenuInsertType::After)
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
