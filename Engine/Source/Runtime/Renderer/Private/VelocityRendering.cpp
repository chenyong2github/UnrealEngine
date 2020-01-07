// Copyright Epic Games, Inc. All Rights Reserved.

#include "VelocityRendering.h"
#include "SceneUtils.h"
#include "Materials/Material.h"
#include "PostProcess/SceneRenderTargets.h"
#include "MaterialShaderType.h"
#include "MaterialShader.h"
#include "MeshMaterialShader.h"
#include "ShaderBaseClasses.h"
#include "SceneRendering.h"
#include "DeferredShadingRenderer.h"
#include "ScenePrivate.h"
#include "ScreenSpaceRayTracing.h"
#include "PostProcess/PostProcessMotionBlur.h"
#include "UnrealEngine.h"
#if WITH_EDITOR
#include "Misc/CoreMisc.h"
#include "Interfaces/ITargetPlatform.h"
#include "Interfaces/ITargetPlatformManagerModule.h"
#endif
#include "VisualizeTexture.h"
#include "MeshPassProcessor.inl"

// Changing this causes a full shader recompile
static TAutoConsoleVariable<int32> CVarBasePassOutputsVelocity(
	TEXT("r.BasePassOutputsVelocity"),
	0,
	TEXT("Enables rendering WPO velocities on the base pass.\n") \
	TEXT(" 0: Renders in a separate pass/rendertarget, all movable static meshes + dynamic.\n") \
	TEXT(" 1: Renders during the regular base pass adding an extra GBuffer, but allowing motion blur on materials with Time-based WPO."),
	ECVF_ReadOnly | ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarParallelVelocity(
	TEXT("r.ParallelVelocity"),
	1,  
	TEXT("Toggles parallel velocity rendering. Parallel rendering must be enabled for this to have an effect."),
	ECVF_RenderThreadSafe
	);

static TAutoConsoleVariable<int32> CVarRHICmdVelocityPassDeferredContexts(
	TEXT("r.RHICmdVelocityPassDeferredContexts"),
	1,
	TEXT("True to use deferred contexts to parallelize velocity pass command list execution."));

static TAutoConsoleVariable<int32> CVarVertexDeformationOutputsVelocity(
	TEXT("r.VertexDeformationOutputsVelocity"),
	0,
	TEXT(
		"Enables materials with World Position Offset and/or World Displacement to output velocities during velocity pass even when the actor has not moved. "
		"This incurs a performance cost and can be quite significant if many objects are using WPO, such as a forest of trees - in that case consider r.BasePassOutputsVelocity and disabling this option."
		));

DECLARE_GPU_STAT_NAMED(RenderVelocities, TEXT("Render Velocities"));

bool IsParallelVelocity()
{
	return GRHICommandList.UseParallelAlgorithms() && CVarParallelVelocity.GetValueOnRenderThread();
}

class FVelocityVS : public FMeshMaterialShader
{
public:
	DECLARE_MESH_MATERIAL_SHADER(FVelocityVS);

	static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
	{
		const FMaterial* Material = Parameters.Material;

		// Compile for default material.
		const bool bIsDefault = Material->IsSpecialEngineMaterial();

		// Compile for masked materials.
		const bool bIsMasked = !Material->WritesEveryPixel();

		// Compile for opaque and two-sided materials.
		const bool bIsOpaqueAndTwoSided = (Material->IsTwoSided() && !IsTranslucentBlendMode(Material->GetBlendMode()));

		// Compile for materials which modify meshes.
		const bool bMayModifyMeshes = Material->MaterialMayModifyMeshPosition();

		// Compile if supported by the hardware.
		const bool bIsFeatureSupported = IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);

		/**
		 * Any material with a vertex factory incompatible with base pass velocity generation must generate
		 * permutations for this shader. Shaders which don't fall into this set are considered "simple" enough
		 * to swap against the default material. This massively simplifies the calculations.
		 */
		const bool bIsSeparateVelocityPassRequired = (bIsDefault || bIsMasked || bIsOpaqueAndTwoSided || bMayModifyMeshes) &&
			FVelocityRendering::IsSeparateVelocityPassRequiredByVertexFactory(Parameters.Platform, Parameters.VertexFactoryType->SupportsStaticLighting());

		// The material may explicitly override and request that it be rendered into the velocity pass.
		const bool bIsSeparateVelocityPassRequiredByMaterial = Material->IsTranslucencyWritingVelocity();

		return bIsFeatureSupported && (bIsSeparateVelocityPassRequired || bIsSeparateVelocityPassRequiredByMaterial);
	}

	FVelocityVS() = default;
	FVelocityVS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FMeshMaterialShader(Initializer)
	{
		PassUniformBuffer.Bind(Initializer.ParameterMap, FSceneTexturesUniformParameters::StaticStructMetadata.GetShaderVariableName());
	}
};

class FVelocityHS : public FBaseHS
{
public:
	DECLARE_MESH_MATERIAL_SHADER(FVelocityHS);

	static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
	{
		return FBaseHS::ShouldCompilePermutation(Parameters) && FVelocityVS::ShouldCompilePermutation(Parameters);
	}

	FVelocityHS() = default;
	FVelocityHS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FBaseHS(Initializer)
	{}
};

class FVelocityDS : public FBaseDS
{
public:
	DECLARE_MESH_MATERIAL_SHADER(FVelocityDS);

	static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
	{
		return FBaseDS::ShouldCompilePermutation(Parameters) && FVelocityVS::ShouldCompilePermutation(Parameters);
	}

	FVelocityDS() = default;
	FVelocityDS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FBaseDS(Initializer)
	{}
};

class FVelocityPS : public FMeshMaterialShader
{
public:
	DECLARE_MESH_MATERIAL_SHADER(FVelocityPS);

	static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
	{
		return FVelocityVS::ShouldCompilePermutation(Parameters);
	}

	static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FMaterialShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetRenderTargetOutputFormat(0, FVelocityRendering::GetFormat());
	}

	FVelocityPS() = default;
	FVelocityPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FMeshMaterialShader(Initializer)
	{
		PassUniformBuffer.Bind(Initializer.ParameterMap, FSceneTexturesUniformParameters::StaticStructMetadata.GetShaderVariableName());
	}
};

IMPLEMENT_MESH_MATERIAL_SHADER(FVelocityVS, "/Engine/Private/VelocityShader.usf", "MainVertexShader", SF_Vertex); 
IMPLEMENT_MESH_MATERIAL_SHADER(FVelocityHS, "/Engine/Private/VelocityShader.usf", "MainHull", SF_Hull); 
IMPLEMENT_MESH_MATERIAL_SHADER(FVelocityDS, "/Engine/Private/VelocityShader.usf", "MainDomain", SF_Domain);
IMPLEMENT_MESH_MATERIAL_SHADER(FVelocityPS, "/Engine/Private/VelocityShader.usf", "MainPixelShader", SF_Pixel);
IMPLEMENT_SHADERPIPELINE_TYPE_VSPS(VelocityPipeline, FVelocityVS, FVelocityPS, true);

EMeshPass::Type GetMeshPassFromVelocityPass(EVelocityPass VelocityPass)
{
	switch (VelocityPass)
	{
	case EVelocityPass::Opaque:
		return EMeshPass::Velocity;
	case EVelocityPass::Translucent:
		return EMeshPass::TranslucentVelocity;
	}
	check(false);
	return EMeshPass::Velocity;
}

static void BeginVelocityRendering(
	FRHICommandList& RHICmdList,
	TRefCountPtr<IPooledRenderTarget>& VelocityRT,
	EVelocityPass VelocityPass,
	bool bPerformClear)
{
	check(RHICmdList.IsOutsideRenderPass());

	FTextureRHIRef VelocityTexture = VelocityRT->GetRenderTargetItem().TargetableTexture;
	FTexture2DRHIRef DepthTexture = FSceneRenderTargets::Get(RHICmdList).GetSceneDepthTexture();	

	FRHIRenderPassInfo RPInfo(VelocityTexture, ERenderTargetActions::Load_Store);
	RPInfo.DepthStencilRenderTarget.Action = MakeDepthStencilTargetActions(ERenderTargetActions::Load_Store, ERenderTargetActions::Load_Store);
	RPInfo.DepthStencilRenderTarget.DepthStencilTarget = DepthTexture;
	RPInfo.DepthStencilRenderTarget.ExclusiveDepthStencil = VelocityPass == EVelocityPass::Opaque ? FExclusiveDepthStencil::DepthRead_StencilWrite : FExclusiveDepthStencil::DepthWrite_StencilWrite;

	if (bPerformClear)
	{
		RPInfo.ColorRenderTargets[0].Action = ERenderTargetActions::Clear_Store;
	}

	RHICmdList.BeginRenderPass(RPInfo, TEXT("VelocityRendering"));

	if (!bPerformClear)
	{
		// some platforms need the clear color when rendertargets transition to SRVs.  We propagate here to allow parallel rendering to always
		// have the proper mapping when the RT is transitioned.
		RHICmdList.BindClearMRTValues(true, false, false);
	}
}

static void SetVelocitiesState(
	FRHICommandList& RHICmdList,
	const FViewInfo& View,
	const FSceneRenderer* SceneRender,
	FMeshPassProcessorRenderState& DrawRenderState,
	TRefCountPtr<IPooledRenderTarget>& VelocityRT,
	EVelocityPass VelocityPass)
{
	const FIntPoint BufferSize = FSceneRenderTargets::Get(RHICmdList).GetBufferSizeXY();
	const FIntPoint VelocityBufferSize = BufferSize;		// full resolution so we can reuse the existing full res z buffer

	if (!View.IsInstancedStereoPass())
	{
		const uint32 MinX = View.ViewRect.Min.X * VelocityBufferSize.X / BufferSize.X;
		const uint32 MinY = View.ViewRect.Min.Y * VelocityBufferSize.Y / BufferSize.Y;
		const uint32 MaxX = View.ViewRect.Max.X * VelocityBufferSize.X / BufferSize.X;
		const uint32 MaxY = View.ViewRect.Max.Y * VelocityBufferSize.Y / BufferSize.Y;
		RHICmdList.SetViewport(MinX, MinY, 0.0f, MaxX, MaxY, 1.0f);
	}
	else
	{
		if (View.bIsMultiViewEnabled)
		{
			const uint32 LeftMinX = SceneRender->Views[0].ViewRect.Min.X;
			const uint32 LeftMaxX = SceneRender->Views[0].ViewRect.Max.X;
			const uint32 RightMinX = SceneRender->Views[1].ViewRect.Min.X;
			const uint32 RightMaxX = SceneRender->Views[1].ViewRect.Max.X;
			
			const uint32 LeftMaxY = SceneRender->Views[0].ViewRect.Max.Y;
			const uint32 RightMaxY = SceneRender->Views[1].ViewRect.Max.Y;
			
			RHICmdList.SetStereoViewport(LeftMinX, RightMinX, 0, 0, 0.0f, LeftMaxX, RightMaxX, LeftMaxY, RightMaxY, 1.0f);
		}
		else
		{
			const uint32 MaxX = SceneRender->InstancedStereoWidth * VelocityBufferSize.X / BufferSize.X;
			const uint32 MaxY = View.ViewRect.Max.Y * VelocityBufferSize.Y / BufferSize.Y;
			RHICmdList.SetViewport(0, 0, 0.0f, MaxX, MaxY, 1.0f);
		}
	}

	DrawRenderState.SetBlendState(TStaticBlendState<CW_RGBA>::GetRHI());

	switch (VelocityPass)
	{
	case EVelocityPass::Opaque:
		DrawRenderState.SetDepthStencilState(TStaticDepthStencilState<false, CF_DepthNearOrEqual>::GetRHI());
		break;

	case EVelocityPass::Translucent:
		DrawRenderState.SetDepthStencilState(TStaticDepthStencilState<true, CF_DepthNearOrEqual>::GetRHI());
		break;
	}
}

DECLARE_CYCLE_STAT(TEXT("Velocity"), STAT_CLP_Velocity, STATGROUP_ParallelCommandListMarkers);

class FVelocityPassParallelCommandListSet : public FParallelCommandListSet
{
	TRefCountPtr<IPooledRenderTarget>& VelocityRT;
	EVelocityPass VelocityPass;

public:
	FVelocityPassParallelCommandListSet(
		const FViewInfo& InView,
		const FSceneRenderer* InSceneRenderer,
		FRHICommandListImmediate& InParentCmdList,
		bool bInParallelExecute,
		bool bInCreateSceneContext,
		const FMeshPassProcessorRenderState& InDrawRenderState,
		TRefCountPtr<IPooledRenderTarget>& InVelocityRT,
		EVelocityPass InVelocityPass)
		: FParallelCommandListSet(GET_STATID(STAT_CLP_Velocity), InView, InSceneRenderer, InParentCmdList, bInParallelExecute, bInCreateSceneContext, InDrawRenderState)
		, VelocityRT(InVelocityRT)
		, VelocityPass(InVelocityPass)
	{
	}

	virtual ~FVelocityPassParallelCommandListSet()
	{
		Dispatch();
	}	

	virtual void SetStateOnCommandList(FRHICommandList& CmdList) override
	{
		FParallelCommandListSet::SetStateOnCommandList(CmdList);
		BeginVelocityRendering(CmdList, VelocityRT, VelocityPass, false);
		SetVelocitiesState(CmdList, View, SceneRenderer, DrawRenderState, VelocityRT, VelocityPass);
	}
};

static TAutoConsoleVariable<int32> CVarRHICmdFlushRenderThreadTasksVelocityPass(
	TEXT("r.RHICmdFlushRenderThreadTasksVelocityPass"),
	0,
	TEXT("Wait for completion of parallel render thread tasks at the end of the velocity pass.  A more granular version of r.RHICmdFlushRenderThreadTasks. If either r.RHICmdFlushRenderThreadTasks or r.RHICmdFlushRenderThreadTasksVelocityPass is > 0 we will flush."));

void FDeferredShadingSceneRenderer::RenderVelocitiesInnerParallel(FRHICommandListImmediate& RHICmdList, TRefCountPtr<IPooledRenderTarget>& VelocityRT, EVelocityPass VelocityPass)
{
	// Parallel rendering requires its own renderpasses so we cannot have an active one at this point
	check(RHICmdList.IsOutsideRenderPass());
	// parallel version
	FScopedCommandListWaitForTasks Flusher(CVarRHICmdFlushRenderThreadTasksVelocityPass.GetValueOnRenderThread() > 0 || CVarRHICmdFlushRenderThreadTasks.GetValueOnRenderThread() > 0, RHICmdList);

	for(int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		const FViewInfo& View = Views[ViewIndex];

		if (View.ShouldRenderView())
		{
			SCOPED_GPU_MASK(RHICmdList, View.GPUMask);

			Scene->UniformBuffers.UpdateViewUniformBuffer(View);

			FSceneTexturesUniformParameters SceneTextureParameters;
			FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);
			SetupSceneTextureUniformParameters(SceneContext, View.FeatureLevel, VelocityPass == EVelocityPass::Opaque ? ESceneTextureSetupMode::None : ESceneTextureSetupMode::All, SceneTextureParameters);
			Scene->UniformBuffers.VelocityPassUniformBuffer.UpdateUniformBufferImmediate(SceneTextureParameters);

			FMeshPassProcessorRenderState DrawRenderState(View, Scene->UniformBuffers.VelocityPassUniformBuffer);

			FVelocityPassParallelCommandListSet ParallelCommandListSet(View,
				this,
				RHICmdList,
				CVarRHICmdVelocityPassDeferredContexts.GetValueOnRenderThread() > 0,
				CVarRHICmdFlushRenderThreadTasksVelocityPass.GetValueOnRenderThread() == 0 && CVarRHICmdFlushRenderThreadTasks.GetValueOnRenderThread() == 0,
				DrawRenderState,
				VelocityRT,
				VelocityPass);

			const EMeshPass::Type MeshPass = GetMeshPassFromVelocityPass(VelocityPass);

			View.ParallelMeshDrawCommandPasses[MeshPass].DispatchDraw(&ParallelCommandListSet, RHICmdList);
		}
	}
}

void FDeferredShadingSceneRenderer::RenderVelocitiesInner(FRHICommandListImmediate& RHICmdList, TRefCountPtr<IPooledRenderTarget>& VelocityRT, EVelocityPass VelocityPass)
{
	check(RHICmdList.IsInsideRenderPass());
	for(int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		const FViewInfo& View = Views[ViewIndex];
		
		FSceneTexturesUniformParameters SceneTextureParameters;
		FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);		
		SetupSceneTextureUniformParameters(SceneContext, View.FeatureLevel, VelocityPass == EVelocityPass::Opaque ? ESceneTextureSetupMode::None : ESceneTextureSetupMode::All, SceneTextureParameters);
		Scene->UniformBuffers.VelocityPassUniformBuffer.UpdateUniformBufferImmediate(SceneTextureParameters);

		FMeshPassProcessorRenderState DrawRenderState(View, Scene->UniformBuffers.VelocityPassUniformBuffer);

		if (View.ShouldRenderView())
		{
			SCOPED_GPU_MASK(RHICmdList, View.GPUMask);

			Scene->UniformBuffers.UpdateViewUniformBuffer(View);

			SetVelocitiesState(RHICmdList, View, this, DrawRenderState, VelocityRT, VelocityPass);

			const EMeshPass::Type MeshPass = GetMeshPassFromVelocityPass(VelocityPass);

			View.ParallelMeshDrawCommandPasses[MeshPass].DispatchDraw(nullptr, RHICmdList);
		}
	}
}

bool FDeferredShadingSceneRenderer::ShouldRenderVelocities() const
{
	if (!FVelocityRendering::IsSeparateVelocityPassSupported())
	{
		return false;
	}

	bool bNeedsVelocity = false;
	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		const FViewInfo& View = Views[ViewIndex];

		bool bTemporalAA = (View.AntiAliasingMethod == AAM_TemporalAA) && !View.bCameraCut;
		bool bMotionBlur = IsMotionBlurEnabled(View);
		bool bDistanceFieldAO = ShouldPrepareForDistanceFieldAO();

		bool bSSRTemporal = ShouldRenderScreenSpaceReflections(View) && IsSSRTemporalPassRequired(View);

		bool bRayTracing = IsRayTracingEnabled();
		bool bDenoise = bRayTracing;

		bool bSSGI = ShouldRenderScreenSpaceDiffuseIndirect(View);
		
		bNeedsVelocity |= bMotionBlur || bTemporalAA || bDistanceFieldAO || bSSRTemporal || bDenoise || bSSGI;
	}

	return bNeedsVelocity;
}

void FDeferredShadingSceneRenderer::RenderVelocities(FRHICommandListImmediate& RHICmdList, TRefCountPtr<IPooledRenderTarget>& VelocityRT, EVelocityPass VelocityPass, bool bClearVelocityRT)
{
	SCOPED_NAMED_EVENT(FDeferredShadingSceneRenderer_RenderVelocities, FColor::Emerald);

	check(FeatureLevel >= ERHIFeatureLevel::SM5);
	CSV_SCOPED_TIMING_STAT_EXCLUSIVE(RenderVelocities);
	SCOPE_CYCLE_COUNTER(STAT_RenderVelocities);

	if (!ShouldRenderVelocities())
	{
		return;
	}

	SCOPED_DRAW_EVENT(RHICmdList, RenderVelocities);
	SCOPED_GPU_STAT(RHICmdList, RenderVelocities);

	if (!VelocityRT)
	{
		FPooledRenderTargetDesc Desc = FVelocityRendering::GetRenderTargetDesc();
		GRenderTargetPool.FindFreeElement(RHICmdList, Desc, VelocityRT, TEXT("Velocity"));
	}

	{
		static const auto MotionBlurDebugVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.MotionBlurDebug"));

		if(MotionBlurDebugVar->GetValueOnRenderThread())
		{
			UE_LOG(LogEngine, Log, TEXT("r.MotionBlurDebug: FrameNumber=%d Pause=%d"), ViewFamily.FrameNumber, ViewFamily.bWorldIsPaused ? 1 : 0);
		}
	}

	BeginVelocityRendering(RHICmdList, VelocityRT, VelocityPass, bClearVelocityRT);

	{
		if (IsParallelVelocity())
		{
			// This initial renderpass will just be a clear in the parallel case.
			RHICmdList.EndRenderPass();

			// Now do parallel encoding.
			RenderVelocitiesInnerParallel(RHICmdList, VelocityRT, VelocityPass);
		}
		else
		{
			RenderVelocitiesInner(RHICmdList, VelocityRT, VelocityPass);
			RHICmdList.EndRenderPass();
		}
		if(VelocityPass != EVelocityPass::Opaque)
		{
			FTexture2DRHIRef DepthTexture = FSceneRenderTargets::Get(RHICmdList).GetSceneDepthTexture();
			if(DepthTexture)
			{
				RHICmdList.TransitionResource(EResourceTransitionAccess::EReadable, DepthTexture);
			}
		}
		RHICmdList.CopyToResolveTarget(VelocityRT->GetRenderTargetItem().TargetableTexture, VelocityRT->GetRenderTargetItem().ShaderResourceTexture, FResolveParams());
	}

	// to be able to observe results with VisualizeTexture
	GVisualizeTexture.SetCheckPoint(RHICmdList, VelocityRT);
}

EPixelFormat FVelocityRendering::GetFormat()
{
	return PF_G16R16;
}

FPooledRenderTargetDesc FVelocityRendering::GetRenderTargetDesc()
{
	const FIntPoint BufferSize = FSceneRenderTargets::Get_FrameConstantsOnly().GetBufferSizeXY();
	const FIntPoint VelocityBufferSize = BufferSize;		// full resolution so we can reuse the existing full res z buffer
	return FPooledRenderTargetDesc(FPooledRenderTargetDesc::Create2DDesc(VelocityBufferSize, GetFormat(), FClearValueBinding::Transparent, TexCreate_None, TexCreate_RenderTargetable | TexCreate_UAV | TexCreate_ShaderResource, false));
}

bool FVelocityRendering::IsSeparateVelocityPassSupported()
{
	return GPixelFormats[GetFormat()].Supported;
}

bool FVelocityRendering::BasePassCanOutputVelocity(EShaderPlatform ShaderPlatform)
{
	return IsUsingBasePassVelocity(ShaderPlatform);
}

bool FVelocityRendering::BasePassCanOutputVelocity(ERHIFeatureLevel::Type FeatureLevel)
{
	EShaderPlatform ShaderPlatform = GetFeatureLevelShaderPlatform(FeatureLevel);
	return BasePassCanOutputVelocity(ShaderPlatform);
}

bool FVelocityRendering::IsSeparateVelocityPassRequiredByVertexFactory(EShaderPlatform ShaderPlatform, bool bVertexFactoryUsesStaticLighting)
{
	// A separate pass is required if the base pass can't do it.
	const bool bBasePassVelocityNotSupported = !BasePassCanOutputVelocity(ShaderPlatform);

	// Meshes with static lighting need a separate velocity pass, but only if we are using selective render target outputs.
	const bool bVertexFactoryRequiresSeparateVelocityPass = IsUsingSelectiveBasePassOutputs(ShaderPlatform) && bVertexFactoryUsesStaticLighting;

	return bBasePassVelocityNotSupported || bVertexFactoryRequiresSeparateVelocityPass;
}

bool FVelocityMeshProcessor::PrimitiveHasVelocityForView(const FViewInfo& View, const FPrimitiveSceneProxy* PrimitiveSceneProxy)
{
	// Skip camera cuts which effectively reset velocity for the new frame.
	if (View.bCameraCut && !View.PreviousViewTransform.IsSet())
	{
		return false;
	}

	const FBoxSphereBounds& PrimitiveBounds = PrimitiveSceneProxy->GetBounds();
	const float LODFactorDistanceSquared = (PrimitiveBounds.Origin - View.ViewMatrices.GetViewOrigin()).SizeSquared() * FMath::Square(View.LODDistanceFactor);

	// The minimum projected screen radius for a primitive to be drawn in the velocity pass, as a fraction of half the horizontal screen width (likely to be 0.08f)
	float MinScreenRadiusForVelocityPass = View.FinalPostProcessSettings.MotionBlurPerObjectSize * (2.0f / 100.0f);
	float MinScreenRadiusForVelocityPassSquared = FMath::Square(MinScreenRadiusForVelocityPass);

	// Skip primitives that only cover a small amount of screen space, motion blur on them won't be noticeable.
	if (FMath::Square(PrimitiveBounds.SphereRadius) <= MinScreenRadiusForVelocityPassSquared * LODFactorDistanceSquared)
	{
		return false;
	}

	return true;
}

bool FOpaqueVelocityMeshProcessor::PrimitiveCanHaveVelocity(EShaderPlatform ShaderPlatform, const FPrimitiveSceneProxy* PrimitiveSceneProxy)
{
	if (!FVelocityRendering::IsSeparateVelocityPassSupported())
	{
		return false;
	}

	if (!PrimitiveSceneProxy->IsMovable())
	{
		return false;
	}

	/**
	 * Whether the vertex factory for this primitive requires that it render in the separate velocity pass, as opposed to the base pass.
	 * In cases where the base pass is rendering opaque velocity for a particular mesh batch, we want to filter it out from this pass,
	 * which performs a separate draw call to render velocity.
	 */
	const bool bIsSeparateVelocityPassRequiredByVertexFactory =
		FVelocityRendering::IsSeparateVelocityPassRequiredByVertexFactory(ShaderPlatform, PrimitiveSceneProxy->HasStaticLighting());

	if (!bIsSeparateVelocityPassRequiredByVertexFactory)
	{
		return false;
	}

	return true;
}

bool FOpaqueVelocityMeshProcessor::PrimitiveHasVelocityForFrame(const FPrimitiveSceneProxy* PrimitiveSceneProxy)
{
	if (!PrimitiveSceneProxy->AlwaysHasVelocity())
	{
		// Check if the primitive has moved.
		const FPrimitiveSceneInfo* PrimitiveSceneInfo = PrimitiveSceneProxy->GetPrimitiveSceneInfo();
		const FScene* Scene = PrimitiveSceneInfo->Scene;
		const FMatrix& LocalToWorld = PrimitiveSceneProxy->GetLocalToWorld();
		FMatrix PreviousLocalToWorld = LocalToWorld;
		Scene->VelocityData.GetComponentPreviousLocalToWorld(PrimitiveSceneInfo->PrimitiveComponentId, PreviousLocalToWorld);

		if (LocalToWorld.Equals(PreviousLocalToWorld, 0.0001f))
		{
			// Hasn't moved (treat as background by not rendering any special velocities)
			return false;
		}
	}

	return true;
}

void FOpaqueVelocityMeshProcessor::AddMeshBatch(
	const FMeshBatch& RESTRICT MeshBatch,
	uint64 BatchElementMask,
	const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy,
	int32 StaticMeshId)
{
	const EShaderPlatform ShaderPlatform = GetFeatureLevelShaderPlatform(FeatureLevel);

	if (!PrimitiveCanHaveVelocity(ShaderPlatform, PrimitiveSceneProxy))
	{
		return;
	}

	if (ViewIfDynamicMeshCommand)
	{
		if (!PrimitiveHasVelocityForFrame(PrimitiveSceneProxy))
		{
			return;
		}

		checkSlow(ViewIfDynamicMeshCommand->bIsViewInfo);
		FViewInfo* ViewInfo = (FViewInfo*)ViewIfDynamicMeshCommand;

		if (!PrimitiveHasVelocityForView(*ViewInfo, PrimitiveSceneProxy))
		{
			return;
		}
	}

	const FMaterialRenderProxy* MaterialRenderProxy = MeshBatch.MaterialRenderProxy;
	const FMaterial* Material = &MaterialRenderProxy->GetMaterialWithFallback(FeatureLevel, MaterialRenderProxy);
	const EBlendMode BlendMode = Material->GetBlendMode();
	const bool bIsNotTranslucent = BlendMode == BLEND_Opaque || BlendMode == BLEND_Masked;

	if (MeshBatch.bUseForMaterial && bIsNotTranslucent && ShouldIncludeMaterialInDefaultOpaquePass(*Material))
	{
		// This is specifically done *before* the material swap, as swapped materials may have different fill / cull modes.
		const ERasterizerFillMode MeshFillMode = ComputeMeshFillMode(MeshBatch, *Material);
		const ERasterizerCullMode MeshCullMode = ComputeMeshCullMode(MeshBatch, *Material);

		/**
		 * Materials without masking or custom vertex modifications can be swapped out
		 * for the default material, which simplifies the shader. However, the default
		 * material also does not support being two-sided.
		 */
		const bool bSwapWithDefaultMaterial = Material->WritesEveryPixel() && !Material->IsTwoSided() && !Material->MaterialModifiesMeshPosition_RenderThread();

		if (bSwapWithDefaultMaterial)
		{
			MaterialRenderProxy = UMaterial::GetDefaultMaterial(MD_Surface)->GetRenderProxy();
			Material = MaterialRenderProxy->GetMaterial(FeatureLevel);
		}

		check(Material && MaterialRenderProxy);

		Process(MeshBatch, BatchElementMask, StaticMeshId, PrimitiveSceneProxy, *MaterialRenderProxy, *Material, MeshFillMode, MeshCullMode);
	}
}

bool FTranslucentVelocityMeshProcessor::PrimitiveCanHaveVelocity(EShaderPlatform ShaderPlatform, const FPrimitiveSceneProxy* PrimitiveSceneProxy)
{
	/**
	 * Velocity for translucency is always relevant because the pass also writes depth.
	 * Therefore, the primitive can't be filtered based on motion, or it will break post
	 * effects like depth of field which rely on depth information.
	 */
	return FVelocityRendering::IsSeparateVelocityPassSupported();
}

bool FTranslucentVelocityMeshProcessor::PrimitiveHasVelocityForFrame(const FPrimitiveSceneProxy* PrimitiveSceneProxy)
{
	return true;
}

void FTranslucentVelocityMeshProcessor::AddMeshBatch(
	const FMeshBatch& RESTRICT MeshBatch,
	uint64 BatchElementMask,
	const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy,
	int32 StaticMeshId)
{
	const EShaderPlatform ShaderPlatform = GetFeatureLevelShaderPlatform(FeatureLevel);

	if (!PrimitiveCanHaveVelocity(ShaderPlatform, PrimitiveSceneProxy))
	{
		return;
	}

	if (ViewIfDynamicMeshCommand)
	{
		if (!PrimitiveHasVelocityForFrame(PrimitiveSceneProxy))
		{
			return;
		}

		checkSlow(ViewIfDynamicMeshCommand->bIsViewInfo);
		FViewInfo* ViewInfo = (FViewInfo*)ViewIfDynamicMeshCommand;

		if (!PrimitiveHasVelocityForView(*ViewInfo, PrimitiveSceneProxy))
		{
			return;
		}
	}

	const FMaterialRenderProxy* MaterialRenderProxy = MeshBatch.MaterialRenderProxy;
	const FMaterial* Material = &MeshBatch.MaterialRenderProxy->GetMaterialWithFallback(FeatureLevel, MaterialRenderProxy);

	// Whether the primitive is marked to write translucent velocity / depth.
	const bool bMaterialWritesVelocity = Material->IsTranslucencyWritingVelocity();

	if (MeshBatch.bUseForMaterial && bMaterialWritesVelocity)
	{
		const ERasterizerFillMode MeshFillMode = ComputeMeshFillMode(MeshBatch, *Material);
		const ERasterizerCullMode MeshCullMode = ComputeMeshCullMode(MeshBatch, *Material);

		Process(MeshBatch, BatchElementMask, StaticMeshId, PrimitiveSceneProxy, *MaterialRenderProxy, *Material, MeshFillMode, MeshCullMode);
	}
}

void GetVelocityPassShaders(
	const FMaterial& Material,
	FVertexFactoryType* VertexFactoryType,
	ERHIFeatureLevel::Type FeatureLevel,
	FVelocityHS*& HullShader,
	FVelocityDS*& DomainShader,
	FVelocityVS*& VertexShader,
	FVelocityPS*& PixelShader)
{
	const EMaterialTessellationMode MaterialTessellationMode = Material.GetTessellationMode();

	const bool bNeedsHSDS = RHISupportsTessellation(GShaderPlatformForFeatureLevel[FeatureLevel])
		&& VertexFactoryType->SupportsTessellationShaders()
		&& MaterialTessellationMode != MTM_NoTessellation;

	if (bNeedsHSDS)
	{
		DomainShader = Material.GetShader<FVelocityDS>(VertexFactoryType);
		HullShader = Material.GetShader<FVelocityHS>(VertexFactoryType);
	}

	static const auto* CVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.ShaderPipelines"));
	const bool bUseShaderPipelines = RHISupportsShaderPipelines(GShaderPlatformForFeatureLevel[FeatureLevel]) && !bNeedsHSDS && CVar && CVar->GetValueOnAnyThread() != 0;

	FShaderPipeline* ShaderPipeline = bUseShaderPipelines ? Material.GetShaderPipeline(&VelocityPipeline, VertexFactoryType, false) : nullptr;
	if (ShaderPipeline)
	{
		VertexShader = ShaderPipeline->GetShader<FVelocityVS>();
		PixelShader = ShaderPipeline->GetShader<FVelocityPS>();
		check(VertexShader && PixelShader);
	}
	else
	{
		VertexShader = Material.GetShader<FVelocityVS>(VertexFactoryType);
		PixelShader = Material.GetShader<FVelocityPS>(VertexFactoryType);
		check(VertexShader && PixelShader);
	}
}

void FVelocityMeshProcessor::Process(
	const FMeshBatch& MeshBatch,
	uint64 BatchElementMask,
	int32 StaticMeshId,
	const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy,
	const FMaterialRenderProxy& RESTRICT MaterialRenderProxy,
	const FMaterial& RESTRICT MaterialResource,
	ERasterizerFillMode MeshFillMode,
	ERasterizerCullMode MeshCullMode)
{
	const FVertexFactory* VertexFactory = MeshBatch.VertexFactory;

	TMeshProcessorShaders<
		FVelocityVS,
		FVelocityHS,
		FVelocityDS,
		FVelocityPS> VelocityPassShaders;

	GetVelocityPassShaders(
		MaterialResource,
		VertexFactory->GetType(),
		FeatureLevel,
		VelocityPassShaders.HullShader,
		VelocityPassShaders.DomainShader,
		VelocityPassShaders.VertexShader,
		VelocityPassShaders.PixelShader
	);

	FMeshMaterialShaderElementData ShaderElementData;
	ShaderElementData.InitializeMeshMaterialData(ViewIfDynamicMeshCommand, PrimitiveSceneProxy, MeshBatch, StaticMeshId, false);

	const FMeshDrawCommandSortKey SortKey = CalculateMeshStaticSortKey(VelocityPassShaders.VertexShader, VelocityPassShaders.PixelShader);

	BuildMeshDrawCommands(
		MeshBatch,
		BatchElementMask,
		PrimitiveSceneProxy,
		MaterialRenderProxy,
		MaterialResource,
		PassDrawRenderState,
		VelocityPassShaders,
		MeshFillMode,
		MeshCullMode,
		SortKey,
		EMeshPassFeatures::Default,
		ShaderElementData);
}

FVelocityMeshProcessor::FVelocityMeshProcessor(const FScene* Scene, const FSceneView* InViewIfDynamicMeshCommand, const FMeshPassProcessorRenderState& InPassDrawRenderState, FMeshPassDrawListContext* InDrawListContext)
	: FMeshPassProcessor(Scene, Scene->GetFeatureLevel(), InViewIfDynamicMeshCommand, InDrawListContext)
{
	PassDrawRenderState = InPassDrawRenderState;
	PassDrawRenderState.SetViewUniformBuffer(Scene->UniformBuffers.ViewUniformBuffer);
	PassDrawRenderState.SetInstancedViewUniformBuffer(Scene->UniformBuffers.InstancedViewUniformBuffer);
	PassDrawRenderState.SetPassUniformBuffer(Scene->UniformBuffers.VelocityPassUniformBuffer);
}

FOpaqueVelocityMeshProcessor::FOpaqueVelocityMeshProcessor(const FScene* Scene, const FSceneView* InViewIfDynamicMeshCommand, const FMeshPassProcessorRenderState& InPassDrawRenderState, FMeshPassDrawListContext* InDrawListContext)
	: FVelocityMeshProcessor(Scene, InViewIfDynamicMeshCommand, InPassDrawRenderState, InDrawListContext)
{}

FMeshPassProcessor* CreateVelocityPassProcessor(const FScene* Scene, const FSceneView* InViewIfDynamicMeshCommand, FMeshPassDrawListContext* InDrawListContext)
{
	FMeshPassProcessorRenderState VelocityPassState;
	VelocityPassState.SetBlendState(TStaticBlendState<CW_RGBA>::GetRHI());
	VelocityPassState.SetDepthStencilState(TStaticDepthStencilState<false, CF_DepthNearOrEqual>::GetRHI());

	return new(FMemStack::Get()) FOpaqueVelocityMeshProcessor(Scene, InViewIfDynamicMeshCommand, VelocityPassState, InDrawListContext);
}

FRegisterPassProcessorCreateFunction RegisterVelocityPass(&CreateVelocityPassProcessor, EShadingPath::Deferred,  EMeshPass::Velocity, EMeshPassFlags::CachedMeshCommands | EMeshPassFlags::MainView);

FTranslucentVelocityMeshProcessor::FTranslucentVelocityMeshProcessor(const FScene* Scene, const FSceneView* InViewIfDynamicMeshCommand, const FMeshPassProcessorRenderState& InPassDrawRenderState, FMeshPassDrawListContext* InDrawListContext)
	: FVelocityMeshProcessor(Scene, InViewIfDynamicMeshCommand, InPassDrawRenderState, InDrawListContext)
{}

FMeshPassProcessor* CreateTranslucentVelocityPassProcessor(const FScene* Scene, const FSceneView* InViewIfDynamicMeshCommand, FMeshPassDrawListContext* InDrawListContext)
{
	FMeshPassProcessorRenderState VelocityPassState;
	VelocityPassState.SetBlendState(TStaticBlendState<CW_RGBA>::GetRHI());
	VelocityPassState.SetDepthStencilState(TStaticDepthStencilState<true, CF_DepthNearOrEqual>::GetRHI());

	return new(FMemStack::Get()) FTranslucentVelocityMeshProcessor(Scene, InViewIfDynamicMeshCommand, VelocityPassState, InDrawListContext);
}

FRegisterPassProcessorCreateFunction RegisterTranslucentVelocityPass(&CreateTranslucentVelocityPassProcessor, EShadingPath::Deferred, EMeshPass::TranslucentVelocity, EMeshPassFlags::CachedMeshCommands | EMeshPassFlags::MainView);