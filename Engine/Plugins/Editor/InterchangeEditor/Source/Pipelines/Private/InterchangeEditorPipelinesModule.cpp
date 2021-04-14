// Copyright Epic Games, Inc. All Rights Reserved.

#include "InterchangeEditorPipelinesModule.h"

#include "CoreMinimal.h"
#include "Engine/Engine.h"
#include "InterchangeEditorPipelineDetails.h"
#include "InterchangeGraphInspectorPipeline.h"
#include "InterchangeManager.h"
#include "Misc/CoreDelegates.h"
#include "Modules/ModuleManager.h"
#include "Nodes/InterchangeBaseNode.h"
#include "PropertyEditorModule.h"


class FInterchangeEditorPipelinesModule : public IInterchangeEditorPipelinesModule
{
	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};

IMPLEMENT_MODULE(FInterchangeEditorPipelinesModule, InterchangeEditorPipelines)

void FInterchangeEditorPipelinesModule::StartupModule()
{
	auto RegisterItems = []()
	{
		UInterchangeManager& InterchangeManager = UInterchangeManager::GetInterchangeManager();
	};

	if (GEngine)
	{
		RegisterItems();
	}
	else
	{
		FCoreDelegates::OnPostEngineInit.AddLambda(RegisterItems);
	}

	ClassesToUnregisterOnShutdown.Reset();
	// Register details customizations for animation controller nodes
	FPropertyEditorModule& PropertyEditorModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");

	ClassesToUnregisterOnShutdown.Add(UInterchangeBaseNode::StaticClass()->GetFName());
	PropertyEditorModule.RegisterCustomClassLayout(ClassesToUnregisterOnShutdown.Last(), FOnGetDetailCustomizationInstance::CreateStatic(&FInterchangeBaseNodeDetailsCustomization::MakeInstance));
}


void FInterchangeEditorPipelinesModule::ShutdownModule()
{
	FPropertyEditorModule* PropertyEditorModule = FModuleManager::GetModulePtr<FPropertyEditorModule>("PropertyEditor");
	if (PropertyEditorModule)
	{
		for (FName ClassName : ClassesToUnregisterOnShutdown)
		{
			PropertyEditorModule->UnregisterCustomClassLayout(ClassName);
		}
	}
}



