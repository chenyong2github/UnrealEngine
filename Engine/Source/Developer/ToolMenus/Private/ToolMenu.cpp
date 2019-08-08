// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "ToolMenu.h"
#include "ToolMenus.h"
#include "IToolMenusModule.h"

#include "Textures/SlateIcon.h"
#include "Framework/Commands/UIAction.h"
#include "Framework/MultiBox/MultiBox.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Internationalization/Internationalization.h"

UToolMenu::UToolMenu() :
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

void UToolMenu::InitMenu(const FToolMenuOwner InOwner, FName InName, FName InParent, EMultiBoxType InType)
{
	MenuOwner = InOwner;
	MenuName = InName;
	MenuParent = InParent;
	MenuType = InType;
}

void UToolMenu::InitGeneratedCopy(const UToolMenu* Source, const FName InMenuName, const FToolMenuContext* InContext)
{
	// Skip sections

	MenuName = InMenuName;
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

	if (InContext)
	{
		Context = *InContext;
	}
}

int32 UToolMenu::IndexOfSection(const FName InSectionName) const
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

int32 UToolMenu::FindInsertIndex(const FToolMenuSection& InSection) const
{
	const FToolMenuInsert InInsertPosition = InSection.InsertPosition;
	if (InInsertPosition.IsDefault())
	{
		return Sections.Num();
	}

	if (InInsertPosition.Position == EToolMenuInsertType::First)
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

	if (InInsertPosition.Position == EToolMenuInsertType::After)
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

FToolMenuSection& UToolMenu::AddDynamicSection(const FName SectionName, const FNewSectionConstructChoice& InConstruct, const FToolMenuInsert InPosition)
{
	FToolMenuSection& Section = AddSection(SectionName, TAttribute< FText >(), InPosition);
	Section.Construct = InConstruct;
	return Section;
}

FToolMenuSection& UToolMenu::AddSection(const FName SectionName, const TAttribute< FText >& InLabel, const FToolMenuInsert InPosition)
{
	for (FToolMenuSection& Section : Sections)
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

	FToolMenuSection& NewSection = Sections.AddDefaulted_GetRef();
	NewSection.InitSection(SectionName, InLabel, InPosition);
	return NewSection;
}

void UToolMenu::AddSectionScript(const FName SectionName, const FText& InLabel, const FName InsertName, const EToolMenuInsertType InsertType)
{
	FToolMenuSection& Section = FindOrAddSection(SectionName);
	Section.Label = InLabel;
	Section.InsertPosition = FToolMenuInsert(InsertName, InsertType);
}

void UToolMenu::AddDynamicSectionScript(const FName SectionName, UToolMenuSectionDynamic* InObject)
{
	FToolMenuSection& Section = FindOrAddSection(SectionName);
	Section.ToolMenuSectionDynamic = InObject;
}

void UToolMenu::AddMenuEntryObject(UToolMenuEntryScript* InObject)
{
	FindOrAddSection(InObject->Data.Section).AddEntryObject(InObject);
}

UToolMenu* UToolMenu::AddSubMenuScript(const FName InOwner, const FName SectionName, const FName InName, const FText& InLabel, const FText& InToolTip)
{
	return AddSubMenu(InOwner, SectionName, InName, InLabel, InToolTip);
}

UToolMenu* UToolMenu::AddSubMenu(const FToolMenuOwner InOwner, const FName SectionName, const FName InName, const FText& InLabel, const FText& InToolTip)
{
	FToolMenuEntry Args = FToolMenuEntry::InitSubMenu(MenuName, InName, InLabel, InToolTip, FNewToolMenuChoice());
	Args.Owner = InOwner;
	FindOrAddSection(SectionName).AddEntry(Args);
	return UToolMenus::Get()->ExtendMenu(*(MenuName.ToString() + TEXT(".") + InName.ToString()));
}

FToolMenuSection* UToolMenu::FindSection(const FName SectionName)
{
	for (FToolMenuSection& Section : Sections)
	{
		if (Section.Name == SectionName)
		{
			return &Section;
		}
	}

	return nullptr;
}

FToolMenuSection& UToolMenu::FindOrAddSection(const FName SectionName)
{
	for (FToolMenuSection& Section : Sections)
	{
		if (Section.Name == SectionName)
		{
			return Section;
		}
	}
	
	return AddSection(SectionName);
}

void UToolMenu::RemoveSection(const FName SectionName)
{
	Sections.RemoveAll([SectionName](const FToolMenuSection& Section) { return Section.Name == SectionName; });
}

bool UToolMenu::FindEntry(const FName EntryName, int32& SectionIndex, int32& EntryIndex) const
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

void UToolMenu::AddMenuEntry(const FName SectionName, const FToolMenuEntry& Args)
{
	FindOrAddSection(SectionName).AddEntry(Args);
}
