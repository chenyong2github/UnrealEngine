// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	AGXGraphicsPipelineState.h: AGX RHI graphics pipeline state class.
=============================================================================*/

#pragma once

class FAGXGraphicsPipelineState : public FRHIGraphicsPipelineState
{
	friend class FAGXDynamicRHI;

public:
	virtual ~FAGXGraphicsPipelineState();

	FAGXShaderPipeline* GetPipeline();

	/** Cached vertex structure */
	TRefCountPtr<FAGXVertexDeclaration> VertexDeclaration;

	/** Cached shaders */
	TRefCountPtr<FAGXVertexShader> VertexShader;
	TRefCountPtr<FAGXPixelShader> PixelShader;
#if PLATFORM_SUPPORTS_GEOMETRY_SHADERS
	TRefCountPtr<FAGXGeometryShader> GeometryShader;
#endif

	/** Cached state objects */
	TRefCountPtr<FAGXDepthStencilState> DepthStencilState;
	TRefCountPtr<FAGXRasterizerState> RasterizerState;

	EPrimitiveType GetPrimitiveType()
	{
		return Initializer.PrimitiveType;
	}

	bool GetDepthBounds() const
	{
		return Initializer.bDepthBounds;
	}

private:
	// This can only be created through the RHI to make sure Compile() is called.
	FAGXGraphicsPipelineState(const FGraphicsPipelineStateInitializer& Init);

	// Compiles the underlying gpu pipeline objects. This must be called before usage.
	bool Compile();

	// Needed to runtime refine shaders currently.
	FGraphicsPipelineStateInitializer Initializer;

	FAGXShaderPipeline* PipelineState;
};
