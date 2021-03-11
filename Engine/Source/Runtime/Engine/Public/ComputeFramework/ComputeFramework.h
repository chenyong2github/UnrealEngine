// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Templates/RefCounting.h"
#include "RHIDefinitions.h"
#include "Shader.h"

class FRDGPooledBuffer;
class FComputeKernelResource;
class FComputeKernelShader;

class UComputeGraph;

extern ENGINE_API bool SupportsComputeFramework(ERHIFeatureLevel::Type FeatureLevel, EShaderPlatform ShaderPlatform);

enum class EComputeExecutionBufferType
{
	Buffer,
	IndexBuffer,
	StructuredBuffer,
	ByteAddressBuffer,
};

struct FComputeExecutionBufferDesc
{
	FName Name;
	uint32 BufferType : 2;
	uint32 Flags : 30;
	uint32 BytesPerElement = 0;
	uint32 ElementCount = 0;
};

struct FComputeExecutionExternalBufferDesc
{
	FName Name;
	TRefCountPtr<FRDGPooledBuffer> Buffer;
};

class FComputeGraph
{
public:
	struct FKernelInvocation
	{
		FName KernelName;
		FName InvocationName;
		FIntVector DispatchDim;
		FComputeKernelResource* Kernel = nullptr;
	};

	void Initialize(UComputeGraph* ComputeGraph);

	TArray<FKernelInvocation> KernelInvocations;
};

class FComputeFramework
{
public:
	struct FShaderInvocation
	{
		FName KernelName;
		FName InvocationName;
		FIntVector DispatchDim;
		const FShaderParametersMetadata* ShaderParamMetadata = nullptr;
		TShaderRef<FComputeKernelShader> Shader;
	};

	void EnqueueForExecution(
		const FComputeGraph* ComputeGraph
		);

	void ExecuteBatches(
		FRHICommandListImmediate& RHICmdList,
		ERHIFeatureLevel::Type FeatureLevel
		);

private:
	TArray<FComputeExecutionExternalBufferDesc> ExternalBuffers;
	TArray<FComputeExecutionBufferDesc> TransientBuffers;
	TArray<FShaderInvocation> ComputeShaders;
};
