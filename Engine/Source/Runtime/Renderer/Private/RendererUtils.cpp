// Copyright Epic Games, Inc. All Rights Reserved.

#include "RendererUtils.h"
#include "RenderTargetPool.h"

template <uint32 NumRenderTargets>
class TRTWriteMaskDecodeCS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(TRTWriteMaskDecodeCS, Global);

public:
	static const uint32 ThreadGroupSizeX = 8;
	static const uint32 ThreadGroupSizeY = 8;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return RHISupportsRenderTargetWriteMask(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.SetDefine(TEXT("NUM_RENDER_TARGETS"), NumRenderTargets);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZEX"), ThreadGroupSizeX);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZEY"), ThreadGroupSizeY);

		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}

	TRTWriteMaskDecodeCS() {}

	FShaderParameter PlatformDataParam;
	FShaderParameter OutCombinedRTWriteMask;	// UAV
	FShaderResourceParameter RTWriteMaskInputs;	// SRV 

	TRTWriteMaskDecodeCS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		PlatformDataParam.Bind(Initializer.ParameterMap, TEXT("PlatformData"), SPF_Mandatory);
		OutCombinedRTWriteMask.Bind(Initializer.ParameterMap, TEXT("OutCombinedRTWriteMask"), SPF_Mandatory);
		RTWriteMaskInputs.Bind(Initializer.ParameterMap, TEXT("RTWriteMaskInputs"), SPF_Mandatory);
	}

	virtual bool Serialize(FArchive& Ar) override
	{
		bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);
		Ar << PlatformDataParam;
		Ar << OutCombinedRTWriteMask;
		Ar << RTWriteMaskInputs;

		return bShaderHasOutdatedParameters;
	}

	void SetParameters(FRHICommandListImmediate& RHICmdList, void* PlatformDataPtr, uint32 PlatformDataSize, IPooledRenderTarget* InRenderTargets[NumRenderTargets], IPooledRenderTarget* OutRTWriteMask)
	{
		RHICmdList.SetShaderParameter(GetComputeShader(), PlatformDataParam.GetBufferIndex(), PlatformDataParam.GetBaseIndex(), PlatformDataSize, PlatformDataPtr);

		for (int32 Index = 0; Index < NumRenderTargets; ++Index)
		{
			RHICmdList.SetShaderResourceViewParameter(GetComputeShader(), RTWriteMaskInputs.GetBaseIndex() + Index, InRenderTargets[Index]->GetRenderTargetItem().RTWriteMaskSRV);
		}

		RHICmdList.SetUAVParameter(GetComputeShader(), OutCombinedRTWriteMask.GetBaseIndex(), OutRTWriteMask->GetRenderTargetItem().UAV);
	}

	void UnsetParameters(FRHICommandListImmediate& RHICmdList)
	{
		RHICmdList.SetUAVParameter(GetComputeShader(), OutCombinedRTWriteMask.GetBaseIndex(), nullptr);
	}
};

IMPLEMENT_SHADER_TYPE(template<>, TRTWriteMaskDecodeCS<1>, TEXT("/Engine/Private/RTWriteMaskDecode.usf"), TEXT("RTWriteMaskDecodeMain"), SF_Compute);
IMPLEMENT_SHADER_TYPE(template<>, TRTWriteMaskDecodeCS<3>, TEXT("/Engine/Private/RTWriteMaskDecode.usf"), TEXT("RTWriteMaskDecodeMain"), SF_Compute);

template <uint32 NumRenderTargets>
void FRenderTargetWriteMask::Decode(FRHICommandListImmediate& RHICmdList, TShaderMap<FGlobalShaderType>* ShaderMap, IPooledRenderTarget* InRenderTargets[NumRenderTargets], TRefCountPtr<IPooledRenderTarget>& OutRTWriteMask, uint32 RTWriteMaskFastVRamConfig, const TCHAR* RTWriteMaskDebugName)
{
	static_assert(NumRenderTargets == 1 || NumRenderTargets == 3, TEXT("FRenderTargetWriteMask::Decode only supports 1 or 3 render targets"));
	check(RHISupportsRenderTargetWriteMask(GMaxRHIShaderPlatform));

	// Do a metadata transition on all the input textures
	FRHITexture* Textures[NumRenderTargets];
	for (int32 Index = 0; Index < NumRenderTargets; ++Index)
	{
		Textures[Index] = InRenderTargets[Index]->GetRenderTargetItem().ShaderResourceTexture;
	}
	RHICmdList.TransitionResources(EResourceTransitionAccess::EMetaData, Textures, UE_ARRAY_COUNT(Textures));

	const uint32 MaskTileSizeX = 8;
	const uint32 MaskTileSizeY = 8;

	FTextureRHIRef RenderTarget0Texture = InRenderTargets[0]->GetRenderTargetItem().TargetableTexture;
	
	FIntPoint RTWriteMaskDims(
		FMath::DivideAndRoundUp(RenderTarget0Texture->GetTexture2D()->GetSizeX(), MaskTileSizeX),
		FMath::DivideAndRoundUp(RenderTarget0Texture->GetTexture2D()->GetSizeY(), MaskTileSizeY));

	// allocate the Mask from the render target pool.
	FPooledRenderTargetDesc MaskDesc(FPooledRenderTargetDesc::Create2DDesc(RTWriteMaskDims,
		NumRenderTargets <= 2 ? PF_R8_UINT : PF_R16_UINT,
		FClearValueBinding::None,
		TexCreate_None | RTWriteMaskFastVRamConfig,
		TexCreate_UAV | TexCreate_RenderTargetable,
		false,
		1,
		false));

	GRenderTargetPool.FindFreeElement(RHICmdList, MaskDesc, OutRTWriteMask, RTWriteMaskDebugName);
	RHICmdList.TransitionResource(EResourceTransitionAccess::EWritable, EResourceTransitionPipeline::EGfxToCompute, OutRTWriteMask->GetRenderTargetItem().UAV);

	// Retrieve the platform specific data that the decode shader needs
	void* PlatformDataPtr = nullptr;
	uint32 PlatformDataSize = 0;
	RenderTarget0Texture->GetWriteMaskProperties(PlatformDataPtr, PlatformDataSize);
	check(PlatformDataSize > 0);

	if (PlatformDataPtr == nullptr)
	{
		// If the returned pointer was null, the platform RHI wants us to allocate the memory instead.
		PlatformDataPtr = alloca(PlatformDataSize);
		RenderTarget0Texture->GetWriteMaskProperties(PlatformDataPtr, PlatformDataSize);
	}

	typedef TRTWriteMaskDecodeCS<NumRenderTargets> FRTWriteMaskDecodeCS;
	TShaderMapRef<FRTWriteMaskDecodeCS> DecodeCS(ShaderMap);
	RHICmdList.SetComputeShader(DecodeCS->GetComputeShader());
	DecodeCS->SetParameters(RHICmdList, PlatformDataPtr, PlatformDataSize, InRenderTargets, OutRTWriteMask);
	
	RHICmdList.DispatchComputeShader(
		FMath::DivideAndRoundUp((uint32)RTWriteMaskDims.X, FRTWriteMaskDecodeCS::ThreadGroupSizeX),
		FMath::DivideAndRoundUp((uint32)RTWriteMaskDims.Y, FRTWriteMaskDecodeCS::ThreadGroupSizeY),
		1);

	RHICmdList.TransitionResource(EResourceTransitionAccess::EReadable, EResourceTransitionPipeline::EComputeToGfx, OutRTWriteMask->GetRenderTargetItem().UAV);
}

template RENDERER_API void FRenderTargetWriteMask::Decode<1>(FRHICommandListImmediate&, TShaderMap<FGlobalShaderType>*, IPooledRenderTarget*[1], TRefCountPtr<IPooledRenderTarget>&, uint32, const TCHAR*);
template RENDERER_API void FRenderTargetWriteMask::Decode<3>(FRHICommandListImmediate&, TShaderMap<FGlobalShaderType>*, IPooledRenderTarget*[3], TRefCountPtr<IPooledRenderTarget>&, uint32, const TCHAR*);
