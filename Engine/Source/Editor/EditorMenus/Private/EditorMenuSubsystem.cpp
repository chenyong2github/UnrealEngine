// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "EditorMenuSubsystem.h"
#include "IEditorMenusModule.h"

#include "Textures/SlateIcon.h"
#include "Framework/Commands/UIAction.h"
#include "Framework/MultiBox/MultiBox.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Internationalization/Internationalization.h"
#include "Editor/EditorPerProjectUserSettings.h"
#include "HAL/PlatformApplicationMisc.h"

#include "Editor.h"

#define LOCTEXT_NAMESPACE "EditorMenuSubsystem"

FAutoConsoleCommand EditorMenusRefreshMenuWidget = FAutoConsoleCommand(
	TEXT("EditorMenus.RefreshAllWidgets"),
	TEXT("Refresh All Editor Menu Widgets"),
	FConsoleCommandDelegate::CreateLambda([]() {
		UEditorMenuSubsystem::Get()->RefreshAllWidgets();
	}));

FName FEditorMenuStringCommand::GetTypeName() const
{
	static const FName CommandName("Command");
	static const FName PythonName("Python");

	switch (Type)
	{
	case EEditorMenuStringCommandType::Command:
		return CommandName;
	case EEditorMenuStringCommandType::Python:
		return PythonName;
	case EEditorMenuStringCommandType::Custom:
		return CustomType;
	default:
		break;
	}

	return NAME_None;
}

FExecuteAction FEditorMenuStringCommand::ToExecuteAction(const FEditorMenuContext& Context) const
{
	if (IsBound())
	{
		return FExecuteAction::CreateStatic(&UEditorMenuSubsystem::ExecuteStringCommand, *this, Context);
	}

	return FExecuteAction();
}

FEditorUIActionChoice::FEditorUIActionChoice(const TSharedPtr< const FUICommandInfo >& InCommand, const FUICommandList& InCommandList)
{
	if (InCommand.IsValid())
	{
		if (const FUIAction* UIAction = InCommandList.GetActionForCommand(InCommand))
		{
			Action = *UIAction;
			EditorAction.Reset();
			DynamicEditorAction.Reset();
		}
	}
}

UEditorMenuSubsystem::UEditorMenuSubsystem() :
	UEditorSubsystem(),
	bNextTickTimerIsSet(false),
	bRefreshWidgetsNextTick(false),
	bCleanupStaleWidgetsNextTick(false)
{
}

void UEditorMenuSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
}

void UEditorMenuSubsystem::Deinitialize()
{
	GeneratedMenuWidgets.Reset();
	WidgetObjectReferences.Reset();
	Menus.Reset();
}

FName UEditorMenuSubsystem::JoinMenuPaths(const FName Base, const FName Child)
{
	return *(Base.ToString() + TEXT(".") + Child.ToString());
}

bool UEditorMenuSubsystem::GetDisplayUIExtensionPoints()
{
	return GetDefault<UEditorPerProjectUserSettings>()->bDisplayUIExtensionPoints;
}

UEditorMenu* UEditorMenuSubsystem::FindMenu(const FName Name)
{
	UEditorMenu** Found = Menus.Find(Name);
	return Found ? *Found : nullptr;
}

bool UEditorMenuSubsystem::IsMenuRegistered(const FName Name) const
{
	const UEditorMenu* const * Found = Menus.Find(Name);
	return Found && *Found && (*Found)->IsRegistered();
}

TArray<UEditorMenu*> UEditorMenuSubsystem::CollectHierarchy(const FName InName)
{
	TArray<UEditorMenu*> Result;

	UEditorMenu* Current = FindMenu(InName);
	while (Current)
	{
		// Detect infinite loop
		for (UEditorMenu* Other : Result)
		{
			if (Other->MenuName == Current->MenuName)
			{
				UE_LOG(LogEditorMenus, Warning, TEXT("Infinite loop detected in editor menu: %s"), *InName.ToString());
				return TArray<UEditorMenu*>();
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

void UEditorMenuSubsystem::ListAllParents(const FName InName, TArray<FName>& AllParents)
{
	for (const UEditorMenu* Menu : CollectHierarchy(InName))
	{
		AllParents.Add(Menu->MenuName);
	}
}

void UEditorMenuSubsystem::AssembleMenuByName(UEditorMenu* GeneratedMenu, const FName InName)
{
	if (UEditorMenu* Menu = FindMenu(InName))
	{
		GeneratedMenu->InitGeneratedCopy(Menu);
		AssembleMenuHierarchy(GeneratedMenu, CollectHierarchy(InName));
	}
}

void UEditorMenuSubsystem::AssembleMenuSection(UEditorMenu* GeneratedMenu, const UEditorMenu* Other, FEditorMenuSection* DestSection, const FEditorMenuSection& OtherSection)
{
	// Build list of blocks in expected order including blocks created by construct delegates
	TArray<FEditorMenuEntry> RemainingBlocks;
	for (const FEditorMenuEntry& Block : OtherSection.Blocks)
	{
		if (Block.IsScriptObjectDynamicConstruct() || Block.Construct.IsBound())
		{
			UEditorMenu* Constructed = NewObject<UEditorMenu>(this);
			Constructed->Context = DestSection->Context;

			if (Block.IsScriptObjectDynamicConstruct())
			{
				Block.ScriptObject->ConstructMenuEntry(*DestSection, DestSection->Context);
			}
			else
			{
				FEditorMenuSection& ConstructedSection = Constructed->AddSection(DestSection->Name);
				Block.Construct.Execute(ConstructedSection);
			}

			// Combine all user's choice of selections here into the current section target
			// If the user wants to add items to different sections they will need to create dynamic section instead (for now)
			for (FEditorMenuSection& ConstructedSection : Constructed->Sections)
			{
				for (FEditorMenuEntry& ConstructedBlock : ConstructedSection.Blocks)
				{
					if (ConstructedBlock.InsertPosition.IsDefault())
					{
						ConstructedBlock.InsertPosition = Block.InsertPosition;
					}
					RemainingBlocks.Add(ConstructedBlock);
				}
			}
		}
		else
		{
			RemainingBlocks.Add(Block);
		}
	}

	// Repeatedly loop because insert location may not exist until later in list
	while (RemainingBlocks.Num() > 0)
	{
		int32 NumHandled = 0;
		for (int32 i = 0; i < RemainingBlocks.Num(); ++i)
		{
			FEditorMenuEntry& Block = RemainingBlocks[i];
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
			for (const FEditorMenuEntry& Block : RemainingBlocks)
			{
				UE_LOG(LogEditorMenus, Warning, TEXT("Menu item not found: '%s' for insert: '%s'"), *Block.InsertPosition.Name.ToString(), *Block.Name.ToString());
			}
			break;
		}
	}
}

void UEditorMenuSubsystem::AssembleMenu(UEditorMenu* GeneratedMenu, const UEditorMenu* Other)
{
	TArray<FEditorMenuSection> RemainingSections;
	for (const FEditorMenuSection& OtherSection : Other->Sections)
	{
		if (OtherSection.EditorMenuSectionDynamic || OtherSection.Construct.NewEditorMenuDelegate.IsBound())
		{
			UEditorMenu* ConstructedSections = NewObject<UEditorMenu>(this);
			ConstructedSections->Context = GeneratedMenu->Context;

			if (OtherSection.EditorMenuSectionDynamic)
			{
				OtherSection.EditorMenuSectionDynamic->ConstructSections(ConstructedSections, GeneratedMenu->Context);
			}
			else if (OtherSection.Construct.NewEditorMenuDelegate.IsBound())
			{
				OtherSection.Construct.NewEditorMenuDelegate.Execute(ConstructedSections);
			}

			for (FEditorMenuSection& ConstructedSection : ConstructedSections->Sections)
			{
				if (ConstructedSection.InsertPosition.IsDefault())
				{
					ConstructedSection.InsertPosition = OtherSection.InsertPosition;
				}
				RemainingSections.Add(ConstructedSection);
			}
		}
		else
		{
			RemainingSections.Add(OtherSection);
		}
	}

	while (RemainingSections.Num() > 0)
	{
		int32 NumHandled = 0;
		for (int32 i=0; i < RemainingSections.Num(); ++i)
		{
			const FEditorMenuSection& RemainingSection = RemainingSections[i];

			// Update existing section
			FEditorMenuSection* Section = GeneratedMenu->FindSection(RemainingSection.Name);
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
			for (const FEditorMenuSection& RemainingSection : RemainingSections)
			{
				UE_LOG(LogEditorMenus, Warning, TEXT("Menu section not found: '%s' for insert: '%s'"), *RemainingSection.InsertPosition.Name.ToString(), *RemainingSection.Name.ToString());
			}
			break;
		}
	}
}

int32 UEditorMenuSubsystem::FindCustomizedMenuIndex(const FName InName)
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

FCustomizedEditorMenu* UEditorMenuSubsystem::FindCustomizedMenu(const FName InName)
{
	int32 FoundIndex = FindCustomizedMenuIndex(InName);
	return (FoundIndex != INDEX_NONE) ? &CustomizedMenus[FoundIndex] : nullptr;
}

void UEditorMenuSubsystem::ApplyCustomization(UEditorMenu* GeneratedMenu)
{
	const FCustomizedEditorMenu* CustomizedMenu = FindCustomizedMenu(GeneratedMenu->MenuName);
	if (CustomizedMenu == nullptr)
	{
		return;
	}

	TArray<FEditorMenuSection> NewSections;
	NewSections.Reserve(GeneratedMenu->Sections.Num());

	for (const FCustomizedEditorMenuSection& CustomizedSection : CustomizedMenu->Sections)
	{
		int32 SectionIndex = GeneratedMenu->IndexOfSection(CustomizedSection.Name);
		if (SectionIndex == INDEX_NONE)
		{
			continue;
		}

		FEditorMenuSection& Section = GeneratedMenu->Sections[SectionIndex];

		TArray<FEditorMenuEntry> NewBlocks;
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
		for (FEditorMenuEntry& Block : Section.Blocks)
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
		FEditorMenuSection& Section = NewSections[SectionIndex];
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

void UEditorMenuSubsystem::AssembleMenuHierarchy(UEditorMenu* GeneratedMenu, const TArray<UEditorMenu*>& Hierarchy)
{
	if (GeneratedMenu->MenuType == EMultiBoxType::MenuBar)
	{
		// Menu Bars require one section
		if (GeneratedMenu->Sections.Num() == 0)
		{
			GeneratedMenu->Sections.AddDefaulted();
		}

		FEditorMenuSection& MenuBarSection = GeneratedMenu->Sections[0];
		for (const UEditorMenu* MenuData : Hierarchy)
		{
			for (const FEditorMenuSection& Section : MenuData->Sections)
			{
				for (const FEditorMenuEntry& Block : Section.Blocks)
				{
					MenuBarSection.AssembleBlock(Block);
				}
			}
		}
	}
	else
	{
		for (const UEditorMenu* FoundParent : Hierarchy)
		{
			AssembleMenu(GeneratedMenu, FoundParent);
		}
	}

	ApplyCustomization(GeneratedMenu);
}

void UEditorMenuSubsystem::FillMenuDynamic(FMenuBuilder& MenuBuilder, FNewEditorMenuDelegate InConstructMenu)
{
	if (InConstructMenu.IsBound())
	{
		// Create final menu
		UEditorMenu* MenuData = NewObject<UEditorMenu>();
		InConstructMenu.Execute(MenuData);

		// Populate menu builder with final menu
		PopulateMenuBuilder(MenuBuilder, MenuData);
	}
}

void UEditorMenuSubsystem::FillMenu(class FMenuBuilder& MenuBuilder, FName InMenuName, FEditorMenuContext InMenuContext)
{
	// Create combined final menu
	UEditorMenu* GeneratedMenu = NewObject<UEditorMenu>();
	GeneratedMenu->Context = InMenuContext;
	AssembleMenuByName(GeneratedMenu, InMenuName);

	// Populate menu builder with final menu
	PopulateMenuBuilder(MenuBuilder, GeneratedMenu);
}

TSharedRef<SWidget> UEditorMenuSubsystem::GenerateToolbarComboButtonMenu(const FName SubMenuFullName, FEditorMenuContext InContext)
{
	return GenerateWidget(SubMenuFullName, InContext);
}

void UEditorMenuSubsystem::FillMenuBarDropDown(class FMenuBuilder& MenuBuilder, FName InParentName, FName InChildName, FEditorMenuContext InMenuContext)
{
	if (UEditorMenu* MenuToUse = FindSubMenuToGenerateWith(InParentName, InChildName))
	{
		// Create combined final menu
		UEditorMenu* GeneratedMenu = NewObject<UEditorMenu>();
		GeneratedMenu->Context = InMenuContext;
		AssembleMenuByName(GeneratedMenu, MenuToUse->MenuName);
		GeneratedMenu->MenuName = JoinMenuPaths(InParentName, InChildName);

		// Populate menu builder with final menu
		PopulateMenuBuilder(MenuBuilder, GeneratedMenu);
	}
}

void UEditorMenuSubsystem::PopulateMenuBuilder(FMenuBuilder& MenuBuilder, UEditorMenu* MenuData)
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

	for (FEditorMenuSection& Section : MenuData->Sections)
	{
		if (Section.Construct.NewEditorMenuDelegateLegacy.IsBound())
		{
			Section.Construct.NewEditorMenuDelegateLegacy.Execute(MenuBuilder, MenuData);
			continue;
		}

		MenuBuilder.BeginSection(Section.Name, Section.Label);

		for (FEditorMenuEntry& Block : Section.Blocks)
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
					else if (Block.SubMenuData.ConstructMenu.NewEditorMenuDelegate.IsBound())
					{
						// SubMenu constructed each time it is opened
						MenuBuilder.AddSubMenu(
							Block.Label,
							Block.ToolTip,
							FNewMenuDelegate::CreateUObject(this, &UEditorMenuSubsystem::FillMenuDynamic, Block.SubMenuData.ConstructMenu.NewEditorMenuDelegate),
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
						FNewMenuDelegate NewMenuDelegate = FNewMenuDelegate::CreateUObject(this, &UEditorMenuSubsystem::FillMenu, SubMenuFullName, MenuData->Context);

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
						MenuBuilder.AddMenuEntry(Block.Command, Block.Name, Block.Label, Block.ToolTip, Block.Icon.Get());
					}
					else if (Block.ScriptObject)
					{
						UEditorMenuEntryScript* ScriptObject = Block.ScriptObject;
						MenuBuilder.AddMenuEntry(ScriptObject->CreateLabelAttribute(MenuData->Context), ScriptObject->CreateToolTipAttribute(MenuData->Context), Block.Icon.Get(), UIAction, ScriptObject->Data.Name, Block.UserInterfaceActionType, Block.TutorialHighlightName);
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
				UE_LOG(LogEditorMenus, Warning, TEXT("Menu '%s', item '%s', type not currently supported: %d"), *MenuData->MenuName.ToString(), *Block.Name.ToString(), Block.Type);
			}
		}

		MenuBuilder.EndSection();
	}

	AddReferencedContextObjects(MenuBuilder.GetMultiBox(), MenuData->Context);
}

void UEditorMenuSubsystem::PopulateToolBarBuilder(FToolBarBuilder& ToolBarBuilder, UEditorMenu* MenuData)
{
	for (FEditorMenuSection& Section : MenuData->Sections)
	{
		if (Section.Construct.NewToolBarDelegateLegacy.IsBound())
		{
			Section.Construct.NewToolBarDelegateLegacy.Execute(ToolBarBuilder, MenuData);
			continue;
		}

		ToolBarBuilder.BeginSection(Section.Name);

		for (FEditorMenuEntry& Block : Section.Blocks)
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
					ToolBarBuilder.AddToolBarButton(Block.Command, Block.Name, Block.Label, Block.ToolTip, Block.Icon, Block.TutorialHighlightName);
				}
				else if (Block.ScriptObject)
				{
					UEditorMenuEntryScript* ScriptObject = Block.ScriptObject;
					ToolBarBuilder.AddToolBarButton(UIAction, ScriptObject->Data.Name, ScriptObject->CreateLabelAttribute(MenuData->Context), ScriptObject->CreateToolTipAttribute(MenuData->Context), Block.Icon, Block.UserInterfaceActionType, Block.TutorialHighlightName);
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
					FOnGetContent Delegate = FOnGetContent::CreateUObject(this, &UEditorMenuSubsystem::GenerateToolbarComboButtonMenu, SubMenuFullName, MenuData->Context);
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
				UE_LOG(LogEditorMenus, Warning, TEXT("Toolbar '%s', item '%s', type not currently supported: %d"), *MenuData->MenuName.ToString(), *Block.Name.ToString(), Block.Type);
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

void UEditorMenuSubsystem::PopulateMenuBarBuilder(FMenuBarBuilder& MenuBarBuilder, UEditorMenu* GeneratedMenu)
{
	if (GeneratedMenu->Sections.Num() > 0)
	{
		for (FEditorMenuEntry& Block : GeneratedMenu->Sections[0].Blocks)
		{
			MenuBarBuilder.AddPullDownMenu(
				Block.Label.Get(),
				Block.ToolTip.Get(),
				FNewMenuDelegate::CreateUObject(this, &UEditorMenuSubsystem::FillMenuBarDropDown, GeneratedMenu->MenuName, Block.Name, GeneratedMenu->Context),
				Block.Name
			);
		}

		AddReferencedContextObjects(MenuBarBuilder.GetMultiBox(), GeneratedMenu->Context);
	}
}

FOnGetContent UEditorMenuSubsystem::ConvertWidgetChoice(const FNewEditorMenuWidgetChoice& Choice, const FEditorMenuContext& Context) const
{
	if (Choice.NewEditorMenuWidget.IsBound())
	{
		return FOnGetContent::CreateLambda([ToCall = Choice.NewEditorMenuWidget, Context]()
		{
			if (ToCall.IsBound())
			{
				return ToCall.Execute(Context);
			}

			return SNullWidget::NullWidget;
		});
	}
	else if (Choice.NewEditorMenu.IsBound())
	{
		return FOnGetContent::CreateLambda([ToCall = Choice.NewEditorMenu, Context]()
		{
			if (ToCall.IsBound())
			{
				UEditorMenu* MenuData = NewObject<UEditorMenu>();
				MenuData->Context = Context;
				ToCall.Execute(MenuData);
				return UEditorMenuSubsystem::Get()->GenerateWidget(MenuData);
			}

			return SNullWidget::NullWidget;
		});
	}
	return Choice.OnGetContent;
}

FUIAction UEditorMenuSubsystem::ConvertUIAction(const FEditorMenuEntry& Block, const FEditorMenuContext& Context)
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

FUIAction UEditorMenuSubsystem::ConvertUIAction(const FEditorUIActionChoice& Choice, const FEditorMenuContext& Context)
{
	if (const FEditorUIAction* EditorAction = Choice.GetEditorUIAction())
	{
		return ConvertUIAction(*EditorAction, Context);
	}
	else if (const FEditorDynamicUIAction* DynamicEditorAction = Choice.GetEditorDynamicUIAction())
	{
		return ConvertUIAction(*DynamicEditorAction, Context);
	}
	else if (const FUIAction* Action = Choice.GetUIAction())
	{
		return *Action;
	}

	return FUIAction();
}

FUIAction UEditorMenuSubsystem::ConvertUIAction(const FEditorUIAction& Actions, const FEditorMenuContext& Context)
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

FUIAction UEditorMenuSubsystem::ConvertUIAction(const FEditorDynamicUIAction& Actions, const FEditorMenuContext& Context)
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

FUIAction UEditorMenuSubsystem::ConvertScriptObjectToUIAction(UEditorMenuEntryScript* ScriptObject, const FEditorMenuContext& Context)
{
	FUIAction UIAction;

	if (ScriptObject)
	{
		UClass* ScriptClass = ScriptObject->GetClass();

		static const FName ExecuteName = GET_FUNCTION_NAME_CHECKED(UEditorMenuEntryScript, Execute);
		if (ScriptClass->IsFunctionImplementedInScript(ExecuteName))
		{
			UIAction.ExecuteAction.BindUFunction(ScriptObject, ExecuteName, Context);
		}

		static const FName CanExecuteName = GET_FUNCTION_NAME_CHECKED(UEditorMenuEntryScript, CanExecute);
		if (ScriptClass->IsFunctionImplementedInScript(CanExecuteName))
		{
			UIAction.CanExecuteAction.BindUFunction(ScriptObject, CanExecuteName, Context);
		}

		static const FName GetCheckStateName = GET_FUNCTION_NAME_CHECKED(UEditorMenuEntryScript, GetCheckState);
		if (ScriptClass->IsFunctionImplementedInScript(GetCheckStateName))
		{
			UIAction.GetActionCheckState.BindUFunction(ScriptObject, GetCheckStateName, Context);
		}

		static const FName IsVisibleName = GET_FUNCTION_NAME_CHECKED(UEditorMenuEntryScript, IsVisible);
		if (ScriptClass->IsFunctionImplementedInScript(IsVisibleName))
		{
			UIAction.IsActionVisibleDelegate.BindUFunction(ScriptObject, IsVisibleName, Context);
		}
	}

	return UIAction;
}

void UEditorMenuSubsystem::ExecuteStringCommand(const FEditorMenuStringCommand StringCommand, const FEditorMenuContext Context)
{
	if (StringCommand.IsBound())
	{
		static const FName CommandTypeName("Command");
		const FName TypeName = StringCommand.GetTypeName();

		UEditorMenuSubsystem* EditorMenus = UEditorMenuSubsystem::Get();
		if (const FEditorMenuExecuteString* Handler = EditorMenus->StringCommandHandlers.Find(TypeName))
		{
			if (Handler->IsBound())
			{
				Handler->Execute(StringCommand.String, Context);
			}
		}
		else if (TypeName == CommandTypeName)
		{
			if (GEditor)
			{
				GEditor->Exec(nullptr, *StringCommand.String);
			}
		}
		else
		{
			UE_LOG(LogEditorMenus, Warning, TEXT("Unknown string command handler type: '%s'"), *TypeName.ToString());
		}
	}
}

UEditorMenu* UEditorMenuSubsystem::FindSubMenuToGenerateWith(const FName InParentName, const FName InChildName)
{
	FName BaseName = InParentName;
	while (BaseName != NAME_None)
	{
		FName JoinedName = JoinMenuPaths(BaseName, InChildName);
		if (UEditorMenu* Found = FindMenu(JoinedName))
		{
			return Found;
		}

		UEditorMenu* BaseData = FindMenu(BaseName);
		BaseName = BaseData ? BaseData->MenuParent : NAME_None;
	}

	return nullptr;
}

UObject* UEditorMenuSubsystem::FindContext(const FEditorMenuContext& InContext, UClass* InClass)
{
	return InContext.FindByClass(InClass);
}

void UEditorMenuSubsystem::AddReferencedContextObjects(const TSharedRef<FMultiBox>& InMultiBox, const FEditorMenuContext& InMenuContext)
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

void UEditorMenuSubsystem::AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector)
{
	UEditorMenuSubsystem* This = CastChecked<UEditorMenuSubsystem>(InThis);

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

UEditorMenu* UEditorMenuSubsystem::GenerateMenu(const FName Name, FEditorMenuContext& InMenuContext)
{
	return GenerateMenu(CollectHierarchy(Name), InMenuContext);
}

UEditorMenu* UEditorMenuSubsystem::GenerateMenu(const TArray<UEditorMenu*>& Hierarchy, FEditorMenuContext& InMenuContext)
{
	UEditorMenu* GeneratedMenu = NewObject<UEditorMenu>(this);

	if (Hierarchy.Num() > 0)
	{
		GeneratedMenu->InitGeneratedCopy(Hierarchy[0]);
		GeneratedMenu->MenuName = Hierarchy.Last()->MenuName;
		GeneratedMenu->Context = InMenuContext;
		AssembleMenuHierarchy(GeneratedMenu, Hierarchy);
	}

	return GeneratedMenu;
}

TSharedRef< class SWidget > UEditorMenuSubsystem::GenerateWidget(const FName InName, FEditorMenuContext& InMenuContext)
{
	UEditorMenu* Generated = GenerateMenu(InName, InMenuContext);
	return GenerateWidget(Generated);
}

TSharedRef<SWidget> UEditorMenuSubsystem::GenerateWidget(const TArray<UEditorMenu*>& Hierarchy, FEditorMenuContext& InMenuContext)
{
	if (Hierarchy.Num() == 0)
	{
		return SNullWidget::NullWidget;
	}

	UEditorMenu* Generated = GenerateMenu(Hierarchy, InMenuContext);
	return GenerateWidget(Generated);
}

TSharedRef<SWidget> UEditorMenuSubsystem::GenerateWidget(UEditorMenu* GeneratedMenu)
{
	CleanupStaleWidgetsNextTick();

	FGeneratedEditorMenuWidgets& WidgetsForMenuName = GeneratedMenuWidgets.FindOrAdd(GeneratedMenu->MenuName);

	// Store a copy so that we can call 'Refresh' on menus not in the database
	FGeneratedEditorMenuWidget& GeneratedMenuWidget = WidgetsForMenuName.Instances.AddDefaulted_GetRef();
	GeneratedMenuWidget.GeneratedMenu = DuplicateObject<UEditorMenu>(GeneratedMenu, this);
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

void UEditorMenuSubsystem::SetNextTickTimer()
{
	if (!bNextTickTimerIsSet)
	{
		bNextTickTimerIsSet = true;
		GEditor->GetTimerManager()->SetTimerForNextTick(this, &UEditorMenuSubsystem::HandleNextTick);
	}
}

void UEditorMenuSubsystem::CleanupStaleWidgetsNextTick()
{
	bCleanupStaleWidgetsNextTick = true;
	SetNextTickTimer();
}

void UEditorMenuSubsystem::RefreshAllWidgets()
{
	bRefreshWidgetsNextTick = true;
	SetNextTickTimer();
}

void UEditorMenuSubsystem::HandleNextTick()
{
	if (bCleanupStaleWidgetsNextTick || bRefreshWidgetsNextTick)
	{
		CleanupStaleWidgets();
		bCleanupStaleWidgetsNextTick = false;

		if (bRefreshWidgetsNextTick)
		{
			for (auto WidgetsForMenuNameIt = GeneratedMenuWidgets.CreateIterator(); WidgetsForMenuNameIt; ++WidgetsForMenuNameIt)
			{
				FGeneratedEditorMenuWidgets& WidgetsForMenuName = WidgetsForMenuNameIt->Value;
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

void UEditorMenuSubsystem::CleanupStaleWidgets()
{
	for (auto WidgetsForMenuNameIt = GeneratedMenuWidgets.CreateIterator(); WidgetsForMenuNameIt; ++WidgetsForMenuNameIt)
	{
		FGeneratedEditorMenuWidgets& WidgetsForMenuName = WidgetsForMenuNameIt->Value;

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

bool UEditorMenuSubsystem::RefreshMenuWidget(const FName InName)
{
	if (FGeneratedEditorMenuWidgets* WidgetsForMenuName = GeneratedMenuWidgets.Find(InName))
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

bool UEditorMenuSubsystem::RefreshMenuWidget(const FName InName, FGeneratedEditorMenuWidget& GeneratedMenuWidget)
{
	if (!GeneratedMenuWidget.Widget.IsValid())
	{
		return false;
	}

	// Regenerate menu from database
	UEditorMenu* GeneratedMenu = GenerateMenu(InName, GeneratedMenuWidget.GeneratedMenu->Context);
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

UEditorMenu* UEditorMenuSubsystem::GenerateMenuAsBuilder(const UEditorMenu* InMenu, FEditorMenuContext& InMenuContext)
{
	TArray<UEditorMenu*> Hierarchy = CollectHierarchy(InMenu->MenuName);

	// Insert InMenu as second to last so items in InMenu appear before items registered in database by other plugins
	if (Hierarchy.Num() > 0)
	{
		Hierarchy.Insert((UEditorMenu*)InMenu, Hierarchy.Num() - 1);
	}
	else
	{
		Hierarchy.Add((UEditorMenu*)InMenu);
	}

	return GenerateMenu(Hierarchy, InMenuContext);
}

UEditorMenu* UEditorMenuSubsystem::RegisterMenu(const FName InName, const FName InParent, EMultiBoxType InType)
{
	if (UEditorMenu* Found = FindMenu(InName))
	{
		if (!Found->bRegistered)
		{
			Found->MenuParent = InParent;
			Found->MenuType = InType;
			Found->MenuOwner = CurrentOwner();
			Found->bRegistered = true;
		}
		else
		{
			UE_LOG(LogEditorMenus, Warning, TEXT("Menu already registered : %s"), *InName.ToString());
		}

		return Found;
	}

	UEditorMenu* EditorMenu = NewObject<UEditorMenu>(this);
	EditorMenu->InitMenu(CurrentOwner(), InName, InParent, InType);
	EditorMenu->bRegistered = true;
	Menus.Add(InName, EditorMenu);
	return EditorMenu;
}

UEditorMenu* UEditorMenuSubsystem::ExtendMenu(const FName InName)
{
	if (UEditorMenu* Found = FindMenu(InName))
	{
		return Found;
	}

	UEditorMenu* EditorMenu = NewObject<UEditorMenu>(this);
	EditorMenu->MenuName = InName;
	EditorMenu->bRegistered = false;
	Menus.Add(InName, EditorMenu);
	return EditorMenu;
}

void UEditorMenuSubsystem::RemoveMenu(const FName MenuName)
{
	Menus.Remove(MenuName);
}

bool UEditorMenuSubsystem::AddMenuEntryObject(UEditorMenuEntryScript* MenuEntryObject)
{
	UEditorMenu* Menu = UEditorMenuSubsystem::Get()->ExtendMenu(MenuEntryObject->Data.Menu);
	Menu->AddMenuEntryObject(MenuEntryObject);
	return true;
}

void UEditorMenuSubsystem::SetSectionLabel(const FName MenuName, const FName SectionName, const FText Label)
{
	ExtendMenu(MenuName)->FindOrAddSection(SectionName).Label = TAttribute<FText>(Label);
}

void UEditorMenuSubsystem::SetSectionPosition(const FName MenuName, const FName SectionName, const FName PositionName, const EEditorMenuInsertType PositionType)
{
	ExtendMenu(MenuName)->FindOrAddSection(SectionName).InsertPosition = FEditorMenuInsert(PositionName, PositionType);
}

void UEditorMenuSubsystem::AddSection(const FName MenuName, const FName SectionName, const TAttribute< FText >& InLabel, const FEditorMenuInsert InPosition)
{
	UEditorMenu* Menu = ExtendMenu(MenuName);
	FEditorMenuSection* Section = Menu->FindSection(SectionName);
	if (!Section)
	{
		Menu->AddSection(SectionName, InLabel, InPosition);
	}
}

void UEditorMenuSubsystem::RemoveSection(const FName MenuName, const FName InSection)
{
	if (UEditorMenu* Menu = FindMenu(MenuName))
	{
		Menu->RemoveSection(InSection);
	}
}

void UEditorMenuSubsystem::AddEntry(const FName MenuName, const FName InSection, const FEditorMenuEntry& InEntry)
{
	ExtendMenu(MenuName)->FindOrAddSection(InSection).AddEntry(InEntry);
}

void UEditorMenuSubsystem::RemoveEntry(const FName MenuName, const FName InSection, const FName InName)
{
	if (UEditorMenu* Menu = FindMenu(MenuName))
	{
		if (FEditorMenuSection* Section = Menu->FindSection(InSection))
		{
			Section->RemoveEntry(InName);
		}
	}
}

void UEditorMenuSubsystem::UnregisterOwnerInternal(FEditorMenuOwner InOwner)
{
	if (InOwner != FEditorMenuOwner())
	{
		for (auto It = Menus.CreateIterator(); It; ++It)
		{
			int32 NumEntriesRemoved = 0;

			UEditorMenu* Menu = It->Value;
			for (FEditorMenuSection& Section : Menu->Sections)
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

FEditorMenuOwner UEditorMenuSubsystem::CurrentOwner() const
{
	if (OwnerStack.Num() > 0)
	{
		return OwnerStack.Last();
	}

	return FEditorMenuOwner();
}

void UEditorMenuSubsystem::PushOwner(const FEditorMenuOwner InOwner)
{
	OwnerStack.Add(InOwner);
}

void UEditorMenuSubsystem::PopOwner(const FEditorMenuOwner InOwner)
{
	FEditorMenuOwner PoppedOwner = OwnerStack.Pop(false);
	check(PoppedOwner == InOwner);
}

void UEditorMenuSubsystem::UnregisterOwnerByName(FName InOwnerName)
{
	UnregisterOwnerInternal(InOwnerName);
}

void UEditorMenuSubsystem::RegisterStringCommandHandler(const FName InName, const FEditorMenuExecuteString& InDelegate)
{
	StringCommandHandlers.Add(InName, InDelegate);
}

void UEditorMenuSubsystem::UnregisterStringCommandHandler(const FName InName)
{
	StringCommandHandlers.Remove(InName);
}
