// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "ToolMenus.h"
#include "IToolMenusModule.h"

#include "Textures/SlateIcon.h"
#include "Framework/Commands/UIAction.h"
#include "Framework/MultiBox/MultiBox.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/Application/SlateApplication.h"
#include "Internationalization/Internationalization.h"

#include "HAL/PlatformApplicationMisc.h" // For clipboard

#define LOCTEXT_NAMESPACE "ToolMenuSubsystem"

FAutoConsoleCommand ToolMenusRefreshMenuWidget = FAutoConsoleCommand(
	TEXT("ToolMenus.RefreshAllWidgets"),
	TEXT("Refresh All Tool Menu Widgets"),
	FConsoleCommandDelegate::CreateLambda([]() {
		UToolMenus::Get()->RefreshAllWidgets();
	}));

FName FToolMenuStringCommand::GetTypeName() const
{
	static const FName CommandName("Command");
	static const FName PythonName("Python");

	switch (Type)
	{
	case EToolMenuStringCommandType::Command:
		return CommandName;
	case EToolMenuStringCommandType::Python:
		return PythonName;
	case EToolMenuStringCommandType::Custom:
		return CustomType;
	default:
		break;
	}

	return NAME_None;
}

FExecuteAction FToolMenuStringCommand::ToExecuteAction(const FToolMenuContext& Context) const
{
	if (IsBound())
	{
		return FExecuteAction::CreateStatic(&UToolMenus::ExecuteStringCommand, *this, Context);
	}

	return FExecuteAction();
}

FToolUIActionChoice::FToolUIActionChoice(const TSharedPtr< const FUICommandInfo >& InCommand, const FUICommandList& InCommandList)
{
	if (InCommand.IsValid())
	{
		if (const FUIAction* UIAction = InCommandList.GetActionForCommand(InCommand))
		{
			Action = *UIAction;
			ToolAction.Reset();
			DynamicToolAction.Reset();
		}
	}
}

UToolMenus::UToolMenus() :
	bNextTickTimerIsSet(false),
	bRefreshWidgetsNextTick(false),
	bCleanupStaleWidgetsNextTick(false)
{
}

UToolMenus* UToolMenus::Get()
{
	static UToolMenus* Singleton = nullptr;
	if (!Singleton)
	{
		Singleton = NewObject<UToolMenus>();
		Singleton->AddToRoot();
		check(Singleton);
	}
	return Singleton;
}

bool UToolMenus::IsToolMenuUIEnabled()
{
	if (!FSlateApplication::IsInitialized())
	{
		return false;
	}

	return !IsRunningCommandlet() && !IsRunningGame() && !IsRunningDedicatedServer() && !IsRunningClientOnly();
}

FName UToolMenus::JoinMenuPaths(const FName Base, const FName Child)
{
	return *(Base.ToString() + TEXT(".") + Child.ToString());
}

bool UToolMenus::GetDisplayUIExtensionPoints() const
{
	return ShouldDisplayExtensionPoints.IsBound() && ShouldDisplayExtensionPoints.Execute();
}

UToolMenu* UToolMenus::FindMenu(const FName Name)
{
	UToolMenu** Found = Menus.Find(Name);
	return Found ? *Found : nullptr;
}

bool UToolMenus::IsMenuRegistered(const FName Name) const
{
	const UToolMenu* const * Found = Menus.Find(Name);
	return Found && *Found && (*Found)->IsRegistered();
}

TArray<UToolMenu*> UToolMenus::CollectHierarchy(const FName InName)
{
	TArray<UToolMenu*> Result;

	UToolMenu* Current = FindMenu(InName);
	while (Current)
	{
		// Detect infinite loop
		for (UToolMenu* Other : Result)
		{
			if (Other->MenuName == Current->MenuName)
			{
				UE_LOG(LogToolMenus, Warning, TEXT("Infinite loop detected in tool menu: %s"), *InName.ToString());
				return TArray<UToolMenu*>();
			}
		}

		Result.Add(Current);

		if (Current->MenuParent != NAME_None)
		{
			Current = FindMenu(Current->MenuParent);
		}
		else
		{
			break;
		}
	}

	Algo::Reverse(Result);

	return Result;
}

void UToolMenus::ListAllParents(const FName InName, TArray<FName>& AllParents)
{
	for (const UToolMenu* Menu : CollectHierarchy(InName))
	{
		AllParents.Add(Menu->MenuName);
	}
}

void UToolMenus::AssembleMenuByName(UToolMenu* GeneratedMenu, const FName InName)
{
	if (UToolMenu* Menu = FindMenu(InName))
	{
		GeneratedMenu->InitGeneratedCopy(Menu, Menu->MenuName);
		AssembleMenuHierarchy(GeneratedMenu, CollectHierarchy(InName));
	}
}

void UToolMenus::AssembleMenuSection(UToolMenu* GeneratedMenu, const UToolMenu* Other, FToolMenuSection* DestSection, const FToolMenuSection& OtherSection)
{
	// Build list of blocks in expected order including blocks created by construct delegates
	TArray<FToolMenuEntry> RemainingBlocks;

	UToolMenu* ConstructedEntries = nullptr;
	for (const FToolMenuEntry& Block : OtherSection.Blocks)
	{
		if (!Block.IsNonLegacyDynamicConstruct())
		{
			RemainingBlocks.Add(Block);
			continue;
		}

		if (ConstructedEntries == nullptr)
		{
			ConstructedEntries = NewObject<UToolMenu>(this);
			ConstructedEntries->Context = DestSection->Context;
		}

		TArray<FToolMenuEntry> GeneratedEntries;
		GeneratedEntries.Add(Block);

		int32 NumIterations = 0;
		while (GeneratedEntries.Num() > 0)
		{
			FToolMenuEntry& GeneratedEntry = GeneratedEntries[0];
			if (GeneratedEntry.IsNonLegacyDynamicConstruct())
			{
				if (NumIterations++ > 5000)
				{
					UE_LOG(LogToolMenus, Warning, TEXT("Possible infinite loop for menu: %s, section: %s, block: %s"), *Other->MenuName.ToString(), *OtherSection.Name.ToString(), *Block.Name.ToString());
					break;
				}
				
				ConstructedEntries->Sections.Reset();
				if (GeneratedEntry.IsScriptObjectDynamicConstruct())
				{
					GeneratedEntry.ScriptObject->ConstructMenuEntry(ConstructedEntries, DestSection->Name, DestSection->Context);
				}
				else
				{
					FToolMenuSection& ConstructedSection = ConstructedEntries->AddSection(DestSection->Name);
					ConstructedSection.Context = ConstructedEntries->Context;
					GeneratedEntry.Construct.Execute(ConstructedSection);
				}
				GeneratedEntries.RemoveAt(0, 1, false);

				// Combine all user's choice of selections here into the current section target
				// If the user wants to add items to different sections they will need to create dynamic section instead (for now)
				int32 NumBlocksInserted = 0;
				for (FToolMenuSection& ConstructedSection : ConstructedEntries->Sections)
				{
					for (FToolMenuEntry& ConstructedBlock : ConstructedSection.Blocks)
					{
						if (ConstructedBlock.InsertPosition.IsDefault())
						{
							ConstructedBlock.InsertPosition = Block.InsertPosition;
						}
					}
					GeneratedEntries.Insert(ConstructedSection.Blocks, NumBlocksInserted);
					NumBlocksInserted += ConstructedSection.Blocks.Num();
				}
			}
			else
			{
				RemainingBlocks.Add(GeneratedEntry);
				GeneratedEntries.RemoveAt(0, 1, false);
			}
		}
	}

	if (ConstructedEntries)
	{
		ConstructedEntries->Sections.Empty();
		ConstructedEntries = nullptr;
	}

	// Repeatedly loop because insert location may not exist until later in list
	while (RemainingBlocks.Num() > 0)
	{
		int32 NumHandled = 0;
		for (int32 i = 0; i < RemainingBlocks.Num(); ++i)
		{
			FToolMenuEntry& Block = RemainingBlocks[i];
			int32 DestIndex = DestSection->FindBlockInsertIndex(Block);
			if (DestIndex != INDEX_NONE)
			{
				DestSection->Blocks.Insert(Block, DestIndex);
				RemainingBlocks.RemoveAt(i);
				--i;
				++NumHandled;
				// Restart loop because items earlier in the list may need to attach to this block
				break;
			}
		}
		if (NumHandled == 0)
		{
			for (const FToolMenuEntry& Block : RemainingBlocks)
			{
				UE_LOG(LogToolMenus, Warning, TEXT("Menu item not found: '%s' for insert: '%s'"), *Block.InsertPosition.Name.ToString(), *Block.Name.ToString());
			}
			break;
		}
	}
}

void UToolMenus::AssembleMenu(UToolMenu* GeneratedMenu, const UToolMenu* Other)
{
	TArray<FToolMenuSection> RemainingSections;

	UToolMenu* ConstructedSections = nullptr;
	for (const FToolMenuSection& OtherSection : Other->Sections)
	{
		if (!OtherSection.IsNonLegacyDynamic())
		{
			RemainingSections.Add(OtherSection);
			continue;
		}
		
		if (ConstructedSections == nullptr)
		{
			ConstructedSections = NewObject<UToolMenu>(this);
			ConstructedSections->Context = GeneratedMenu->Context;
		}

		TArray<FToolMenuSection> GeneratedSections;
		GeneratedSections.Add(OtherSection);

		int32 NumIterations = 0;
		while (GeneratedSections.Num() > 0)
		{
			if (GeneratedSections[0].IsNonLegacyDynamic())
			{
				if (NumIterations++ > 5000)
				{
					UE_LOG(LogToolMenus, Warning, TEXT("Possible infinite loop for menu: %s, section: %s"), *Other->MenuName.ToString(), *OtherSection.Name.ToString());
					break;
				}

				ConstructedSections->Sections.Reset();
				
				if (GeneratedSections[0].ToolMenuSectionDynamic)
				{
					GeneratedSections[0].ToolMenuSectionDynamic->ConstructSections(ConstructedSections, GeneratedMenu->Context);
				}
				else if (GeneratedSections[0].Construct.NewToolMenuDelegate.IsBound())
				{
					GeneratedSections[0].Construct.NewToolMenuDelegate.Execute(ConstructedSections);
				}

				for (FToolMenuSection& ConstructedSection : ConstructedSections->Sections)
				{
					if (ConstructedSection.InsertPosition.IsDefault())
					{
						ConstructedSection.InsertPosition = GeneratedSections[0].InsertPosition;
					}
				}
				
				GeneratedSections.RemoveAt(0, 1, false);				
				GeneratedSections.Insert(ConstructedSections->Sections, 0);
			}
			else
			{
				RemainingSections.Add(GeneratedSections[0]);
				GeneratedSections.RemoveAt(0, 1, false);
			}
		}
	}

	if (ConstructedSections)
	{
		ConstructedSections->Sections.Empty();
		ConstructedSections = nullptr;
	}

	while (RemainingSections.Num() > 0)
	{
		int32 NumHandled = 0;
		for (int32 i=0; i < RemainingSections.Num(); ++i)
		{
			const FToolMenuSection& RemainingSection = RemainingSections[i];

			// Update existing section
			FToolMenuSection* Section = GeneratedMenu->FindSection(RemainingSection.Name);
			if (!Section)
			{
				// Try add new section (if insert location exists)
				int32 DestIndex = GeneratedMenu->FindInsertIndex(RemainingSection);
				if (DestIndex != INDEX_NONE)
				{
					GeneratedMenu->Sections.InsertDefaulted(DestIndex);
					Section = &GeneratedMenu->Sections[DestIndex];
					Section->InitGeneratedSectionCopy(RemainingSection, GeneratedMenu->Context);
				}
				else
				{
					continue;
				}
			}

			AssembleMenuSection(GeneratedMenu, Other, Section, RemainingSection);
			RemainingSections.RemoveAt(i);
			--i;
			++NumHandled;
			break;
		}
		if (NumHandled == 0)
		{
			for (const FToolMenuSection& RemainingSection : RemainingSections)
			{
				UE_LOG(LogToolMenus, Warning, TEXT("Menu section not found: '%s' for insert: '%s'"), *RemainingSection.InsertPosition.Name.ToString(), *RemainingSection.Name.ToString());
			}
			break;
		}
	}
}

int32 UToolMenus::FindCustomizedMenuIndex(const FName InName)
{
	for (int32 i = 0; i < CustomizedMenus.Num(); ++i)
	{
		if (CustomizedMenus[i].Name == InName)
		{
			return i;
		}
	}

	return INDEX_NONE;
}

FCustomizedToolMenu* UToolMenus::FindCustomizedMenu(const FName InName)
{
	int32 FoundIndex = FindCustomizedMenuIndex(InName);
	return (FoundIndex != INDEX_NONE) ? &CustomizedMenus[FoundIndex] : nullptr;
}

void UToolMenus::ApplyCustomization(UToolMenu* GeneratedMenu)
{
	const FCustomizedToolMenu* CustomizedMenu = FindCustomizedMenu(GeneratedMenu->MenuName);
	if (CustomizedMenu == nullptr)
	{
		return;
	}

	TArray<FToolMenuSection> NewSections;
	NewSections.Reserve(GeneratedMenu->Sections.Num());

	for (const FCustomizedToolMenuSection& CustomizedSection : CustomizedMenu->Sections)
	{
		int32 SectionIndex = GeneratedMenu->IndexOfSection(CustomizedSection.Name);
		if (SectionIndex == INDEX_NONE)
		{
			continue;
		}

		FToolMenuSection& Section = GeneratedMenu->Sections[SectionIndex];

		TArray<FToolMenuEntry> NewBlocks;
		NewBlocks.Reserve(Section.Blocks.Num());

		for (const FName ItemName : CustomizedSection.Items)
		{
			int32 EntrySectionIndex = INDEX_NONE;
			int32 EntryIndex = INDEX_NONE;
			if (GeneratedMenu->FindEntry(ItemName, EntrySectionIndex, EntryIndex))
			{
				NewBlocks.Add(GeneratedMenu->Sections[EntrySectionIndex].Blocks[EntryIndex]);
				GeneratedMenu->Sections[EntrySectionIndex].Blocks.RemoveAt(EntryIndex);
			}
		}

		// Remaining blocks
		for (FToolMenuEntry& Block : Section.Blocks)
		{
			NewBlocks.Add(Block);
		}

		Section.Blocks = NewBlocks;

		NewSections.Add(Section);
		GeneratedMenu->Sections.RemoveAt(SectionIndex);
	}

	// Remaining sections
	NewSections.Append(GeneratedMenu->Sections);

	// Hide
	for (int32 SectionIndex=0; SectionIndex < NewSections.Num(); ++SectionIndex)
	{
		FToolMenuSection& Section = NewSections[SectionIndex];
		if (CustomizedMenu->HiddenSections.Contains(Section.Name))
		{
			NewSections.RemoveAt(SectionIndex);
			--SectionIndex;
			continue;
		}

		for (int32 i = 0; i < Section.Blocks.Num(); ++i)
		{
			if (CustomizedMenu->HiddenItems.Contains(Section.Blocks[i].Name))
			{
				Section.Blocks.RemoveAt(i);
				--i;
			}
		}
	}

	GeneratedMenu->Sections = NewSections;
}

void UToolMenus::AssembleMenuHierarchy(UToolMenu* GeneratedMenu, const TArray<UToolMenu*>& Hierarchy)
{
	if (GeneratedMenu->MenuType == EMultiBoxType::MenuBar)
	{
		// Menu Bars require one section
		if (GeneratedMenu->Sections.Num() == 0)
		{
			GeneratedMenu->Sections.AddDefaulted();
		}

		FToolMenuSection& MenuBarSection = GeneratedMenu->Sections[0];
		for (const UToolMenu* MenuData : Hierarchy)
		{
			for (const FToolMenuSection& Section : MenuData->Sections)
			{
				for (const FToolMenuEntry& Block : Section.Blocks)
				{
					MenuBarSection.AssembleBlock(Block);
				}
			}
		}
	}
	else
	{
		for (const UToolMenu* FoundParent : Hierarchy)
		{
			AssembleMenu(GeneratedMenu, FoundParent);
		}
	}

	ApplyCustomization(GeneratedMenu);
}

void UToolMenus::FillMenuDynamic(FMenuBuilder& MenuBuilder, FNewToolMenuDelegate InConstructMenu, const FToolMenuContext Context)
{
	if (InConstructMenu.IsBound())
	{
		// Create final menu
		UToolMenu* MenuData = NewObject<UToolMenu>();
		MenuData->Context = Context;
		InConstructMenu.Execute(MenuData);

		// Populate menu builder with final menu
		PopulateMenuBuilder(MenuBuilder, MenuData);
	}
}

void UToolMenus::FillMenu(class FMenuBuilder& MenuBuilder, FName InMenuName, FToolMenuContext InMenuContext)
{
	// Create combined final menu
	UToolMenu* GeneratedMenu = NewObject<UToolMenu>();
	GeneratedMenu->Context = InMenuContext;
	AssembleMenuByName(GeneratedMenu, InMenuName);

	// Populate menu builder with final menu
	PopulateMenuBuilder(MenuBuilder, GeneratedMenu);
}

TSharedRef<SWidget> UToolMenus::GenerateToolbarComboButtonMenu(const FName SubMenuFullName, FToolMenuContext InContext)
{
	return GenerateWidget(SubMenuFullName, InContext);
}

void UToolMenus::FillMenuBarDropDown(class FMenuBuilder& MenuBuilder, FName InParentName, FName InChildName, FToolMenuContext InMenuContext)
{
	if (UToolMenu* MenuToUse = FindSubMenuToGenerateWith(InParentName, InChildName))
	{
		// Create combined final menu
		UToolMenu* GeneratedMenu = NewObject<UToolMenu>();
		GeneratedMenu->Context = InMenuContext;
		AssembleMenuByName(GeneratedMenu, MenuToUse->MenuName);
		GeneratedMenu->MenuName = JoinMenuPaths(InParentName, InChildName);

		// Populate menu builder with final menu
		PopulateMenuBuilder(MenuBuilder, GeneratedMenu);
	}
}

void UToolMenus::PopulateMenuBuilder(FMenuBuilder& MenuBuilder, UToolMenu* MenuData)
{
	if (GetDisplayUIExtensionPoints())
	{
		const FName MenuName = MenuData->GetMenuName();
		MenuBuilder.AddMenuEntry(
			FText::FromName(MenuName),
			LOCTEXT("CopyMenuNameToClipboard", "Copy menu name to clipboard"),
			FSlateIcon(),
			FExecuteAction::CreateLambda([MenuName]() { FPlatformApplicationMisc::ClipboardCopy(*MenuName.ToString()); }),
			"MenuName"
		);
	}

	for (int i=0; i < MenuData->Sections.Num(); ++i)
	{
		FToolMenuSection& Section = MenuData->Sections[i];
		if (Section.Construct.NewToolMenuDelegateLegacy.IsBound())
		{
			Section.Construct.NewToolMenuDelegateLegacy.Execute(MenuBuilder, MenuData);
			continue;
		}

		MenuBuilder.BeginSection(Section.Name, Section.Label);

		for (FToolMenuEntry& Block : Section.Blocks)
		{
			if (Block.ConstructLegacy.IsBound())
			{
				Block.ConstructLegacy.Execute(MenuBuilder, MenuData);
				continue;
			}

			FUIAction UIAction = ConvertUIAction(Block, MenuData->Context);

			TSharedPtr<SWidget> Widget;
			if (Block.MakeWidget.IsBound())
			{
				Widget = Block.MakeWidget.Execute(MenuData->Context);
			}

			if (Block.Type == EMultiBlockType::MenuEntry)
			{
				if (Block.IsSubMenu())
				{
					if (Block.SubMenuData.ConstructMenu.NewMenuDelegate.IsBound())
					{
						MenuBuilder.AddSubMenu(
							Block.Label,
							Block.ToolTip,
							Block.SubMenuData.ConstructMenu.NewMenuDelegate,
							Block.SubMenuData.bOpenSubMenuOnClick,
							Block.Icon.Get(),
							Block.bShouldCloseWindowAfterMenuSelection,
							Block.Name
						);
					}
					else if (Block.SubMenuData.ConstructMenu.NewToolMenuDelegate.IsBound())
					{
						// SubMenu constructed each time it is opened
						MenuBuilder.AddSubMenu(
							Block.Label,
							Block.ToolTip,
							FNewMenuDelegate::CreateUObject(this, &UToolMenus::FillMenuDynamic, Block.SubMenuData.ConstructMenu.NewToolMenuDelegate, MenuData->Context),
							Block.SubMenuData.bOpenSubMenuOnClick,
							Block.Icon.Get(),
							Block.bShouldCloseWindowAfterMenuSelection,
							Block.Name
						);
					}
					else
					{
						// SubMenu registered once by name in database
						FName SubMenuFullName = JoinMenuPaths(MenuData->MenuName, Block.Name);
						FNewMenuDelegate NewMenuDelegate = FNewMenuDelegate::CreateUObject(this, &UToolMenus::FillMenu, SubMenuFullName, MenuData->Context);

						if (Widget.IsValid())
						{
							// Could also check if Visible/Enabled bound as well
							if (UIAction.IsBound())
							{
								MenuBuilder.AddSubMenu(UIAction, Widget.ToSharedRef(), NewMenuDelegate, Block.bShouldCloseWindowAfterMenuSelection);
							}
							else
							{
								MenuBuilder.AddSubMenu(Widget.ToSharedRef(), NewMenuDelegate, Block.SubMenuData.bOpenSubMenuOnClick, Block.bShouldCloseWindowAfterMenuSelection);
							}
						}
						else
						{
							MenuBuilder.AddSubMenu(
								Block.Label,
								Block.ToolTip,
								NewMenuDelegate,
								Block.SubMenuData.bOpenSubMenuOnClick,
								Block.Icon.Get(),
								Block.bShouldCloseWindowAfterMenuSelection,
								Block.Name
							);
						}
					}
				}
				else
				{
					if (Block.Command.IsValid())
					{
						bool bPopCommandList = false;
						TSharedPtr<const FUICommandList> CommandListForAction;
						if (Block.GetActionForCommand(MenuData->Context, CommandListForAction) != nullptr && CommandListForAction.IsValid())
						{
							MenuBuilder.PushCommandList(CommandListForAction.ToSharedRef());
							bPopCommandList = true;
						}
						else
						{
							UE_LOG(LogToolMenus, Error, TEXT("UI command not found for menu entry: %s, menu: %s"), *Block.Name.ToString(), *MenuData->MenuName.ToString());
						}

						MenuBuilder.AddMenuEntry(Block.Command, Block.Name, Block.Label, Block.ToolTip, Block.Icon.Get());

						if (bPopCommandList)
						{
							MenuBuilder.PopCommandList();
						}
					}
					else if (Block.ScriptObject)
					{
						UToolMenuEntryScript* ScriptObject = Block.ScriptObject;
						const FSlateIcon Icon = ScriptObject->CreateIconAttribute(MenuData->Context).Get();
						MenuBuilder.AddMenuEntry(ScriptObject->CreateLabelAttribute(MenuData->Context), ScriptObject->CreateToolTipAttribute(MenuData->Context), Icon, UIAction, ScriptObject->Data.Name, Block.UserInterfaceActionType, Block.TutorialHighlightName);
					}
					else
					{
						if (Widget.IsValid())
						{
							MenuBuilder.AddMenuEntry(UIAction, Widget.ToSharedRef(), Block.Name, Block.ToolTip, Block.UserInterfaceActionType, Block.TutorialHighlightName);
						}
						else
						{
							MenuBuilder.AddMenuEntry(Block.Label, Block.ToolTip, Block.Icon.Get(), UIAction, Block.Name, Block.UserInterfaceActionType, Block.TutorialHighlightName);
						}
					}
				}
			}
			else if (Block.Type == EMultiBlockType::MenuSeparator)
			{
				MenuBuilder.AddMenuSeparator(Block.Name);
			}
			else if (Block.Type == EMultiBlockType::Widget)
			{
				MenuBuilder.AddWidget(Widget.ToSharedRef(), Block.Label.Get(), Block.WidgetData.bNoIndent, Block.WidgetData.bSearchable);
			}
			else
			{
				UE_LOG(LogToolMenus, Warning, TEXT("Menu '%s', item '%s', type not currently supported: %d"), *MenuData->MenuName.ToString(), *Block.Name.ToString(), Block.Type);
			}
		}

		MenuBuilder.EndSection();
	}

	AddReferencedContextObjects(MenuBuilder.GetMultiBox(), MenuData->Context);
}

void UToolMenus::PopulateToolBarBuilder(FToolBarBuilder& ToolBarBuilder, UToolMenu* MenuData)
{
	for (FToolMenuSection& Section : MenuData->Sections)
	{
		if (Section.Construct.NewToolBarDelegateLegacy.IsBound())
		{
			Section.Construct.NewToolBarDelegateLegacy.Execute(ToolBarBuilder, MenuData);
			continue;
		}

		ToolBarBuilder.BeginSection(Section.Name);

		for (FToolMenuEntry& Block : Section.Blocks)
		{
			if (Block.ToolBarData.ConstructLegacy.IsBound())
			{
				Block.ToolBarData.ConstructLegacy.Execute(ToolBarBuilder, MenuData);
				continue;
			}

			FUIAction UIAction = ConvertUIAction(Block, MenuData->Context);

			TSharedPtr<SWidget> Widget;
			if (Block.MakeWidget.IsBound())
			{
				Widget = Block.MakeWidget.Execute(MenuData->Context);
			}

			if (Block.Type == EMultiBlockType::ToolBarButton)
			{
				if (Block.Command.IsValid())
				{
					bool bPopCommandList = false;
					TSharedPtr<const FUICommandList> CommandListForAction;
					if (Block.GetActionForCommand(MenuData->Context, CommandListForAction) != nullptr && CommandListForAction.IsValid())
					{
						ToolBarBuilder.PushCommandList(CommandListForAction.ToSharedRef());
						bPopCommandList = true;
					}
					else
					{
						UE_LOG(LogToolMenus, Error, TEXT("UI command not found for toolbar entry: %s, toolbar: %s"), *Block.Name.ToString(), *MenuData->MenuName.ToString());
					}

					ToolBarBuilder.AddToolBarButton(Block.Command, Block.Name, Block.Label, Block.ToolTip, Block.Icon, Block.TutorialHighlightName);

					if (bPopCommandList)
					{
						ToolBarBuilder.PopCommandList();
					}
				}
				else if (Block.ScriptObject)
				{
					UToolMenuEntryScript* ScriptObject = Block.ScriptObject;
					TAttribute<FSlateIcon> Icon = ScriptObject->CreateIconAttribute(MenuData->Context);
					ToolBarBuilder.AddToolBarButton(UIAction, ScriptObject->Data.Name, ScriptObject->CreateLabelAttribute(MenuData->Context), ScriptObject->CreateToolTipAttribute(MenuData->Context), Icon, Block.UserInterfaceActionType, Block.TutorialHighlightName);
				}
				else
				{
					ToolBarBuilder.AddToolBarButton(UIAction, Block.Name, Block.Label, Block.ToolTip, Block.Icon, Block.UserInterfaceActionType, Block.TutorialHighlightName);
				}
			}
			else if (Block.Type == EMultiBlockType::ToolBarComboButton)
			{
				FOnGetContent OnGetContent = ConvertWidgetChoice(Block.ToolBarData.ComboButtonContextMenuGenerator, MenuData->Context);
				if (OnGetContent.IsBound())
				{
					ToolBarBuilder.AddComboButton(UIAction, OnGetContent, Block.Label, Block.ToolTip, Block.Icon, Block.ToolBarData.bSimpleComboBox, Block.TutorialHighlightName);
				}
				else
				{
					FName SubMenuFullName = JoinMenuPaths(MenuData->MenuName, Block.Name);
					FOnGetContent Delegate = FOnGetContent::CreateUObject(this, &UToolMenus::GenerateToolbarComboButtonMenu, SubMenuFullName, MenuData->Context);
					ToolBarBuilder.AddComboButton(UIAction, Delegate, Block.Label, Block.ToolTip, Block.Icon, Block.ToolBarData.bSimpleComboBox, Block.TutorialHighlightName);
				}
			}
			else if (Block.Type == EMultiBlockType::ToolBarSeparator)
			{
				ToolBarBuilder.AddSeparator(Block.Name);
			}
			else if (Block.Type == EMultiBlockType::Widget)
			{
				ToolBarBuilder.AddWidget(Widget.ToSharedRef(), Block.TutorialHighlightName, Block.WidgetData.bSearchable);
			}
			else
			{
				UE_LOG(LogToolMenus, Warning, TEXT("Toolbar '%s', item '%s', type not currently supported: %d"), *MenuData->MenuName.ToString(), *Block.Name.ToString(), Block.Type);
			}
		}

		ToolBarBuilder.EndSection();
	}

	if (GetDisplayUIExtensionPoints())
	{
		const FName MenuName = MenuData->GetMenuName();
		ToolBarBuilder.BeginSection(MenuName);
		ToolBarBuilder.AddToolBarButton(
			FExecuteAction::CreateLambda([MenuName]() { FPlatformApplicationMisc::ClipboardCopy(*MenuName.ToString()); }), 
			"MenuName",
			LOCTEXT("CopyNameToClipboard", "Copy Name"),
			LOCTEXT("CopyMenuNameToClipboard", "Copy menu name to clipboard")
		);
		ToolBarBuilder.EndSection();
	}

	AddReferencedContextObjects(ToolBarBuilder.GetMultiBox(), MenuData->Context);
}

void UToolMenus::PopulateMenuBarBuilder(FMenuBarBuilder& MenuBarBuilder, UToolMenu* GeneratedMenu)
{
	if (GeneratedMenu->Sections.Num() > 0)
	{
		for (FToolMenuEntry& Block : GeneratedMenu->Sections[0].Blocks)
		{
			MenuBarBuilder.AddPullDownMenu(
				Block.Label.Get(),
				Block.ToolTip.Get(),
				FNewMenuDelegate::CreateUObject(this, &UToolMenus::FillMenuBarDropDown, GeneratedMenu->MenuName, Block.Name, GeneratedMenu->Context),
				Block.Name
			);
		}

		AddReferencedContextObjects(MenuBarBuilder.GetMultiBox(), GeneratedMenu->Context);
	}
}

FOnGetContent UToolMenus::ConvertWidgetChoice(const FNewToolMenuWidgetChoice& Choice, const FToolMenuContext& Context) const
{
	if (Choice.NewToolMenuWidget.IsBound())
	{
		return FOnGetContent::CreateLambda([ToCall = Choice.NewToolMenuWidget, Context]()
		{
			if (ToCall.IsBound())
			{
				return ToCall.Execute(Context);
			}

			return SNullWidget::NullWidget;
		});
	}
	else if (Choice.NewToolMenu.IsBound())
	{
		return FOnGetContent::CreateLambda([ToCall = Choice.NewToolMenu, Context]()
		{
			if (ToCall.IsBound())
			{
				UToolMenu* MenuData = NewObject<UToolMenu>();
				MenuData->Context = Context;
				ToCall.Execute(MenuData);
				return UToolMenus::Get()->GenerateWidget(MenuData);
			}

			return SNullWidget::NullWidget;
		});
	}
	return Choice.OnGetContent;
}

FUIAction UToolMenus::ConvertUIAction(const FToolMenuEntry& Block, const FToolMenuContext& Context)
{
	FUIAction UIAction;
	
	if (Block.ScriptObject)
	{
		UIAction = ConvertScriptObjectToUIAction(Block.ScriptObject, Context);
	}
	else
	{
		UIAction = ConvertUIAction(Block.Action, Context);
	}
	
	if (!UIAction.ExecuteAction.IsBound() && Block.StringExecuteAction.IsBound())
	{
		UIAction.ExecuteAction = Block.StringExecuteAction.ToExecuteAction(Context);
	}

	return UIAction;
}

FUIAction UToolMenus::ConvertUIAction(const FToolUIActionChoice& Choice, const FToolMenuContext& Context)
{
	if (const FToolUIAction* ToolAction = Choice.GetToolUIAction())
	{
		return ConvertUIAction(*ToolAction, Context);
	}
	else if (const FToolDynamicUIAction* DynamicToolAction = Choice.GetToolDynamicUIAction())
	{
		return ConvertUIAction(*DynamicToolAction, Context);
	}
	else if (const FUIAction* Action = Choice.GetUIAction())
	{
		return *Action;
	}

	return FUIAction();
}

FUIAction UToolMenus::ConvertUIAction(const FToolUIAction& Actions, const FToolMenuContext& Context)
{
	FUIAction UIAction;

	if (Actions.ExecuteAction.IsBound())
	{
		UIAction.ExecuteAction.BindLambda([DelegateToCall = Actions.ExecuteAction, Context]()
		{
			DelegateToCall.ExecuteIfBound(Context);
		});
	}

	if (Actions.CanExecuteAction.IsBound())
	{
		UIAction.CanExecuteAction.BindLambda([DelegateToCall = Actions.CanExecuteAction, Context]()
		{
			return DelegateToCall.Execute(Context);
		});
	}

	if (Actions.GetActionCheckState.IsBound())
	{
		UIAction.GetActionCheckState.BindLambda([DelegateToCall = Actions.GetActionCheckState, Context]()
		{
			return DelegateToCall.Execute(Context);
		});
	}

	if (Actions.IsActionVisibleDelegate.IsBound())
	{
		UIAction.IsActionVisibleDelegate.BindLambda([DelegateToCall = Actions.IsActionVisibleDelegate, Context]()
		{
			return DelegateToCall.Execute(Context);
		});
	}

	return UIAction;
}

FUIAction UToolMenus::ConvertUIAction(const FToolDynamicUIAction& Actions, const FToolMenuContext& Context)
{
	FUIAction UIAction;

	if (Actions.ExecuteAction.IsBound())
	{
		UIAction.ExecuteAction.BindLambda([DelegateToCall = Actions.ExecuteAction, Context]()
		{
			DelegateToCall.ExecuteIfBound(Context);
		});
	}

	if (Actions.CanExecuteAction.IsBound())
	{
		UIAction.CanExecuteAction.BindLambda([DelegateToCall = Actions.CanExecuteAction, Context]()
		{
			return DelegateToCall.Execute(Context);
		});
	}

	if (Actions.GetActionCheckState.IsBound())
	{
		UIAction.GetActionCheckState.BindLambda([DelegateToCall = Actions.GetActionCheckState, Context]()
		{
			return DelegateToCall.Execute(Context);
		});
	}

	if (Actions.IsActionVisibleDelegate.IsBound())
	{
		UIAction.IsActionVisibleDelegate.BindLambda([DelegateToCall = Actions.IsActionVisibleDelegate, Context]()
		{
			return DelegateToCall.Execute(Context);
		});
	}

	return UIAction;
}

FUIAction UToolMenus::ConvertScriptObjectToUIAction(UToolMenuEntryScript* ScriptObject, const FToolMenuContext& Context)
{
	FUIAction UIAction;

	if (ScriptObject)
	{
		UClass* ScriptClass = ScriptObject->GetClass();

		static const FName ExecuteName = GET_FUNCTION_NAME_CHECKED(UToolMenuEntryScript, Execute);
		if (ScriptClass->IsFunctionImplementedInScript(ExecuteName))
		{
			UIAction.ExecuteAction.BindUFunction(ScriptObject, ExecuteName, Context);
		}

		static const FName CanExecuteName = GET_FUNCTION_NAME_CHECKED(UToolMenuEntryScript, CanExecute);
		if (ScriptClass->IsFunctionImplementedInScript(CanExecuteName))
		{
			UIAction.CanExecuteAction.BindUFunction(ScriptObject, CanExecuteName, Context);
		}

		static const FName GetCheckStateName = GET_FUNCTION_NAME_CHECKED(UToolMenuEntryScript, GetCheckState);
		if (ScriptClass->IsFunctionImplementedInScript(GetCheckStateName))
		{
			UIAction.GetActionCheckState.BindUFunction(ScriptObject, GetCheckStateName, Context);
		}

		static const FName IsVisibleName = GET_FUNCTION_NAME_CHECKED(UToolMenuEntryScript, IsVisible);
		if (ScriptClass->IsFunctionImplementedInScript(IsVisibleName))
		{
			UIAction.IsActionVisibleDelegate.BindUFunction(ScriptObject, IsVisibleName, Context);
		}
	}

	return UIAction;
}

void UToolMenus::ExecuteStringCommand(const FToolMenuStringCommand StringCommand, const FToolMenuContext Context)
{
	if (StringCommand.IsBound())
	{
		const FName TypeName = StringCommand.GetTypeName();
		UToolMenus* ToolMenus = UToolMenus::Get();
		if (const FToolMenuExecuteString* Handler = ToolMenus->StringCommandHandlers.Find(TypeName))
		{
			if (Handler->IsBound())
			{
				Handler->Execute(StringCommand.String, Context);
			}
		}
		else
		{
			UE_LOG(LogToolMenus, Warning, TEXT("Unknown string command handler type: '%s'"), *TypeName.ToString());
		}
	}
}

UToolMenu* UToolMenus::FindSubMenuToGenerateWith(const FName InParentName, const FName InChildName)
{
	FName BaseName = InParentName;
	while (BaseName != NAME_None)
	{
		FName JoinedName = JoinMenuPaths(BaseName, InChildName);
		if (UToolMenu* Found = FindMenu(JoinedName))
		{
			return Found;
		}

		UToolMenu* BaseData = FindMenu(BaseName);
		BaseName = BaseData ? BaseData->MenuParent : NAME_None;
	}

	return nullptr;
}

UObject* UToolMenus::FindContext(const FToolMenuContext& InContext, UClass* InClass)
{
	return InContext.FindByClass(InClass);
}

void UToolMenus::AddReferencedContextObjects(const TSharedRef<FMultiBox>& InMultiBox, const FToolMenuContext& InMenuContext)
{
	if (InMenuContext.ContextObjects.Num() == 0)
	{
		return;
	}

	TArray<UObject*>& References = WidgetObjectReferences.FindOrAdd(InMultiBox);
	for (const TWeakObjectPtr<UObject>& WeakObject : InMenuContext.ContextObjects)
	{
		if (UObject* Object = WeakObject.Get())
		{
			References.AddUnique(Object);
		}
	}
}

void UToolMenus::AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector)
{
	UToolMenus* This = CastChecked<UToolMenus>(InThis);

	for (auto It = This->WidgetObjectReferences.CreateIterator(); It; ++It)
	{
		if (It->Key.IsValid())
		{
			Collector.AddReferencedObjects(It->Value, InThis);
		}
		else
		{
			It.RemoveCurrent();
		}
	}

	Super::AddReferencedObjects(InThis, Collector);
}

UToolMenu* UToolMenus::GenerateMenu(const FName Name, FToolMenuContext& InMenuContext)
{
	return GenerateMenu(CollectHierarchy(Name), InMenuContext);
}

UToolMenu* UToolMenus::GenerateMenu(const TArray<UToolMenu*>& Hierarchy, FToolMenuContext& InMenuContext)
{
	UToolMenu* GeneratedMenu = NewObject<UToolMenu>(this);

	if (Hierarchy.Num() > 0)
	{
		GeneratedMenu->InitGeneratedCopy(Hierarchy[0], Hierarchy.Last()->MenuName, &InMenuContext);
		AssembleMenuHierarchy(GeneratedMenu, Hierarchy);
	}

	return GeneratedMenu;
}

TSharedRef< class SWidget > UToolMenus::GenerateWidget(const FName InName, FToolMenuContext& InMenuContext)
{
	UToolMenu* Generated = GenerateMenu(InName, InMenuContext);
	return GenerateWidget(Generated);
}

TSharedRef<SWidget> UToolMenus::GenerateWidget(const TArray<UToolMenu*>& Hierarchy, FToolMenuContext& InMenuContext)
{
	if (Hierarchy.Num() == 0)
	{
		return SNullWidget::NullWidget;
	}

	UToolMenu* Generated = GenerateMenu(Hierarchy, InMenuContext);
	return GenerateWidget(Generated);
}

TSharedRef<SWidget> UToolMenus::GenerateWidget(UToolMenu* GeneratedMenu)
{
	CleanupStaleWidgetsNextTick();

	FGeneratedToolMenuWidgets& WidgetsForMenuName = GeneratedMenuWidgets.FindOrAdd(GeneratedMenu->MenuName);

	// Store a copy so that we can call 'Refresh' on menus not in the database
	FGeneratedToolMenuWidget& GeneratedMenuWidget = WidgetsForMenuName.Instances.AddDefaulted_GetRef();
	GeneratedMenuWidget.GeneratedMenu = DuplicateObject<UToolMenu>(GeneratedMenu, this);
	// Copy native properties that serialize does not
	GeneratedMenuWidget.GeneratedMenu->Context = GeneratedMenu->Context;
	GeneratedMenuWidget.GeneratedMenu->StyleSet = GeneratedMenu->StyleSet;

	if (GeneratedMenu->MenuType == EMultiBoxType::Menu)
	{
		FMenuBuilder MenuBuilder(GeneratedMenu->bShouldCloseWindowAfterMenuSelection, GeneratedMenu->Context.CommandList, GeneratedMenu->Context.GetAllExtenders(), GeneratedMenu->bCloseSelfOnly, GeneratedMenu->StyleSet, GeneratedMenu->bSearchable);
		PopulateMenuBuilder(MenuBuilder, GeneratedMenu);
		TSharedRef<SWidget> Result = MenuBuilder.MakeWidget();
		GeneratedMenuWidget.Widget = Result;
		return Result;
	}
	else if (GeneratedMenu->MenuType == EMultiBoxType::MenuBar)
	{
		FMenuBarBuilder MenuBarBuilder(GeneratedMenu->Context.CommandList, GeneratedMenu->Context.GetAllExtenders(), GeneratedMenu->StyleSet);
		PopulateMenuBarBuilder(MenuBarBuilder, GeneratedMenu);
		TSharedRef<SWidget> Result = MenuBarBuilder.MakeWidget();
		GeneratedMenuWidget.Widget = Result;
		return Result;
	}
	else if (GeneratedMenu->MenuType == EMultiBoxType::ToolBar || GeneratedMenu->MenuType == EMultiBoxType::VerticalToolBar)
	{
		const EOrientation ToolBarOrientation = (GeneratedMenu->MenuType == EMultiBoxType::VerticalToolBar) ? Orient_Vertical : Orient_Horizontal;
		FToolBarBuilder ToolbarBuilder(GeneratedMenu->Context.CommandList, GeneratedMenu->MenuName, GeneratedMenu->Context.GetAllExtenders(), ToolBarOrientation, GeneratedMenu->bToolBarForceSmallIcons);
		ToolbarBuilder.SetIsFocusable(GeneratedMenu->bToolBarIsFocusable);
		PopulateToolBarBuilder(ToolbarBuilder, GeneratedMenu);
		TSharedRef<SWidget> Result = ToolbarBuilder.MakeWidget();
		GeneratedMenuWidget.Widget = Result;
		return Result;
	}

	return SNullWidget::NullWidget;
}

void UToolMenus::AssignSetTimerForNextTickDelegate(const FSimpleDelegate& InDelegate)
{
	SetTimerForNextTickDelegate = InDelegate;
}

void UToolMenus::SetNextTickTimer()
{
	if (!bNextTickTimerIsSet)
	{
		bNextTickTimerIsSet = true;

		SetTimerForNextTickDelegate.ExecuteIfBound();
	}
}

void UToolMenus::CleanupStaleWidgetsNextTick()
{
	bCleanupStaleWidgetsNextTick = true;
	SetNextTickTimer();
}

void UToolMenus::RefreshAllWidgets()
{
	bRefreshWidgetsNextTick = true;
	SetNextTickTimer();
}

void UToolMenus::HandleNextTick()
{
	if (bCleanupStaleWidgetsNextTick || bRefreshWidgetsNextTick)
	{
		CleanupStaleWidgets();
		bCleanupStaleWidgetsNextTick = false;

		if (bRefreshWidgetsNextTick)
		{
			for (auto WidgetsForMenuNameIt = GeneratedMenuWidgets.CreateIterator(); WidgetsForMenuNameIt; ++WidgetsForMenuNameIt)
			{
				FGeneratedToolMenuWidgets& WidgetsForMenuName = WidgetsForMenuNameIt->Value;
				for (auto Instance = WidgetsForMenuName.Instances.CreateIterator(); Instance; ++Instance)
				{
					if (Instance->Widget.IsValid())
					{
						RefreshMenuWidget(WidgetsForMenuNameIt->Key, *Instance);
					}
				}
			}

			bRefreshWidgetsNextTick = false;
		}
	}

	bNextTickTimerIsSet = false;
}

void UToolMenus::CleanupStaleWidgets()
{
	for (auto WidgetsForMenuNameIt = GeneratedMenuWidgets.CreateIterator(); WidgetsForMenuNameIt; ++WidgetsForMenuNameIt)
	{
		FGeneratedToolMenuWidgets& WidgetsForMenuName = WidgetsForMenuNameIt->Value;

		for (auto Instance = WidgetsForMenuName.Instances.CreateIterator(); Instance; ++Instance)
		{
			if (!Instance->Widget.IsValid())
			{
				Instance.RemoveCurrent();
			}
		}

		if (WidgetsForMenuName.Instances.Num() == 0)
		{
			WidgetsForMenuNameIt.RemoveCurrent();
		}
	}
}

bool UToolMenus::RefreshMenuWidget(const FName InName)
{
	if (FGeneratedToolMenuWidgets* WidgetsForMenuName = GeneratedMenuWidgets.Find(InName))
	{
		for (auto Instance = WidgetsForMenuName->Instances.CreateIterator(); Instance; ++Instance)
		{
			if (RefreshMenuWidget(InName, *Instance))
			{
				return true;
			}
			else
			{
				Instance.RemoveCurrent();
			}
		}
	}

	return false;
}

bool UToolMenus::RefreshMenuWidget(const FName InName, FGeneratedToolMenuWidget& GeneratedMenuWidget)
{
	if (!GeneratedMenuWidget.Widget.IsValid())
	{
		return false;
	}

	// Regenerate menu from database
	UToolMenu* GeneratedMenu = GenerateMenu(InName, GeneratedMenuWidget.GeneratedMenu->Context);
	GeneratedMenuWidget.GeneratedMenu = GeneratedMenu;

	// Regenerate Multibox
	TSharedRef<SMultiBoxWidget> MultiBoxWidget = StaticCastSharedRef<SMultiBoxWidget>(GeneratedMenuWidget.Widget.Pin().ToSharedRef());
	if (GeneratedMenu->MenuType == EMultiBoxType::Menu)
	{
		FMenuBuilder MenuBuilder(GeneratedMenu->bShouldCloseWindowAfterMenuSelection, GeneratedMenu->Context.CommandList, GeneratedMenu->Context.GetAllExtenders(), GeneratedMenu->bCloseSelfOnly, GeneratedMenu->StyleSet, GeneratedMenu->bSearchable);
		PopulateMenuBuilder(MenuBuilder, GeneratedMenu);
		MultiBoxWidget->SetMultiBox(MenuBuilder.GetMultiBox());
	}
	else if (GeneratedMenu->MenuType == EMultiBoxType::MenuBar)
	{
		FMenuBarBuilder MenuBarBuilder(GeneratedMenu->Context.CommandList, GeneratedMenu->Context.GetAllExtenders(), GeneratedMenu->StyleSet);
		PopulateMenuBarBuilder(MenuBarBuilder, GeneratedMenu);
		MultiBoxWidget->SetMultiBox(MenuBarBuilder.GetMultiBox());
	}
	else if (GeneratedMenu->MenuType == EMultiBoxType::ToolBar || GeneratedMenu->MenuType == EMultiBoxType::VerticalToolBar)
	{
		const EOrientation ToolBarOrientation = (GeneratedMenu->MenuType == EMultiBoxType::VerticalToolBar) ? Orient_Vertical : Orient_Horizontal;
		FToolBarBuilder ToolbarBuilder(GeneratedMenu->Context.CommandList, GeneratedMenu->MenuName, GeneratedMenu->Context.GetAllExtenders(), ToolBarOrientation, GeneratedMenu->bToolBarForceSmallIcons);
		ToolbarBuilder.SetIsFocusable(GeneratedMenu->bToolBarIsFocusable);
		PopulateToolBarBuilder(ToolbarBuilder, GeneratedMenu);
		MultiBoxWidget->SetMultiBox(ToolbarBuilder.GetMultiBox());
	}

	MultiBoxWidget->BuildMultiBoxWidget();
	return true;
}

UToolMenu* UToolMenus::GenerateMenuAsBuilder(const UToolMenu* InMenu, FToolMenuContext& InMenuContext)
{
	TArray<UToolMenu*> Hierarchy = CollectHierarchy(InMenu->MenuName);

	// Insert InMenu as second to last so items in InMenu appear before items registered in database by other plugins
	if (Hierarchy.Num() > 0)
	{
		Hierarchy.Insert((UToolMenu*)InMenu, Hierarchy.Num() - 1);
	}
	else
	{
		Hierarchy.Add((UToolMenu*)InMenu);
	}

	return GenerateMenu(Hierarchy, InMenuContext);
}

UToolMenu* UToolMenus::RegisterMenu(const FName InName, const FName InParent, EMultiBoxType InType, bool bWarnIfAlreadyRegistered)
{
	if (UToolMenu* Found = FindMenu(InName))
	{
		if (!Found->bRegistered)
		{
			Found->MenuParent = InParent;
			Found->MenuType = InType;
			Found->MenuOwner = CurrentOwner();
			Found->bRegistered = true;
		}
		else if (bWarnIfAlreadyRegistered)
		{
			UE_LOG(LogToolMenus, Warning, TEXT("Menu already registered : %s"), *InName.ToString());
		}

		return Found;
	}

	UToolMenu* ToolMenu = NewObject<UToolMenu>(this);
	ToolMenu->InitMenu(CurrentOwner(), InName, InParent, InType);
	ToolMenu->bRegistered = true;
	Menus.Add(InName, ToolMenu);
	return ToolMenu;
}

UToolMenu* UToolMenus::ExtendMenu(const FName InName)
{
	if (UToolMenu* Found = FindMenu(InName))
	{
		return Found;
	}

	UToolMenu* ToolMenu = NewObject<UToolMenu>(this);
	ToolMenu->MenuName = InName;
	ToolMenu->bRegistered = false;
	Menus.Add(InName, ToolMenu);
	return ToolMenu;
}

void UToolMenus::RemoveMenu(const FName MenuName)
{
	Menus.Remove(MenuName);
}

bool UToolMenus::AddMenuEntryObject(UToolMenuEntryScript* MenuEntryObject)
{
	UToolMenu* Menu = UToolMenus::Get()->ExtendMenu(MenuEntryObject->Data.Menu);
	Menu->AddMenuEntryObject(MenuEntryObject);
	return true;
}

void UToolMenus::SetSectionLabel(const FName MenuName, const FName SectionName, const FText Label)
{
	ExtendMenu(MenuName)->FindOrAddSection(SectionName).Label = TAttribute<FText>(Label);
}

void UToolMenus::SetSectionPosition(const FName MenuName, const FName SectionName, const FName PositionName, const EToolMenuInsertType PositionType)
{
	ExtendMenu(MenuName)->FindOrAddSection(SectionName).InsertPosition = FToolMenuInsert(PositionName, PositionType);
}

void UToolMenus::AddSection(const FName MenuName, const FName SectionName, const TAttribute< FText >& InLabel, const FToolMenuInsert InPosition)
{
	UToolMenu* Menu = ExtendMenu(MenuName);
	FToolMenuSection* Section = Menu->FindSection(SectionName);
	if (!Section)
	{
		Menu->AddSection(SectionName, InLabel, InPosition);
	}
}

void UToolMenus::RemoveSection(const FName MenuName, const FName InSection)
{
	if (UToolMenu* Menu = FindMenu(MenuName))
	{
		Menu->RemoveSection(InSection);
	}
}

void UToolMenus::AddEntry(const FName MenuName, const FName InSection, const FToolMenuEntry& InEntry)
{
	ExtendMenu(MenuName)->FindOrAddSection(InSection).AddEntry(InEntry);
}

void UToolMenus::RemoveEntry(const FName MenuName, const FName InSection, const FName InName)
{
	if (UToolMenu* Menu = FindMenu(MenuName))
	{
		if (FToolMenuSection* Section = Menu->FindSection(InSection))
		{
			Section->RemoveEntry(InName);
		}
	}
}

void UToolMenus::UnregisterOwnerInternal(FToolMenuOwner InOwner)
{
	if (InOwner != FToolMenuOwner())
	{
		for (auto It = Menus.CreateIterator(); It; ++It)
		{
			int32 NumEntriesRemoved = 0;

			UToolMenu* Menu = It->Value;
			for (FToolMenuSection& Section : Menu->Sections)
			{
				NumEntriesRemoved += Section.RemoveEntriesByOwner(InOwner);
			}

			// Refresh any widgets that are currently displayed to the user
			if (NumEntriesRemoved > 0)
			{
				RefreshAllWidgets();
			}
		}
	}
}

FToolMenuOwner UToolMenus::CurrentOwner() const
{
	if (OwnerStack.Num() > 0)
	{
		return OwnerStack.Last();
	}

	return FToolMenuOwner();
}

void UToolMenus::PushOwner(const FToolMenuOwner InOwner)
{
	OwnerStack.Add(InOwner);
}

void UToolMenus::PopOwner(const FToolMenuOwner InOwner)
{
	FToolMenuOwner PoppedOwner = OwnerStack.Pop(false);
	check(PoppedOwner == InOwner);
}

void UToolMenus::UnregisterOwnerByName(FName InOwnerName)
{
	UnregisterOwnerInternal(InOwnerName);
}

void UToolMenus::RegisterStringCommandHandler(const FName InName, const FToolMenuExecuteString& InDelegate)
{
	StringCommandHandlers.Add(InName, InDelegate);
}

void UToolMenus::UnregisterStringCommandHandler(const FName InName)
{
	StringCommandHandlers.Remove(InName);
}
