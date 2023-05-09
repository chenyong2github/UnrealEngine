// Copyright Epic Games, Inc. All Rights Reserved.

#include "SDMXControlConsoleEditorFixturePatchVerticalBox.h"

#include "DMXControlConsoleData.h"
#include "DMXControlConsoleFaderGroup.h"
#include "DMXControlConsoleFaderGroupRow.h"
#include "DMXControlConsoleEditorManager.h"
#include "DMXControlConsoleEditorSelection.h"
#include "Commands/DMXControlConsoleEditorCommands.h"
#include "Library/DMXEntityFixturePatch.h"
#include "Library/DMXEntityReference.h"
#include "Library/DMXLibrary.h"
#include "Models/DMXControlConsoleEditorModel.h"
#include "Widgets/SDMXReadOnlyFixturePatchList.h"

#include "ScopedTransaction.h"
#include "Algo/Find.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Styling/AppStyle.h"
#include "Styling/StyleColors.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Text/STextBlock.h"


#define LOCTEXT_NAMESPACE "SDMXControlConsoleEditorFixturePatchVerticalBox"

SDMXControlConsoleEditorFixturePatchVerticalBox::~SDMXControlConsoleEditorFixturePatchVerticalBox()
{
	if (FixturePatchList.IsValid())
	{
		const FDMXReadOnlyFixturePatchListDescriptor ListDescriptor = FixturePatchList->GetListDescriptor();

		UDMXControlConsoleEditorModel* EditorConsoleModel = GetMutableDefault<UDMXControlConsoleEditorModel>();
		EditorConsoleModel->SaveFixturePatchListDescriptorToConfig(ListDescriptor);
	}
}

void SDMXControlConsoleEditorFixturePatchVerticalBox::Construct(const FArguments& InArgs)
{
	RegisterCommands();

	UDMXControlConsoleEditorModel* EditorConsoleModel = GetMutableDefault<UDMXControlConsoleEditorModel>();
	const FDMXReadOnlyFixturePatchListDescriptor ListDescriptor = EditorConsoleModel->GetFixturePatchListDescriptor();

	const UDMXControlConsoleData* EditorConsoleData = FDMXControlConsoleEditorManager::Get().GetEditorConsoleData();
	UDMXLibrary* DMXLibrary = EditorConsoleData ? EditorConsoleData->GetDMXLibrary() : nullptr;

	ChildSlot
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				GenerateFixturePatchListToolbar()
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SAssignNew(FixturePatchList, SDMXReadOnlyFixturePatchList)
				.ListDescriptor(ListDescriptor)
				.DMXLibrary(DMXLibrary)
				.OnContextMenuOpening(this, &SDMXControlConsoleEditorFixturePatchVerticalBox::CreateRowContextMenu)
				.IsRowEnabled(this, &SDMXControlConsoleEditorFixturePatchVerticalBox::IsFixturePatchListRowEnabled)
			]
		];
}

void SDMXControlConsoleEditorFixturePatchVerticalBox::ForceRefresh()
{
	const UDMXControlConsoleData* EditorConsoleData = FDMXControlConsoleEditorManager::Get().GetEditorConsoleData();
	if (EditorConsoleData && FixturePatchList.IsValid())
	{
		UDMXLibrary* NewLibrary = EditorConsoleData->GetDMXLibrary();
		FixturePatchList->SetDMXLibrary(NewLibrary);
	}
}

void SDMXControlConsoleEditorFixturePatchVerticalBox::RegisterCommands()
{
	CommandList = MakeShared<FUICommandList>();

	CommandList->MapAction
	(
		FDMXControlConsoleEditorCommands::Get().AddPatchNext,
		FExecuteAction::CreateSP(this, &SDMXControlConsoleEditorFixturePatchVerticalBox::OnGenerateFromFixturePatchOnLastRow),
		FCanExecuteAction::CreateSP(this, &SDMXControlConsoleEditorFixturePatchVerticalBox::CanExecuteAddNext)
	);

	CommandList->MapAction
	(
		FDMXControlConsoleEditorCommands::Get().AddPatchNextRow,
		FExecuteAction::CreateSP(this, &SDMXControlConsoleEditorFixturePatchVerticalBox::OnGenerateFromFixturePatchOnNewRow),
		FCanExecuteAction::CreateSP(this, &SDMXControlConsoleEditorFixturePatchVerticalBox::CanExecuteAddRow)
	);

	CommandList->MapAction
	(
		FDMXControlConsoleEditorCommands::Get().AddPatchToSelection,
		FExecuteAction::CreateSP(this, &SDMXControlConsoleEditorFixturePatchVerticalBox::OnGenerateSelectedFaderGroupFromFixturePatch),
		FCanExecuteAction::CreateSP(this, &SDMXControlConsoleEditorFixturePatchVerticalBox::CanExecuteAddSelected)
	);
}

TSharedRef<SWidget> SDMXControlConsoleEditorFixturePatchVerticalBox::GenerateFixturePatchListToolbar()
{
	const auto GenerateAddButtonContentLambda = [](const FText& AddButtonText, const FText& AddButtonToolTip)
	{
		return
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			[
				SNew(SImage)
				.ColorAndOpacity(FStyleColors::AccentGreen)
				.Image(FAppStyle::Get().GetBrush("Icons.Plus"))
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(3.f, 0.f, 0.f, 0.f)
			[
				SNew(STextBlock)
				.Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")))
				.Text(AddButtonText)
				.ToolTipText(AddButtonToolTip)
				.TextStyle(FAppStyle::Get(), "SmallButtonText")
			];
	};

	const TSharedRef<SWidget> FixturePatchListToolbar =
		SNew(SHorizontalBox)

		// Add All Button
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.MaxWidth(160.f)
		.HAlign(HAlign_Left)
		.Padding(8.f, 8.f, 4.f, 8.f)
		[
			SNew(SButton)
			.ButtonStyle(&FAppStyle::Get().GetWidgetStyle<FButtonStyle>("Button"))
			.ForegroundColor(FSlateColor::UseStyle())
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.IsEnabled(this, &SDMXControlConsoleEditorFixturePatchVerticalBox::IsAddAllPatchesButtonEnabled)
			.OnClicked(this, &SDMXControlConsoleEditorFixturePatchVerticalBox::OnAddAllPatchesClicked)
			[
				GenerateAddButtonContentLambda
				(
					LOCTEXT("AddAllFixturePatchFromList", "Add All Patches"),
					LOCTEXT("AddAllFixturePatchFromList_ToolTip", "Add all Fixture Patches from the list.")
				)
			]
		]

		// Add Combo Button
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.MaxWidth(160.f)
		.HAlign(HAlign_Left)
		.Padding(4.f, 8.f, 8.f, 8.f)
		[
			SNew(SComboButton)
			.ButtonStyle(&FAppStyle::Get().GetWidgetStyle<FButtonStyle>("Button"))
			.ForegroundColor(FSlateColor::UseStyle())
			.HasDownArrow(true)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.OnGetMenuContent(this, &SDMXControlConsoleEditorFixturePatchVerticalBox::CreateAddPatchMenu)
			.ButtonContent()
			[
				GenerateAddButtonContentLambda
				(
					LOCTEXT("AddFixturePatchFromList", "Add Patch"),
					LOCTEXT("AddFixturePatchFromList_ToolTip", "Add a Fixture Patch from the list.")
				)
			]
		];

	return FixturePatchListToolbar;
}

TSharedPtr<SWidget> SDMXControlConsoleEditorFixturePatchVerticalBox::CreateRowContextMenu()
{
	ensureMsgf(CommandList.IsValid(), TEXT("Invalid command list for control console asset picker."));
	FMenuBuilder MenuBuilder(true, CommandList);

	MenuBuilder.BeginSection(NAME_None, LOCTEXT("AddPatchButtonMainSection", "Add Patch"));
	{
		MenuBuilder.AddMenuEntry(FDMXControlConsoleEditorCommands::Get().AddPatchNext);
		MenuBuilder.AddMenuEntry(FDMXControlConsoleEditorCommands::Get().AddPatchNextRow);
		MenuBuilder.AddMenuEntry(FDMXControlConsoleEditorCommands::Get().AddPatchToSelection);
	}
	MenuBuilder.EndSection();

	return MenuBuilder.MakeWidget();
}

TSharedRef<SWidget> SDMXControlConsoleEditorFixturePatchVerticalBox::CreateAddPatchMenu()
{
	return CreateRowContextMenu().ToSharedRef();
}

void SDMXControlConsoleEditorFixturePatchVerticalBox::GenerateFaderGroupFromFixturePatch(UDMXControlConsoleFaderGroup* FaderGroup, UDMXEntityFixturePatch* FixturePatch)
{
	if (!FaderGroup || !FixturePatch)
	{
		return;
	}

	const TSharedRef<FDMXControlConsoleEditorSelection> SelectionHandler = FDMXControlConsoleEditorManager::Get().GetSelectionHandler();
	SelectionHandler->ClearFadersSelection(FaderGroup);

	const FScopedTransaction GenerateFaderGroupFromFixturePatchTransaction(LOCTEXT("GenerateFaderGroupFromFixturePatchTransaction", "Generate Fader Group from Fixture Patch"));
	FaderGroup->PreEditChange(UDMXControlConsoleFaderGroup::StaticClass()->FindPropertyByName(UDMXControlConsoleFaderGroup::GetSoftFixturePatchPtrPropertyName()));

	FaderGroup->GenerateFromFixturePatch(FixturePatch);

	FaderGroup->PostEditChange();
}

void SDMXControlConsoleEditorFixturePatchVerticalBox::OnGenerateFromFixturePatchOnLastRow()
{
	if (!FixturePatchList.IsValid())
	{
		return;
	}

	const UDMXControlConsoleData* EditorConsoleData = FDMXControlConsoleEditorManager::Get().GetEditorConsoleData();
	if (!EditorConsoleData)
	{
		return;
	}

	const TArray<UDMXControlConsoleFaderGroupRow*> FaderGroupRows = EditorConsoleData->GetFaderGroupRows();
	if (FaderGroupRows.IsEmpty())
	{
		return;
	}

	UDMXControlConsoleFaderGroupRow* FaderGroupRow = nullptr;
	int32 Index = -1;

	const TSharedRef<FDMXControlConsoleEditorSelection> SelectionHandler = FDMXControlConsoleEditorManager::Get().GetSelectionHandler();
	const TArray<TWeakObjectPtr<UObject>> SelectedFaderGroupsObjects = SelectionHandler->GetSelectedFaderGroups();
	if (SelectedFaderGroupsObjects.IsEmpty())
	{
		FaderGroupRow = FaderGroupRows.Last();
		if (!FaderGroupRow)
		{
			return;
		}

		Index = FaderGroupRow->GetFaderGroups().Num();
	}
	else
	{
		const UDMXControlConsoleFaderGroup* SelectedFaderGroup = SelectionHandler->GetFirstSelectedFaderGroup(true);
		if (!SelectedFaderGroup)
		{
			return;
		}

		FaderGroupRow = &SelectedFaderGroup->GetOwnerFaderGroupRowChecked();
		Index = SelectedFaderGroup->GetIndex() + 1;
	}

	// Add all selected Fixture Patches from Fixture Patch List
	for (const TSharedPtr<FDMXEntityFixturePatchRef>& FixturePatchRef : FixturePatchList->GetSelectedFixturePatchRefs())
	{
		if (!FixturePatchRef.IsValid())
		{
			continue;
		}

		const FScopedTransaction GenerateFromFixturePatchOnLastRowTransaction(LOCTEXT("GenerateFromFixturePatchOnLastRowTransaction", "Generate Fader Group from Fixture Patch"));
		FaderGroupRow->PreEditChange(nullptr);
		UDMXControlConsoleFaderGroup* FaderGroup = FaderGroupRow->AddFaderGroup(Index);
		FaderGroupRow->PostEditChange();

		UDMXEntityFixturePatch* FixturePatch = FixturePatchRef->GetFixturePatch();
		GenerateFaderGroupFromFixturePatch(FaderGroup, FixturePatch);

		Index++;
	}
}

void SDMXControlConsoleEditorFixturePatchVerticalBox::OnGenerateFromFixturePatchOnNewRow()
{
	if (!FixturePatchList.IsValid())
	{
		return;
	}

	UDMXControlConsoleData* EditorConsoleData = FDMXControlConsoleEditorManager::Get().GetEditorConsoleData();
	if (!EditorConsoleData)
	{
		return;
	}

	int32 NewRowIndex = -1;

	const TSharedRef<FDMXControlConsoleEditorSelection> SelectionHandler = FDMXControlConsoleEditorManager::Get().GetSelectionHandler();
	const TArray<TWeakObjectPtr<UObject>> SelectedFaderGroupsObjects = SelectionHandler->GetSelectedFaderGroups();
	if (SelectedFaderGroupsObjects.IsEmpty())
	{
		NewRowIndex = EditorConsoleData->GetFaderGroupRows().Num();
	}
	else
	{
		const UDMXControlConsoleFaderGroup* SelectedFaderGroup = SelectionHandler->GetFirstSelectedFaderGroup(true);
		if (!SelectedFaderGroup)
		{
			return;
		}

		const int32 SelectedFaderGroupRowIndex = SelectedFaderGroup->GetOwnerFaderGroupRowChecked().GetRowIndex();
		NewRowIndex = SelectedFaderGroupRowIndex + 1;
	}

	const FScopedTransaction GenerateFromFixturePatchOnNewRowTransaction(LOCTEXT("GenerateFromFixturePatchOnNewRowTransaction", "Generate Fader Group from Fixture Patch"));
	EditorConsoleData->PreEditChange(nullptr);
	UDMXControlConsoleFaderGroupRow* NewRow = EditorConsoleData->AddFaderGroupRow(NewRowIndex);
	EditorConsoleData->PostEditChange();

	int32 Index = 0;
	// Add all selected Fixture Patches from Fixture Patch List
	for (const TSharedPtr<FDMXEntityFixturePatchRef>& FixturePatchRef : FixturePatchList->GetSelectedFixturePatchRefs())
	{
		if (!FixturePatchRef.IsValid())
		{
			continue;
		}

		// New rows should have an already created first Fader Group
		const bool bIsFirstFaderGroup = Index == 0 && !NewRow->GetFaderGroups().IsEmpty();
		UDMXControlConsoleFaderGroup* FaderGroup = bIsFirstFaderGroup ? NewRow->GetFaderGroups()[Index] : NewRow->AddFaderGroup(Index);
		
		UDMXEntityFixturePatch* FixturePatch = FixturePatchRef->GetFixturePatch();
		GenerateFaderGroupFromFixturePatch(FaderGroup, FixturePatch);

		Index++;
	}
}

void SDMXControlConsoleEditorFixturePatchVerticalBox::OnGenerateSelectedFaderGroupFromFixturePatch()
{
	if (!FixturePatchList.IsValid())
	{
		return;
	}

	const TSharedRef<FDMXControlConsoleEditorSelection> SelectionHandler = FDMXControlConsoleEditorManager::Get().GetSelectionHandler();
	const TArray<TWeakObjectPtr<UObject>> SelectedFaderGroupsObjects = SelectionHandler->GetSelectedFaderGroups();
	const TArray<TSharedPtr<FDMXEntityFixturePatchRef>> SelectedFixturePatches = FixturePatchList->GetSelectedFixturePatchRefs();

	if (SelectedFaderGroupsObjects.IsEmpty() || SelectedFixturePatches.IsEmpty())
	{
		return;
	}
	
	UDMXControlConsoleFaderGroup* FaderGroup =  SelectionHandler->GetFirstSelectedFaderGroup();
	UDMXEntityFixturePatch* FixturePatch = SelectedFixturePatches[0]->GetFixturePatch();

	GenerateFaderGroupFromFixturePatch(FaderGroup, FixturePatch);
}

FReply SDMXControlConsoleEditorFixturePatchVerticalBox::OnAddAllPatchesClicked()
{
	UDMXControlConsoleData* EditorConsoleData = FDMXControlConsoleEditorManager::Get().GetEditorConsoleData();
	if (EditorConsoleData)
	{
		const FScopedTransaction AddAllPatchesTransaction(LOCTEXT("AddAllPatchesTransaction", "Generate from Library"));
		EditorConsoleData->PreEditChange(nullptr);
		EditorConsoleData->GenerateFromDMXLibrary();
		EditorConsoleData->PostEditChange();
	}

	return FReply::Handled();
}

bool SDMXControlConsoleEditorFixturePatchVerticalBox::IsFixturePatchListRowEnabled(const FDMXEntityFixturePatchRef InFixturePatchRef) const
{
	if (const UDMXEntityFixturePatch* InFixturePatch = InFixturePatchRef.GetFixturePatch())
	{
		if (const UDMXControlConsoleData* EditorConsoleData = FDMXControlConsoleEditorManager::Get().GetEditorConsoleData())
		{
			const TArray<UDMXControlConsoleFaderGroup*> AllFaderGroups = EditorConsoleData->GetAllFaderGroups();

			auto IsFixturePatchInUseLambda = [InFixturePatch](const UDMXControlConsoleFaderGroup* FaderGroup)
			{
				if (!FaderGroup)
				{
					return false;
				}

				if (!FaderGroup->HasFixturePatch())
				{
					return false;
				}

				const UDMXEntityFixturePatch* FixturePatch = FaderGroup->GetFixturePatch();
				if (FixturePatch != InFixturePatch)
				{
					return false;
				}

				return true;
			};

			return Algo::FindByPredicate(AllFaderGroups, IsFixturePatchInUseLambda) ? false : true;
		}
	}

	return true;
}

bool SDMXControlConsoleEditorFixturePatchVerticalBox::IsAddAllPatchesButtonEnabled() const
{
	const UDMXControlConsoleData* EditorConsoleData = FDMXControlConsoleEditorManager::Get().GetEditorConsoleData();
	if (!EditorConsoleData)
	{
		return false;
	}

	return IsValid(EditorConsoleData->GetDMXLibrary());
}

bool SDMXControlConsoleEditorFixturePatchVerticalBox::CanExecuteAddNext() const
{
	const UDMXControlConsoleData* EditorConsoleData = FDMXControlConsoleEditorManager::Get().GetEditorConsoleData();
	if (!EditorConsoleData || !FixturePatchList.IsValid())
	{
		return false;
	}

	const bool bCanExecute =
		!FixturePatchList->GetSelectedFixturePatchRefs().IsEmpty()
		&& !EditorConsoleData->GetAllFaderGroups().IsEmpty()
		&& EditorConsoleData->FilterString.IsEmpty();

	return bCanExecute;
}

bool SDMXControlConsoleEditorFixturePatchVerticalBox::CanExecuteAddRow() const
{
	const UDMXControlConsoleData* EditorConsoleData = FDMXControlConsoleEditorManager::Get().GetEditorConsoleData();
	if (!EditorConsoleData || !FixturePatchList.IsValid())
	{
		return false;
	}

	const bool bCanExecute = 
		!FixturePatchList->GetSelectedFixturePatchRefs().IsEmpty()
		&& EditorConsoleData->FilterString.IsEmpty();

	return bCanExecute;
}

bool SDMXControlConsoleEditorFixturePatchVerticalBox::CanExecuteAddSelected() const
{
	const UDMXControlConsoleData* EditorConsoleData = FDMXControlConsoleEditorManager::Get().GetEditorConsoleData();
	if (!EditorConsoleData || !FixturePatchList.IsValid())
	{
		return false;
	}

	const TSharedRef<FDMXControlConsoleEditorSelection> SelectionHandler = FDMXControlConsoleEditorManager::Get().GetSelectionHandler();
	const bool bCanExecute =
		!FixturePatchList->GetSelectedFixturePatchRefs().IsEmpty()
		&& !SelectionHandler->GetSelectedFaderGroups().IsEmpty()
		&& EditorConsoleData->FilterString.IsEmpty();

	return bCanExecute;
}

#undef LOCTEXT_NAMESPACE
