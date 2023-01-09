// Copyright Epic Games, Inc. All Rights Reserved.

#include "DMXControlConsoleEditorManager.h"

#include "DMXEditorModule.h"
#include "DMXControlConsole.h"
#include "DMXControlConsoleEditorSelection.h"
#include "DMXControlConsolePreset.h"

#include "AssetToolsModule.h"
#include "ScopedTransaction.h"
#include "AssetRegistry/AssetRegistryModule.h"


#define LOCTEXT_NAMESPACE "DMXControlConsoleEditorManager"

TSharedPtr<FDMXControlConsoleEditorManager> FDMXControlConsoleEditorManager::Instance;

FDMXControlConsoleEditorManager::~FDMXControlConsoleEditorManager()
{
	FCoreDelegates::OnEnginePreExit.RemoveAll(this);
}

void FDMXControlConsoleEditorManager::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(ControlConsole);
}

FDMXControlConsoleEditorManager& FDMXControlConsoleEditorManager::Get()
{
	if (!Instance.IsValid())
	{
		Instance = MakeShareable<FDMXControlConsoleEditorManager>(new FDMXControlConsoleEditorManager());
	}
	checkf(Instance.IsValid() && Instance->ControlConsole, TEXT("Unexpected: Invalid DMX Control Console manager instance, or DMX Control Console is null."));

	return *Instance.Get();
}

TSharedRef<FDMXControlConsoleEditorSelection> FDMXControlConsoleEditorManager::GetSelectionHandler()
{
	if (!SelectionHandler.IsValid())
	{
		SelectionHandler = MakeShareable(new FDMXControlConsoleEditorSelection(AsShared()));
	}

	return SelectionHandler.ToSharedRef();
}

UDMXControlConsolePreset* FDMXControlConsoleEditorManager::CreateNewPreset(const FString& InAssetPath, const FString& InAssetName)
{
	FString PackageName;
	FString AssetName;

	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
	AssetToolsModule.Get().CreateUniqueAssetName(InAssetPath / InAssetName, TEXT(""), PackageName, AssetName);

	UPackage* Package = CreatePackage(*PackageName);
	check(Package);
	Package->FullyLoad();

	UDMXControlConsolePreset* NewPreset = NewObject<UDMXControlConsolePreset>(Package, FName(AssetName), RF_Public | RF_Standalone | RF_Transactional);
	UDMXControlConsole* CurrentControlConsole = FDMXControlConsoleEditorManager::Get().GetDMXControlConsole();
	NewPreset->SetControlConsole(CurrentControlConsole);

	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	FAssetRegistryModule::AssetCreated(NewPreset);
	Package->MarkPackageDirty();

	return NewPreset;
}

void FDMXControlConsoleEditorManager::LoadFromPreset(const UDMXControlConsolePreset* Preset)
{
	if (!Preset)
	{
		return;
	}

	UDMXControlConsole* NewControlConsole = Preset->GetControlConsole();
	if (!NewControlConsole || NewControlConsole == ControlConsole)
	{
		return;
	}

	ControlConsole->StopDMX();
	ControlConsole = NewControlConsole;

	OnControlConsoleLoaded.Broadcast();
}

void FDMXControlConsoleEditorManager::SendDMX()
{
	if (!ensureMsgf(ControlConsole, TEXT("Invalid DMX Control Console, can't send DMX correctly.")))
	{
		return;
	}

	ControlConsole->SendDMX();
}

void FDMXControlConsoleEditorManager::StopDMX()
{
	if (!ensureMsgf(ControlConsole, TEXT("Invalid DMX Control Console, can't stop DMX correctly.")))
	{
		return;
	}

	ControlConsole->StopDMX();
}

bool FDMXControlConsoleEditorManager::IsSendingDMX() const
{
	return IsValid(ControlConsole) ? ControlConsole->IsSendingDMX() : false;
}

void FDMXControlConsoleEditorManager::ClearAll()
{
	if (!ensureMsgf(ControlConsole, TEXT("Invalid DMX Control Console, can't clear fader group rows correctly.")))
	{
		return;
	}

	SelectionHandler->ClearSelection();

	const FScopedTransaction ClearAllTransaction(LOCTEXT("ClearAllTransaction", "Clear All"));
	ControlConsole->Modify();

	ControlConsole->Reset();
}

FDMXControlConsoleEditorManager::FDMXControlConsoleEditorManager()
{
	ControlConsole = NewObject<UDMXControlConsole>(GetTransientPackage(), NAME_None, RF_Transactional);

	FCoreDelegates::OnEnginePreExit.AddRaw(this, &FDMXControlConsoleEditorManager::Destroy);
}

void FDMXControlConsoleEditorManager::Destroy()
{
	ControlConsole->StopDMX();

	Instance.Reset();
}

#undef LOCTEXT_NAMESPACE
