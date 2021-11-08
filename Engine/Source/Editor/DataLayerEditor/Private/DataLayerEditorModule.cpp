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
#include "DataLayer/SDataLayerBrowser.h"
#include "DataLayer/DataLayerNameEditSink.h"
#include "WorldPartition/DataLayer/DataLayer.h"

IMPLEMENT_MODULE(FDataLayerEditorModule, DataLayerEditor );

static const FName NAME_ActorDataLayer(TEXT("ActorDataLayer"));

void FDataLayerEditorModule::StartupModule()
{
	FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	PropertyModule.RegisterCustomPropertyTypeLayout(NAME_ActorDataLayer, FOnGetPropertyTypeCustomizationInstance::CreateLambda([] { return MakeShared<FDataLayerPropertyTypeCustomization>(); }));

	FEditorWidgetsModule& EditorWidgetsModule = FModuleManager::LoadModuleChecked<FEditorWidgetsModule>("EditorWidgets");
	EditorWidgetsModule.GetObjectNameEditSinkRegistry()->RegisterObjectNameEditSink(MakeShared<FDataLayerNameEditSink>());
}

void FDataLayerEditorModule::ShutdownModule()
{
	if (FPropertyEditorModule* PropertyModule = FModuleManager::GetModulePtr<FPropertyEditorModule>("PropertyEditor"))
	{
		PropertyModule->UnregisterCustomPropertyTypeLayout(NAME_ActorDataLayer);
	}
}

TSharedRef<SWidget> FDataLayerEditorModule::CreateDataLayerBrowser()
{
	TSharedRef<SWidget> NewDataLayerBrowser = SNew(SDataLayerBrowser);
	DataLayerBrowser = NewDataLayerBrowser;
	return NewDataLayerBrowser;
}

void FDataLayerEditorModule::SyncDataLayerBrowserToDataLayer(const UDataLayer* DataLayer)
{
	if (DataLayerBrowser.IsValid())
	{
		TSharedRef<SDataLayerBrowser> Browser = StaticCastSharedRef<SDataLayerBrowser>(DataLayerBrowser.Pin().ToSharedRef());
		Browser->SyncDataLayerBrowserToDataLayer(DataLayer);
	}
}