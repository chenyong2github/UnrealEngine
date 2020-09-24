// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RHIDefinitions.h"
#include "Shader.h"
#include "Templates/RefCounting.h"

class FComputeKernelShader;

extern ENGINE_API bool SupportsComputeFramework(ERHIFeatureLevel::Type FeatureLevel, EShaderPlatform ShaderPlatform);

class FRDGPooledBuffer;
struct FComputeExecutionExternalBufferDesc
{
	FName Name;
	TRefCountPtr<FRDGPooledBuffer> Buffer;
};

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

struct FComputeExecutionShader
{
	FName KernelName;
	FName InvocationName;
	FIntVector DispatchDim;
	TShaderRef<FComputeKernelShader> Shader;
};

class FComputeFramework
{
public:
	void ExecuteBatches(
		FRHICommandListImmediate& RHICmdList
		);

private:
	TArray<FComputeExecutionExternalBufferDesc> ExternalBuffers;
	TArray<FComputeExecutionBufferDesc> TransientBuffers;
	TArray<FComputeExecutionShader> ComputeShaders;
};
