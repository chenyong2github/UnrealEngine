// Copyright Epic Games, Inc. All Rights Reserved.

#include "DMXControlConsoleEditorManager.h"

#include "DMXControlConsole.h"
#include "DMXControlConsoleData.h"
#include "DMXControlConsoleFaderBase.h"
#include "DMXControlConsoleFaderGroup.h"
#include "DMXControlConsoleEditorSelection.h"
#include "Models/DMXControlConsoleEditorModel.h"

#include "ScopedTransaction.h"
#include "Misc/CoreDelegates.h"


#define LOCTEXT_NAMESPACE "DMXControlConsoleEditorManager"

TSharedPtr<FDMXControlConsoleEditorManager> FDMXControlConsoleEditorManager::Instance;

FDMXControlConsoleEditorManager::~FDMXControlConsoleEditorManager()
{
	FCoreDelegates::OnEnginePreExit.RemoveAll(this);
}

FDMXControlConsoleEditorManager& FDMXControlConsoleEditorManager::Get()
{
	if (!Instance.IsValid())
	{
		Instance = MakeShareable(new FDMXControlConsoleEditorManager());
	}
	checkf(Instance.IsValid(), TEXT(" DMX Control Console Manager instance is null."));

	return *Instance.Get();
}

UDMXControlConsole* FDMXControlConsoleEditorManager::GetEditorConsole() const
{
	UDMXControlConsoleEditorModel* EditorConsoleModel = GetMutableDefault<UDMXControlConsoleEditorModel>();
	return EditorConsoleModel->GetEditorConsole();
}

UDMXControlConsoleData* FDMXControlConsoleEditorManager::GetEditorConsoleData() const
{
	if (UDMXControlConsole* EditorConsole = GetEditorConsole())
	{
		return EditorConsole->GetControlConsoleData();
	}

	return nullptr;
}

TSharedRef<FDMXControlConsoleEditorSelection> FDMXControlConsoleEditorManager::GetSelectionHandler()
{
	if (!SelectionHandler.IsValid())
	{
		SelectionHandler = MakeShareable(new FDMXControlConsoleEditorSelection(AsShared()));
	}

	return SelectionHandler.ToSharedRef();
}

void FDMXControlConsoleEditorManager::SetFaderGroupsViewMode(EDMXControlConsoleEditorViewMode ViewMode)
{
	FaderGroupsViewMode = ViewMode;
	OnFaderGroupsViewModeChanged.Broadcast();
}

void FDMXControlConsoleEditorManager::SetFadersViewMode(EDMXControlConsoleEditorViewMode ViewMode)
{
	FadersViewMode = ViewMode;
	OnFadersViewModeChanged.Broadcast();
}

void FDMXControlConsoleEditorManager::SendDMX()
{
	UDMXControlConsoleData* EditorConsoleData = GetEditorConsoleData();
	if (ensureMsgf(EditorConsoleData, TEXT("Invalid Editor Control Console Data, can't send DMX correctly.")))
	{
		EditorConsoleData->StartSendingDMX();
	}
}

void FDMXControlConsoleEditorManager::StopDMX()
{
	UDMXControlConsoleData* EditorConsoleData = GetEditorConsoleData();
	if (ensureMsgf(EditorConsoleData, TEXT("Invalid Editor Control Console Data, can't stop DMX correctly.")))
	{
		EditorConsoleData->StopSendingDMX();
	}
}

bool FDMXControlConsoleEditorManager::IsSendingDMX() const
{
	UDMXControlConsoleData* EditorConsoleData = GetEditorConsoleData();
	if (ensureMsgf(EditorConsoleData, TEXT("Invalid Editor Control Console Data, cannot deduce if it is sending DMX.")))
	{
		return EditorConsoleData->IsSendingDMX();
	}
	return false;
}

void FDMXControlConsoleEditorManager::RemoveAllSelectedElements()
{
	const TArray<TWeakObjectPtr<UObject>> SelectedFaderGroupsObjects = SelectionHandler->GetSelectedFaderGroups();
	if (!SelectedFaderGroupsObjects.IsEmpty())
	{
		const FScopedTransaction RemoveAllSelectedElementsTransaction(LOCTEXT("RemoveAllSelectedElementsTransaction", "Selected Elements removed"));
		
		// Delete all selected fader groups
		for (const TWeakObjectPtr<UObject>& SelectedFaderGroupObject : SelectedFaderGroupsObjects)
		{
			UDMXControlConsoleFaderGroup* SelectedFaderGroup = Cast<UDMXControlConsoleFaderGroup>(SelectedFaderGroupObject);
			if (SelectedFaderGroup && SelectionHandler->GetSelectedFadersFromFaderGroup(SelectedFaderGroup).IsEmpty())
			{
				// If there's only one fader group to delete, replace it in selection
				if (SelectedFaderGroupsObjects.Num() == 1)
				{
					SelectionHandler->ReplaceInSelection(SelectedFaderGroup);
				}

				constexpr bool bNotifySelectedFaderGroupChange = false;
				SelectionHandler->RemoveFromSelection(SelectedFaderGroup, bNotifySelectedFaderGroupChange);
				
				SelectedFaderGroup->PreEditChange(nullptr);
				SelectedFaderGroup->Destroy();
				SelectedFaderGroup->PostEditChange();
			}
		}

		// Delete all selected faders
		const TArray<TWeakObjectPtr<UObject>> SelectedFadersObjects = SelectionHandler->GetSelectedFaders();
		if (!SelectedFadersObjects.IsEmpty())
		{
			for (TWeakObjectPtr<UObject> SelectedFaderObject : SelectedFadersObjects)
			{
				UDMXControlConsoleFaderBase* SelectedFader = Cast<UDMXControlConsoleFaderBase>(SelectedFaderObject);
				if (SelectedFader && !SelectedFader->GetOwnerFaderGroupChecked().HasFixturePatch())
				{
					// If there's only one fader to delete, replace it in selection
					if (SelectedFadersObjects.Num() == 1)
					{
						SelectionHandler->ReplaceInSelection(SelectedFader);
					}

					constexpr bool bNotifyFaderSelectionChange = false;
					SelectionHandler->RemoveFromSelection(SelectedFader, bNotifyFaderSelectionChange);
					
					SelectedFader->PreEditChange(nullptr);
					SelectedFader->Destroy();
					SelectedFader->PostEditChange();
				}
			}
		}
	}

	SelectionHandler->RemoveInvalidObjectsFromSelection();
}

void FDMXControlConsoleEditorManager::ClearAll()
{
	UDMXControlConsoleData* EditorConsoleData = GetEditorConsoleData();
	if (ensureMsgf(EditorConsoleData, TEXT("Invalid Editor Console Data, cannot clear all its children.")))
	{
		SelectionHandler->ClearSelection();

		const FScopedTransaction ClearAllTransaction(LOCTEXT("ClearAllTransaction", "Clear All"));
		EditorConsoleData->Modify();

		EditorConsoleData->Reset();
	}
}

FDMXControlConsoleEditorManager::FDMXControlConsoleEditorManager()
{
	FCoreDelegates::OnEnginePreExit.AddRaw(this, &FDMXControlConsoleEditorManager::OnEnginePreExit);
}

void FDMXControlConsoleEditorManager::OnEnginePreExit()
{
	UDMXControlConsoleData* EditorConsoleData = GetEditorConsoleData();
	if (EditorConsoleData)
	{
		StopDMX();
	}

	Instance.Reset();
}

#undef LOCTEXT_NAMESPACE
