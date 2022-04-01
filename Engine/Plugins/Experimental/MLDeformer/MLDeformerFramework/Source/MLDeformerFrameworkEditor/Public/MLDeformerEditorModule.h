// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
#include "MLDeformerModelRegistry.h"

namespace UE::MLDeformer
{
	class FMLDeformerAssetActions;

	class MLDEFORMERFRAMEWORKEDITOR_API FMLDeformerEditorModule
		: public IModuleInterface
	{
	public:
		// IModuleInterface overrides.
		void StartupModule() override;
		void ShutdownModule() override;
		// ~END IModuleInterface overrides.

		FMLDeformerEditorModelRegistry& GetModelRegistry() { return ModelRegistry; }
		const FMLDeformerEditorModelRegistry& GetModelRegistry() const { return ModelRegistry; }

	private:
		FMLDeformerEditorModelRegistry ModelRegistry;
		TSharedPtr<FMLDeformerAssetActions> MLDeformerAssetActions;
	};

}	// namespace UE::MLDeformer
