// Copyright Epic Games, Inc. All Rights Reserved.

#include "DataLayerEditorModule.h"
#include "PropertyEditorModule.h"
#include "Widgets/SWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Editor.h"
#include "Modules/ModuleManager.h"
#include "DataLayer/DataLayerPropertyTypeCustomization.h"
#include "DataLayer/SDataLayerBrowser.h"
#include "WorldPartition/DataLayer/DataLayer.h"

IMPLEMENT_MODULE(FDataLayerEditorModule, DataLayerEditor );

static const FName NAME_ActorDataLayer(TEXT("ActorDataLayer"));

void FDataLayerEditorModule::StartupModule()
{
	FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	PropertyModule.RegisterCustomPropertyTypeLayout(NAME_ActorDataLayer, FOnGetPropertyTypeCustomizationInstance::CreateLambda([] { return MakeShared<FDataLayerPropertyTypeCustomization>(); }));
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
	return SNew(SDataLayerBrowser);
}