// Copyright Epic Games, Inc. All Rights Reserved.

#include "ComputeFramework/ComputeGraphScheduler.h"

#include "ComputeFramework/ComputeKernel.h"
#include "ComputeFramework/ComputeKernelShader.h"
#include "ComputeFramework/ComputeGraph.h"
#include "ProfilingDebugging/RealtimeGPUProfiler.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RHIDefinitions.h"

DECLARE_GPU_STAT_NAMED(ComputeFramework_ExecuteBatches, TEXT("ComputeFramework::ExecuteBatches"));

void FComputeGraphProxy::Initialize(UComputeGraph* ComputeGraph)
{
	const int32 NumKernels = ComputeGraph->GetNumKernelInvocations();
	for (int32 KernelIndex = 0; KernelIndex < NumKernels; ++KernelIndex)
	{
		UComputeKernel const* Kernel = ComputeGraph->GetKernelInvocation(KernelIndex);
		FComputeKernelResource const* KernelResource = ComputeGraph->GetKernelResource(KernelIndex);
		FShaderParametersMetadata const* ShaderMetadata = ComputeGraph->GetKernelShaderMetadata(KernelIndex);

		if (Kernel != nullptr && KernelResource != nullptr && ShaderMetadata != nullptr)
		{
			FKernelInvocation KernelInvocation = {
				Kernel->GetFName(),
				FName("InvocationName"),
				FIntVector(32, 1, 1), // todo[CF]: read group size from kernel (or possibly apply it through defines)
				ShaderMetadata,
				KernelResource };

			KernelInvocations.Emplace(KernelInvocation);
		}
	}
}

void FComputeGraphScheduler::EnqueueForExecution(const FComputeGraphProxy* ComputeGraph, TArrayView<FComputeDataProviderRenderProxy* const> ComputeDataProviders)
{
	// todo[CF]: Allocate a specific data provider per kernel to drive the number of invocations?
	int32 FirstProvider = -1;
	for (int32 ProviderIndex = 0; ProviderIndex < ComputeDataProviders.Num(); ++ProviderIndex)
	{
		if (ComputeDataProviders[ProviderIndex] != nullptr)
		{
			FirstProvider = ProviderIndex;
			break;
		}
	}

	const int32 SubInvocationCount = FirstProvider != -1 ? ComputeDataProviders[FirstProvider]->GetInvocationCount() : 1;

	for (FComputeGraphProxy::FKernelInvocation const& Invocation : ComputeGraph->KernelInvocations)
	{
		// todo[CF]: If you hit this then shader compilation might not have happened yet.
		if (ensure(Invocation.Kernel->GetShader().IsValid()))
		{
			for (int32 SubInvocationIndex = 0; SubInvocationIndex < SubInvocationCount; ++ SubInvocationIndex)
			{
				// todo[CF]: dispatch dimension logic needs to be way more involved
				const FIntVector DispatchDim = FirstProvider != -1 ? ComputeDataProviders[FirstProvider]->GetDispatchDim(SubInvocationIndex, Invocation.GroupDim) : FIntVector(1, 1, 1);

				FShaderInvocation ShaderInvocation = {
					Invocation.KernelName,
					Invocation.InvocationName,
					DispatchDim,
					Invocation.Kernel->GetShaderParamMetadata(),
					Invocation.Kernel->GetShader(),
					Bindings.Num() + SubInvocationIndex
					};

				ComputeShaders.Emplace(ShaderInvocation);
			}
		}
	}

	// todo[CF]: Move GetBindings() calls to ExecuteBatches() so that RDG buffers can be registered and bound.
	Bindings.AddDefaulted(SubInvocationCount);
	for (int32 SubInvocationIndex = 0; SubInvocationIndex < SubInvocationCount; ++SubInvocationIndex)
	{
		for (int32 ProviderIndex = 0; ProviderIndex < ComputeDataProviders.Num(); ++ProviderIndex)
		{
			if (ComputeDataProviders[ProviderIndex] != nullptr)
			{
				TCHAR const* UID = UComputeGraph::GetDataInterfaceUID(ProviderIndex);
				ComputeDataProviders[ProviderIndex]->GetBindings(SubInvocationIndex, UID, Bindings[SubInvocationIndex]);
			}
		}
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

void FComputeGraphScheduler::ExecuteBatches(
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

		// todo[CF]: Implement RDG allocations in the data providers. Probably need to move GetBindings() into the RDG block here?
		/*
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
		*/

		for (int32 KernelIndex = 0; KernelIndex < ComputeShaders.Num(); KernelIndex++)
		{
			FShaderInvocation& Compute = ComputeShaders[KernelIndex];
			FComputeDataProviderRenderProxy::FBindings& Binding = Bindings[Compute.BindingsIndex];

			FRDGBufferDesc BufferDesc = FRDGBufferDesc::CreateBufferDesc(4, 32);

			void* RawBuffer = GraphBuilder.Alloc(Compute.ShaderParamMetadata->GetSize(), SHADER_PARAMETER_STRUCT_ALIGNMENT);
			FMemory::Memzero(RawBuffer, Compute.ShaderParamMetadata->GetSize());

			bool bBindFailed = false;
			uint8* ParamBuffer = static_cast<uint8*>(RawBuffer);
			TArray<FShaderParametersMetadata::FMember> ParamMembers = Compute.ShaderParamMetadata->GetMembers();
			for (auto& Member : ParamMembers)
			{
				switch (Member.GetBaseType())
				{
				case EUniformBufferBaseType::UBMT_INT32:
					{
						int32* ParamValue = Binding.ParamsInt.Find(Member.GetName());
						*reinterpret_cast<int32*>(&ParamBuffer[Member.GetOffset()]) = ParamValue != nullptr ? *ParamValue : 0;
					}
					break;

				case EUniformBufferBaseType::UBMT_UINT32:
					{
						uint32* ParamValue = Binding.ParamsUint.Find(Member.GetName());
						*reinterpret_cast<uint32*>(&ParamBuffer[Member.GetOffset()]) = ParamValue != nullptr ? *ParamValue : 0;
					}
				break;

				case EUniformBufferBaseType::UBMT_FLOAT32:
					{
						float* ParamValue = Binding.ParamsFloat.Find(Member.GetName());
						*reinterpret_cast<float*>(&ParamBuffer[Member.GetOffset()]) = ParamValue != nullptr ? *ParamValue : 0;
					}
					break;

				case EUniformBufferBaseType::UBMT_NESTED_STRUCT:
					{
						TArray<uint8>* ParamValue = Binding.Structs.Find(Member.GetName());
						if (ParamValue != nullptr)
						{
							FMemory::Memcpy(&ParamBuffer[Member.GetOffset()], ParamValue->GetData(), Member.GetStructMetadata()->GetSize());
						}
						else
						{
							bBindFailed = true;
						}
					}
					break;

				default:
					check(!"Unsupported type");
					bBindFailed = true;
					break;
				};
			}

			if (ensure(!bBindFailed))
			{
				TCHAR KernelName[128];
				Compute.KernelName.ToString(KernelName);

				TCHAR InvocationName[128];
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
		}

		GraphBuilder.Execute();

		ComputeShaders.Reset();
		Bindings.Reset();
	}
}
