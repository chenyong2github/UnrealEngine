// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RendererInterface.h"
#include "PostProcess/RenderingCompositionGraph.h"

// PostProcess node to manage the ShaderPrint debugging output
class FRCPassPostProcessShaderPrint : public TRenderingCompositePassBase<1, 1>
{
public:
	static bool IsEnabled(FViewInfo const& View);

	virtual bool FrameBufferBlendingWithInput0() const override { return true; }
	virtual void Process(FRenderingCompositePassContext& Context) override;
	virtual void Release() override { delete this; }
	virtual FPooledRenderTargetDesc ComputeOutputDesc(EPassOutputId InPassOutputId) const override;
};
