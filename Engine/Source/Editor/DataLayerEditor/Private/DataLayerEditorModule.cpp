// Copyright Epic Games, Inc. All Rights Reserved.

#include "DataLayerEditorModule.h"
#include "PropertyEditorModule.h"
#include "EditorWidgetsModule.h"
#include "ObjectNameEditSinkRegistry.h"
#include "Widgets/SWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Editor.h"
#include "Modules/ModuleManager.h"
#include "DataLayer/DataLayerPropertyTypeCustomization.h"
#include "DataLayer/DataLayerInstanceCustomization.h"
#include "DataLayer/SDataLayerBrowser.h"
#include "DataLayer/DataLayerNameEditSink.h"
#include "DataLayer/DataLayerEditorSubsystem.h"
#include "WorldPartition/DataLayer/DataLayerInstance.h"

IMPLEMENT_MODULE(FDataLayerEditorModule, DataLayerEditor );

static const FName NAME_ActorDataLayer(TEXT("ActorDataLayer"));
static const FName NAME_DataLayerInstance(TEXT("DataLayerInstance"));

void FDataLayerEditorModule::StartupModule()
{
	FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	PropertyModule.RegisterCustomPropertyTypeLayout(NAME_ActorDataLayer, FOnGetPropertyTypeCustomizationInstance::CreateLambda([] { return MakeShared<FDataLayerPropertyTypeCustomization>(); }));
	PropertyModule.RegisterCustomClassLayout(NAME_DataLayerInstance, FOnGetDetailCustomizationInstance::CreateStatic(&FDataLayerInstanceDetails::MakeInstance));

	FEditorWidgetsModule& EditorWidgetsModule = FModuleManager::LoadModuleChecked<FEditorWidgetsModule>("EditorWidgets");
	EditorWidgetsModule.GetObjectNameEditSinkRegistry()->RegisterObjectNameEditSink(MakeShared<FDataLayerNameEditSink>());
}

void FDataLayerEditorModule::ShutdownModule()
{
	if (FPropertyEditorModule* PropertyModule = FModuleManager::GetModulePtr<FPropertyEditorModule>("PropertyEditor"))
	{
		PropertyModule->UnregisterCustomPropertyTypeLayout(NAME_ActorDataLayer);
		PropertyModule->UnregisterCustomPropertyTypeLayout(NAME_DataLayerInstance);
	}
}

TSharedRef<SWidget> FDataLayerEditorModule::CreateDataLayerBrowser()
{
	TSharedRef<SWidget> NewDataLayerBrowser = SNew(SDataLayerBrowser);
	DataLayerBrowser = NewDataLayerBrowser;
	return NewDataLayerBrowser;
}

void FDataLayerEditorModule::SyncDataLayerBrowserToDataLayer(const UDataLayerInstance* DataLayerInstance)
{
	if (DataLayerBrowser.IsValid())
	{
		TSharedRef<SDataLayerBrowser> Browser = StaticCastSharedRef<SDataLayerBrowser>(DataLayerBrowser.Pin().ToSharedRef());
		Browser->SyncDataLayerBrowserToDataLayer(DataLayerInstance);
	}
}

bool FDataLayerEditorModule::AddActorToDataLayers(AActor* Actor, const TArray<UDataLayerInstance*>& DataLayers)
{
	return UDataLayerEditorSubsystem::Get()->AddActorToDataLayers(Actor, DataLayers);
}