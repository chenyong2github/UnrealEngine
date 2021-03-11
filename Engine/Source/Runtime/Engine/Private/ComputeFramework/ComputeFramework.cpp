// Copyright Epic Games, Inc. All Rights Reserved.

#include "ComputeFramework/ComputeFramework.h"
#include "ComputeFramework/ComputeKernel.h"
#include "ComputeFramework/ComputeGraph.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RHIDefinitions.h"
#include "ComputeFramework/ComputeKernelShader.h"
#include "ProfilingDebugging/RealtimeGPUProfiler.h"

DECLARE_GPU_STAT_NAMED(ComputeFramework_ExecuteBatches, TEXT("ComputeFramework::ExecuteBatches"));

static int32 GComputeFrameworkMode = 1;
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
			Invocation.ComputeKernel->GetFName(),
			FName("InvocationName"),
			FIntVector(1, 1, 1),
			Invocation.ComputeKernel->GetResource()
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
			Invocation.Kernel->GetShaderParamMetadata(),
			Invocation.Kernel->GetShader()
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
	FRHICommandListImmediate& RHICmdList,
	ERHIFeatureLevel::Type FeatureLevel
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
		FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(FeatureLevel);

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
			FRDGBufferDesc BufferDesc = FRDGBufferDesc::CreateBufferDesc(4, 32);

			void* RawBuffer = GraphBuilder.Alloc(Compute.ShaderParamMetadata->GetSize(), SHADER_PARAMETER_STRUCT_ALIGNMENT);
			FMemory::Memzero(RawBuffer, Compute.ShaderParamMetadata->GetSize());

			uint8* ParamBuffer = static_cast<uint8*>(RawBuffer);
			TArray<FShaderParametersMetadata::FMember> ParamMembers = Compute.ShaderParamMetadata->GetMembers();
			for (auto& Member : ParamMembers)
			{
				switch (Member.GetBaseType())
				{
				case EUniformBufferBaseType::UBMT_INT32:
				case EUniformBufferBaseType::UBMT_UINT32:
				case EUniformBufferBaseType::UBMT_FLOAT32:
					// Fill is CBuffer data
					break;

				case EUniformBufferBaseType::UBMT_RDG_BUFFER_SRV:
					{
						FRDGBufferRef InputBuffer = GraphBuilder.CreateBuffer(BufferDesc, Member.GetName());

						FComputeShaderUtils::ClearUAV(
							GraphBuilder,
							ShaderMap,
							GraphBuilder.CreateUAV(InputBuffer, PF_A32B32G32R32F),
							FVector4(0.0f)
							);

						*reinterpret_cast<FRDGBufferSRVRef*>(&ParamBuffer[Member.GetOffset()]) = GraphBuilder.CreateSRV(InputBuffer, PF_R32_FLOAT);
					}
					break;

				case EUniformBufferBaseType::UBMT_RDG_BUFFER_UAV:
					{						
						FRDGBufferRef OutputBuffer = GraphBuilder.CreateBuffer(BufferDesc, Member.GetName());

						*reinterpret_cast<FRDGBufferUAVRef*>(&ParamBuffer[Member.GetOffset()]) = GraphBuilder.CreateUAV(OutputBuffer, PF_R32_FLOAT);
					}
					break;

				default:
					check(!"Unsupported type");
					break;
				};
			}

			TCHAR KernelName[FComputeKernelInvocation::MAX_NAME_LENGTH];
			Compute.KernelName.ToString(KernelName);

			TCHAR InvocationName[FComputeKernelInvocation::MAX_NAME_LENGTH];
			Compute.InvocationName.ToString(InvocationName);

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("Compute[%s]: %s", KernelName, InvocationName),
				ERDGPassFlags::Compute | ERDGPassFlags::NeverCull,
				Compute.Shader,
				Compute.ShaderParamMetadata,
				static_cast<FComputeKernelShader::FParameters*>(RawBuffer),
				Compute.DispatchDim
				);		
		}

		GraphBuilder.Execute();

		ComputeShaders.Reset();
	}
}
