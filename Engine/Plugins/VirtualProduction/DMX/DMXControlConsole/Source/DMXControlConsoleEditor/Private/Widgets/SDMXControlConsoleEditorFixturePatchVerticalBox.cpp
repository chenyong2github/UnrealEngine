// Copyright Epic Games, Inc. All Rights Reserved.

#include "SDMXControlConsoleEditorFixturePatchVerticalBox.h"

#include "Algo/AllOf.h"
#include "Algo/AnyOf.h"
#include "Algo/Find.h"
#include "Algo/ForEach.h"
#include "DMXControlConsoleData.h"
#include "DMXControlConsoleFaderBase.h"
#include "DMXControlConsoleFaderGroup.h"
#include "DMXControlConsoleFaderGroupRow.h"
#include "DMXControlConsoleEditorSelection.h"
#include "Commands/DMXControlConsoleEditorCommands.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Library/DMXEntityFixturePatch.h"
#include "Library/DMXEntityReference.h"
#include "Library/DMXLibrary.h"
#include "Models/DMXControlConsoleEditorModel.h"
#include "ScopedTransaction.h"
#include "Style/DMXControlConsoleEditorStyle.h"
#include "Styling/AppStyle.h"
#include "Styling/StyleColors.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SDMXControlConsoleReadOnlyFixturePatchList.h"


#define LOCTEXT_NAMESPACE "SDMXControlConsoleEditorFixturePatchVerticalBox"

SDMXControlConsoleEditorFixturePatchVerticalBox::~SDMXControlConsoleEditorFixturePatchVerticalBox()
{
	if (FixturePatchList.IsValid())
	{
		const FDMXReadOnlyFixturePatchListDescriptor ListDescriptor = FixturePatchList->MakeListDescriptor();

		UDMXControlConsoleEditorModel* EditorConsoleModel = GetMutableDefault<UDMXControlConsoleEditorModel>();
		EditorConsoleModel->SaveFixturePatchListDescriptorToConfig(ListDescriptor);
	}
}

void SDMXControlConsoleEditorFixturePatchVerticalBox::Construct(const FArguments& InArgs)
{
	RegisterCommands();

	UDMXControlConsoleEditorModel* EditorConsoleModel = GetMutableDefault<UDMXControlConsoleEditorModel>();
	const FDMXReadOnlyFixturePatchListDescriptor ListDescriptor = EditorConsoleModel->GetFixturePatchListDescriptor();

	const UDMXControlConsoleData* EditorConsoleData = EditorConsoleModel->GetEditorConsoleData();
	UDMXLibrary* DMXLibrary = EditorConsoleData ? EditorConsoleData->GetDMXLibrary() : nullptr;

	ChildSlot
		.Padding(0.f, 8.f, 0.f, 0.f)
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SAssignNew(FixturePatchList, SDMXControlConsoleReadOnlyFixturePatchList)
				.ListDescriptor(ListDescriptor)
				.DMXLibrary(DMXLibrary)
				.OnContextMenuOpening(this, &SDMXControlConsoleEditorFixturePatchVerticalBox::CreateRowContextMenu)
				.OnRowSelectionChanged(this, &SDMXControlConsoleEditorFixturePatchVerticalBox::OnRowSelectionChanged)
				.OnRowClicked(this, &SDMXControlConsoleEditorFixturePatchVerticalBox::OnRowClicked)
				.OnRowDoubleClicked(this, &SDMXControlConsoleEditorFixturePatchVerticalBox::OnRowDoubleClicked)
				.OnCheckBoxStateChanged(this, &SDMXControlConsoleEditorFixturePatchVerticalBox::OnListCheckBoxStateChanged)
				.OnRowCheckBoxStateChanged(this, &SDMXControlConsoleEditorFixturePatchVerticalBox::OnRowCheckBoxStateChanged)
				.IsChecked(this, &SDMXControlConsoleEditorFixturePatchVerticalBox::IsListChecked)
				.IsRowChecked(this, &SDMXControlConsoleEditorFixturePatchVerticalBox::IsRowChecked)
			]
		];
}

void SDMXControlConsoleEditorFixturePatchVerticalBox::ForceRefresh()
{
	const UDMXControlConsoleEditorModel* EditorConsoleModel = GetDefault<UDMXControlConsoleEditorModel>();
	const UDMXControlConsoleData* EditorConsoleData = EditorConsoleModel->GetEditorConsoleData();
	if (EditorConsoleData && FixturePatchList.IsValid())
	{
		UDMXLibrary* NewLibrary = EditorConsoleData->GetDMXLibrary();
		FixturePatchList->SetDMXLibrary(NewLibrary);
	}
}

void SDMXControlConsoleEditorFixturePatchVerticalBox::RegisterCommands()
{
	CommandList = MakeShared<FUICommandList>();

	const auto MapActionLambda = [this](TSharedPtr<FUICommandInfo> CommandInfo, bool bMute, bool bOnlyActive)
		{
			CommandList->MapAction
			(
				CommandInfo,
				FExecuteAction::CreateSP(this, &SDMXControlConsoleEditorFixturePatchVerticalBox::OnMuteAllFaderGroups, bMute, bOnlyActive),
				FCanExecuteAction::CreateSP(this, &SDMXControlConsoleEditorFixturePatchVerticalBox::IsAnyFaderGroupsMuted, !bMute, bOnlyActive),
				FGetActionCheckState(),
				FIsActionButtonVisible::CreateSP(this, &SDMXControlConsoleEditorFixturePatchVerticalBox::IsAnyFaderGroupsMuted, !bMute, bOnlyActive)
			);
		};

	MapActionLambda(FDMXControlConsoleEditorCommands::Get().Mute, true, true);
	MapActionLambda(FDMXControlConsoleEditorCommands::Get().MuteAll, true, false);
	MapActionLambda(FDMXControlConsoleEditorCommands::Get().Unmute, false, true);
	MapActionLambda(FDMXControlConsoleEditorCommands::Get().UnmuteAll, false, false);
}

TSharedPtr<SWidget> SDMXControlConsoleEditorFixturePatchVerticalBox::CreateRowContextMenu()
{
	ensureMsgf(CommandList.IsValid(), TEXT("Invalid command list for control console asset picker."));
	FMenuBuilder MenuBuilder(true, CommandList);

	MenuBuilder.BeginSection(NAME_None, LOCTEXT("MuteFaderGroupContextMenu", "Mute"));
	{
		MenuBuilder.AddMenuEntry
		(
			FDMXControlConsoleEditorCommands::Get().Mute,
			NAME_None,
			LOCTEXT("MuteContextMenu_Label", "Only Active"),
			FText::GetEmpty(),
			FSlateIcon(FDMXControlConsoleEditorStyle::Get().GetStyleSetName(), "DMXControlConsole.Fader.Mute")
		);

		MenuBuilder.AddMenuEntry
		(
			FDMXControlConsoleEditorCommands::Get().MuteAll,
			NAME_None,
			LOCTEXT("MuteAllContextMenu_Label", "All"),
			FText::GetEmpty(),
			FSlateIcon(FDMXControlConsoleEditorStyle::Get().GetStyleSetName(), "DMXControlConsole.Fader.Mute")
		);
	}
	MenuBuilder.EndSection();

	MenuBuilder.BeginSection(NAME_None, LOCTEXT("UnmuteFaderGroupContextMenu", "Unmute"));
	{
		MenuBuilder.AddMenuEntry
		(
			FDMXControlConsoleEditorCommands::Get().Unmute,
			NAME_None,
			LOCTEXT("UnmuteContextMenu_Label", "Only Active"),
			FText::GetEmpty(),
			FSlateIcon(FDMXControlConsoleEditorStyle::Get().GetStyleSetName(), "DMXControlConsole.Fader.Unmute")
		);

		MenuBuilder.AddMenuEntry
		(
			FDMXControlConsoleEditorCommands::Get().UnmuteAll,
			NAME_None,
			LOCTEXT("UnmuteAllContextMenu_Label", "All"),
			FText::GetEmpty(),
			FSlateIcon(FDMXControlConsoleEditorStyle::Get().GetStyleSetName(), "DMXControlConsole.Fader.Unmute")
		);
	}
	MenuBuilder.EndSection();

	return MenuBuilder.MakeWidget();
}

TSharedRef<SWidget> SDMXControlConsoleEditorFixturePatchVerticalBox::CreateAddPatchMenu()
{
	return CreateRowContextMenu().ToSharedRef();
}

void SDMXControlConsoleEditorFixturePatchVerticalBox::OnRowSelectionChanged(const TSharedPtr<FDMXEntityFixturePatchRef> NewSelection, ESelectInfo::Type SelectInfo)
{
	if (!FixturePatchList.IsValid() || SelectInfo == ESelectInfo::Direct)
	{
		return;
	}

	UDMXControlConsoleEditorModel* EditorConsoleModel = GetMutableDefault<UDMXControlConsoleEditorModel>();
	if (const UDMXControlConsoleData* EditorConsoleData = EditorConsoleModel->GetEditorConsoleData())
	{
		const TArray<TSharedPtr<FDMXEntityFixturePatchRef>> SelectedItems = FixturePatchList->GetSelectedFixturePatchRefs();
		const TArray<UDMXControlConsoleFaderGroup*> AllFaderGroups = EditorConsoleData->GetAllFaderGroups();
		TArray<UObject*> FaderGroupsToAddToSelection;
		TArray<UObject*> FaderGroupsToRemoveFromSelection;
		Algo::ForEach(AllFaderGroups, [SelectedItems, &FaderGroupsToAddToSelection, &FaderGroupsToRemoveFromSelection](UDMXControlConsoleFaderGroup* FaderGroup)
			{
				if (!FaderGroup || !FaderGroup->HasFixturePatch())
				{
					return;
				}

				const UDMXEntityFixturePatch* FixturePatch = FaderGroup->GetFixturePatch();
				const bool bIsFixturePatchSelected = SelectedItems.ContainsByPredicate([FixturePatch](const TSharedPtr<FDMXEntityFixturePatchRef>& FixturePatchRef)
					{
						return FixturePatchRef.IsValid() && FixturePatchRef->GetFixturePatch() == FixturePatch;
					});

				FaderGroup->SetIsActive(bIsFixturePatchSelected);
				if (bIsFixturePatchSelected)
				{
					const TArray<UDMXControlConsoleFaderBase*> AllFaders = FaderGroup->GetAllFaders();
					FaderGroupsToAddToSelection.Append(AllFaders);
				}
				else
				{
					FaderGroupsToRemoveFromSelection.Add(FaderGroup);
				}
			});

		const TSharedRef<FDMXControlConsoleEditorSelection> SelectionHandler = EditorConsoleModel->GetSelectionHandler();
		SelectionHandler->AddToSelection(FaderGroupsToAddToSelection);
		SelectionHandler->RemoveFromSelection(FaderGroupsToRemoveFromSelection);
	}
}

void SDMXControlConsoleEditorFixturePatchVerticalBox::OnRowClicked(const TSharedPtr<FDMXEntityFixturePatchRef> ItemClicked)
{
	if (!ItemClicked.IsValid())
	{
		return;
	}

	const UDMXControlConsoleEditorModel* EditorConsoleModel = GetDefault<UDMXControlConsoleEditorModel>();
	if (const UDMXControlConsoleData* EditorConsoleData = EditorConsoleModel->GetEditorConsoleData())
	{
		if (const UDMXEntityFixturePatch* FixturePatch = ItemClicked->GetFixturePatch())
		{
			UDMXControlConsoleFaderGroup* FaderGroup = EditorConsoleData->FindFaderGroupByFixturePatch(FixturePatch);
			if (FaderGroup && FaderGroup->IsActive())
			{
				EditorConsoleModel->ScrollIntoView(FaderGroup);
			}
		}
	}
}

void SDMXControlConsoleEditorFixturePatchVerticalBox::OnRowDoubleClicked(const TSharedPtr<FDMXEntityFixturePatchRef> ItemClicked)
{
	if (!ItemClicked.IsValid())
	{
		return;
	}

	UDMXControlConsoleEditorModel* EditorConsoleModel = GetMutableDefault<UDMXControlConsoleEditorModel>();
	const UDMXControlConsoleData* EditorConsoleData = EditorConsoleModel->GetEditorConsoleData();
	if (!EditorConsoleData)
	{
		return;
	}

	const UDMXEntityFixturePatch* FixturePatch = ItemClicked->GetFixturePatch();
	if (!FixturePatch)
	{
		return;
	}

	UDMXControlConsoleFaderGroup* FaderGroup = EditorConsoleData->FindFaderGroupByFixturePatch(FixturePatch);
	if (FaderGroup && FaderGroup->IsActive())
	{
		FaderGroup->SetIsExpanded(true);
	}
}

void SDMXControlConsoleEditorFixturePatchVerticalBox::OnListCheckBoxStateChanged(ECheckBoxState CheckBoxState)
{
	const UDMXControlConsoleEditorModel* EditorConsoleModel = GetDefault<UDMXControlConsoleEditorModel>();
	if (const UDMXControlConsoleData* EditorConsoleData = EditorConsoleModel->GetEditorConsoleData())
	{
		const TArray<UDMXControlConsoleFaderGroup*> AllFaderGroups = EditorConsoleData->GetAllFaderGroups();
		Algo::ForEach(AllFaderGroups, [CheckBoxState](UDMXControlConsoleFaderGroup* FaderGroup)
			{
				if (FaderGroup && FaderGroup->HasFixturePatch())
				{
					const bool bIsMuted = CheckBoxState == ECheckBoxState::Unchecked;
					FaderGroup->SetMute(bIsMuted);
				}
			});
	}
}

void SDMXControlConsoleEditorFixturePatchVerticalBox::OnRowCheckBoxStateChanged(ECheckBoxState CheckBoxState, const TSharedPtr<FDMXEntityFixturePatchRef> InFixturePatchRef)
{
	const UDMXEntityFixturePatch* FixturePatch = InFixturePatchRef.IsValid() ? InFixturePatchRef->GetFixturePatch() : nullptr;
	if (!FixturePatch)
	{
		return;
	}

	const UDMXControlConsoleEditorModel* EditorConsoleModel = GetDefault<UDMXControlConsoleEditorModel>();
	const UDMXControlConsoleData* EditorConsoleData = EditorConsoleModel->GetEditorConsoleData();
	if (!EditorConsoleData)
	{
		return;
	}

	UDMXControlConsoleFaderGroup* FaderGroup = EditorConsoleData->FindFaderGroupByFixturePatch(FixturePatch);
	if (FaderGroup)
	{
		FaderGroup->ToggleMute();
	}
}

ECheckBoxState SDMXControlConsoleEditorFixturePatchVerticalBox::IsListChecked() const
{
	const UDMXControlConsoleEditorModel* EditorConsoleModel = GetDefault<UDMXControlConsoleEditorModel>();
	if (const UDMXControlConsoleData* EditorConsoleData = EditorConsoleModel->GetEditorConsoleData())
	{
		// Get all patched Fader Groups
		TArray<UDMXControlConsoleFaderGroup*> AllPatchedFaderGroups = EditorConsoleData->GetAllFaderGroups();
		AllPatchedFaderGroups.RemoveAll([](const UDMXControlConsoleFaderGroup* FaderGroup)
			{
				return FaderGroup && !FaderGroup->HasFixturePatch();
			});

		const bool bAreAllFaderGroupsUnmuted = Algo::AllOf(AllPatchedFaderGroups, [](const UDMXControlConsoleFaderGroup* FaderGroup)
			{
				return FaderGroup && !FaderGroup->IsMuted();
			});

		if (bAreAllFaderGroupsUnmuted)
		{
			return ECheckBoxState::Checked;
		}

		const bool bIsAnyFaderGroupUnmuted = Algo::AnyOf(AllPatchedFaderGroups, [](const UDMXControlConsoleFaderGroup* FaderGroup)
			{
				return FaderGroup && !FaderGroup->IsMuted();
			});

		return bIsAnyFaderGroupUnmuted ? ECheckBoxState::Undetermined : ECheckBoxState::Unchecked;
	}

	return ECheckBoxState::Undetermined;
}

ECheckBoxState SDMXControlConsoleEditorFixturePatchVerticalBox::IsRowChecked(const TSharedPtr<FDMXEntityFixturePatchRef> InFixturePatchRef) const
{
	const UDMXEntityFixturePatch* FixturePatch = InFixturePatchRef.IsValid() ? InFixturePatchRef->GetFixturePatch() : nullptr;
	if (FixturePatch)
	{
		const UDMXControlConsoleEditorModel* EditorConsoleModel = GetDefault<UDMXControlConsoleEditorModel>();
		if (const UDMXControlConsoleData* EditorConsoleData = EditorConsoleModel->GetEditorConsoleData())
		{
			if (const UDMXControlConsoleFaderGroup* FaderGroup = EditorConsoleData->FindFaderGroupByFixturePatch(FixturePatch))
			{
				if (FaderGroup->IsMuted())
				{
					const TArray<UDMXControlConsoleFaderBase*> AllFaders = FaderGroup->GetAllFaders();
					const bool bIsAnyFaderUnmuted = Algo::AnyOf(AllFaders, [](const UDMXControlConsoleFaderBase* Fader)
						{
							return Fader && !Fader->IsMuted();
						});

					return bIsAnyFaderUnmuted ? ECheckBoxState::Undetermined : ECheckBoxState::Unchecked;
				}

				return ECheckBoxState::Checked;
			}
		}
	}

	return ECheckBoxState::Undetermined;
}

void SDMXControlConsoleEditorFixturePatchVerticalBox::OnMuteAllFaderGroups(bool bMute, bool bOnlyActive) const
{
	const UDMXControlConsoleEditorModel* EditorConsoleModel = GetDefault<UDMXControlConsoleEditorModel>();
	if (const UDMXControlConsoleData* EditorConsoleData = EditorConsoleModel->GetEditorConsoleData())
	{
		const TArray<UDMXControlConsoleFaderGroup*> AllFaderGroups = bOnlyActive ? EditorConsoleData->GetAllActiveFaderGroups() : EditorConsoleData->GetAllFaderGroups();
		Algo::ForEach(AllFaderGroups, [bMute](UDMXControlConsoleFaderGroup* FaderGroup)
			{
				if (FaderGroup)
				{
					FaderGroup->SetMute(bMute);
				}
			});
	}
}

bool SDMXControlConsoleEditorFixturePatchVerticalBox::IsAnyFaderGroupsMuted(bool bMute, bool bOnlyActive) const
{
	const UDMXControlConsoleEditorModel* EditorConsoleModel = GetDefault<UDMXControlConsoleEditorModel>();
	if (const UDMXControlConsoleData* EditorConsoleData = EditorConsoleModel->GetEditorConsoleData())
	{
		const TArray<UDMXControlConsoleFaderGroup*> AllFaderGroups = bOnlyActive ? EditorConsoleData->GetAllActiveFaderGroups() : EditorConsoleData->GetAllFaderGroups();
		return Algo::AnyOf(AllFaderGroups, [bMute](UDMXControlConsoleFaderGroup* FaderGroup)
			{
				return FaderGroup && FaderGroup->IsMuted() == bMute;
			});
	}

	return false;
}

#undef LOCTEXT_NAMESPACE
