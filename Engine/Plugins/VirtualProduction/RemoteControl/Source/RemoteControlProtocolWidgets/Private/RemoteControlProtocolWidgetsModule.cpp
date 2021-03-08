// Copyright Epic Games, Inc. All Rights Reserved.

#include "RemoteControlProtocolWidgetsModule.h"

#include "RemoteControlProtocolBinding.h"
#include "RemoteControlProtocolMappingDetails.h"
#include "RemoteControlPreset.h"
#include "IRemoteControlProtocolModule.h"

#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "IRemoteControlUIModule.h"

#include "Widgets/Layout/SBox.h"
#include "SRemoteControlProtocolWidgetExtension.h"


IMPLEMENT_MODULE(FRemoteControlProtocolWidgetsModule, RemoteControlProtocolWidgets);

void FRemoteControlProtocolWidgetsModule::StartupModule()
{  
	// Extension
	IRemoteControlUIModule::Get().GetRemoteControlFieldExtension().BindLambda([this](const TSharedRef<SBox>& InBox, URemoteControlPreset* InPreset , const FName& InPropertyLabel)
	{
		InBox->SetContent(SNew(SRemoteControlProtocolWidgetExtension)
			.Preset(InPreset)
			.PropertyLabel(InPropertyLabel)
			);
	});
}

void FRemoteControlProtocolWidgetsModule::ShutdownModule()
{
}
