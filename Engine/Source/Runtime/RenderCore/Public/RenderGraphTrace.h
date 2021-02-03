// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RenderGraphDefinitions.h"

#if RDG_ENABLE_TRACE

UE_TRACE_CHANNEL_EXTERN(RDGChannel, RENDERCORE_API);

class RENDERCORE_API FRDGTrace
{
public:
	void OutputGraphBegin();
	void OutputGraphEnd(const FRDGBuilder& GraphBuilder);

private:
	uint64 GraphStartCycles{};
};

#endif