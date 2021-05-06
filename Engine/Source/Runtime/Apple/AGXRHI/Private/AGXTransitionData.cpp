// Copyright Epic Games, Inc. All Rights Reserved.

/*==============================================================================
	AGXTransitionData.cpp: AGX RHI Resource Transition Implementation.
==============================================================================*/

#include "AGXRHIPrivate.h"
#include "AGXTransitionData.h"


//------------------------------------------------------------------------------

#pragma mark - AGX RHI Resource Transition Data Definitions -

#define UE_METAL_TRANSITION_DATA_USES_EXPLICIT_FENCING					0


//------------------------------------------------------------------------------

#pragma mark - AGX RHI Resource Transition Data Implementation -


FAGXTransitionData::FAGXTransitionData(ERHIPipeline                         InSrcPipelines,
										   ERHIPipeline                         InDstPipelines,
										   ERHITransitionCreateFlags            InCreateFlags,
										   TArrayView<const FRHITransitionInfo> InInfos)
{
	SrcPipelines   = InSrcPipelines;
	DstPipelines   = InDstPipelines;
	CreateFlags    = InCreateFlags;

	bCrossPipeline = (SrcPipelines != DstPipelines);

	Infos.Append(InInfos.GetData(), InInfos.Num());

	// TODO: Determine whether the AGX RHI needs to create a separate, per-transition fence.
#if UE_METAL_TRANSITION_DATA_USES_EXPLICIT_FENCING
	if (bCrossPipeline && !EnumHasAnyFlags(CreateFlags, ERHITransitionCreateFlags::NoFence))
	{
		// Get the current context pointer.
		FAGXContext* Context = GetAGXDeviceContext().GetCurrentContext();

		// Get the current render pass fence.
		TRefCountPtr<FAGXFence> const& MetalFence = Context->GetCurrentRenderPass().End();

		// Write it again as we may wait on this fence in two different encoders.
		Context->GetCurrentRenderPass().Update(MetalFence);

		// Write it into the transition data.
		Fence = MetalFence;
		Fence->AddRef();

		if (GSupportsEfficientAsyncCompute)
		{
			static_cast<FAGXRHICommandContext*>(RHIGetDefaultContext())->RHISubmitCommandsHint();
		}
	}
#endif // UE_METAL_TRANSITION_DATA_USES_EXPLICIT_FENCING
}

void FAGXTransitionData::BeginResourceTransitions() const
{
	// TODO: Determine whether the AGX RHI needs to do anything here.
#if UE_METAL_TRANSITION_DATA_USES_EXPLICIT_FENCING
	if (Fence.IsValid())
	{
		FAGXContext* Context = GetAGXDeviceContext().GetCurrentContext();

		if (Context->GetCurrentCommandBuffer())
		{
			Context->SubmitCommandsHint(EAGXSubmitFlagsNone);
		}

		Context->GetCurrentRenderPass().Begin(Fence);
	}
#endif // UE_METAL_TRANSITION_DATA_USES_EXPLICIT_FENCING
}

void FAGXTransitionData::EndResourceTransitions() const
{
	// No action necessary for same pipe transitions
	if (SrcPipelines == DstPipelines)
	{
		return;
	}

	for (const auto& Info : Infos)
	{
		if (nullptr == Info.Resource)
		{
			continue;
		}

		switch (Info.Type)
		{
			case FRHITransitionInfo::EType::UAV:
				GetAGXDeviceContext().TransitionResource(Info.UAV);
				break;

			case FRHITransitionInfo::EType::Buffer:
				GetAGXDeviceContext().TransitionRHIResource(Info.Buffer);
				break;

			case FRHITransitionInfo::EType::Texture:
				GetAGXDeviceContext().TransitionResource(Info.Texture);
				break;

			default:
				checkNoEntry();
				break;
		}
	}
}
