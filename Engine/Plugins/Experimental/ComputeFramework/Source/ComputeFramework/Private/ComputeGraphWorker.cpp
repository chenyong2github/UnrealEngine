// Copyright Epic Games, Inc. All Rights Reserved.

#include "ComputeFramework/ComputeGraphWorker.h"

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
			TMap<int32, TArray<uint8>> KernelBindings;
		
			ComputeGraph->GetKernelBindings(KernelIndex, KernelBindings);
			
			FKernelInvocation KernelInvocation = {
				Kernel->GetFName(),
				FName("InvocationName"),
				FIntVector(64, 1, 1), // todo[CF]: read group size from kernel (or possibly apply it through defines)
				ShaderMetadata,
				KernelBindings,
				KernelResource
				};

			KernelInvocations.Emplace(KernelInvocation);
		}
	}
}

void FComputeGraphTaskWorker::Enqueue(const FComputeGraphProxy* ComputeGraph, TArray<FComputeDataProviderRenderProxy*> ComputeDataProviders)
{
	FGraphInvocation& GraphInvocation = GraphInvocations.AddDefaulted_GetRef();

	// todo[CF]: Allocate a specific data provider per kernel to drive the number of invocations?
	int32 FirstProvider = -1;
	for (int32 ProviderIndex = 0; ProviderIndex < ComputeDataProviders.Num(); ++ProviderIndex)
	{
		if (ComputeDataProviders[ProviderIndex] != nullptr && ComputeDataProviders[ProviderIndex]->GetInvocationCount() > 0)
		{
			FirstProvider = ProviderIndex;
			break;
		}
	}

	const int32 NumSubInvocations = FirstProvider != -1 ? ComputeDataProviders[FirstProvider]->GetInvocationCount() : 1;
	GraphInvocation.NumSubInvocations = NumSubInvocations;

	for (FComputeGraphProxy::FKernelInvocation const& Invocation : ComputeGraph->KernelInvocations)
	{
		// todo[CF]: If you hit this then shader compilation might not have happened yet.
		if (Invocation.Kernel->GetShader().IsValid())
		{
			if (Invocation.Kernel->GetShader()->Bindings.StructureLayoutHash != Invocation.ShaderMetadata->GetLayoutHash())
			{
				// todo[CF]: Fix issue where shader metadata is updated out of sync with shader compilation.
				continue;
			}

			for (int32 SubInvocationIndex = 0; SubInvocationIndex < NumSubInvocations; ++ SubInvocationIndex)
			{
				// todo[CF]: dispatch dimension logic needs to be way more involved
				const FIntVector DispatchDim = FirstProvider != -1 ? ComputeDataProviders[FirstProvider]->GetDispatchDim(SubInvocationIndex, Invocation.GroupDim) : FIntVector(1, 1, 1);

				FShaderInvocation ShaderInvocation = {
					Invocation.KernelName,
					Invocation.InvocationName,
					DispatchDim,
					Invocation.ShaderMetadata,
					Invocation.ShaderParamBindings,
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

void FComputeGraphTaskWorker::SubmitWork(
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
			AllBindings.AddDefaulted(GraphInvocations[GraphIndex].NumSubInvocations);

			TArray<FComputeDataProviderRenderProxy*> const& DataProviders = GraphInvocations[GraphIndex].DataProviders;
			for (int32 DataProviderIndex = 0; DataProviderIndex < DataProviders.Num(); ++DataProviderIndex)
			{
				FComputeDataProviderRenderProxy* DataProvider = DataProviders[DataProviderIndex];
				if (DataProvider != nullptr)
				{
					DataProvider->AllocateResources(GraphBuilder);

					for (int32 InvocationIndex = 0; InvocationIndex < AllBindings.Num(); ++InvocationIndex)
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

				uint8* ParamBuffer = static_cast<uint8*>(RawBuffer);
				const TArray<FShaderParametersMetadata::FMember>& ParamMembers = Compute.ShaderParamMetadata->GetMembers();

				// Copy in the shader parameter bindings first.
				for (const TPair<int32, TArray<uint8>>& Binding: Compute.ShaderParamBindings)
				{
					const FShaderParametersMetadata::FMember& Member = ParamMembers[Binding.Key];
					const TArray<uint8>& ParamValue = Binding.Value;

					SIZE_T ParamSize;
					if (const FShaderParametersMetadata *StructMetaData = Member.GetStructMetadata())
					{
						// TODO: Rows/Columns/ElemCount?
						ParamSize = StructMetaData->GetSize();
					}
					else
					{
						ParamSize = Member.GetMemberSize();
					}

					if (ensure(ParamSize == ParamValue.Num()))
					{
						FMemory::Memcpy(&ParamBuffer[Member.GetOffset()], ParamValue.GetData(), ParamSize);
					}
				}				

				// Then all the data interface bindings.
				FComputeDataProviderRenderProxy::FBindings& Bindings = AllBindings[Compute.SubInvocationIndex];

				bool bBindFailed = false;
				for (auto& Member : ParamMembers)
				{
					switch (Member.GetBaseType())
					{
					case EUniformBufferBaseType::UBMT_INT32:
						{
							int32* ParamValue = Bindings.ParamsInt.Find(Member.GetName());
							if (ParamValue)
							{
								*reinterpret_cast<int32*>(&ParamBuffer[Member.GetOffset()]) = *ParamValue;
							}
						}
						break;

					case EUniformBufferBaseType::UBMT_UINT32:
						{
							uint32* ParamValue = Bindings.ParamsUint.Find(Member.GetName());
							if (ParamValue)
							{
								*reinterpret_cast<uint32*>(&ParamBuffer[Member.GetOffset()]) = *ParamValue;
							}
						}
					break;

					case EUniformBufferBaseType::UBMT_FLOAT32:
						{
							float* ParamValue = Bindings.ParamsFloat.Find(Member.GetName());
							if (ParamValue)
							{
								*reinterpret_cast<float*>(&ParamBuffer[Member.GetOffset()]) = *ParamValue;
							}
						}
						break;

					case EUniformBufferBaseType::UBMT_NESTED_STRUCT:
						{
							TArray<uint8>* ParamValue = Bindings.Structs.Find(Member.GetName());
							if (ParamValue != nullptr)
							{
								FMemory::Memcpy(&ParamBuffer[Member.GetOffset()], ParamValue->GetData(), Member.GetStructMetadata()->GetSize());
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

FComputeGraphTaskWorker::FGraphInvocation::~FGraphInvocation()
{
	for (FComputeDataProviderRenderProxy* DataProvider : DataProviders)
	{
		delete DataProvider;
	}
}
