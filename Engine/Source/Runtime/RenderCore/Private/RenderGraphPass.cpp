// Copyright Epic Games, Inc. All Rights Reserved.

#include "RenderGraphPass.h"
#include "RenderGraphPrivate.h"
#include "RenderGraphUtils.h"

void FRDGPassParameterStruct::ClearUniformBuffers() const
{
	/** The pass parameter struct is mostly POD, with the exception of uniform buffers.
	 *  Since the explicit type of the struct is unknown, the method traverses and destructs
	 *  all uniform buffer references manually.
	 */
	const auto& Resources = Layout->Resources;

	for (int32 ResourceIndex = 0; ResourceIndex < Resources.Num(); ++ResourceIndex)
	{
		const EUniformBufferBaseType MemberType = Resources[ResourceIndex].MemberType;

		if (MemberType == UBMT_REFERENCED_STRUCT)
		{
			const uint16 MemberOffset = Resources[ResourceIndex].MemberOffset;

			FUniformBufferRHIRef* UniformBufferPtr = reinterpret_cast<FUniformBufferRHIRef*>(Contents + MemberOffset);
			*UniformBufferPtr = FUniformBufferRHIRef();
		}
	}
}

FUniformBufferStaticBindings FRDGPassParameterStruct::GetGlobalUniformBuffers() const
{
	FUniformBufferStaticBindings GlobalUniformBuffers;

	const auto& Resources = Layout->Resources;

	for (int32 ResourceIndex = 0; ResourceIndex < Resources.Num(); ++ResourceIndex)
	{
		const EUniformBufferBaseType MemberType = Resources[ResourceIndex].MemberType;

		if (MemberType == UBMT_REFERENCED_STRUCT)
		{
			const uint16 MemberOffset = Resources[ResourceIndex].MemberOffset;

			FRHIUniformBuffer* UniformBufferPtr = *reinterpret_cast<FUniformBufferRHIRef*>(Contents + MemberOffset);

			if (UniformBufferPtr && UniformBufferPtr->HasStaticSlot())
			{
				GlobalUniformBuffers.AddUniformBuffer(UniformBufferPtr);
			}
		}
	}

	return GlobalUniformBuffers;
}

bool FRDGPassParameterStruct::HasExternalOutputs() const
{
	return Layout->bHasNonGraphOutputs;
}

const FRenderTargetBindingSlots* FRDGPassParameterStruct::GetRenderTargetBindingSlots() const
{
	if (Layout->HasRenderTargets())
	{
		return reinterpret_cast<const FRenderTargetBindingSlots*>(Contents + Layout->RenderTargetsOffset);
	}
	return nullptr;
}

void FRDGBarrierBatch::SetSubmitted()
{
	check(!bSubmitted);
	bSubmitted = true;
}

FString FRDGBarrierBatch::GetName() const
{
	return FString::Printf(TEXT("[%s(%s)]: %s"), Pass->GetName(), GetPipelineName(Pass->GetPipeline()), Name);
}

FRDGBarrierBatchBegin::FRDGBarrierBatchBegin(const FRDGPass* InPass, const TCHAR* InName, TOptional<ERDGPipeline> InOverridePipelineToEnd)
	: FRDGBarrierBatch(InPass, InName)
	, OverridePipelineToEnd(InOverridePipelineToEnd)
{}

FRDGBarrierBatchBegin::~FRDGBarrierBatchBegin()
{
	checkf(!Resources.Num() && !Transitions.Num(), TEXT("Begin barrier batch has unsubmitted transitions."));
	checkf(!Transition, TEXT("Begin barrier batch %s is currently active and was never ended."), *GetName());
}

void FRDGBarrierBatchBegin::AddTransition(FRDGParentResourceRef Resource, const FRHITransitionInfo& Info)
{
	check(Resource);
	checkf(!IsSubmitted(), TEXT("Attempting to add transition for resource '%s' into begin batch '%s', when the begin batch has already been submitted."), Resource->Name, *GetName());
	Resources.Add(Resource);
	Transitions.Add(Info);
}

void FRDGBarrierBatchBegin::Submit(FRHIComputeCommandList& RHICmdList)
{
	SetSubmitted();

	check(!Transition);
	if (Transitions.Num() || bUseCrossPipelineFence)
	{
		// Patch in the RHI resources
		for (int32 Index = 0; Index < Transitions.Num(); ++Index)
		{
			FRHIResource* RHIResource = GetRHIUnchecked<FRHIResource>(Resources[Index]);
			check(RHIResource);

			Transitions[Index].Resource = RHIResource;
		}

		const ERDGPipeline PassPipeline = Pass->GetPipeline();

		EResourceTransitionPipelineFlags Flags = EResourceTransitionPipelineFlags::NoFence;

		if (bUseCrossPipelineFence)
		{
			Flags = EResourceTransitionPipelineFlags::None;
		}

		const EResourceTransitionPipeline TransitionPipeline = GetResourceTransitionPipeline(
			PassPipeline,
			OverridePipelineToEnd ? OverridePipelineToEnd.GetValue() : PassPipeline);

		Transition = RHICreateResourceTransition(TransitionPipeline, Flags, Transitions);

		RHICmdList.BeginResourceTransitions(MakeArrayView(&Transition, 1));

		Transitions.Empty();
		Resources.Empty();
	}
}

FRDGBarrierBatchEnd::~FRDGBarrierBatchEnd()
{
	checkf(!Dependencies.Num(), TEXT("End barrier batch has unsubmitted dependencies."));
}

void FRDGBarrierBatchEnd::AddDependency(FRDGBarrierBatchBegin* BeginBatch)
{
	check(BeginBatch);
	checkf(!IsSubmitted(), TEXT("Attempting to add a dependency on begin batch '%s' into end batch '%s', when the end batch has already been submitted."), *BeginBatch->GetName(), *GetName());
	Dependencies.AddUnique(BeginBatch);
}

void FRDGBarrierBatchEnd::Submit(FRHIComputeCommandList& RHICmdList)
{
	SetSubmitted();

	TArray<const FRHITransition*, SceneRenderingAllocator> Transitions;
	Transitions.Reserve(Dependencies.Num());

	// Process dependencies with cross-pipeline fences first.
	for (FRDGBarrierBatchBegin* Dependent : Dependencies)
	{
		check(Dependent->IsSubmitted());

		if (Dependent->Transition && Dependent->bUseCrossPipelineFence)
		{
			Transitions.Add(Dependent->Transition);
			Dependent->Transition = nullptr;
		}
	}

	for (FRDGBarrierBatchBegin* Dependent : Dependencies)
	{
		if (Dependent->Transition)
		{
			Transitions.Add(Dependent->Transition);
			Dependent->Transition = nullptr;
		}
	}

	Dependencies.Empty();

	if (Transitions.Num())
	{
		RHICmdList.EndResourceTransitions(Transitions);
	}
}

FRDGPass::FRDGPass(
	FRDGEventName&& InName,
	FRDGPassParameterStruct InParameterStruct,
	ERDGPassFlags InFlags)
	: Name(Forward<FRDGEventName&&>(InName))
	, ParameterStruct(InParameterStruct)
	, Flags(InFlags)
	, Pipeline(EnumHasAnyFlags(Flags, ERDGPassFlags::AsyncCompute) ? ERDGPipeline::AsyncCompute : ERDGPipeline::Graphics)
	, bSkipRenderPassBegin(0)
	, bSkipRenderPassEnd(0)
	, bAsyncComputeBegin(0)
	, bAsyncComputeEnd(0)
	, bAsyncComputeEndExecute(0)
	, bGraphicsFork(0)
	, bGraphicsJoin(0)
	, bSubresourceTrackingRequired(0)
	, PrologueBarriersToBegin(this, TEXT("Prologue"))
	, PrologueBarriersToEnd(this, TEXT("Prologue"))
	, EpilogueBarriersToBeginForGraphics(this, TEXT("Epilogue (ForGraphics)"), ERDGPipeline::Graphics)
	, EpilogueBarriersToBeginForAsyncCompute(this, TEXT("Epilogue (ForAsyncCompute)"), ERDGPipeline::AsyncCompute)
{}

void FRDGPass::Execute(FRHIComputeCommandList& RHICmdList) const
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_FRDGPass_Execute);

	check(!EnumHasAnyFlags(Flags, ERDGPassFlags::Raster) || RHICmdList.IsInsideRenderPass());

	const FUniformBufferStaticBindings GlobalUniformBuffers = ParameterStruct.GetGlobalUniformBuffers();

	if (GlobalUniformBuffers.GetUniformBufferCount())
	{
		RHICmdList.SetGlobalUniformBuffers(GlobalUniformBuffers);
	}

	ExecuteImpl(RHICmdList);

	if (GlobalUniformBuffers.GetUniformBufferCount())
	{
		RHICmdList.SetGlobalUniformBuffers({});
	}

	ParameterStruct.ClearUniformBuffers();
}