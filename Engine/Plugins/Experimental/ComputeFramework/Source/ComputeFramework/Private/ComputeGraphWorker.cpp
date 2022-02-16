// Copyright Epic Games, Inc. All Rights Reserved.

#include "ComputeFramework/ComputeGraphWorker.h"

#include "ComputeFramework/ComputeKernel.h"
#include "ComputeFramework/ComputeKernelShader.h"
#include "ComputeFramework/ComputeGraph.h"
#include "ComputeFramework/ComputeGraphRenderProxy.h"
#include "ProfilingDebugging/RealtimeGPUProfiler.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RHIDefinitions.h"

DECLARE_GPU_STAT_NAMED(ComputeFramework_ExecuteBatches, TEXT("ComputeFramework::ExecuteBatches"));

void FComputeGraphTaskWorker::Enqueue(const FComputeGraphRenderProxy* ComputeGraph, TArray<FComputeDataProviderRenderProxy*> ComputeDataProviders)
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

	GraphInvocation.ComputeShaders.Reserve(NumSubInvocations);
	for (FComputeGraphRenderProxy::FKernelInvocation const& Invocation : ComputeGraph->KernelInvocations)
	{
		FShaderInvocation& ShaderInvocation = GraphInvocation.ComputeShaders.AddDefaulted_GetRef();
		ShaderInvocation.KernelName = Invocation.KernelName;
		ShaderInvocation.KernelResource = Invocation.KernelResource;
		ShaderInvocation.ShaderParamMetadata = Invocation.ShaderMetadata;
		ShaderInvocation.ShaderPermutationVector = Invocation.ShaderPermutationVector;
		ShaderInvocation.ShaderParamBindings = Invocation.ShaderParamBindings;

		// todo[CF]: dispatch dimension logic needs to be way more involved
		TArray<FIntVector> DispatchDimensions;
		ShaderInvocation.DispatchDimensions.SetNum(NumSubInvocations);
		for (int32 SubInvocationIndex = 0; SubInvocationIndex < NumSubInvocations; ++SubInvocationIndex)
		{
			ShaderInvocation.DispatchDimensions[SubInvocationIndex] = FirstProvider != -1 ? ComputeDataProviders[FirstProvider]->GetDispatchDim(SubInvocationIndex, Invocation.GroupDim) : FIntVector(1, 1, 1);
		}
	}

	GraphInvocation.DataProviderProxies = MoveTemp(ComputeDataProviders);
}

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

		for (int32 GraphIndex = 0; GraphIndex < GraphInvocations.Num(); ++GraphIndex)
		{
			FGraphInvocation const& GraphInvocation = GraphInvocations[GraphIndex];
			
			for (int32 DataProviderIndex = 0; DataProviderIndex < GraphInvocation.DataProviderProxies.Num(); ++DataProviderIndex)
			{
				FComputeDataProviderRenderProxy* DataProvider = GraphInvocation.DataProviderProxies[DataProviderIndex];
				if (DataProvider != nullptr)
				{
					DataProvider->AllocateResources(GraphBuilder);
				}
			}

			for (int32 KernelIndex = 0; KernelIndex < GraphInvocation.ComputeShaders.Num(); ++KernelIndex)
			{
				FShaderInvocation const& KernelInvocation = GraphInvocation.ComputeShaders[KernelIndex];

				const int32 NumSubInvocations = KernelInvocation.DispatchDimensions.Num();
				const int32 ParameterBufferSize = Align(KernelInvocation.ShaderParamMetadata->GetSize(), SHADER_PARAMETER_STRUCT_ALIGNMENT);

				void* RawBuffer = GraphBuilder.Alloc(NumSubInvocations * ParameterBufferSize, SHADER_PARAMETER_STRUCT_ALIGNMENT);
				FMemory::Memzero(RawBuffer, NumSubInvocations * ParameterBufferSize);
				uint8* ParameterBuffer = static_cast<uint8*>(RawBuffer);

				TArray<FShaderParametersMetadata::FMember> const& ParamMembers = KernelInvocation.ShaderParamMetadata->GetMembers();

				// Copy in the shader parameter bindings first.
				for (const TPair<int32, TArray<uint8>>& Binding : KernelInvocation.ShaderParamBindings)
				{
					const FShaderParametersMetadata::FMember& Member = ParamMembers[Binding.Key];
					const TArray<uint8>& ParamValue = Binding.Value;

					SIZE_T ParamSize;
					if (const FShaderParametersMetadata* StructMetaData = Member.GetStructMetadata())
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
						// Need to fill in for each sub invocation.
						for (int32 SubInvocationIndex = 0; SubInvocationIndex < NumSubInvocations; ++SubInvocationIndex)
						{
							FMemory::Memcpy(&ParameterBuffer[Member.GetOffset() + SubInvocationIndex * ParameterBufferSize], ParamValue.GetData(), ParamSize);
						}
					}
				}

				// Iterate data providers to fill data structures.
				FComputeDataProviderRenderProxy::FCollectedDispatchData DispatchData;
				DispatchData.ParameterBuffer = ParameterBuffer;
				DispatchData.PermutationId.AddZeroed(NumSubInvocations);

				FComputeDataProviderRenderProxy::FDispatchSetup DispatchSetup{ NumSubInvocations, 0, ParameterBufferSize, 0, *KernelInvocation.ShaderPermutationVector };

				for (int32 DataProviderIndex = 0; DataProviderIndex < GraphInvocation.DataProviderProxies.Num(); ++DataProviderIndex)
				{
					FComputeDataProviderRenderProxy* DataProvider = GraphInvocation.DataProviderProxies[DataProviderIndex];
					if (DataProvider != nullptr)
					{
						bool bFound = false;
						TCHAR const* UID = UComputeGraph::GetDataInterfaceUID(DataProviderIndex);
						for (FShaderParametersMetadata::FMember const& Member : ParamMembers)
						{
							if (Member.GetBaseType() == EUniformBufferBaseType::UBMT_NESTED_STRUCT && Member.GetName() == UID)
							{
								DispatchSetup.ParameterBufferOffset = Member.GetOffset();
								DispatchSetup.ParameterStructSizeForValidation = Member.GetStructMetadata()->GetSize();
								bFound = true;
								break;
							}
						}

						if (bFound)
						{
							DataProvider->GatherDispatchData(DispatchSetup, DispatchData);
						}
					}
				}

				TCHAR KernelName[128];
				KernelInvocation.KernelName.ToString(KernelName);

				for (int32 SubInvocationIndex = 0; SubInvocationIndex < NumSubInvocations; ++SubInvocationIndex)
				{
					TShaderRef<FComputeKernelShader> Shader = KernelInvocation.KernelResource->GetShader(DispatchData.PermutationId[SubInvocationIndex]);

					FComputeShaderUtils::AddPass(
						GraphBuilder,
						RDG_EVENT_NAME("Compute[%s (%d)]", KernelName, SubInvocationIndex),
						ERDGPassFlags::Compute | ERDGPassFlags::NeverCull,
						Shader,
						KernelInvocation.ShaderParamMetadata,
						reinterpret_cast<FComputeKernelShader::FParameters*>(ParameterBuffer + ParameterBufferSize * SubInvocationIndex),
						KernelInvocation.DispatchDimensions[SubInvocationIndex]
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
	for (FComputeDataProviderRenderProxy* DataProvider : DataProviderProxies)
	{
		delete DataProvider;
	}
}
