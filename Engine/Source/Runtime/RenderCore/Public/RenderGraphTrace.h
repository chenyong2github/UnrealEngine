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

	void AddResource(FRDGParentResource* Resource);
	void AddTexturePassDependency(FRDGTexture* Texture, FRDGPass* Pass);
	void AddBufferPassDependency(FRDGBuffer* Buffer, FRDGPass* Pass);

private:
	uint64 GraphStartCycles{};
	uint32 ResourceOrder{};
};

#endif