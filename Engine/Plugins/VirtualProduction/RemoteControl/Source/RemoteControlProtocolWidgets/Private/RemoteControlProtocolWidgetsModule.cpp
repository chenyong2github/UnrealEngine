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
	// Register Asset Load Event
	FCoreUObjectDelegates::OnAssetLoaded.AddRaw(this, &FRemoteControlProtocolWidgetsModule::OnAssetLoaded);
}

void FRemoteControlProtocolWidgetsModule::ShutdownModule()
{
	// Unregister Asset Load Event
	FCoreUObjectDelegates::OnAssetLoaded.RemoveAll(this);
}

void FRemoteControlProtocolWidgetsModule::OnAssetLoaded(UObject* InAsset)
{
	// @note: workaround for a current limitation that requires rebinding protocols on Preset load
	// Check if asset is a RemoteControlPreset
	if(URemoteControlPreset* Preset = Cast<URemoteControlPreset>(InAsset))
	{
		if(!ensure(IsValid(Preset)))
		{
			UE_LOG(LogRemoteControlProtocolWidgets, Error, TEXT("Attempted to load an invalid Preset: %s"), *InAsset->GetFName().ToString());
			return;
		}
		
		// Iterate over each contained property
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

TSharedRef<SWidget> FRemoteControlProtocolWidgetsModule::GenerateDetailsForEntity(URemoteControlPreset* InPreset, const FGuid& InFieldId, const EExposedFieldType& InFieldType)
{
	check(InPreset);

	// Currently only supports Properties
	if(InFieldId.IsValid() && InFieldType == EExposedFieldType::Property)
	{
		return SNew(SRCProtocolBindingList, FProtocolEntityViewModel::Create(InPreset, InFieldId));
	}

	return SNullWidget::NullWidget;
}
