// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"

#include "PropertyEditorModule.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"

#include "RemoteControlProtocolDMX.h"
#include "RemoteControlProtocolDMXEditorCustomization.h"

/**
 * Remote control protocol DMX editor that allows have editor functionality for the protocol
 */
class FRemoteControlProtocolDMXEditorModule : public IModuleInterface
{
public:
	//~ Begin IModuleInterface interface
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	//~ End IModuleInterface interface
};

void FRemoteControlProtocolDMXEditorModule::StartupModule()
{
	FPropertyEditorModule& PropertyEditorModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");

	PropertyEditorModule.RegisterCustomPropertyTypeLayout(
		FRemoteControlDMXProtocolEntityExtraSetting::StaticStruct()->GetFName(),
		FOnGetPropertyTypeCustomizationInstance::CreateStatic(FRemoteControlProtocolDMXEditorTypeCustomization::MakeInstance));
}

void FRemoteControlProtocolDMXEditorModule::ShutdownModule()
{
	if (UObjectInitialized() && FModuleManager::Get().IsModuleLoaded(TEXT("PropertyEditor")))
	{
		FPropertyEditorModule& PropertyEditorModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
		PropertyEditorModule.UnregisterCustomPropertyTypeLayout(FRemoteControlDMXProtocolEntityExtraSetting::StaticStruct()->GetFName());
	}
}

IMPLEMENT_MODULE(FRemoteControlProtocolDMXEditorModule, RemoteControlProtocolDMXEditorModule);