// Copyright Epic Games, Inc. All Rights Reserved.

#include "DMXControlConsoleEditorManager.h"

#include "DMXControlConsole.h"
#include "DMXControlConsoleEditorSelection.h"
#include "DMXControlConsolePreset.h"
#include "Models/DMXControlConsoleEditorPresetModel.h"

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

UDMXControlConsolePreset* FDMXControlConsoleEditorManager::GetPreset() const
{
	UDMXControlConsoleEditorPresetModel* PresetModel = GetMutableDefault<UDMXControlConsoleEditorPresetModel>();
	return PresetModel->GetEditorPreset();
}

UDMXControlConsole* FDMXControlConsoleEditorManager::GetDMXControlConsole() const
{
	if (UDMXControlConsolePreset* Preset = GetPreset())
	{
		return Preset->GetControlConsole();
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

void FDMXControlConsoleEditorManager::SendDMX()
{
	UDMXControlConsole* ControlConsole = GetDMXControlConsole();
	if (ensureMsgf(ControlConsole, TEXT("Invalid DMX Control Console, can't send DMX correctly.")))
	{
		ControlConsole->StartSendingDMX();
	}
}

void FDMXControlConsoleEditorManager::StopDMX()
{
	UDMXControlConsole* ControlConsole = GetDMXControlConsole();
	if (ensureMsgf(ControlConsole, TEXT("Invalid DMX Control Console, can't stop DMX correctly.")))
	{
		ControlConsole->StopSendingDMX();
	}
}

bool FDMXControlConsoleEditorManager::IsSendingDMX() const
{
	UDMXControlConsole* ControlConsole = GetDMXControlConsole();
	if (ensureMsgf(ControlConsole, TEXT("Invalid DMX Control Console, can't stop DMX correctly.")))
	{
		return ControlConsole->IsSendingDMX();
	}
	return false;
}

void FDMXControlConsoleEditorManager::ClearAll()
{
	UDMXControlConsole* ControlConsole = GetDMXControlConsole();
	if (ensureMsgf(ControlConsole, TEXT("Invalid DMX Control Console, can't stop DMX correctly.")))
	{
		SelectionHandler->ClearSelection();

		const FScopedTransaction ClearAllTransaction(LOCTEXT("ClearAllTransaction", "Clear All"));
		ControlConsole->Modify();

		ControlConsole->Reset();
	}
}

FDMXControlConsoleEditorManager::FDMXControlConsoleEditorManager()
{
	FCoreDelegates::OnEnginePreExit.AddRaw(this, &FDMXControlConsoleEditorManager::OnEnginePreExit);
}

void FDMXControlConsoleEditorManager::OnEnginePreExit()
{
	UDMXControlConsole* ControlConsole = GetDMXControlConsole();
	if (ensureMsgf(ControlConsole, TEXT("Invalid DMX Control Console, can't stop DMX correctly.")))
	{
		ControlConsole->StopSendingDMX();
	}

	Instance.Reset();
}

#undef LOCTEXT_NAMESPACE
