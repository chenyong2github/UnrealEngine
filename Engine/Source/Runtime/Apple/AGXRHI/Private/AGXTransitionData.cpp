// Copyright Epic Games, Inc. All Rights Reserved.

/*==============================================================================
	AGXTransitionData.cpp: AGX RHI Resource Transition Implementation.
==============================================================================*/

#include "AGXRHIPrivate.h"
#include "AGXTransitionData.h"


//------------------------------------------------------------------------------

#pragma mark - AGX RHI Resource Transition Data Definitions -

// TODO: Put AGXRHI resource transition data definitions here


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
}

void FAGXTransitionData::BeginResourceTransitions() const
{
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
