// Copyright Epic Games, Inc. All Rights Reserved.

#include "LegacyVertexDeltaModelModule.h"
#include "Modules/ModuleManager.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "ShaderCore.h"

LEGACYVERTEXDELTAMODEL_API DEFINE_LOG_CATEGORY(LogLegacyVertexDeltaModel)

#define LOCTEXT_NAMESPACE "LegacyVertexDeltaModelModule"

IMPLEMENT_MODULE(UE::LegacyVertexDeltaModel::FLegacyVertexDeltaModelModule, LegacyVertexDeltaModel)

namespace UE::LegacyVertexDeltaModel
{
	void FLegacyVertexDeltaModelModule::StartupModule()
	{
		// Register an additional shader path for our shaders used inside the deformer graph system.
		const FString PluginShaderDir = FPaths::Combine(IPluginManager::Get().FindPlugin(TEXT("LegacyVertexDeltaModel"))->GetBaseDir(), TEXT("Shaders"));
		AddShaderSourceDirectoryMapping(TEXT("/Plugin/LegacyVertexDeltaModel"), PluginShaderDir);
	}
}	// namespace UE::LegacyVertexDeltaModel

#undef LOCTEXT_NAMESPACE
