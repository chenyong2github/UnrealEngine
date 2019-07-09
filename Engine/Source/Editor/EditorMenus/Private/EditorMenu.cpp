// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "EditorMenu.h"
#include "EditorMenuSubsystem.h"
#include "IEditorMenusModule.h"

#include "Textures/SlateIcon.h"
#include "Framework/Commands/UIAction.h"
#include "Framework/MultiBox/MultiBox.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Internationalization/Internationalization.h"
#include "AssetTypeCategories.h"

#include "Editor.h"

UEditorMenu::UEditorMenu() :
	MenuType(EMultiBoxType::Menu)
	, bShouldCloseWindowAfterMenuSelection(true)
	, bCloseSelfOnly(false)
	, bSearchable(false)
	, bToolBarIsFocusable(false)
	, bToolBarForceSmallIcons(false)
	, bRegistered(false)
	, StyleSet(&FCoreStyle::Get())
{
}

void UEditorMenu::InitMenu(const FEditorMenuOwner InOwner, FName InName, FName InParent, EMultiBoxType InType)
{
	MenuOwner = InOwner;
	MenuName = InName;
	MenuParent = InParent;
	MenuType = InType;
}

void UEditorMenu::InitGeneratedCopy(const UEditorMenu* Source)
{
	// Skip sections and context

	MenuName = Source->MenuName;
	MenuParent = Source->MenuParent;
	StyleName = Source->StyleName;
	TutorialHighlightName = Source->TutorialHighlightName;
	MenuType = Source->MenuType;
	StyleSet = Source->StyleSet;
	bShouldCloseWindowAfterMenuSelection = Source->bShouldCloseWindowAfterMenuSelection;
	bCloseSelfOnly = Source->bCloseSelfOnly;
	bSearchable = Source->bSearchable;
	bToolBarIsFocusable = Source->bToolBarIsFocusable;
	bToolBarForceSmallIcons = Source->bToolBarForceSmallIcons;
	MenuOwner = Source->MenuOwner;
}

int32 UEditorMenu::IndexOfSection(const FName InSectionName) const
{
	for (int32 i=0; i < Sections.Num(); ++i)
	{
		if (Sections[i].Name == InSectionName)
		{
			return i;
		}
	}

	return INDEX_NONE;
}

int32 UEditorMenu::FindInsertIndex(const FEditorMenuSection& InSection) const
{
	const FEditorMenuInsert InInsertPosition = InSection.InsertPosition;
	if (InInsertPosition.IsDefault())
	{
		return Sections.Num();
	}

	if (InInsertPosition.Position == EEditorMenuInsertType::First)
	{
		for (int32 i = 0; i < Sections.Num(); ++i)
		{
			if (Sections[i].InsertPosition.Position != InInsertPosition.Position)
			{
				return i;
			}
		}

		return Sections.Num();
	}

	int32 DestIndex = IndexOfSection(InInsertPosition.Name);
	if (DestIndex == INDEX_NONE)
	{
		return DestIndex;
	}

	if (InInsertPosition.Position == EEditorMenuInsertType::After)
	{
		++DestIndex;
	}

	for (int32 i = DestIndex; i < Sections.Num(); ++i)
	{
		if (Sections[i].InsertPosition != InInsertPosition)
		{
			return i;
		}
	}

	return Sections.Num();
}

FEditorMenuSection& UEditorMenu::AddDynamicSection(const FName SectionName, const FNewSectionConstructChoice& InConstruct, const FEditorMenuInsert InPosition)
{
	FEditorMenuSection& Section = AddSection(SectionName, TAttribute< FText >(), InPosition);
	Section.Construct = InConstruct;
	return Section;
}

FEditorMenuSection& UEditorMenu::AddSection(const FName SectionName, const TAttribute< FText >& InLabel, const FEditorMenuInsert InPosition)
{
	for (FEditorMenuSection& Section : Sections)
	{
		if (Section.Name == SectionName)
		{
			if (InLabel.IsSet())
			{
				Section.Label = InLabel;
			}

			if (InPosition.Name != NAME_None)
			{
				Section.InsertPosition = InPosition;
			}

			return Section;
		}
	}

	FEditorMenuSection& NewSection = Sections.AddDefaulted_GetRef();
	NewSection.InitSection(SectionName, InLabel, InPosition);
	return NewSection;
}

void UEditorMenu::AddSectionScript(const FName SectionName, const FText& InLabel, const FName InsertName, const EEditorMenuInsertType InsertType)
{
	FEditorMenuSection& Section = FindOrAddSection(SectionName);
	Section.Label = InLabel;
	Section.InsertPosition = FEditorMenuInsert(InsertName, InsertType);
}

void UEditorMenu::AddDynamicSectionScript(const FName SectionName, UEditorMenuSectionDynamic* InObject)
{
	FEditorMenuSection& Section = FindOrAddSection(SectionName);
	Section.EditorMenuSectionDynamic = InObject;
}

void UEditorMenu::AddMenuEntryObject(UEditorMenuEntryScript* InObject)
{
	FindOrAddSection(InObject->Data.Section).AddEntryObject(InObject);
}

UEditorMenu* UEditorMenu::AddSubMenu(const FEditorMenuOwner InOwner, const FName SectionName, const FName InName, const FText& InLabel, const FText& InToolTip)
{
	FEditorMenuEntry Args = FEditorMenuEntry::InitSubMenu(MenuName, InName, InLabel, InToolTip, FNewEditorMenuChoice());
	Args.Owner = InOwner;
	FindOrAddSection(NAME_None).AddEntry(Args);
	return UEditorMenuSubsystem::Get()->ExtendMenu(*(MenuName.ToString() + TEXT(".") + InName.ToString()));
}

FEditorMenuSection* UEditorMenu::FindSection(const FName SectionName)
{
	for (FEditorMenuSection& Section : Sections)
	{
		if (Section.Name == SectionName)
		{
			return &Section;
		}
	}

	return nullptr;
}

FEditorMenuSection& UEditorMenu::FindOrAddSection(const FName SectionName)
{
	for (FEditorMenuSection& Section : Sections)
	{
		if (Section.Name == SectionName)
		{
			return Section;
		}
	}
	
	return AddSection(SectionName);
}

void UEditorMenu::RemoveSection(const FName SectionName)
{
	Sections.RemoveAll([SectionName](const FEditorMenuSection& Section) { return Section.Name == SectionName; });
}

bool UEditorMenu::FindEntry(const FName EntryName, int32& SectionIndex, int32& EntryIndex) const
{
	for (int32 i=0; i < Sections.Num(); ++i)
	{
		EntryIndex = Sections[i].IndexOfBlock(EntryName);
		if (EntryIndex != INDEX_NONE)
		{
			SectionIndex = i;
			return true;
		}
	}

	return false;
}

void UEditorMenu::AddMenuEntry(const FName SectionName, const FEditorMenuEntry& Args)
{
	FindOrAddSection(SectionName).AddEntry(Args);
}
