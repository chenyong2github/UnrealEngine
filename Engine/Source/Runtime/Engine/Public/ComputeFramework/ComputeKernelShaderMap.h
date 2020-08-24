// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RHI.h"
#include "RHIDefinitions.h"
#include "Serialization/MemoryImage.h"
#include "Shader.h"


class FComputeKernelResource;

/* Stores all the output/stats from the compilation process, eg. error messages, error codes. */
class FComputeKernelCompilationOutput
{
	DECLARE_TYPE_LAYOUT(FComputeKernelCompilationOutput, NonVirtual);

public:
};

/** Contains all the information needed to uniquely identify a FComputeKernelShaderMap. */
class FComputeKernelShaderMapId
{
	DECLARE_TYPE_LAYOUT(FComputeKernelShaderMapId, NonVirtual);

public:
	LAYOUT_FIELD(ERHIFeatureLevel::Type, FeatureLevel);
	LAYOUT_FIELD(TMemoryImageArray<FMemoryImageString>, AdditionalDefines);
	LAYOUT_FIELD_EDITORONLY(FMemoryImageString, FriendlyName);

	FComputeKernelShaderMapId()
		: FeatureLevel(GMaxRHIFeatureLevel)
	{
	}

	SIZE_T GetSizeBytes() const
	{
		return sizeof(*this);
	}

#if WITH_EDITORONLY_DATA
	const FMemoryImageString& GetFriendlyName() const { return FriendlyName; }
#endif
};

class FComputeKernelShaderMapContent : public FShaderMapContent
{
	DECLARE_TYPE_LAYOUT(FComputeKernelShaderMapContent, NonVirtual);
	friend class FComputeKernelShaderMap;

public:
	using Super = FShaderMapContent;

	explicit FComputeKernelShaderMapContent(EShaderPlatform InPlatform = EShaderPlatform::SP_NumPlatforms) 
		: FShaderMapContent(InPlatform) 
	{
	}

private:
	LAYOUT_FIELD(FComputeKernelCompilationOutput, CompilationOutput);
	LAYOUT_FIELD(FSHAHash, ShaderContentHash);
	LAYOUT_FIELD(FComputeKernelShaderMapId, ShaderMapId);

	LAYOUT_FIELD_EDITORONLY(FMemoryImageString, FriendlyName);
	LAYOUT_FIELD_EDITORONLY(FMemoryImageString, DebugDescription);
};

class FComputeKernelShaderMap : public TShaderMap<FComputeKernelShaderMapContent, FShaderMapPointerTable>, public FDeferredCleanupInterface
{
public:
	static FComputeKernelShaderMap* Find(
		EShaderPlatform ShaderPlatform, 
		const FComputeKernelShaderMapId& ShaderMapId
		) 
	{ 
		return nullptr; 
	}

	static void LoadFromDerivedDataCache(
		EShaderPlatform ShaderPlatform, 
		const FComputeKernelShaderMapId& ShaderMapId, 
		FComputeKernelResource* KernelShader, 
		TRefCountPtr<FComputeKernelShaderMap>& InOutGameThreadShaderMap
		);

	/*
	 * Checks to see if the shader map is already being compiled, and if so
	 * add this to the list to be applied once the compile finishes.
	 * Returns true if the shader map was being compiled and was added.
	 */
	bool TryToAddToExistingCompilationTask(FComputeKernelResource* KernelShader);




	bool IsValid() { return false; }

#if WITH_EDITORONLY_DATA
	const FMemoryImageString& GetFriendlyName() const { return GetContent()->FriendlyName; }
#endif

	void Compile(
		EShaderPlatform ShaderPlatform,
		FComputeKernelResource* KernelShader,
		const FComputeKernelShaderMapId& ShaderMapId,
		bool bSynchronousCompile
		);

	uint32 GetCompilingId() const { return CompilationRequestId; }
	bool CompiledSuccessfully() const { return bCompiledSuccessfully; }
	bool IsComplete() const { return false; }

private:
	/* Tracks resources and their shader map compilations that are in flight. */
	static TMap<TRefCountPtr<FComputeKernelShaderMap>, TArray<const FComputeKernelResource*>> ComputeKernelShaderMapsBeingCompiled;

	/* 
	 * Uniquely identifies this shader map during compilation. Needed for deferred compilation
	 * where many shader compilations are running concurrently owned by different shader maps. 
	 */
	uint32 CompilationRequestId;

	bool bCompiledSuccessfully : 1;
};
