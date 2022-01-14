// Copyright Epic Games, Inc. All Rights Reserved.

/*==============================================================================
	AGXTransitionData.h: AGX RHI Resource Transition Definitions.
==============================================================================*/

#pragma once


//------------------------------------------------------------------------------

#pragma mark - AGX RHI Resource Transition Info Array Type Definition -


typedef TArray<FRHITransitionInfo, TInlineAllocator<4> > FAGXTransitionInfoArray;


//------------------------------------------------------------------------------

#pragma mark - AGX RHI Resource Transition Data Class -


class FAGXTransitionData
{
public:
	explicit FAGXTransitionData(ERHIPipeline                         InSrcPipelines,
								ERHIPipeline                         InDstPipelines,
								ERHITransitionCreateFlags            InCreateFlags,
								TArrayView<const FRHITransitionInfo> InInfos);

	// The default destructor is sufficient.
	~FAGXTransitionData() = default;

	// Disallow default, copy and move constructors.
	FAGXTransitionData()                           = delete;
	FAGXTransitionData(const FAGXTransitionData&)  = delete;
	FAGXTransitionData(const FAGXTransitionData&&) = delete;

	// Begin resource transitions.
	void BeginResourceTransitions() const;

	// End resource transitions.
	void EndResourceTransitions() const;

private:
	ERHIPipeline              SrcPipelines   = ERHIPipeline::Num;
	ERHIPipeline              DstPipelines   = ERHIPipeline::Num;
	ERHITransitionCreateFlags CreateFlags    = ERHITransitionCreateFlags::None;
	bool                      bCrossPipeline = false;
	FAGXTransitionInfoArray   Infos          = {};
};
