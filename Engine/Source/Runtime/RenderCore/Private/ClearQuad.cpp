// Copyright Epic Games, Inc. All Rights Reserved.

#include "ClearQuad.h"
#include "Shader.h"
#include "RHIStaticStates.h"
#include "OneColorShader.h"
#include "PipelineStateCache.h"
#include "ClearReplacementShaders.h"
#include "RendererInterface.h"
#include "Logging/LogMacros.h"

TGlobalResource<FClearVertexBuffer> GClearVertexBuffer;

static TAutoConsoleVariable<int32> CVarFastClearUAVMaxSize(
	TEXT("r.RHI.FastClearUAVMaxSize"),
	0,
	TEXT("Max size in bytes to fast clear tiny UAV. 0 by default or when feature is not supported by the platform"),
	ECVF_RenderThreadSafe
);

DEFINE_LOG_CATEGORY_STATIC(LogClearQuad, Log, Log)

static void ClearQuadSetup( FRHICommandList& RHICmdList, bool bClearColor, int32 NumClearColors, const FLinearColor* ClearColorArray, bool bClearDepth, float Depth, bool bClearStencil, uint32 Stencil, TFunction<void(FGraphicsPipelineStateInitializer&)> PSOModifier = nullptr)
{
	if (UNLIKELY(!FApp::CanEverRender()))
	{
		return;
	}

	// Set new states
	FRHIBlendState* BlendStateRHI = bClearColor
		? TStaticBlendState<>::GetRHI()
		: TStaticBlendStateWriteMask<CW_NONE,CW_NONE,CW_NONE,CW_NONE,CW_NONE,CW_NONE,CW_NONE,CW_NONE>::GetRHI();
	
	FRHIDepthStencilState* DepthStencilStateRHI =
		(bClearDepth && bClearStencil)
			? TStaticDepthStencilState<
				true, CF_Always,
				true,CF_Always,SO_Replace,SO_Replace,SO_Replace,
				false,CF_Always,SO_Replace,SO_Replace,SO_Replace,
				0xff,0xff
				>::GetRHI()
			: bClearDepth
				? TStaticDepthStencilState<true, CF_Always>::GetRHI()
				: bClearStencil
					? TStaticDepthStencilState<
						false, CF_Always,
						true,CF_Always,SO_Replace,SO_Replace,SO_Replace,
						false,CF_Always,SO_Replace,SO_Replace,SO_Replace,
						0xff,0xff
						>::GetRHI()
					: TStaticDepthStencilState<false, CF_Always>::GetRHI();

	FGraphicsPipelineStateInitializer GraphicsPSOInit;
	RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);

	GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();
	GraphicsPSOInit.BlendState = BlendStateRHI;
	GraphicsPSOInit.DepthStencilState = DepthStencilStateRHI;

	auto* ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);

	// Set the new shaders
	TShaderMapRef<TOneColorVS<true> > VertexShader(ShaderMap);

	FOneColorPS* PixelShader = NULL;

	// Set the shader to write to the appropriate number of render targets
	// On AMD PC hardware, outputting to a color index in the shader without a matching render target set has a significant performance hit
	if (NumClearColors <= 1)
	{
		TShaderMapRef<TOneColorPixelShaderMRT<1> > MRTPixelShader(ShaderMap);
		PixelShader = *MRTPixelShader;
	}
	else if (NumClearColors == 2)
	{
		TShaderMapRef<TOneColorPixelShaderMRT<2> > MRTPixelShader(ShaderMap);
		PixelShader = *MRTPixelShader;
	}
	else if (NumClearColors == 3)
	{
		TShaderMapRef<TOneColorPixelShaderMRT<3> > MRTPixelShader(ShaderMap);
		PixelShader = *MRTPixelShader;
	}
	else if (NumClearColors == 4)
	{
		TShaderMapRef<TOneColorPixelShaderMRT<4> > MRTPixelShader(ShaderMap);
		PixelShader = *MRTPixelShader;
	}
	else if (NumClearColors == 5)
	{
		TShaderMapRef<TOneColorPixelShaderMRT<5> > MRTPixelShader(ShaderMap);
		PixelShader = *MRTPixelShader;
	}
	else if (NumClearColors == 6)
	{
		TShaderMapRef<TOneColorPixelShaderMRT<6> > MRTPixelShader(ShaderMap);
		PixelShader = *MRTPixelShader;
	}
	else if (NumClearColors == 7)
	{
		TShaderMapRef<TOneColorPixelShaderMRT<7> > MRTPixelShader(ShaderMap);
		PixelShader = *MRTPixelShader;
	}
	else if (NumClearColors == 8)
	{
		TShaderMapRef<TOneColorPixelShaderMRT<8> > MRTPixelShader(ShaderMap);
		PixelShader = *MRTPixelShader;
	}

	GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GetVertexDeclarationFVector4();
	GraphicsPSOInit.BoundShaderState.VertexShaderRHI = GETSAFERHISHADER_VERTEX(*VertexShader);
	GraphicsPSOInit.BoundShaderState.PixelShaderRHI = GETSAFERHISHADER_PIXEL(PixelShader);
	GraphicsPSOInit.PrimitiveType = PT_TriangleStrip;

	if (PSOModifier)
	{
		PSOModifier(GraphicsPSOInit);
	}

	SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);
	RHICmdList.SetStencilRef(Stencil);

	VertexShader->SetDepthParameter(RHICmdList, Depth);
	PixelShader->SetColors(RHICmdList, ClearColorArray, NumClearColors);
}

static void ClearUAVShader(FRHICommandList& RHICmdList, FRHIUnorderedAccessView* UnorderedAccessViewRHI, uint32 SizeInBytes, uint32 ClearValue, bool bBarriers = true)
{
	UE_CLOG((SizeInBytes & 0x3) != 0, LogClearQuad, Warning,
		TEXT("Buffer size is not a multiple of DWORDs. Up to 3 bytes after buffer end will also be cleared"));

	TShaderMapRef<FClearReplacementCS_Buffer_Uint_Bounds> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
	FRHIComputeShader* ShaderRHI = ComputeShader->GetComputeShader();
	
	uint32 NumDWordsToClear = FMath::DivideAndRoundUp(SizeInBytes, 4u);
	RHICmdList.SetComputeShader(ShaderRHI);

	SetShaderValue(RHICmdList, ShaderRHI, ComputeShader->GetClearValueParam(), ClearValue);
	SetShaderValue(RHICmdList, ShaderRHI, ComputeShader->GetMinBoundsParam(), FUintVector4(0, 0, 0, 0));
	SetShaderValue(RHICmdList, ShaderRHI, ComputeShader->GetMaxBoundsParam(), FUintVector4(NumDWordsToClear, 1, 1, 0));
	ComputeShader->SetResource(RHICmdList, UnorderedAccessViewRHI);

	RHICmdList.DispatchComputeShader(FMath::DivideAndRoundUp(NumDWordsToClear, ComputeShader->ThreadGroupSizeX), 1, 1);

	ComputeShader->FinalizeResource(RHICmdList, UnorderedAccessViewRHI);
}

void ClearUAV(FRHICommandList& RHICmdList, const FRWBufferStructured& StructuredBuffer, uint32 Value)
{
	
	if (StructuredBuffer.NumBytes <= uint32(CVarFastClearUAVMaxSize.GetValueOnRenderThread()))
	{
		uint32 Values[4] = { Value, Value, Value, Value };
		RHICmdList.ClearTinyUAV(StructuredBuffer.UAV, Values);
	}
	else
	{
		ClearUAVShader(RHICmdList, StructuredBuffer.UAV, StructuredBuffer.NumBytes, Value);
	}
}

void ClearUAV(FRHICommandList& RHICmdList, const FRWBuffer& Buffer, uint32 Value, bool bBarriers)
{
	if (Buffer.NumBytes <= uint32(CVarFastClearUAVMaxSize.GetValueOnRenderThread()))
	{
		uint32 Values[4] = { Value, Value, Value, Value };
		RHICmdList.ClearTinyUAV(Buffer.UAV, Values);
		check(bBarriers); //  TODO ClearTinyUAV is doing transitions as of today
	}
	else
	{
		ClearUAVShader(RHICmdList, Buffer.UAV, Buffer.NumBytes, Value, bBarriers);
	}
}

void ClearUAV(FRHICommandList& RHICmdList, FRHIUnorderedAccessView* Buffer, uint32 NumBytes, uint32 Value)
{
	if (NumBytes <= uint32(CVarFastClearUAVMaxSize.GetValueOnRenderThread()))
	{
		uint32 Values[4] = { Value, Value, Value, Value };
		RHICmdList.ClearTinyUAV(Buffer, Values);
	}
	else
	{
		ClearUAVShader(RHICmdList, Buffer, NumBytes, Value);
	}
}

template< typename T >
inline void ClearUAV_T(FRHICommandList& RHICmdList, FRHITexture* Texture, FRHIUnorderedAccessView* TextureUAV, const T(&ClearValues)[4])
{
	check( Texture );
	check( TextureUAV );

	if (auto Texture2d = Texture->GetTexture2D())
	{
		TShaderMapRef<TClearReplacementCS<EClearReplacementResourceType::Texture2D, TClearReplacementBase<T, 4>>> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		FRHIComputeShader* ShaderRHI = ComputeShader->GetComputeShader();
		RHICmdList.SetComputeShader(ShaderRHI);
		SetShaderValue(RHICmdList, ShaderRHI, ComputeShader->GetClearValueParam(), ClearValues);
		ComputeShader->SetResource(RHICmdList, TextureUAV);

		RHICmdList.DispatchComputeShader(
			FMath::DivideAndRoundUp(Texture2d->GetSizeX(), ComputeShader->ThreadGroupSizeX),
			FMath::DivideAndRoundUp(Texture2d->GetSizeY(), ComputeShader->ThreadGroupSizeY),
			1
		);

		ComputeShader->FinalizeResource(RHICmdList, TextureUAV);
	}
	else if (auto Texture2dArray = Texture->GetTexture2DArray())
	{
		typedef TClearReplacementCS<EClearReplacementResourceType::Texture2DArray, TClearReplacementBase<T, 4>> FClearShader;

		TShaderMapRef<FClearShader> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		FRHIComputeShader* ShaderRHI = ComputeShader->GetComputeShader();
		RHICmdList.SetComputeShader(ShaderRHI);
		SetShaderValue(RHICmdList, ShaderRHI, ComputeShader->GetClearValueParam(), ClearValues);
		ComputeShader->SetResource(RHICmdList, TextureUAV);

		RHICmdList.DispatchComputeShader(
			FMath::DivideAndRoundUp(Texture2dArray->GetSizeX(), ComputeShader->ThreadGroupSizeX),
			FMath::DivideAndRoundUp(Texture2dArray->GetSizeY(), ComputeShader->ThreadGroupSizeY),
			FMath::DivideAndRoundUp(Texture2dArray->GetSizeZ(), ComputeShader->ThreadGroupSizeZ)
		);

		ComputeShader->FinalizeResource(RHICmdList, TextureUAV);
	}
	else if (auto TextureCube = Texture->GetTextureCube())
	{
		typedef TClearReplacementCS<EClearReplacementResourceType::Texture2DArray, TClearReplacementBase<T, 4>> FClearShader;

		TShaderMapRef<FClearShader> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		FRHIComputeShader* ShaderRHI = ComputeShader->GetComputeShader();
		RHICmdList.SetComputeShader(ShaderRHI);
		SetShaderValue(RHICmdList, ShaderRHI, ComputeShader->GetClearValueParam(), ClearValues);
		ComputeShader->SetResource(RHICmdList, TextureUAV);

		// One complete cube of 6 faces
		const uint32 NumSlices = 6;

		RHICmdList.DispatchComputeShader(
			FMath::DivideAndRoundUp(TextureCube->GetSize(), ComputeShader->ThreadGroupSizeX),
			FMath::DivideAndRoundUp(TextureCube->GetSize(), ComputeShader->ThreadGroupSizeY),
			FMath::DivideAndRoundUp(NumSlices,              ComputeShader->ThreadGroupSizeZ)
		);

		ComputeShader->FinalizeResource(RHICmdList, TextureUAV);
	}
	else if (auto Texture3d = Texture->GetTexture3D())
	{
		typedef TClearReplacementCS<EClearReplacementResourceType::Texture3D, TClearReplacementBase<T, 4>> FClearShader;

		TShaderMapRef<FClearShader> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		FRHIComputeShader* ShaderRHI = ComputeShader->GetComputeShader();
		RHICmdList.SetComputeShader(ShaderRHI);
		SetShaderValue(RHICmdList, ShaderRHI, ComputeShader->GetClearValueParam(), ClearValues);
		ComputeShader->SetResource(RHICmdList, TextureUAV);

		RHICmdList.DispatchComputeShader(
			FMath::DivideAndRoundUp(Texture3d->GetSizeX(), ComputeShader->ThreadGroupSizeX),
			FMath::DivideAndRoundUp(Texture3d->GetSizeY(), ComputeShader->ThreadGroupSizeY),
			FMath::DivideAndRoundUp(Texture3d->GetSizeZ(), ComputeShader->ThreadGroupSizeZ)
		);

		ComputeShader->FinalizeResource(RHICmdList, TextureUAV);
	}
	else
	{
		check(0);
	}
}

void ClearUAV(FRHICommandList& RHICmdList, FRHIUnorderedAccessView* UAV, uint32 Width, uint32 Height, const FLinearColor& ClearColor)
{
	typedef TClearReplacementCS<EClearReplacementResourceType::Texture2D, TClearReplacementBase<float, 4>> FClearShader;

	TShaderMapRef<FClearShader> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
	FRHIComputeShader* ShaderRHI = ComputeShader->GetComputeShader();
	RHICmdList.SetComputeShader(ShaderRHI);
	SetShaderValue(RHICmdList, ShaderRHI, ComputeShader->GetClearValueParam(), ClearColor);
	ComputeShader->SetResource(RHICmdList, UAV);

	RHICmdList.DispatchComputeShader(
		FMath::DivideAndRoundUp(Width, ComputeShader->ThreadGroupSizeX),
		FMath::DivideAndRoundUp(Height, ComputeShader->ThreadGroupSizeY),
		1
	);

	ComputeShader->FinalizeResource(RHICmdList, UAV);
}

void ClearUAV(FRHICommandList& RHICmdList, const FTextureRWBuffer2D& Buffer, FLinearColor Value)
{
	ClearUAV_T(RHICmdList, Buffer.Buffer, Buffer.UAV, reinterpret_cast<const float(&)[4]>(Value));
}

void ClearUAV(FRHICommandList& RHICmdList, const FTextureRWBuffer3D& Buffer, FLinearColor Value)
{
	ClearUAV_T(RHICmdList, Buffer.Buffer, Buffer.UAV, reinterpret_cast<const float(&)[4]>(Value));
}

void ClearUAV(FRHICommandList& RHICmdList, const FSceneRenderTargetItem& RenderTargetItem, const float(&ClearValues)[4])
{
	ClearUAV_T(RHICmdList, RenderTargetItem.TargetableTexture, RenderTargetItem.UAV, ClearValues);
}

void ClearUAV(FRHICommandList& RHICmdList, const FSceneRenderTargetItem& RenderTargetItem, const uint32(&ClearValues)[4])
{
	ClearUAV_T(RHICmdList, RenderTargetItem.TargetableTexture, RenderTargetItem.UAV, ClearValues);
}

void ClearUAV(FRHICommandList& RHICmdList, const FSceneRenderTargetItem& RenderTargetItem, const FLinearColor& ClearColor)
{
	ClearUAV_T(RHICmdList, RenderTargetItem.TargetableTexture, RenderTargetItem.UAV, reinterpret_cast<const float(&)[4]>(ClearColor));
}

void ClearUAV(FRHICommandList& RHICmdList, FRHITexture* Texture, FRHIUnorderedAccessView* TextureUAV, const float(&ClearValues)[4])
{
	ClearUAV_T(RHICmdList, Texture, TextureUAV, ClearValues);
}

void ClearUAV(FRHICommandList& RHICmdList, FRHITexture* Texture, FRHIUnorderedAccessView* TextureUAV, const uint32(&ClearValues)[4])
{
	ClearUAV_T(RHICmdList, Texture, TextureUAV, ClearValues);
}

void ClearUAV(FRHICommandList& RHICmdList, FRHITexture* Texture, FRHIUnorderedAccessView* TextureUAV, const FLinearColor& ClearColor)
{
	ClearUAV_T(RHICmdList, Texture, TextureUAV, reinterpret_cast<const float(&)[4]>(ClearColor));
}

void DrawClearQuadMRT(FRHICommandList& RHICmdList, bool bClearColor, int32 NumClearColors, const FLinearColor* ClearColorArray, bool bClearDepth, float Depth, bool bClearStencil, uint32 Stencil)
{
	ClearQuadSetup(RHICmdList, bClearColor, NumClearColors, ClearColorArray, bClearDepth, Depth, bClearStencil, Stencil);

	RHICmdList.SetStreamSource(0, GClearVertexBuffer.VertexBufferRHI, 0);
	RHICmdList.DrawPrimitive(0, 2, 1);
}

void DrawClearQuadMRT(FRHICommandList& RHICmdList, bool bClearColor, int32 NumClearColors, const FLinearColor* ClearColorArray, bool bClearDepth, float Depth, bool bClearStencil, uint32 Stencil, FClearQuadCallbacks ClearQuadCallbacks)
{
	ClearQuadSetup(RHICmdList, bClearColor, NumClearColors, ClearColorArray, bClearDepth, Depth, bClearStencil, Stencil, ClearQuadCallbacks.PSOModifier);

	if (ClearQuadCallbacks.PreClear)
	{
		ClearQuadCallbacks.PreClear(RHICmdList);
	}

	// Draw a fullscreen quad without a hole
	RHICmdList.SetStreamSource(0, GClearVertexBuffer.VertexBufferRHI, 0);
	RHICmdList.DrawPrimitive(0, 2, 1);

	if (ClearQuadCallbacks.PostClear)
	{
		ClearQuadCallbacks.PostClear(RHICmdList);
	}
}

void DrawClearQuadMRT(FRHICommandList& RHICmdList, bool bClearColor, int32 NumClearColors, const FLinearColor* ClearColorArray, bool bClearDepth, float Depth, bool bClearStencil, uint32 Stencil, FIntPoint ViewSize, FIntRect ExcludeRect)
{
	if (ExcludeRect.Min == FIntPoint::ZeroValue && ExcludeRect.Max == ViewSize)
	{
		// Early out if the entire surface is excluded
		return;
	}

	ClearQuadSetup(RHICmdList, bClearColor, NumClearColors, ClearColorArray, bClearDepth, Depth, bClearStencil, Stencil);

	// Draw a fullscreen quad
	if (ExcludeRect.Width() > 0 && ExcludeRect.Height() > 0)
	{
		// with a hole in it
		FVector4 OuterVertices[4];
		OuterVertices[0].Set(-1.0f, 1.0f, Depth, 1.0f);
		OuterVertices[1].Set(1.0f, 1.0f, Depth, 1.0f);
		OuterVertices[2].Set(1.0f, -1.0f, Depth, 1.0f);
		OuterVertices[3].Set(-1.0f, -1.0f, Depth, 1.0f);

		float InvViewWidth = 1.0f / ViewSize.X;
		float InvViewHeight = 1.0f / ViewSize.Y;
		FVector4 FractionRect = FVector4(ExcludeRect.Min.X * InvViewWidth, ExcludeRect.Min.Y * InvViewHeight, (ExcludeRect.Max.X - 1) * InvViewWidth, (ExcludeRect.Max.Y - 1) * InvViewHeight);

		FVector4 InnerVertices[4];
		InnerVertices[0].Set(FMath::Lerp(-1.0f, 1.0f, FractionRect.X), FMath::Lerp(1.0f, -1.0f, FractionRect.Y), Depth, 1.0f);
		InnerVertices[1].Set(FMath::Lerp(-1.0f, 1.0f, FractionRect.Z), FMath::Lerp(1.0f, -1.0f, FractionRect.Y), Depth, 1.0f);
		InnerVertices[2].Set(FMath::Lerp(-1.0f, 1.0f, FractionRect.Z), FMath::Lerp(1.0f, -1.0f, FractionRect.W), Depth, 1.0f);
		InnerVertices[3].Set(FMath::Lerp(-1.0f, 1.0f, FractionRect.X), FMath::Lerp(1.0f, -1.0f, FractionRect.W), Depth, 1.0f);

		FRHIResourceCreateInfo CreateInfo;
		FVertexBufferRHIRef VertexBufferRHI = RHICreateVertexBuffer(sizeof(FVector4) * 10, BUF_Volatile, CreateInfo);
		void* VoidPtr = RHILockVertexBuffer(VertexBufferRHI, 0, sizeof(FVector4) * 10, RLM_WriteOnly);
		
		FVector4* Vertices = reinterpret_cast<FVector4*>(VoidPtr);
		Vertices[0] = OuterVertices[0];
		Vertices[1] = InnerVertices[0];
		Vertices[2] = OuterVertices[1];
		Vertices[3] = InnerVertices[1];
		Vertices[4] = OuterVertices[2];
		Vertices[5] = InnerVertices[2];
		Vertices[6] = OuterVertices[3];
		Vertices[7] = InnerVertices[3];
		Vertices[8] = OuterVertices[0];
		Vertices[9] = InnerVertices[0];

		RHIUnlockVertexBuffer(VertexBufferRHI);
		RHICmdList.SetStreamSource(0, VertexBufferRHI, 0);

		RHICmdList.DrawPrimitive(0, 8, 1);

		VertexBufferRHI.SafeRelease();
	}
	else
	{
		// without a hole
		RHICmdList.SetStreamSource(0, GClearVertexBuffer.VertexBufferRHI, 0);
		RHICmdList.DrawPrimitive(0, 2, 1);
	}
}