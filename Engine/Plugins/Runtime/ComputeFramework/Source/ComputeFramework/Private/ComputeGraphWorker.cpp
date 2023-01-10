// Copyright Epic Games, Inc. All Rights Reserved.

#include "ComputeFramework/ComputeGraphWorker.h"

#include "ComputeFramework/ComputeKernel.h"
#include "ComputeFramework/ComputeKernelPermutationVector.h"
#include "ComputeFramework/ComputeKernelShader.h"
#include "ComputeFramework/ComputeGraph.h"
#include "ComputeFramework/ComputeGraphRenderProxy.h"
#include "ProfilingDebugging/RealtimeGPUProfiler.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RHIDefinitions.h"

DECLARE_GPU_STAT_NAMED(ComputeFramework_ExecuteBatches, TEXT("ComputeFramework::ExecuteBatches"));

void FComputeGraphTaskWorker::Enqueue(FName InExecutionGroupName, FName InOwnerName, FComputeGraphRenderProxy const* InGraphRenderProxy, TArray<FComputeDataProviderRenderProxy*> InDataProviderRenderProxies)
{
	FGraphInvocation& GraphInvocation = GraphInvocationsPerGroup.FindOrAdd(InExecutionGroupName).AddDefaulted_GetRef();
	GraphInvocation.OwnerName = InOwnerName;
	GraphInvocation.GraphRenderProxy = InGraphRenderProxy;
	GraphInvocation.DataProviderRenderProxies = MoveTemp(InDataProviderRenderProxies);
}

void FComputeGraphTaskWorker::SubmitWork(FRDGBuilder& GraphBuilder, FName InExecutionGroupName, ERHIFeatureLevel::Type FeatureLevel)
{
	TArray<FGraphInvocation>& GraphInvocations = GraphInvocationsPerGroup.FindOrAdd(InExecutionGroupName);
	if (GraphInvocations.IsEmpty())
	{
		return;
	}

	{
		SCOPED_DRAW_EVENTF(GraphBuilder.RHICmdList, ComputeFramework_ExecuteBatches, TEXT("ComputeFramework::ExecuteBatches"));
		SCOPED_GPU_STAT(GraphBuilder.RHICmdList, ComputeFramework_ExecuteBatches);

		for (int32 GraphIndex = 0; GraphIndex < GraphInvocations.Num(); ++GraphIndex)
		{
			FGraphInvocation const& GraphInvocation = GraphInvocations[GraphIndex];
			FComputeGraphRenderProxy const* GraphRenderProxy = GraphInvocation.GraphRenderProxy;

			TArray<TShaderRef<FComputeKernelShader>> Shaders;

			// Validation phase.
			// Check if all DataInterfaces are valid.
			// At the same time gather the permutation id so that we can validate if shader is compiled.
			bool bIsValid = true;
			for (int32 KernelIndex = 0; bIsValid && KernelIndex < GraphRenderProxy->KernelInvocations.Num(); ++KernelIndex)
			{
				FComputeGraphRenderProxy::FKernelInvocation const& KernelInvocation = GraphRenderProxy->KernelInvocations[KernelIndex];

				TArray<FIntVector> ThreadCounts;
				const int32 NumSubInvocations = GraphInvocation.DataProviderRenderProxies[KernelInvocation.ExecutionProviderIndex]->GetDispatchThreadCount(ThreadCounts);

				// Iterate shader parameter members to fill the dispatch data structures.
				// We assume that the members were filled out with a single data interface per member, and that the
				// order is the same one defined in the KernelInvocation.BoundProviderIndices.
				TArray<FShaderParametersMetadata::FMember> const& ParamMembers = KernelInvocation.ShaderParameterMetadata->GetMembers();

				FComputeDataProviderRenderProxy::FPermutationData PermutationData{ NumSubInvocations, GraphRenderProxy->ShaderPermutationVectors[KernelIndex] };
				PermutationData.PermutationIds.AddZeroed(NumSubInvocations);

				for (int32 MemberIndex = 0; bIsValid && MemberIndex < ParamMembers.Num(); ++MemberIndex)
				{
					FShaderParametersMetadata::FMember const& Member = ParamMembers[MemberIndex];
					if (ensure(Member.GetBaseType() == EUniformBufferBaseType::UBMT_NESTED_STRUCT))
					{
						const int32 DataProviderIndex = KernelInvocation.BoundProviderIndices[MemberIndex];
						FComputeDataProviderRenderProxy* DataProvider = GraphInvocation.DataProviderRenderProxies[DataProviderIndex];
						if (ensure(DataProvider != nullptr))
						{
							FComputeDataProviderRenderProxy::FValidationData ValidationData{ NumSubInvocations, (int32)Member.GetStructMetadata()->GetSize() };
							bIsValid &= DataProvider->IsValid(ValidationData);

							if (bIsValid)
							{
								DataProvider->GatherPermutations(PermutationData);
							}
						}
					}
				}

				// Get shader. This can fail if compilation is pending.
				Shaders.Reserve(Shaders.Num() + NumSubInvocations);
				for (int32 SubInvocationIndex = 0; bIsValid && SubInvocationIndex < NumSubInvocations; ++SubInvocationIndex)
				{
					TShaderRef<FComputeKernelShader> Shader = KernelInvocation.KernelResource->GetShader(PermutationData.PermutationIds[SubInvocationIndex]);
					bIsValid &= Shader.IsValid();
					Shaders.Add(Shader);
				}
			}

			// If we can't run the graph for any reason, back out now.
			if (!bIsValid)
			{
				continue;
			}

			// From here on we are committed to submitting the work to the GPU.
			RDG_EVENT_SCOPE(GraphBuilder, "%s:%s", *GraphInvocation.OwnerName.ToString(), *GraphRenderProxy->GraphName.ToString());

			// Do resource allocation for all the data providers in the graph.
			for (int32 DataProviderIndex = 0; DataProviderIndex < GraphInvocation.DataProviderRenderProxies.Num(); ++DataProviderIndex)
			{
				FComputeDataProviderRenderProxy* DataProvider = GraphInvocation.DataProviderRenderProxies[DataProviderIndex];
				if (DataProvider != nullptr)
				{
					DataProvider->AllocateResources(GraphBuilder);
				}
			}

			// Iterate the graph kernels to collect shader bindings and dispatch work.
			for (int32 KernelIndex = 0; KernelIndex < GraphRenderProxy->KernelInvocations.Num(); ++KernelIndex)
			{
				FComputeGraphRenderProxy::FKernelInvocation const& KernelInvocation = GraphRenderProxy->KernelInvocations[KernelIndex];

				RDG_EVENT_SCOPE(GraphBuilder, "%s", *KernelInvocation.KernelName);

				TArray<FIntVector> ThreadCounts;
				const int32 NumSubInvocations = GraphInvocation.DataProviderRenderProxies[KernelInvocation.ExecutionProviderIndex]->GetDispatchThreadCount(ThreadCounts);

				TStridedView<FComputeKernelShader::FParameters> ParameterArray = GraphBuilder.AllocParameters<FComputeKernelShader::FParameters>(KernelInvocation.ShaderParameterMetadata, NumSubInvocations);
				FComputeDataProviderRenderProxy::FDispatchData DispatchData{ NumSubInvocations, 0, 0, ParameterArray.GetStride(), reinterpret_cast<uint8*>(&ParameterArray[0]) };

				// Iterate shader parameter members to fill the dispatch data structures.
				// We assume that the members were filled out with a single data interface per member, and that the
				// order is the same one defined in the KernelInvocation.BoundProviderIndices.
				TArray<FShaderParametersMetadata::FMember> const& ParamMembers = KernelInvocation.ShaderParameterMetadata->GetMembers();
				for (int32 MemberIndex = 0; MemberIndex < ParamMembers.Num(); ++MemberIndex)
				{
					FShaderParametersMetadata::FMember const& Member = ParamMembers[MemberIndex];
					if (ensure(Member.GetBaseType() == EUniformBufferBaseType::UBMT_NESTED_STRUCT))
					{
						const int32 DataProviderIndex = KernelInvocation.BoundProviderIndices[MemberIndex];
						FComputeDataProviderRenderProxy* DataProvider = GraphInvocation.DataProviderRenderProxies[DataProviderIndex];
						if (ensure(DataProvider != nullptr))
						{
							DispatchData.ParameterStructSize = Member.GetStructMetadata()->GetSize();
							DispatchData.ParameterBufferOffset = Member.GetOffset();
							DataProvider->GatherDispatchData(DispatchData);
						}
					}
				}

				// Dispatch work to the render graph.
				for (int32 SubInvocationIndex = 0; SubInvocationIndex < NumSubInvocations; ++SubInvocationIndex)
				{
					TShaderRef<FComputeKernelShader> Shader = Shaders[KernelIndex * NumSubInvocations + SubInvocationIndex];
					const FIntVector GroupCount = FComputeShaderUtils::GetGroupCount(ThreadCounts[SubInvocationIndex], KernelInvocation.KernelGroupSize);

					FComputeShaderUtils::AddPass(
						GraphBuilder,
						{},
						ERDGPassFlags::Compute | ERDGPassFlags::NeverCull,
						Shader,
						KernelInvocation.ShaderParameterMetadata,
						&ParameterArray[SubInvocationIndex],
						GroupCount
					);
				}
			}
		}

		// Release any graph resources at the end of graph execution.
		GraphBuilder.AddPass(
			RDG_EVENT_NAME("Release Data Providers"),
			ERDGPassFlags::None,
			[this, InExecutionGroupName](FRHICommandList&)
			{
				GraphInvocationsPerGroup.FindChecked(InExecutionGroupName).Reset();
			});
	}
}

FComputeGraphTaskWorker::FGraphInvocation::~FGraphInvocation()
{
	// DataProviderRenderProxy objects are created per frame and destroyed here after render work has been submitted.
	// todo[CF]: Some proxies can probably persist, but will need logic to define that and flag when they need recreating.
	for (FComputeDataProviderRenderProxy* DataProvider : DataProviderRenderProxies)
	{
		delete DataProvider;
	}
}
