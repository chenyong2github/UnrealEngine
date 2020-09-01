// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ComputeFramework/ComputeKernelShaderMap.h"

struct FComputeKernelCompilationResults
{
	bool bIsSuccess : 1;

	TArray<FString> CompileWarnings;
	TArray<FString> CompileErrors;

	FComputeKernelCompilationResults()
		: bIsSuccess(false)
	{}
};

class FComputeKernelResource
{
public:
	uint32 GetKernelFlags() const { return Flags; }

	const FComputeKernelCompilationResults& GetCompilationResults() const { return CompilationResults; }

#if WITH_EDITOR
	void CacheShaders(
		EShaderPlatform ShaderPlatform,
		uint32 CompilationFlags
		);
#endif

private:
	FComputeKernelCompilationResults CompilationResults;

	/*
	 * Game thread view of the shader map. The shader map uses deferred deletion so that the rendering thread 
	 * has a chance to process, then release when the shader map is no longer used by rendering thread.
	 * Code that sets this is responsible for updating ShaderMap_RT in a thread safe way.
	 * During an async compile, this will be nullptr.
	 */
	TRefCountPtr<FComputeKernelShaderMap> ShaderMap_GT;

	/**
	 * Render thread view of the shader map. Updates should originate by modifying ShaderMap_GT and then 
	 * propagate to the render thread.
	 */
	FComputeKernelShaderMap* ShaderMap_RT;

	/*
	 * Contains the compiling IDs of this shader map when it is being compiled asynchronously.
	 * This can be used to access the shader map during async compiling.
	 */
	TArray<uint32> InFlightCompilationIds;

	uint32 Flags = 0;

	void CreateShaderMapId(
		FComputeKernelShaderMapId* OutShaderMapId
		) const;

#if WITH_EDITOR
	void BeginCompileShaderMap(
		EShaderPlatform ShaderPlatform,
		uint32 CompilationFlags,
		const FComputeKernelShaderMapId& ShaderMapId,
		TRefCountPtr<FComputeKernelShaderMap>& OutShaderMap
		);
#endif















	
public:
	const FString& GetFriendlyName() const { return FriendlyName; }
	const TCHAR* GetSourceFileName() const { return nullptr; }
	const TCHAR* GetEntryPointName() const { return nullptr; }
	uint32 GetPermutationId() const { return 0; }

	void Invalidate() {}

	TRefCountPtr<FShaderCompilerEnvironment> CreateShaderCompilationEnvironment(EShaderPlatform ShaderPlatform) const 
	{ 
		return new FShaderCompilerEnvironment(); 
	}

	bool ShouldCache(EShaderPlatform ShaderPlatform, const FShaderType* ShaderType) const 
	{ 
		return ShaderType->GetComputeKernelShaderType() != nullptr; 
	}

private:
	FString FriendlyName;
};
