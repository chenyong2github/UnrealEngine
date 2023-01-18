// Copyright Epic Games, Inc. All Rights Reserved.

#include "DMXControlConsoleEditorManager.h"

#include "DMXControlConsole.h"
#include "DMXControlConsoleEditorSelection.h"
#include "DMXControlConsoleEditorSettings.h"
#include "DMXControlConsolePreset.h"

#include "AssetToolsModule.h"
#include "ScopedTransaction.h"
#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Misc/CoreDelegates.h"


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

UDMXControlConsolePreset* FDMXControlConsoleEditorManager::GetDefaultControlConsolePreset() const
{
	const UDMXControlConsoleEditorSettings* ControlConsoleEditorSettings = GetDefault<UDMXControlConsoleEditorSettings>();
	if (!ControlConsoleEditorSettings)
	{
		return nullptr;
	}

	FSoftObjectPath DefaultControlConsoleAssetPath = ControlConsoleEditorSettings->DefaultControlConsoleAssetPath;
	if (!DefaultControlConsoleAssetPath.IsValid())
	{
		return nullptr;
	}

	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	FAssetData DefaultControlConsoleAsset = AssetRegistryModule.Get().GetAssetByObjectPath(DefaultControlConsoleAssetPath);
	if (!DefaultControlConsoleAsset.IsValid())
	{
		return nullptr;
	}

	return Cast<UDMXControlConsolePreset>(DefaultControlConsoleAsset.GetAsset());
}

void FDMXControlConsoleEditorManager::SetDefaultControlConsolePreset(const FSoftObjectPath& PresetAssetPath)
{
	if (!PresetAssetPath.IsValid())
	{
		return;
	}

	UDMXControlConsoleEditorSettings* ControlConsoleEditorSettings = GetMutableDefault<UDMXControlConsoleEditorSettings>();
	if (!ControlConsoleEditorSettings)
	{
		return;
	}

	ControlConsoleEditorSettings->DefaultControlConsoleAssetPath = PresetAssetPath;
	ControlConsoleEditorSettings->SaveConfig();
}

UDMXControlConsole* FDMXControlConsoleEditorManager::CreateNewTransientConsole()
{
	if (ControlConsole)
	{
		// Don't create the new console if the current one is already transient
		if (ControlConsole->GetPackage() == GetTransientPackage())
		{
			return nullptr;
		}

		ControlConsole->StopSendingDMX();
	}

	ControlConsole = NewObject<UDMXControlConsole>(GetTransientPackage(), NAME_None, RF_Transactional);
	OnControlConsoleLoaded.Broadcast();

	return ControlConsole;
}

UDMXControlConsolePreset* FDMXControlConsoleEditorManager::CreateNewPreset(const FString& InAssetPath, const FString& InAssetName, UDMXControlConsole* InControlConsole)
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
	if (InControlConsole)
	{
		CurrentControlConsole = InControlConsole;
	}

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

	ControlConsole->StopSendingDMX();
	ControlConsole = NewControlConsole;

	OnControlConsoleLoaded.Broadcast();
}

void FDMXControlConsoleEditorManager::SendDMX()
{
	if (!ensureMsgf(ControlConsole, TEXT("Invalid DMX Control Console, can't send DMX correctly.")))
	{
		return;
	}

	ControlConsole->StartSendingDMX();
}

void FDMXControlConsoleEditorManager::StopDMX()
{
	if (!ensureMsgf(ControlConsole, TEXT("Invalid DMX Control Console, can't stop DMX correctly.")))
	{
		return;
	}

	ControlConsole->StopSendingDMX();
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
	CreateNewTransientConsole();

	FCoreDelegates::OnEnginePreExit.AddRaw(this, &FDMXControlConsoleEditorManager::Destroy);
}

void FDMXControlConsoleEditorManager::Destroy()
{
	ControlConsole->StopSendingDMX();

	Instance.Reset();
}

#undef LOCTEXT_NAMESPACE
