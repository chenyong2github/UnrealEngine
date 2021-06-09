// Copyright Epic Games, Inc. All Rights Reserved.

#include "CompositionLighting/PostProcessDeferredDecals.h"
#include "SceneUtils.h"
#include "ScenePrivate.h"
#include "DecalRenderingShared.h"
#include "ClearQuad.h"
#include "PipelineStateCache.h"
#include "VisualizeTexture.h"
#include "RendererUtils.h"
#include "SceneTextureParameters.h"

static TAutoConsoleVariable<float> CVarStencilSizeThreshold(
	TEXT("r.Decal.StencilSizeThreshold"),
	0.1f,
	TEXT("Control a per decal stencil pass that allows to large (screen space) decals faster. It adds more overhead per decals so this\n")
	TEXT("  <0: optimization is disabled\n")
	TEXT("   0: optimization is enabled no matter how small (screen space) the decal is\n")
	TEXT("0..1: optimization is enabled, value defines the minimum size (screen space) to trigger the optimization (default 0.1)")
);

IMPLEMENT_STATIC_UNIFORM_BUFFER_STRUCT(FDecalPassUniformParameters, "DecalPass", SceneTextures);

FDeferredDecalPassTextures GetDeferredDecalPassTextures(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View)
{
	FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(GraphBuilder.RHICmdList);

	FDeferredDecalPassTextures PassTextures;

	auto* Parameters = GraphBuilder.AllocParameters<FDecalPassUniformParameters>();
	const ESceneTextureSetupMode TextureReadAccess = ESceneTextureSetupMode::GBufferA | ESceneTextureSetupMode::SceneDepth | ESceneTextureSetupMode::CustomDepth;
	SetupSceneTextureUniformParameters(GraphBuilder, View.FeatureLevel, TextureReadAccess, Parameters->SceneTextures);
	Parameters->EyeAdaptationTexture = GetEyeAdaptationTexture(GraphBuilder, View);
	PassTextures.DecalPassUniformBuffer = GraphBuilder.CreateUniformBuffer(Parameters);

	PassTextures.Depth = RegisterExternalTextureMSAA(GraphBuilder, SceneContext.SceneDepthZ);
	PassTextures.Color = TryRegisterExternalTexture(GraphBuilder, SceneContext.GetSceneColor(), ERenderTargetTexture::Targetable);
	PassTextures.GBufferA = TryRegisterExternalTexture(GraphBuilder, SceneContext.GBufferA);
	PassTextures.GBufferB = TryRegisterExternalTexture(GraphBuilder, SceneContext.GBufferB);
	PassTextures.GBufferC = TryRegisterExternalTexture(GraphBuilder, SceneContext.GBufferC);
	PassTextures.GBufferE = TryRegisterExternalTexture(GraphBuilder, SceneContext.GBufferE);

	return PassTextures;
}

void GetDeferredDecalPassParameters(
	const FViewInfo& View,
	FDeferredDecalPassTextures& Textures,
	FDecalRenderingCommon::ERenderTargetMode RenderTargetMode,
	FDeferredDecalPassParameters& PassParameters)
{
	const bool bWritingToGBufferA = IsWritingToGBufferA(RenderTargetMode);
	const bool bWritingToDepth = IsWritingToDepth(RenderTargetMode);

	PassParameters.DecalPass = Textures.DecalPassUniformBuffer;

	FRDGTextureRef DepthTexture = Textures.Depth.Target;

	FRenderTargetBindingSlots& RenderTargets = PassParameters.RenderTargets;

	uint32 ColorTargetIndex = 0;

	const auto AddColorTarget = [&](FRDGTextureRef Texture, ERenderTargetLoadAction LoadAction = ERenderTargetLoadAction::ELoad)
	{
		checkf(Texture, TEXT("Attempting to bind decal render targets, but the texture is null."));
		RenderTargets[ColorTargetIndex++] = FRenderTargetBinding(Texture, LoadAction);
	};

	switch (RenderTargetMode)
	{
	case FDecalRenderingCommon::RTM_SceneColorAndGBufferWithNormal:
	case FDecalRenderingCommon::RTM_SceneColorAndGBufferNoNormal:
		AddColorTarget(Textures.Color);
		if (bWritingToGBufferA)
		{
			AddColorTarget(Textures.GBufferA);
		}
		AddColorTarget(Textures.GBufferB);
		AddColorTarget(Textures.GBufferC);
		break;

	case FDecalRenderingCommon::RTM_SceneColorAndGBufferDepthWriteWithNormal:
	case FDecalRenderingCommon::RTM_SceneColorAndGBufferDepthWriteNoNormal:
		AddColorTarget(Textures.Color);
		if (bWritingToGBufferA)
		{
			AddColorTarget(Textures.GBufferA);
		}
		AddColorTarget(Textures.GBufferB);
		AddColorTarget(Textures.GBufferC);
		AddColorTarget(Textures.GBufferE);
		break;

	case FDecalRenderingCommon::RTM_GBufferNormal:
		AddColorTarget(Textures.GBufferA);
		break;

	case FDecalRenderingCommon::RTM_SceneColor:
		AddColorTarget(Textures.Color);
		break;

	case FDecalRenderingCommon::RTM_DBuffer:
	{
		AddColorTarget(Textures.DBufferA, Textures.DBufferLoadAction);
		AddColorTarget(Textures.DBufferB, Textures.DBufferLoadAction);
		AddColorTarget(Textures.DBufferC, Textures.DBufferLoadAction);
		if (Textures.DBufferMask)
		{
			AddColorTarget(Textures.DBufferMask, Textures.DBufferLoadAction);
		}

		// D-Buffer always uses the resolved depth; no MSAA.
		DepthTexture = Textures.Depth.Resolve;

		if (!View.Family->bMultiGPUForkAndJoin)
		{
			Textures.DBufferLoadAction = ERenderTargetLoadAction::ELoad;
		}
		break;
	}
	case FDecalRenderingCommon::RTM_AmbientOcclusion:
	{
		AddColorTarget(Textures.ScreenSpaceAO);
		break;
	}

	default:
		checkNoEntry();
	}

	RenderTargets.DepthStencil = FDepthStencilBinding(
		DepthTexture,
		ERenderTargetLoadAction::ELoad,
		ERenderTargetLoadAction::ELoad,
		bWritingToDepth ? FExclusiveDepthStencil::DepthWrite_StencilWrite : FExclusiveDepthStencil::DepthRead_StencilWrite);
}

enum EDecalDepthInputState
{
	DDS_Undefined,
	DDS_Always,
	DDS_DepthTest,
	DDS_DepthAlways_StencilEqual1,
	DDS_DepthAlways_StencilEqual1_IgnoreMask,
	DDS_DepthAlways_StencilEqual0,
	DDS_DepthTest_StencilEqual1,
	DDS_DepthTest_StencilEqual1_IgnoreMask,
	DDS_DepthTest_StencilEqual0,
};

enum class EDecalDBufferMaskTechnique
{
	// DBufferMask is not enabled.
	Disabled,

	// DBufferMask is written explicitly by the shader during the DBuffer pass.
	PerPixel,

	// DBufferMask is constructed after the DBuffer pass by compositing DBuffer write mask planes together in a compute shader.
	WriteMask,
};

struct FDecalDepthState
{
	EDecalDepthInputState DepthTest;
	bool bDepthOutput;

	FDecalDepthState()
		: DepthTest(DDS_Undefined)
		, bDepthOutput(false)
	{
	}

	bool operator !=(const FDecalDepthState &rhs) const
	{
		return DepthTest != rhs.DepthTest || bDepthOutput != rhs.bDepthOutput;
	}
};

static bool RenderPreStencil(FRHICommandList& RHICmdList, const FViewInfo& View, const FMatrix& ComponentToWorldMatrix, const FMatrix& FrustumComponentToClip)
{
	float Distance = (View.ViewMatrices.GetViewOrigin() - ComponentToWorldMatrix.GetOrigin()).Size();
	float Radius = ComponentToWorldMatrix.GetMaximumAxisScale();

	// if not inside
	if (Distance > Radius)
	{
		float EstimatedDecalSize = Radius / Distance;

		float StencilSizeThreshold = CVarStencilSizeThreshold.GetValueOnRenderThread();

		// Check if it's large enough on screen
		if (EstimatedDecalSize < StencilSizeThreshold)
		{
			return false;
		}
	}

	FGraphicsPipelineStateInitializer GraphicsPSOInit;
	RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);

	// Set states, the state cache helps us avoiding redundant sets
	GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();

	// all the same to have DX10 working
	GraphicsPSOInit.BlendState = TStaticBlendState<
		CW_NONE, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_One,	// Emissive
		CW_NONE, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_One,	// Normal
		CW_NONE, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_One,	// Metallic, Specular, Roughness
		CW_NONE, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_One		// BaseColor
	>::GetRHI();

	// Carmack's reverse the sandbox stencil bit on the bounds
	GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<
		false, CF_LessEqual,
		true, CF_Always, SO_Keep, SO_Keep, SO_Invert,
		true, CF_Always, SO_Keep, SO_Keep, SO_Invert,
		STENCIL_SANDBOX_MASK, STENCIL_SANDBOX_MASK
	>::GetRHI();

	FDecalRendering::SetVertexShaderOnly(RHICmdList, GraphicsPSOInit, View, FrustumComponentToClip);
	RHICmdList.SetStencilRef(0);

	// Set stream source after updating cached strides
	RHICmdList.SetStreamSource(0, GetUnitCubeVertexBuffer(), 0);

	// Render decal mask
	RHICmdList.DrawIndexedPrimitive(GetUnitCubeIndexBuffer(), 0, 0, 8, 0, UE_ARRAY_COUNT(GCubeIndices) / 3, 1);

	return true;
}

static EDecalRasterizerState ComputeDecalRasterizerState(bool bInsideDecal, bool bIsInverted, const FViewInfo& View)
{
	bool bClockwise = bInsideDecal;

	if (View.bReverseCulling)
	{
		bClockwise = !bClockwise;
	}

	if (bIsInverted)
	{
		bClockwise = !bClockwise;
	}
	return bClockwise ? DRS_CW : DRS_CCW;
}

static FDecalDepthState ComputeDecalDepthState(EDecalRenderStage LocalDecalStage, bool bInsideDecal, bool bThisDecalUsesStencil)
{
	FDecalDepthState Ret;

	Ret.bDepthOutput = (LocalDecalStage == DRS_AfterBasePass);

	if (Ret.bDepthOutput)
	{
		// can be made one enum
		Ret.DepthTest = DDS_DepthTest;
		return Ret;
	}

	const bool bUseDecalMask = 
		LocalDecalStage == DRS_BeforeLighting || 
		LocalDecalStage == DRS_Emissive || 
		LocalDecalStage == DRS_AmbientOcclusion;

	if (bInsideDecal)
	{
		if (bThisDecalUsesStencil)
		{
			Ret.DepthTest = bUseDecalMask ? DDS_DepthAlways_StencilEqual1 : DDS_DepthAlways_StencilEqual1_IgnoreMask;
		}
		else
		{
			Ret.DepthTest = bUseDecalMask ? DDS_DepthAlways_StencilEqual0 : DDS_Always;
		}
	}
	else
	{
		if (bThisDecalUsesStencil)
		{
			Ret.DepthTest = bUseDecalMask ? DDS_DepthTest_StencilEqual1 : DDS_DepthTest_StencilEqual1_IgnoreMask;
		}
		else
		{
			Ret.DepthTest = bUseDecalMask ? DDS_DepthTest_StencilEqual0 : DDS_DepthTest;
		}
	}

	return Ret;
}

static FRHIDepthStencilState* GetDecalDepthState(uint32& StencilRef, FDecalDepthState DecalDepthState)
{
	switch (DecalDepthState.DepthTest)
	{
	case DDS_DepthAlways_StencilEqual1:
		check(!DecalDepthState.bDepthOutput);			// todo
		StencilRef = STENCIL_SANDBOX_MASK | GET_STENCIL_BIT_MASK(RECEIVE_DECAL, 1);
		return TStaticDepthStencilState<
			false, CF_Always,
			true, CF_Equal, SO_Zero, SO_Zero, SO_Zero,
			true, CF_Equal, SO_Zero, SO_Zero, SO_Zero,
			STENCIL_SANDBOX_MASK | GET_STENCIL_BIT_MASK(RECEIVE_DECAL, 1), STENCIL_SANDBOX_MASK>::GetRHI();

	case DDS_DepthAlways_StencilEqual1_IgnoreMask:
		check(!DecalDepthState.bDepthOutput);			// todo
		StencilRef = STENCIL_SANDBOX_MASK;
		return TStaticDepthStencilState<
			false, CF_Always,
			true, CF_Equal, SO_Zero, SO_Zero, SO_Zero,
			true, CF_Equal, SO_Zero, SO_Zero, SO_Zero,
			STENCIL_SANDBOX_MASK, STENCIL_SANDBOX_MASK>::GetRHI();

	case DDS_DepthAlways_StencilEqual0:
		check(!DecalDepthState.bDepthOutput);			// todo
		StencilRef = GET_STENCIL_BIT_MASK(RECEIVE_DECAL, 1);
		return TStaticDepthStencilState<
			false, CF_Always,
			true, CF_Equal, SO_Keep, SO_Keep, SO_Keep,
			false, CF_Always, SO_Keep, SO_Keep, SO_Keep,
			STENCIL_SANDBOX_MASK | GET_STENCIL_BIT_MASK(RECEIVE_DECAL, 1), 0x00>::GetRHI();

	case DDS_Always:
		check(!DecalDepthState.bDepthOutput);			// todo 
		StencilRef = 0;
		return TStaticDepthStencilState<false, CF_Always>::GetRHI();

	case DDS_DepthTest_StencilEqual1:
		check(!DecalDepthState.bDepthOutput);			// todo
		StencilRef = STENCIL_SANDBOX_MASK | GET_STENCIL_BIT_MASK(RECEIVE_DECAL, 1);
		return TStaticDepthStencilState<
			false, CF_DepthNearOrEqual,
			true, CF_Equal, SO_Zero, SO_Zero, SO_Zero,
			true, CF_Equal, SO_Zero, SO_Zero, SO_Zero,
			STENCIL_SANDBOX_MASK | GET_STENCIL_BIT_MASK(RECEIVE_DECAL, 1), STENCIL_SANDBOX_MASK>::GetRHI();

	case DDS_DepthTest_StencilEqual1_IgnoreMask:
		check(!DecalDepthState.bDepthOutput);			// todo
		StencilRef = STENCIL_SANDBOX_MASK;
		return TStaticDepthStencilState<
			false, CF_DepthNearOrEqual,
			true, CF_Equal, SO_Zero, SO_Zero, SO_Zero,
			true, CF_Equal, SO_Zero, SO_Zero, SO_Zero,
			STENCIL_SANDBOX_MASK, STENCIL_SANDBOX_MASK>::GetRHI();

	case DDS_DepthTest_StencilEqual0:
		check(!DecalDepthState.bDepthOutput);			// todo
		StencilRef = GET_STENCIL_BIT_MASK(RECEIVE_DECAL, 1);
		return TStaticDepthStencilState<
			false, CF_DepthNearOrEqual,
			true, CF_Equal, SO_Keep, SO_Keep, SO_Keep,
			false, CF_Always, SO_Keep, SO_Keep, SO_Keep,
			STENCIL_SANDBOX_MASK | GET_STENCIL_BIT_MASK(RECEIVE_DECAL, 1), 0x00>::GetRHI();

	case DDS_DepthTest:
		if (DecalDepthState.bDepthOutput)
		{
			StencilRef = 0;
			return TStaticDepthStencilState<true, CF_DepthNearOrEqual>::GetRHI();
		}
		else
		{
			StencilRef = 0;
			return TStaticDepthStencilState<false, CF_DepthNearOrEqual>::GetRHI();
		}

	default:
		check(0);
		return nullptr;
	}
}

FRHIRasterizerState* GetDecalRasterizerState(EDecalRasterizerState DecalRasterizerState)
{
	switch (DecalRasterizerState)
	{
	case DRS_CW: return TStaticRasterizerState<FM_Solid, CM_CW>::GetRHI();
	case DRS_CCW: return TStaticRasterizerState<FM_Solid, CM_CCW>::GetRHI();
	default: check(0); return nullptr;
	}
}

static bool IsStencilOptimizationAvailable(EDecalRenderStage RenderStage)
{
	return RenderStage == DRS_BeforeLighting || RenderStage == DRS_BeforeBasePass || RenderStage == DRS_Emissive;
}

static EDecalDBufferMaskTechnique GetDBufferMaskTechnique(EShaderPlatform ShaderPlatform)
{
	const bool bWriteMaskDBufferMask = RHISupportsRenderTargetWriteMask(ShaderPlatform);
	const bool bPerPixelDBufferMask = FDataDrivenShaderPlatformInfo::GetSupportsPerPixelDBufferMask(ShaderPlatform);
	checkf(!bWriteMaskDBufferMask || !bPerPixelDBufferMask, TEXT("The WriteMask and PerPixel DBufferMask approaches cannot be enabled at the same time. They are mutually exclusive."));

	if (bWriteMaskDBufferMask)
	{
		return EDecalDBufferMaskTechnique::WriteMask;
	}
	else if (bPerPixelDBufferMask)
	{
		return EDecalDBufferMaskTechnique::PerPixel;
	}
	return EDecalDBufferMaskTechnique::Disabled;
}

static const TCHAR* GetStageName(EDecalRenderStage Stage)
{
	// could be implemented with enum reflections as well

	switch (Stage)
	{
	case DRS_BeforeBasePass: return TEXT("DRS_BeforeBasePass");
	case DRS_AfterBasePass: return TEXT("DRS_AfterBasePass");
	case DRS_BeforeLighting: return TEXT("DRS_BeforeLighting");
	case DRS_Mobile: return TEXT("DRS_Mobile");
	case DRS_AmbientOcclusion: return TEXT("DRS_AmbientOcclusion");
	case DRS_Emissive: return TEXT("DRS_Emissive");
	}
	return TEXT("<UNKNOWN>");
}

void AddDeferredDecalPass(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	FDeferredDecalPassTextures& PassTextures,
	EDecalRenderStage DecalRenderStage)
{
	check(PassTextures.Depth.IsValid());

	const FSceneViewFamily& ViewFamily = *(View.Family);

	// Debug view framework does not yet support decals.
	if (!ViewFamily.EngineShowFlags.Decals || ViewFamily.UseDebugViewPS())
	{
		return;
	}

	FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(GraphBuilder.RHICmdList);

	const FScene& Scene = *(FScene*)ViewFamily.Scene;
	const EShaderPlatform ShaderPlatform = View.GetShaderPlatform();
	const ERHIFeatureLevel::Type FeatureLevel = View.GetFeatureLevel();
	const uint32 MeshDecalCount = View.MeshDecalBatches.Num();
	const uint32 DecalCount = Scene.Decals.Num();
	uint32 SortedDecalCount = 0;
	FTransientDecalRenderDataList* SortedDecals = nullptr;

	checkf(DecalRenderStage != DRS_AmbientOcclusion || PassTextures.ScreenSpaceAO, TEXT("Attepting to render AO decals without SSAO having emitted a valid render target."));
	checkf(DecalRenderStage != DRS_BeforeBasePass || IsUsingDBuffers(ShaderPlatform), TEXT("Only DBuffer decals are supported before the base pass."));

	if (DecalCount)
	{
		SortedDecals = GraphBuilder.AllocObject<FTransientDecalRenderDataList>();
		FDecalRendering::BuildVisibleDecalList(Scene, View, DecalRenderStage, SortedDecals);
		SortedDecalCount = SortedDecals->Num();

		INC_DWORD_STAT_BY(STAT_Decals, SortedDecalCount);
	}

	const bool bVisibleDecalsInView = MeshDecalCount > 0 || SortedDecalCount > 0;
	const bool bShaderComplexity = View.Family->EngineShowFlags.ShaderComplexity;
	const bool bStencilSizeThreshold = CVarStencilSizeThreshold.GetValueOnRenderThread() >= 0;

	// Attempt to clear the D-Buffer if it's appropriate for this view.
	const EDecalDBufferMaskTechnique DBufferMaskTechnique = GetDBufferMaskTechnique(ShaderPlatform);

	const auto CreateOrImportTexture = [&](TRefCountPtr<IPooledRenderTarget>& Target, const FRDGTextureDesc& Desc, const TCHAR* Name, ERDGTextureFlags Flags = ERDGTextureFlags::None)
	{
		if (Target)
		{
			return GraphBuilder.RegisterExternalTexture(Target, ERenderTargetTexture::ShaderResource, Flags);
		}
		else
		{
			FRDGTextureRef Texture = GraphBuilder.CreateTexture(Desc, Name, Flags);
			ConvertToExternalTexture(GraphBuilder, Texture, Target);
			return Texture;
		}
	};

	const auto RenderDecals = [&](uint32 DecalIndexBegin, uint32 DecalIndexEnd, FDecalRenderingCommon::ERenderTargetMode RenderTargetMode)
	{
		auto* PassParameters = GraphBuilder.AllocParameters<FDeferredDecalPassParameters>();
		GetDeferredDecalPassParameters(View, PassTextures, RenderTargetMode, *PassParameters);
		GraphBuilder.AddPass(
			RDG_EVENT_NAME("Batch [%d, %d]", DecalIndexBegin, DecalIndexEnd - 1),
			PassParameters,
			ERDGPassFlags::Raster,
			[&View, FeatureLevel, ShaderPlatform, DecalIndexBegin, DecalIndexEnd, SortedDecals, DecalRenderStage, bStencilSizeThreshold, bShaderComplexity](FRHICommandList& RHICmdList)
		{
			RHICmdList.SetViewport(View.ViewRect.Min.X, View.ViewRect.Min.Y, 0.0f, View.ViewRect.Max.X, View.ViewRect.Max.Y, 1.0f);

			for (uint32 DecalIndex = DecalIndexBegin; DecalIndex < DecalIndexEnd; ++DecalIndex)
			{
				const FTransientDecalRenderData& DecalData = (*SortedDecals)[DecalIndex];
				const FDeferredDecalProxy& DecalProxy = *DecalData.DecalProxy;
				const FMatrix ComponentToWorldMatrix = DecalProxy.ComponentTrans.ToMatrixWithScale();
				const FMatrix FrustumComponentToClip = FDecalRendering::ComputeComponentToClipMatrix(View, ComponentToWorldMatrix);
				const EDecalBlendMode DecalBlendMode = bShaderComplexity ? DBM_Emissive : FDecalRenderingCommon::ComputeDecalBlendModeForRenderStage(DecalData.FinalDecalBlendMode, DecalRenderStage);
				const EDecalRenderStage LocalDecalStage = FDecalRenderingCommon::ComputeRenderStage(ShaderPlatform, DecalBlendMode);
				const bool bStencilThisDecal = IsStencilOptimizationAvailable(LocalDecalStage);

				bool bThisDecalUsesStencil = false;

				if (bStencilThisDecal && bStencilSizeThreshold)
				{
					bThisDecalUsesStencil = RenderPreStencil(RHICmdList, View, ComponentToWorldMatrix, FrustumComponentToClip);
				}

				const bool bInsideDecal = ((FVector)View.ViewMatrices.GetViewOrigin() - ComponentToWorldMatrix.GetOrigin()).SizeSquared() < FMath::Square(DecalData.ConservativeRadius * 1.05f + View.NearClippingDistance * 2.0f);

				FGraphicsPipelineStateInitializer GraphicsPSOInit;
				RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);

				{
					// Account for the reversal of handedness caused by negative scale on the decal
					const FVector Scale = DecalProxy.ComponentTrans.GetScale3D();
					const bool bReverseHanded = Scale.X * Scale.Y * Scale.Z < 0.0f;
					const EDecalRasterizerState DecalRasterizerState = FDecalRenderingCommon::ComputeDecalRasterizerState(bInsideDecal, bReverseHanded, View.bReverseCulling);
					GraphicsPSOInit.RasterizerState = GetDecalRasterizerState(DecalRasterizerState);
				}

				uint32 StencilRef = 0;

				{
					const FDecalDepthState DecalDepthState = ComputeDecalDepthState(LocalDecalStage, bInsideDecal, bThisDecalUsesStencil);
					GraphicsPSOInit.DepthStencilState = GetDecalDepthState(StencilRef, DecalDepthState);
				}

				GraphicsPSOInit.BlendState = FDecalRendering::GetDecalBlendState(FeatureLevel, DecalRenderStage, DecalBlendMode, DecalData.bHasNormal);
				GraphicsPSOInit.PrimitiveType = PT_TriangleList;

				FDecalRendering::SetShader(RHICmdList, GraphicsPSOInit, View, DecalData, DecalRenderStage, FrustumComponentToClip);
				RHICmdList.SetStencilRef(StencilRef);
				RHICmdList.DrawIndexedPrimitive(GetUnitCubeIndexBuffer(), 0, 0, 8, 0, UE_ARRAY_COUNT(GCubeIndices) / 3, 1);
			}
		});
	};

	const auto GetRenderTargetMode = [&](const FTransientDecalRenderData& DecalData)
	{
		const EDecalBlendMode DecalBlendMode = FDecalRenderingCommon::ComputeDecalBlendModeForRenderStage(DecalData.FinalDecalBlendMode, DecalRenderStage);
		return bShaderComplexity ? FDecalRenderingCommon::RTM_SceneColor : FDecalRenderingCommon::ComputeRenderTargetMode(ShaderPlatform, DecalBlendMode, DecalData.bHasNormal);
	};

	if (bVisibleDecalsInView)
	{
		RDG_EVENT_SCOPE(GraphBuilder, "DeferredDecals %s", GetStageName(DecalRenderStage));

		if (DecalRenderStage == DRS_BeforeBasePass)
		{
			const ETextureCreateFlags WriteMaskFlags = DBufferMaskTechnique == EDecalDBufferMaskTechnique::WriteMask ? TexCreate_NoFastClearFinalize | TexCreate_DisableDCC : TexCreate_None;
			const ETextureCreateFlags BaseFlags = WriteMaskFlags | TexCreate_ShaderResource | TexCreate_RenderTargetable;

			FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(PassTextures.Depth.Target->Desc.Extent, PF_B8G8R8A8, FClearValueBinding::None, BaseFlags);

			ERDGTextureFlags RDGTexFlags = DBufferMaskTechnique != EDecalDBufferMaskTechnique::Disabled
				? ERDGTextureFlags::MaintainCompression
				: ERDGTextureFlags::None;

			{
				Desc.Flags = BaseFlags | GFastVRamConfig.DBufferA;
				Desc.ClearValue = FClearValueBinding::Black;
				PassTextures.DBufferA = CreateOrImportTexture(SceneContext.DBufferA, Desc, TEXT("DBufferA"), RDGTexFlags);
			}

			{
				Desc.Flags = BaseFlags | GFastVRamConfig.DBufferB;
				Desc.ClearValue = FClearValueBinding(FLinearColor(128.0f / 255.0f, 128.0f / 255.0f, 128.0f / 255.0f, 1));
				PassTextures.DBufferB = CreateOrImportTexture(SceneContext.DBufferB, Desc, TEXT("DBufferB"), RDGTexFlags);
			}

			{
				Desc.Flags = BaseFlags | GFastVRamConfig.DBufferC;
				Desc.ClearValue = FClearValueBinding(FLinearColor(0, 0, 0, 1));
				PassTextures.DBufferC = CreateOrImportTexture(SceneContext.DBufferC, Desc, TEXT("DBufferC"), RDGTexFlags);
			}

			if (DBufferMaskTechnique == EDecalDBufferMaskTechnique::PerPixel)
			{
				// Note: 32bpp format is used here to utilize color compression hardware (same as other DBuffer targets).
				// This significantly reduces bandwidth for clearing, writing and reading on some GPUs.
				// While a smaller format, such as R8_UINT, will use less video memory, it will result in slower clears and higher bandwidth requirements.
				check(Desc.Format == PF_B8G8R8A8);
				Desc.Flags = BaseFlags;
				Desc.ClearValue = FClearValueBinding::Transparent;
				PassTextures.DBufferMask = CreateOrImportTexture(SceneContext.DBufferMask, Desc, TEXT("DBufferMask"));
			}
		}

		if (MeshDecalCount > 0 && (DecalRenderStage == DRS_BeforeBasePass || DecalRenderStage == DRS_BeforeLighting || DecalRenderStage == DRS_Emissive))
		{
			RenderMeshDecals(GraphBuilder, View, PassTextures, DecalRenderStage);
		}

		if (SortedDecalCount > 0)
		{
			RDG_EVENT_SCOPE(GraphBuilder, "Decals (Visible %d, Total: %d)", SortedDecalCount, DecalCount);

			uint32 SortedDecalIndex = 1;
			uint32 LastSortedDecalIndex = 0;
			FDecalRenderingCommon::ERenderTargetMode LastRenderTargetMode = GetRenderTargetMode((*SortedDecals)[0]);

			for (; SortedDecalIndex < SortedDecalCount; ++SortedDecalIndex)
			{
				const FDecalRenderingCommon::ERenderTargetMode RenderTargetMode = GetRenderTargetMode((*SortedDecals)[SortedDecalIndex]);

				if (LastRenderTargetMode != RenderTargetMode)
				{
					RenderDecals(LastSortedDecalIndex, SortedDecalIndex, LastRenderTargetMode);
					LastRenderTargetMode = RenderTargetMode;
					LastSortedDecalIndex = SortedDecalIndex;
				}
			}

			if (LastSortedDecalIndex != SortedDecalIndex)
			{
				RenderDecals(LastSortedDecalIndex, SortedDecalIndex, LastRenderTargetMode);
			}
		}
	}

	// Last D-Buffer pass in the frame decodes the write mask (if supported and decals were rendered).
	if (DBufferMaskTechnique == EDecalDBufferMaskTechnique::WriteMask &&
		DecalRenderStage == DRS_BeforeBasePass &&
		PassTextures.DBufferA != nullptr &&
		View.IsLastInFamily())
	{
		// Combine DBuffer RTWriteMasks; will end up in one texture we can load from in the base pass PS and decide whether to do the actual work or not.
		FRDGTextureRef Textures[] = { PassTextures.DBufferA, PassTextures.DBufferB, PassTextures.DBufferC };
		FRDGTextureRef OutputTexture = nullptr;
		FRenderTargetWriteMask::Decode(GraphBuilder, View.ShaderMap, MakeArrayView(Textures), OutputTexture, GFastVRamConfig.DBufferMask, TEXT("DBufferMaskCombine"));
		ConvertToExternalTexture(GraphBuilder, OutputTexture, SceneContext.DBufferMask);
	}
}
