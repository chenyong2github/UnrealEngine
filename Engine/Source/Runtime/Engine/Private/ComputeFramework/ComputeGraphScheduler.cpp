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

void FComputeGraphScheduler::EnqueueForExecution(const FComputeGraphProxy* ComputeGraph, TArray<FComputeDataProviderRenderProxy*> ComputeDataProviders)
{
	FGraphInvocation& GraphInvocation = GraphInvocations.AddDefaulted_GetRef();

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
					Invocation.ShaderMetadata,
					Invocation.Kernel->GetShader(),
					SubInvocationIndex };

				GraphInvocation.ComputeShaders.Emplace(ShaderInvocation);
			}
		}
	}

	GraphInvocation.DataProviders = MoveTemp(ComputeDataProviders);
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
	if (GraphInvocations.IsEmpty())
	{
		return;
	}

	FMemMark MemStackMark(FMemStack::Get());
	{
		SCOPED_DRAW_EVENTF(RHICmdList, ComputeFramework_ExecuteBatches, TEXT("ComputeFramework::ExecuteBatches"));
		SCOPED_GPU_STAT(RHICmdList, ComputeFramework_ExecuteBatches);

		FRDGBuilder GraphBuilder(RHICmdList);
		FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(FeatureLevel);

		for (int32 GraphIndex = 0; GraphIndex < GraphInvocations.Num(); ++GraphIndex)
		{
			// Gather from all providers.
			// todo[CF]: This is first pass and needs profiling. Probably with some care we can remove a bunch of heap allocations.
			TArray<FComputeDataProviderRenderProxy::FBindings> AllBindings;

			TArray<FComputeDataProviderRenderProxy*> const& DataProviders = GraphInvocations[GraphIndex].DataProviders;
			for (int32 DataProviderIndex = 0; DataProviderIndex < DataProviders.Num(); ++DataProviderIndex)
			{
				FComputeDataProviderRenderProxy* DataProvider = DataProviders[DataProviderIndex];
				if (DataProvider != nullptr)
				{
					if (AllBindings.Num() < DataProvider->GetInvocationCount())
					{
						AllBindings.SetNum(DataProvider->GetInvocationCount());
					}

					DataProvider->AllocateResources(GraphBuilder);

					for (int32 InvocationIndex = 0; InvocationIndex < DataProvider->GetInvocationCount(); ++InvocationIndex)
					{
						TCHAR const* UID = UComputeGraph::GetDataInterfaceUID(DataProviderIndex);
						DataProvider->GetBindings(InvocationIndex, UID, AllBindings[InvocationIndex]);
					}
				}
			}

			// Add compute passes.
			TArray<FShaderInvocation> const& ComputeShaders = GraphInvocations[GraphIndex].ComputeShaders;
			for (FShaderInvocation const& Compute : ComputeShaders)
			{
				FRDGBufferDesc BufferDesc = FRDGBufferDesc::CreateBufferDesc(4, 32);

				void* RawBuffer = GraphBuilder.Alloc(Compute.ShaderParamMetadata->GetSize(), SHADER_PARAMETER_STRUCT_ALIGNMENT);
				FMemory::Memzero(RawBuffer, Compute.ShaderParamMetadata->GetSize());

				FComputeDataProviderRenderProxy::FBindings& Bindings = AllBindings[Compute.SubInvocationIndex];

				bool bBindFailed = false;
				uint8* ParamBuffer = static_cast<uint8*>(RawBuffer);
				TArray<FShaderParametersMetadata::FMember> ParamMembers = Compute.ShaderParamMetadata->GetMembers();
				for (auto& Member : ParamMembers)
				{
					switch (Member.GetBaseType())
					{
					case EUniformBufferBaseType::UBMT_INT32:
						{
							int32* ParamValue = Bindings.ParamsInt.Find(Member.GetName());
							*reinterpret_cast<int32*>(&ParamBuffer[Member.GetOffset()]) = ParamValue != nullptr ? *ParamValue : 0;
						}
						break;

					case EUniformBufferBaseType::UBMT_UINT32:
						{
							uint32* ParamValue = Bindings.ParamsUint.Find(Member.GetName());
							*reinterpret_cast<uint32*>(&ParamBuffer[Member.GetOffset()]) = ParamValue != nullptr ? *ParamValue : 0;
						}
					break;

					case EUniformBufferBaseType::UBMT_FLOAT32:
						{
							float* ParamValue = Bindings.ParamsFloat.Find(Member.GetName());
							*reinterpret_cast<float*>(&ParamBuffer[Member.GetOffset()]) = ParamValue != nullptr ? *ParamValue : 0;
						}
						break;

					case EUniformBufferBaseType::UBMT_NESTED_STRUCT:
						{
							TArray<uint8>* ParamValue = Bindings.Structs.Find(Member.GetName());
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
		}

		GraphBuilder.Execute();

		GraphInvocations.Reset();
	}
}

FComputeGraphScheduler::FGraphInvocation::~FGraphInvocation()
{
	for (FComputeDataProviderRenderProxy* DataProvider : DataProviders)
	{
		delete DataProvider;
	}
}
