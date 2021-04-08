// Copyright Epic Games, Inc. All Rights Reserved.

#include "RemoteControlProtocolWidgetsModule.h"

#include "IRemoteControlModule.h"
#include "IRemoteControlProtocol.h"
#include "IRemoteControlProtocolModule.h"
#include "RemoteControlPreset.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Modules/ModuleManager.h"
#include "ViewModels/ProtocolEntityViewModel.h"
#include "Widgets/SRCProtocolBindingList.h"

DEFINE_LOG_CATEGORY(LogRemoteControlProtocolWidgets);

IMPLEMENT_MODULE(FRemoteControlProtocolWidgetsModule, RemoteControlProtocolWidgets);

void FRemoteControlProtocolWidgetsModule::StartupModule()
{
	IAssetRegistry& AssetRegistry = FModuleManager::Get().LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	if(AssetRegistry.IsLoadingAssets())
	{
		AssetRegistry.OnFilesLoaded().AddRaw(this, &FRemoteControlProtocolWidgetsModule::OnAssetsLoaded);
	}
	else
	{
		OnAssetsLoaded();
	}
}

void FRemoteControlProtocolWidgetsModule::ShutdownModule()
{
	if (FAssetRegistryModule* AssetRegistryModule = FModuleManager::GetModulePtr<FAssetRegistryModule>("AssetRegistry"))
	{
		AssetRegistryModule->Get().OnFilesLoaded().RemoveAll(this);
	}
}

void FRemoteControlProtocolWidgetsModule::OnAssetsLoaded()
{
	// Rebinds all protocols (not ideal, current limitation).
	TArray<TSoftObjectPtr<URemoteControlPreset>> Presets;
	IRemoteControlModule::Get().GetPresets(Presets);

	for(TSoftObjectPtr<URemoteControlPreset> PresetPtr : Presets)
	{
		URemoteControlPreset* Preset = PresetPtr.LoadSynchronous();
		for(TWeakPtr<FRemoteControlProperty> ExposedProperty : Preset->GetExposedEntities<FRemoteControlProperty>())
		{
			for(FRemoteControlProtocolBinding& Binding : ExposedProperty.Pin()->ProtocolBinding)
			{
				const TSharedPtr<IRemoteControlProtocol> Protocol = IRemoteControlProtocolModule::Get().GetProtocolByName(Binding.GetProtocolName());
				// Supporting plugin needs to be loaded/protocol available.
				if(Protocol.IsValid())
				{
					Protocol->Bind(Binding.GetRemoteControlProtocolEntityPtr());
				}
			}
		}
	}
}

TSharedRef<SWidget> FRemoteControlProtocolWidgetsModule::GenerateDetailsForEntity(URemoteControlPreset* InPreset, const FGuid& InFieldId)
{
	check(InPreset);
	if(InFieldId.IsValid())
	{
		return SNew(SRCProtocolBindingList, FProtocolEntityViewModel::Create(InPreset, InFieldId));
	}

	return SNullWidget::NullWidget;
}
