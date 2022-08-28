// Copyright Epic Games, Inc. All Rights Reserved.

#include "GLTFExporterModule.h"

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "Interfaces/IPluginManager.h"

DEFINE_LOG_CATEGORY(LogGLTFExporter);

/**
 * glTF Exporter module implementation (private)
 */
class FGLTFExporterModule final : public IGLTFExporterModule
{

public:
	virtual void StartupModule() override
	{
		FString PluginShaderDir = FPaths::Combine(IPluginManager::Get().FindPlugin(TEXT("GLTFExporter"))->GetBaseDir(), TEXT("Shaders"));
		AddShaderSourceDirectoryMapping(TEXT("/Plugin/GLTFExporter"), PluginShaderDir);
	}

	virtual void ShutdownModule() override {}
};

IMPLEMENT_MODULE(FGLTFExporterModule, GLTFExporter);
