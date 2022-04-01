// Copyright Epic Games, Inc. All Rights Reserved.

#include "VertexDeltaModelModule.h"
#include "Modules/ModuleManager.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "ShaderCore.h"

VERTEXDELTAMODEL_API DEFINE_LOG_CATEGORY(LogVertexDeltaModel)

#define LOCTEXT_NAMESPACE "VertexDeltaModelModule"

IMPLEMENT_MODULE(UE::VertexDeltaModel::FVertexDeltaModelModule, VertexDeltaModel)

namespace UE::VertexDeltaModel
{
	void FVertexDeltaModelModule::StartupModule()
	{
		// Register an additional shader path for our shaders used inside the deformer graph system.
		const FString PluginShaderDir = FPaths::Combine(IPluginManager::Get().FindPlugin(TEXT("VertexDeltaModel"))->GetBaseDir(), TEXT("Shaders"));
		AddShaderSourceDirectoryMapping(TEXT("/Plugin/VertexDeltaModel"), PluginShaderDir);
	}
}	// namespace UE::VertexDeltaModel

#undef LOCTEXT_NAMESPACE
