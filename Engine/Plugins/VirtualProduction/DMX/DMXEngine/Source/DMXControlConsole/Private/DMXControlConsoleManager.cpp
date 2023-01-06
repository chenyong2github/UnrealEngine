// Copyright Epic Games, Inc. All Rights Reserved.

#include "DMXControlConsoleManager.h"

#include "DMXEditorModule.h"
#include "DMXControlConsole.h"
#include "DMXControlConsolePreset.h"
#include "DMXControlConsoleSelection.h"

#include "AssetToolsModule.h"
#include "ScopedTransaction.h"
#include "AssetRegistry/AssetRegistryModule.h"


#define LOCTEXT_NAMESPACE "DMXControlConsole"

TSharedPtr<FDMXControlConsoleManager> FDMXControlConsoleManager::Instance;

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

UDMXControlConsolePreset* FDMXControlConsoleManager::CreateNewPreset(const FString& InAssetPath, const FString& InAssetName)
{
	FString PackageName;
	FString AssetName;

	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
	AssetToolsModule.Get().CreateUniqueAssetName(InAssetPath / InAssetName, TEXT(""), PackageName, AssetName);

	UPackage* Package = CreatePackage(*PackageName);
	check(Package);
	Package->FullyLoad();

	UDMXControlConsolePreset* NewPreset = NewObject<UDMXControlConsolePreset>(Package, FName(AssetName), RF_Public | RF_Standalone | RF_Transactional);
	UDMXControlConsole* CurrentControlConsole = FDMXControlConsoleManager::Get().GetDMXControlConsole();
	NewPreset->SetControlConsole(CurrentControlConsole);

	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	FAssetRegistryModule::AssetCreated(NewPreset);
	Package->MarkPackageDirty();

	return NewPreset;
}

void FDMXControlConsoleManager::LoadFromPreset(const UDMXControlConsolePreset* Preset)
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

void FDMXControlConsoleManager::SendDMX()
{
	if (!ensureMsgf(ControlConsole, TEXT("Invalid DMX Control Console, can't send DMX correctly.")))
	{
		return;
	}

	ControlConsole->SendDMX();
}

void FDMXControlConsoleManager::StopDMX()
{
	if (!ensureMsgf(ControlConsole, TEXT("Invalid DMX Control Console, can't stop DMX correctly.")))
	{
		return;
	}

	ControlConsole->StopDMX();
}

bool FDMXControlConsoleManager::IsSendingDMX() const
{
	return IsValid(ControlConsole) ? ControlConsole->IsSendingDMX() : false;
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

FDMXControlConsoleManager::FDMXControlConsoleManager()
{
	ControlConsole = NewObject<UDMXControlConsole>(GetTransientPackage(), NAME_None, RF_Transactional);

	FCoreDelegates::OnEnginePreExit.AddRaw(this, &FDMXControlConsoleManager::Destroy);
}

void FDMXControlConsoleManager::Destroy()
{
	ControlConsole->StopDMX();

	Instance.Reset();
}

#undef LOCTEXT_NAMESPACE
