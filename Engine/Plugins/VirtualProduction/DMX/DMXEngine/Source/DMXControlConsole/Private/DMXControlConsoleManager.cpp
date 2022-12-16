// Copyright Epic Games, Inc. All Rights Reserved.

#include "DMXControlConsoleManager.h"

#include "DMXEditorModule.h"
#include "DMXControlConsole.h"
#include "DMXControlConsoleSelection.h"
#include "Commands/DMXControlConsoleCommands.h"

#include "ScopedTransaction.h"
#include "Framework/Commands/GenericCommands.h"
#include "Framework/Commands/UICommandList.h"


#define LOCTEXT_NAMESPACE "DMXControlConsole"

TSharedPtr<FDMXControlConsoleManager> FDMXControlConsoleManager::Instance;

FDMXControlConsoleManager::FDMXControlConsoleManager()
{
	ControlConsole = NewObject<UDMXControlConsole>(GetTransientPackage(), NAME_None, RF_Transactional);

	FDMXControlConsoleCommands::Register();
	ControlConsoleCommands = MakeShareable(new FUICommandList());

	FCoreDelegates::OnEnginePreExit.AddRaw(this, &FDMXControlConsoleManager::Destroy);
}

FDMXControlConsoleManager::~FDMXControlConsoleManager()
{
	FCoreDelegates::OnEnginePreExit.RemoveAll(this);
}

void FDMXControlConsoleManager::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(ControlConsole);
}

FDMXControlConsoleManager& FDMXControlConsoleManager::Get()
{
	if (!Instance.IsValid())
	{
		Instance = MakeShareable<FDMXControlConsoleManager>(new FDMXControlConsoleManager());
	}
	checkf(Instance.IsValid() && Instance->ControlConsole, TEXT("Unexpected: Invalid DMX Control Console manager instance, or DMX Control Console is null."));

	return *Instance.Get();
}

TSharedRef<FDMXControlConsoleSelection> FDMXControlConsoleManager::GetSelectionHandler()
{
	if (!SelectionHandler.IsValid())
	{
		SelectionHandler = MakeShareable(new FDMXControlConsoleSelection(AsShared()));
	}

	return SelectionHandler.ToSharedRef();
}

void FDMXControlConsoleManager::SetupCommands()
{
	if (!ensureAlwaysMsgf(ControlConsole, TEXT("Invalid DMX Control Console, can't setup commands correctly.")))
	{
		return;
	}

	ControlConsoleCommands->MapAction
	(
		FDMXControlConsoleCommands::Get().PlayDMX,
		FExecuteAction::CreateUObject(ControlConsole, &UDMXControlConsole::PlayDMX),
		FCanExecuteAction::CreateLambda([this]
			{
				return !ControlConsole->IsPlayingDMX();
			}),
		FIsActionChecked(),
		FIsActionButtonVisible::CreateLambda([this]
			{
				return !ControlConsole->IsPlayingDMX();
			})
	);

	ControlConsoleCommands->MapAction
	(
		FDMXControlConsoleCommands::Get().StopDMX,
		FExecuteAction::CreateUObject(ControlConsole, &UDMXControlConsole::StopDMX),
		FCanExecuteAction::CreateLambda([this]
			{
				return ControlConsole->IsPlayingDMX();
			}),
		FIsActionChecked(),
		FIsActionButtonVisible::CreateLambda([this]
			{
				return ControlConsole->IsPlayingDMX();
			})
	);

	ControlConsoleCommands->MapAction
	(
		FDMXControlConsoleCommands::Get().ClearAll,
		FExecuteAction::CreateSP(this, &FDMXControlConsoleManager::ClearAll)
	);
}

void FDMXControlConsoleManager::Destroy()
{
	ControlConsole->StopDMX();

	Instance.Reset();
}

void FDMXControlConsoleManager::ClearAll()
{
	if (!ensureMsgf(ControlConsole, TEXT("Invalid DMX Control Console, can't clear fader group rows correctly.")))
	{
		return;
	}

	SelectionHandler->ClearSelection();

	const FScopedTransaction ControlConsoleTransaction(LOCTEXT("ControlConsoleTransaction", "Clear All"));
	ControlConsole->Modify();

	ControlConsole->Reset();
}

#undef LOCTEXT_NAMESPACE
