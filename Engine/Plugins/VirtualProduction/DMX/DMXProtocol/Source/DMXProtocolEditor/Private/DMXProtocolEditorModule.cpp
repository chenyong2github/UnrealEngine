// Copyright Epic Games, Inc. All Rights Reserved.

#include "DMXProtocolEditorModule.h"

#include "DMXProtocolModule.h"
#include "DMXProtocolSettings.h"
#include "IO/DMXInputPortReference.h"
#include "IO/DMXOutputPortReference.h"
#include "DetailsCustomizations/DMXInputPortConfigCustomization.h"
#include "DetailsCustomizations/DMXOutputPortConfigCustomization.h"
#include "DetailsCustomizations/DMXInputPortReferenceCustomization.h"
#include "DetailsCustomizations/DMXOutputPortReferenceCustomization.h"

#include "PropertyEditorModule.h"
#include "Modules/ModuleManager.h"

IMPLEMENT_MODULE( FDMXProtocolEditorModule, DMXProtocolEditor );


#define LOCTEXT_NAMESPACE "DMXProtocolEditorModule"

void FDMXProtocolEditorModule::StartupModule()
{
	FDMXProtocolModule& ProtocolModule = FModuleManager::GetModuleChecked<FDMXProtocolModule>("DMXProtocol");
	ProtocolModule.OnProtocolsRegistered.AddRaw(this, &FDMXProtocolEditorModule::OnProtocolsRegistered);
}

void FDMXProtocolEditorModule::ShutdownModule()
{
	FDMXProtocolModule& ProtocolModule = FModuleManager::GetModuleChecked<FDMXProtocolModule>("DMXProtocol");
	ProtocolModule.OnProtocolsRegistered.RemoveAll(this);

	UnregisterDetailsCustomizations();
}

FDMXProtocolEditorModule& FDMXProtocolEditorModule::Get()
{
	return FModuleManager::GetModuleChecked<FDMXProtocolEditorModule>("DMXProtocolEditor");
}

void FDMXProtocolEditorModule::OnProtocolsRegistered()
{
	RegisterDetailsCustomizations();
}

void FDMXProtocolEditorModule::RegisterDetailsCustomizations()
{
	FPropertyEditorModule& PropertyModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");

	PropertyModule.RegisterCustomPropertyTypeLayout(FDMXInputPortConfig::StaticStruct()->GetFName(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FDMXInputPortConfigCustomization::MakeInstance));
	PropertyModule.RegisterCustomPropertyTypeLayout(FDMXOutputPortConfig::StaticStruct()->GetFName(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FDMXOutputPortConfigCustomization::MakeInstance));
	
	PropertyModule.RegisterCustomPropertyTypeLayout(FDMXInputPortReference::StaticStruct()->GetFName(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FDMXInputPortReferenceCustomization::MakeInstance));
	PropertyModule.RegisterCustomPropertyTypeLayout(FDMXOutputPortReference::StaticStruct()->GetFName(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FDMXOutputPortReferenceCustomization::MakeInstance));

	PropertyModule.NotifyCustomizationModuleChanged();
}

void FDMXProtocolEditorModule::UnregisterDetailsCustomizations()
{
	FPropertyEditorModule& PropertyModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");

	PropertyModule.UnregisterCustomPropertyTypeLayout(FDMXInputPortConfig::StaticStruct()->GetFName());
	PropertyModule.UnregisterCustomPropertyTypeLayout(FDMXOutputPortConfig::StaticStruct()->GetFName());

	PropertyModule.UnregisterCustomPropertyTypeLayout(FDMXInputPortReference::StaticStruct()->GetFName());
	PropertyModule.UnregisterCustomPropertyTypeLayout(FDMXOutputPortReference::StaticStruct()->GetFName());
}

#undef LOCTEXT_NAMESPACE
