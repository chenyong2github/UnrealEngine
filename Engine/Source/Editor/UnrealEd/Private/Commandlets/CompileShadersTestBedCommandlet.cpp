// Copyright Epic Games, Inc. All Rights Reserved.

#include "Commandlets/CompileShadersTestBedCommandlet.h"
#include "GlobalShader.h"
#include "Materials/Material.h"
#include "ShaderCompiler.h"

DEFINE_LOG_CATEGORY_STATIC(LogCompileShadersTestBedCommandlet, Log, All);

UCompileShadersTestBedCommandlet::UCompileShadersTestBedCommandlet(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

int32 UCompileShadersTestBedCommandlet::Main(const FString& Params)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UCompileShadersTestBedCommandlet::Main);

	TArray<FString> Tokens;
	TArray<FString> Switches;
	TMap<FString, FString> ParamVals;
	UCommandlet::ParseCommandLine(*Params, Tokens, Switches, ParamVals);

	// Display help
	if (Switches.Contains("help"))
	{
		UE_LOG(LogCompileShadersTestBedCommandlet, Log, TEXT("CompileShadersTestBed"));
		UE_LOG(LogCompileShadersTestBedCommandlet, Log, TEXT("This commandlet compiles global and default material shaders.  Used to profile and test shader compilation."));
		return 0;
	}

	PRIVATE_GAllowCommandletRendering = true;

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(DefaultMaterials);

		for (int32 Domain = 0; Domain < MD_MAX; ++Domain)
		{
			UMaterial::GetDefaultMaterial(static_cast<EMaterialDomain>(Domain))->CacheShaders(EMaterialShaderPrecompileMode::Background);
		}
		GShaderCompilingManager->FinishAllCompilation();
	}

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(GlobalShaders);

		CompileGlobalShaderMap(true);

		TArray<int32> ShaderMapIds;
		ShaderMapIds.Add(GlobalShaderMapId);
		GShaderCompilingManager->FinishCompilation(TEXT("Global"), ShaderMapIds);
	}

	return 0;
}
