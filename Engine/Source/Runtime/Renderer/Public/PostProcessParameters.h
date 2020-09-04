// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	PostProcessParameters.h: Shader base classes
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "ShaderParameters.h"
#include "RHIStaticStates.h"

class FShaderParameterMap;
struct FRenderingCompositePassContext;

// This is the index for the texture input of this pass.
// More that that should not be needed.
// Could be an uint32 but for better readability and type safety it's an enum.
// Counting starts from 0 in consecutive order.
enum EPassInputId
{
	ePId_Input0,
	ePId_Input1,
	ePId_Input2,
	ePId_Input3,
	ePId_Input4,
	ePId_Input5,
	ePId_Input6,
	ePId_Input7,
	ePId_Input8,
	ePId_Input9,
	ePId_Input10,
/*	ePId_Input11,
	ePId_Input12,
	ePId_Input13,
	ePId_Input14,
	ePId_Input15*/

	// to get the total count of inputs
	ePId_Input_MAX
};

// Usually the same as the MRT number but it doesn't have to be implemented as MRT.
// More that that should not be needed.
// Could be an uint32 but for better readability and type safety it's an enum.
// Counting starts from 0 in consecutive order.
enum EPassOutputId
{
	ePId_Output0,
	ePId_Output1,
	ePId_Output2,
	ePId_Output3,
	ePId_Output4,
	ePId_Output5,
	ePId_Output6,
	ePId_Output7
};

enum EFallbackColor
{
	// float4(0,0,0,0) BlackDummy
	eFC_0000,
	// float4(1,1,1,1) WhiteDummy 
	eFC_1111,
	// float4(0,0,0,1) BlackAlphaOneDummy
	eFC_0001,
};

// currently hard coded to 4 input textures
// convenient but not the most optimized solution
struct RENDERER_API FPostProcessPassParameters
{
	DECLARE_TYPE_LAYOUT(FPostProcessPassParameters, NonVirtual);
public:
	/** Initialization constructor. */
	 void Bind(const FShaderParameterMap& ParameterMap);

	/** Set the pixel shader parameter values. */
	template <typename TRHICmdList>
	void SetPS(TRHICmdList& RHICmdList, FRHIPixelShader* ShaderRHI, const FRenderingCompositePassContext& Context, FRHISamplerState* Filter = TStaticSamplerState<>::GetRHI(), EFallbackColor FallbackColor = eFC_0000, FRHISamplerState** FilterOverrideArray = 0);

	/** Set the compute shader parameter values. */
	template< typename TRHICmdList >
	void SetCS(FRHIComputeShader* ShaderRHI, const FRenderingCompositePassContext& Context, TRHICmdList& RHICmdList, FRHISamplerState* Filter = TStaticSamplerState<>::GetRHI(), EFallbackColor FallbackColor = eFC_0000, FRHISamplerState** FilterOverrideArray = 0);

	/** Set the vertex shader parameter values. */
	void SetVS(FRHIVertexShader* ShaderRHI, const FRenderingCompositePassContext& Context, FRHISamplerState* Filter = TStaticSamplerState<>::GetRHI(), EFallbackColor FallbackColor = eFC_0000, FRHISamplerState** FilterOverrideArray = 0);

	/** Serializer. */
	RENDERER_API friend FArchive& operator<<(FArchive& Ar,FPostProcessPassParameters& P);

private:
	LAYOUT_FIELD(FShaderParameter, ViewportSize);
	LAYOUT_FIELD(FShaderParameter, ViewportRect);
	LAYOUT_ARRAY(FShaderResourceParameter, PostprocessInputParameter, ePId_Input_MAX);
	LAYOUT_ARRAY(FShaderResourceParameter, PostprocessInputParameterSampler, ePId_Input_MAX);
	LAYOUT_ARRAY(FShaderParameter, PostprocessInputSizeParameter, ePId_Input_MAX);
	LAYOUT_ARRAY(FShaderParameter, PostProcessInputMinMaxParameter, ePId_Input_MAX);
	LAYOUT_FIELD(FShaderParameter, ScreenPosToPixel);
	LAYOUT_FIELD(FShaderParameter, SceneColorBufferUVViewport);
	
public:
	// @param Filter can be 0 if FilterOverrideArray is used
	// @param FilterOverrideArray can be 0 if Filter is used
	template< typename TRHIShader, typename TRHICmdList >
	void Set(
		TRHICmdList& RHICmdList,
		TRHIShader* ShaderRHI,
		const FRenderingCompositePassContext& Context,
		FRHISamplerState* Filter,
		EFallbackColor FallbackColor,
		FRHISamplerState** FilterOverrideArray = 0
	);
};
