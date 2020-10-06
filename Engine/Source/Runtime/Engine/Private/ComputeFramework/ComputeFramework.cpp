// Copyright Epic Games, Inc. All Rights Reserved.

#include "ComputeFramework/ComputeFramework.h"
#include "ComputeFramework/ComputeKernel.h"
#include "ComputeFramework/ComputeGraph.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RHIDefinitions.h"
#include "ComputeFramework/ComputeKernelShaderMap.h"
#include "ComputeFramework/ComputeKernelShader.h"
#include "ProfilingDebugging/RealtimeGPUProfiler.h"

DECLARE_GPU_STAT_NAMED(ComputeFramework_ExecuteBatches, TEXT("ComputeFramework::ExecuteBatches"));

static int32 GComputeFrameworkMode = 0;
static FAutoConsoleVariableRef CVarComputeFrameworkMode(
	TEXT("r.ComputeFramework.mode"),
	GComputeFrameworkMode,
	TEXT("The mode Compute Framework should operate.\n")
	TEXT("    0: disabled (Default)\n")
	TEXT("    1: enabled\n"),
	ECVF_Scalability | ECVF_RenderThreadSafe
	);

bool SupportsComputeFramework(ERHIFeatureLevel::Type FeatureLevel, EShaderPlatform ShaderPlatform)
{
	return GComputeFrameworkMode > 0
		&& FeatureLevel >= ERHIFeatureLevel::SM5
		&& FDataDrivenShaderPlatformInfo::GetSupportsComputeFramework(ShaderPlatform);
}

void FComputeGraph::Initialize(UComputeGraph* ComputeGraph)
{
	for (auto& Invocation : ComputeGraph->GetShaderInvocationList())
	{
		FKernelInvocation KernelInvocation = {
			Invocation.ComputeKernel ? Invocation.ComputeKernel->GetFName() : "NULL Kernel",
			FName("InvocationName"),
			FIntVector(1, 1, 1),
			Invocation.ComputeKernel ? Invocation.ComputeKernel->GetResource() : nullptr
			};

		KernelInvocations.Emplace(KernelInvocation);
	}
}

void FComputeFramework::EnqueueForExecution(const FComputeGraph* ComputeGraph)
{
	//ExternalBuffers.Append(ComputeGraph.ExternalBuffers);
	//TransientBuffers.Append(ComputeGraph.TransientBuffers);

	for (auto& Invocation : ComputeGraph->KernelInvocations)
	{
		FShaderInvocation ShaderInvocation = {
			Invocation.KernelName,
			Invocation.InvocationName,
			Invocation.DispatchDim,
			Invocation.KernelResource ? 
				Invocation.KernelResource->ShaderMap_RT->GetShader<FComputeKernelShader>(0) : 
				GetGlobalShaderMap(ERHIFeatureLevel::SM5)->GetShader<FComputeKernelShader>()
			};

		ComputeShaders.Emplace(ShaderInvocation);
	}
}

struct FComputeExecutionBuffer
{
	FComputeExecutionBuffer() = default;

	FComputeExecutionBuffer(FName InName, FRDGBufferRef InBufferRef)
		: Name(InName)
		, BufferRef(InBufferRef)
	{
	}

	FName Name;
	FRDGBufferRef BufferRef;
};

void FComputeFramework::ExecuteBatches(
	FRHICommandListImmediate& RHICmdList
	)
{
	if (ComputeShaders.IsEmpty())
	{
		return;
	}

	FMemMark MemStackMark(FMemStack::Get());
	{
		SCOPED_DRAW_EVENTF(RHICmdList, ComputeFramework_ExecuteBatches, TEXT("ComputeFramework::ExecuteBatches"));
		SCOPED_GPU_STAT(RHICmdList, ComputeFramework_ExecuteBatches);

		FRDGBuilder GraphBuilder(RHICmdList);

		TArray<FComputeExecutionBuffer> ExecutionBuffers;

		for (auto& ExtRes : ExternalBuffers)
		{
			TCHAR ResName[FComputeResource::MAX_NAME_LENGTH];
			ExtRes.Name.ToString(ResName);

			ExecutionBuffers.Emplace(
				ExtRes.Name,
				GraphBuilder.RegisterExternalBuffer(ExtRes.Buffer, ResName)
				);
		}

		for (auto& TransRes : TransientBuffers)
		{
			TCHAR ResName[FComputeResource::MAX_NAME_LENGTH];
			TransRes.Name.ToString(ResName);

			FRDGBufferDesc BufferDesc;
		
			EComputeExecutionBufferType BufferType = (EComputeExecutionBufferType)TransRes.BufferType;
			switch (BufferType)
			{
			case EComputeExecutionBufferType::Buffer:
				BufferDesc = FRDGBufferDesc::CreateBufferDesc(TransRes.BytesPerElement, TransRes.ElementCount);
				break;

			case EComputeExecutionBufferType::StructuredBuffer:
				BufferDesc = FRDGBufferDesc::CreateStructuredDesc(TransRes.BytesPerElement, TransRes.ElementCount);
				break;

			case EComputeExecutionBufferType::ByteAddressBuffer:
				BufferDesc = FRDGBufferDesc::CreateByteAddressDesc(TransRes.BytesPerElement * TransRes.ElementCount);
				break;

			default:
				check(!"Unsupported buffer type");
				break;
			}

			ExecutionBuffers.Emplace(
				TransRes.Name,
				GraphBuilder.CreateBuffer(BufferDesc, ResName)
				);
		}

		for (auto& Compute : ComputeShaders)
		{
			TCHAR KernelName[FComputeKernelInvocation::MAX_NAME_LENGTH];
			Compute.KernelName.ToString(KernelName);

			TCHAR InvocationName[FComputeKernelInvocation::MAX_NAME_LENGTH];
			Compute.InvocationName.ToString(InvocationName);

			auto* Params = GraphBuilder.AllocParameters<FComputeKernelShader::FParameters>();

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("Compute[%s]: %s", KernelName, InvocationName),
				ERDGPassFlags::Compute,
				Compute.Shader,
				Params,
				Compute.DispatchDim
				);
		}

		GraphBuilder.Execute();
	}
}
