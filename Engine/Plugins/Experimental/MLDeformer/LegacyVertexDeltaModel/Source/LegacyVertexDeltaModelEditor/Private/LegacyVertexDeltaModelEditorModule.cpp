// Copyright Epic Games, Inc. All Rights Reserved.

#include "LegacyVertexDeltaModelEditorModule.h"
#include "MLDeformerEditorModule.h"
#include "LegacyVertexDeltaModelVizSettingsDetails.h"
#include "LegacyVertexDeltaModelDetails.h"
#include "LegacyVertexDeltaEditorModel.h"
#include "LegacyVertexDeltaModel.h"
#include "Modules/ModuleManager.h"
#include "EditorModeRegistry.h"

#define LOCTEXT_NAMESPACE "LegacyVertexDeltaModelEditorModule"

IMPLEMENT_MODULE(UE::LegacyVertexDeltaModel::FLegacyVertexDeltaModelEditorModule, LegacyVertexDeltaModelEditor)

namespace UE::LegacyVertexDeltaModel
{
	using namespace UE::MLDeformer;

	void FLegacyVertexDeltaModelEditorModule::StartupModule()
	{
		// Register object detail customizations.
		FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
		PropertyModule.RegisterCustomClassLayout("LegacyVertexDeltaModelVizSettings", FOnGetDetailCustomizationInstance::CreateStatic(&FLegacyVertexDeltaModelVizSettingsDetails::MakeInstance));
		PropertyModule.RegisterCustomClassLayout("LegacyVertexDeltaModel", FOnGetDetailCustomizationInstance::CreateStatic(&FLegacyVertexDeltaModelDetails::MakeInstance));
		PropertyModule.NotifyCustomizationModuleChanged();

		// Register our custom ML deformer model to the model registry in the ML Deformer Framework.
		FMLDeformerEditorModule& EditorModule = FModuleManager::LoadModuleChecked<FMLDeformerEditorModule>("MLDeformerFrameworkEditor");
		FMLDeformerEditorModelRegistry& ModelRegistry = EditorModule.GetModelRegistry();
		ModelRegistry.RegisterEditorModel(ULegacyVertexDeltaModel::StaticClass(), FOnGetEditorModelInstance::CreateStatic(&FLegacyVertexDeltaEditorModel::MakeInstance), /*ModelPriority*/0);
	}

	void FLegacyVertexDeltaModelEditorModule::ShutdownModule()
	{
		// Unregister our ML Deformer model.
		if (FModuleManager::Get().IsModuleLoaded(TEXT("MLDeformerFrameworkEditor")))
		{
			FMLDeformerEditorModule& EditorModule = FModuleManager::GetModuleChecked<FMLDeformerEditorModule>("MLDeformerFrameworkEditor");
			FMLDeformerEditorModelRegistry& ModelRegistry = EditorModule.GetModelRegistry();
			ModelRegistry.UnregisterEditorModel(ULegacyVertexDeltaModel::StaticClass());
		}

		// Unregister object detail customizations for this model.
		if (FModuleManager::Get().IsModuleLoaded(TEXT("PropertyEditor")))
		{
			FPropertyEditorModule& PropertyModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));
			PropertyModule.UnregisterCustomClassLayout(TEXT("LegacyVertexDeltaModelVizSettings"));
			PropertyModule.UnregisterCustomClassLayout(TEXT("LegacyVertexDeltaModel"));
			PropertyModule.NotifyCustomizationModuleChanged();
		}
	}

}	// namespace UE::LegacyVertexDeltaModel

#undef LOCTEXT_NAMESPACE
