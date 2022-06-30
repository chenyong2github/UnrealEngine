// Copyright Epic Games, Inc. All Rights Reserved.

#include "DisplayClusterMeshProjectionRenderer.h"

#include "PrimitiveSceneInfo.h"
#include "PrimitiveSceneProxy.h"
#include "MeshPassProcessor.h"
#include "MeshMaterialShader.h"
#include "MeshPassProcessor.inl"
#include "Shader.h"
#include "CanvasTypes.h"
#include "EngineModule.h"
#include "SceneManagement.h"
#include "SceneViewExtension.h"
#include "ScreenPass.h"
#include "Components/PrimitiveComponent.h"
#include "InstanceCulling/InstanceCullingContext.h"
#include "Materials/Material.h"


//////////////////////////////////////////////////////////////////////////
// Base Render Pass

template<EDisplayClusterMeshProjectionType ProjectionType>
class FMeshProjectionVS : public FMeshMaterialShader
{
public:
	DECLARE_SHADER_TYPE(FMeshProjectionVS, MeshMaterial);

	FMeshProjectionVS() { }
	FMeshProjectionVS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FMeshMaterialShader(Initializer)
	{
		PassUniformBuffer.Bind(Initializer.ParameterMap, FSceneTextureUniformParameters::StaticStructMetadata.GetShaderVariableName());
	}

	static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
	{
		return EnumHasAllFlags(Parameters.Flags, EShaderPermutationFlags::HasEditorOnlyData)
			&& IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5)
			&& Parameters.VertexFactoryType == FindVertexFactoryType(TEXT("FLocalVertexFactory"));
	}
};

using FMeshPerspectiveProjectionVS = FMeshProjectionVS<EDisplayClusterMeshProjectionType::Linear>;
using FMeshAzimuthalProjectionVS = FMeshProjectionVS<EDisplayClusterMeshProjectionType::Azimuthal>;

IMPLEMENT_MATERIAL_SHADER_TYPE(template<>, FMeshPerspectiveProjectionVS, TEXT("/Plugin/nDisplay/Private/MeshProjectionShaders.usf"), TEXT("PerspectiveVS"), SF_Vertex);
IMPLEMENT_MATERIAL_SHADER_TYPE(template<>, FMeshAzimuthalProjectionVS, TEXT("/Plugin/nDisplay/Private/MeshProjectionShaders.usf"), TEXT("AzimuthalVS"), SF_Vertex);

enum EMeshProjectionShaderOutput
{
	Default,
	Normals
};

template<EMeshProjectionShaderOutput OutputType>
class FMeshProjectionPS : public FMeshMaterialShader
{
public:
	DECLARE_SHADER_TYPE(FMeshProjectionPS, MeshMaterial);

	FMeshProjectionPS() { }
	FMeshProjectionPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FMeshMaterialShader(Initializer)
	{
		PassUniformBuffer.Bind(Initializer.ParameterMap, FSceneTextureUniformParameters::StaticStructMetadata.GetShaderVariableName());
	}

	static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
	{
		return EnumHasAllFlags(Parameters.Flags, EShaderPermutationFlags::HasEditorOnlyData)
			&& IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5)
			&& Parameters.VertexFactoryType == FindVertexFactoryType(TEXT("FLocalVertexFactory"));
	}
};

using FMeshProjectionDefaultPS = FMeshProjectionPS<EMeshProjectionShaderOutput::Default>;
using FMeshProjectionNormalPS = FMeshProjectionPS<EMeshProjectionShaderOutput::Normals>;

IMPLEMENT_MATERIAL_SHADER_TYPE(template<>, FMeshProjectionDefaultPS, TEXT("/Plugin/nDisplay/Private/MeshProjectionShaders.usf"), TEXT("MainPS"), SF_Pixel);
IMPLEMENT_MATERIAL_SHADER_TYPE(template<>, FMeshProjectionNormalPS, TEXT("/Plugin/nDisplay/Private/MeshProjectionShaders.usf"), TEXT("NormalPS"), SF_Pixel);

BEGIN_SHADER_PARAMETER_STRUCT(FMeshProjectionPassParameters, )
	SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
	SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FInstanceCullingGlobalUniforms, InstanceCulling)
	SHADER_PARAMETER_STRUCT_INCLUDE(FInstanceCullingDrawParams, InstanceCullingDrawParams)
	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

template<EDisplayClusterMeshProjectionType ProjectionType, EMeshProjectionShaderOutput OutputType = EMeshProjectionShaderOutput::Default> 
class FLightCardEditorMeshPassProcessor : public FMeshPassProcessor
{
public:
	FLightCardEditorMeshPassProcessor(const FScene* InScene, const FSceneView* InView, FMeshPassDrawListContext* InDrawListContext, bool bIsTranslucencyPass = false)
		: FMeshPassProcessor(InScene, GMaxRHIFeatureLevel, InView, InDrawListContext)
		, bTranslucencyPass(bIsTranslucencyPass)
		, bIgnoreTranslucency(false)
	{

		if (bTranslucencyPass)
		{
			DrawRenderState.SetDepthStencilState(TStaticDepthStencilState<false, CF_DepthNearOrEqual>::GetRHI());
			DrawRenderState.SetBlendState(TStaticBlendState<CW_RGBA, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_One>::GetRHI());
		}
		else
		{
			DrawRenderState.SetDepthStencilState(TStaticDepthStencilState<true, CF_DepthNearOrEqual, true, CF_Always, SO_Keep, SO_Keep, SO_Replace>::GetRHI());
			DrawRenderState.SetBlendState(TStaticBlendStateWriteMask<CW_RGBA, CW_RGBA, CW_RGBA, CW_RGBA>::GetRHI());
		}
	}

	virtual void AddMeshBatch(const FMeshBatch& RESTRICT MeshBatch, uint64 BatchElementMask, const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy, int32 StaticMeshId = -1) override final
	{
		const FMaterialRenderProxy* FallbackMaterialRenderProxy = nullptr;
		const FMaterial& Material = MeshBatch.MaterialRenderProxy->GetMaterialWithFallback(FeatureLevel, FallbackMaterialRenderProxy);
		const FMaterialRenderProxy& MaterialRenderProxy = FallbackMaterialRenderProxy ? *FallbackMaterialRenderProxy : *MeshBatch.MaterialRenderProxy;

		if (!CanDrawMeshBatch(MeshBatch, Material))
		{
			return;
		}

		const FVertexFactory* VertexFactory = MeshBatch.VertexFactory;

		TMeshProcessorShaders<FMeshProjectionVS<ProjectionType>, FMeshProjectionPS<OutputType>> PassShaders;

		FMaterialShaderTypes ShaderTypes;
		ShaderTypes.AddShaderType<FMeshProjectionVS<ProjectionType>>();
		ShaderTypes.AddShaderType<FMeshProjectionPS<OutputType>>();

		FMaterialShaders Shaders;
		if (!Material.TryGetShaders(ShaderTypes, VertexFactory->GetType(), Shaders))
		{
			return;
		}

		Shaders.TryGetVertexShader(PassShaders.VertexShader);
		Shaders.TryGetPixelShader(PassShaders.PixelShader);

		const FMeshDrawingPolicyOverrideSettings OverrideSettings = ComputeMeshOverrideSettings(MeshBatch);
		ERasterizerFillMode MeshFillMode = ComputeMeshFillMode(MeshBatch, Material, OverrideSettings);
		ERasterizerCullMode MeshCullMode = ComputeMeshCullMode(MeshBatch, Material, OverrideSettings);

		FMeshMaterialShaderElementData ShaderElementData;
		ShaderElementData.InitializeMeshMaterialData(ViewIfDynamicMeshCommand, PrimitiveSceneProxy, MeshBatch, StaticMeshId, false);

		FMeshDrawCommandSortKey SortKey = CreateMeshSortKey(MeshBatch, PrimitiveSceneProxy, Material, PassShaders.VertexShader.GetShader(), PassShaders.PixelShader.GetShader());

		DrawRenderState.SetStencilRef(StencilValue);

		BuildMeshDrawCommands(
			MeshBatch,
			BatchElementMask,
			PrimitiveSceneProxy,
			MaterialRenderProxy,
			Material,
			DrawRenderState,
			PassShaders,
			MeshFillMode,
			MeshCullMode,
			SortKey,
			EMeshPassFeatures::Default,
			ShaderElementData);
	}

	void SetStencilValue(uint32 InStencilValue)
	{
		StencilValue = InStencilValue;
	}

	void SetIgnoreTranslucency(bool bInIgnoreTranslucency)
	{
		bIgnoreTranslucency = bInIgnoreTranslucency;
	}

private:
	bool CanDrawMeshBatch(const FMeshBatch& RESTRICT MeshBatch, const FMaterial& Material)
	{
		const bool bIsTranslucent = IsTranslucentBlendMode(Material.GetBlendMode());

		if (bTranslucencyPass)
		{
			return bIsTranslucent;
		}
		else
		{
			return bIgnoreTranslucency || !bIsTranslucent;
		}
	}

	FMeshDrawCommandSortKey CreateMeshSortKey(const FMeshBatch& RESTRICT MeshBatch,
		const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy,
		const FMaterial& Material,
		const FMeshMaterialShader* VertexShader, 
		const FMeshMaterialShader* PixelShader)
	{
		FMeshDrawCommandSortKey SortKey = FMeshDrawCommandSortKey::Default;

		if (bTranslucencyPass)
		{
			uint16 SortKeyPriority = 0;
			float DistanceOffset = 0.0f;

			if (PrimitiveSceneProxy)
			{
				const FPrimitiveSceneInfo* PrimitiveSceneInfo = PrimitiveSceneProxy->GetPrimitiveSceneInfo();
				SortKeyPriority = (uint16)((int32)PrimitiveSceneInfo->Proxy->GetTranslucencySortPriority() - (int32)SHRT_MIN);
				DistanceOffset = PrimitiveSceneInfo->Proxy->GetTranslucencySortDistanceOffset();
			}

			SortKey.Translucent.MeshIdInPrimitive = MeshBatch.MeshIdInPrimitive;
			SortKey.Translucent.Priority = SortKeyPriority;
			SortKey.Translucent.Distance = *(uint32*)(&DistanceOffset);
		}
		else
		{
			SortKey.BasePass.VertexShaderHash = (VertexShader ? VertexShader->GetSortKey() : 0) & 0xFFFF;
			SortKey.BasePass.PixelShaderHash = PixelShader ? PixelShader->GetSortKey() : 0;
			SortKey.BasePass.Masked = Material.GetBlendMode() == EBlendMode::BLEND_Masked ? 1 : 0;
		}

		return SortKey;
	}

private:
	FMeshPassProcessorRenderState DrawRenderState;
	uint32 StencilValue;
	bool bTranslucencyPass;
	bool bIgnoreTranslucency;
};


//////////////////////////////////////////////////////////////////////////
// Hit Proxy Render Pass

class FMeshProjectionHitProxyShaderElementData : public FMeshMaterialShaderElementData
{
public:
	FMeshProjectionHitProxyShaderElementData(FHitProxyId InBatchHitProxyId)
		: BatchHitProxyId(InBatchHitProxyId)
	{
	}

	FHitProxyId BatchHitProxyId;
};

class FMeshProjectionHitProxyPS : public FMeshMaterialShader
{
	DECLARE_SHADER_TYPE(FMeshProjectionHitProxyPS, MeshMaterial);

public:
	static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
	{
		// Only compile the hit proxy shader on desktop editor platforms
		return IsPCPlatform(Parameters.Platform) && EnumHasAllFlags(Parameters.Flags, EShaderPermutationFlags::HasEditorOnlyData)
			// and only compile for default materials or materials that are masked.
			&& (Parameters.MaterialParameters.bIsSpecialEngineMaterial ||
				!Parameters.MaterialParameters.bWritesEveryPixel ||
				Parameters.MaterialParameters.bMaterialMayModifyMeshPosition ||
				Parameters.MaterialParameters.bIsTwoSided);
	}

	FMeshProjectionHitProxyPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer):
		FMeshMaterialShader(Initializer)
	{
		HitProxyId.Bind(Initializer.ParameterMap, TEXT("HitProxyId"), SPF_Optional);
	}

	FMeshProjectionHitProxyPS() {}

	void GetShaderBindings(
		const FScene* Scene,
		ERHIFeatureLevel::Type FeatureLevel,
		const FPrimitiveSceneProxy* PrimitiveSceneProxy,
		const FMaterialRenderProxy& MaterialRenderProxy,
		const FMaterial& Material,
		const FMeshPassProcessorRenderState& DrawRenderState,
		const FMeshProjectionHitProxyShaderElementData& ShaderElementData,
		FMeshDrawSingleShaderBindings& ShaderBindings) const
	{
		FMeshMaterialShader::GetShaderBindings(Scene, FeatureLevel, PrimitiveSceneProxy, MaterialRenderProxy, Material, DrawRenderState, ShaderElementData, ShaderBindings);

		ShaderBindings.Add(HitProxyId, ShaderElementData.BatchHitProxyId.GetColor().ReinterpretAsLinear());
	}

private:
	LAYOUT_FIELD(FShaderParameter, HitProxyId)
};

IMPLEMENT_MATERIAL_SHADER_TYPE(, FMeshProjectionHitProxyPS, TEXT("/Plugin/nDisplay/Private/MeshProjectionHitProxy.usf"), TEXT("Main") ,SF_Pixel);

template<EDisplayClusterMeshProjectionType ProjectionType> 
class FLightCardEditorHitProxyMeshPassProcessor : public FMeshPassProcessor
{
public:
	FLightCardEditorHitProxyMeshPassProcessor(const FScene* InScene, const FSceneView* InView, FMeshPassDrawListContext* InDrawListContext)
		: FMeshPassProcessor(InScene, GMaxRHIFeatureLevel, InView, InDrawListContext)
	{
		DrawRenderState.SetDepthStencilState(TStaticDepthStencilState<true, CF_DepthNearOrEqual>::GetRHI());
		DrawRenderState.SetBlendState(TStaticBlendState<>::GetRHI());
	}

	virtual void AddMeshBatch(const FMeshBatch& RESTRICT MeshBatch, uint64 BatchElementMask, const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy, int32 StaticMeshId = -1) override final
	{
		const bool bDrawMeshBatch = MeshBatch.bUseForMaterial
			&& MeshBatch.BatchHitProxyId != FHitProxyId::InvisibleHitProxyId
			&& MeshBatch.bSelectable
			&& PrimitiveSceneProxy
			&& PrimitiveSceneProxy->IsSelectable();

		if (bDrawMeshBatch)
		{
			const FMaterialRenderProxy* MaterialRenderProxy = MeshBatch.MaterialRenderProxy;
			const FMaterial* Material = MaterialRenderProxy->GetMaterialNoFallback(FeatureLevel);
			if (Material && Material->GetRenderingThreadShaderMap())
			{
				if (Material->WritesEveryPixel() && !Material->IsTwoSided() && !Material->MaterialModifiesMeshPosition_RenderThread())
				{
					// Default material doesn't handle masked, and doesn't have the correct bIsTwoSided setting.
					MaterialRenderProxy = UMaterial::GetDefaultMaterial(MD_Surface)->GetRenderProxy();
					check(MaterialRenderProxy);
					Material = MaterialRenderProxy->GetMaterialNoFallback(FeatureLevel);
					check(Material);
				}

				const FVertexFactory* VertexFactory = MeshBatch.VertexFactory;

				TMeshProcessorShaders<FMeshProjectionVS<ProjectionType>, FMeshProjectionHitProxyPS> PassShaders;

				FMaterialShaderTypes ShaderTypes;
				ShaderTypes.AddShaderType<FMeshProjectionVS<ProjectionType>>();
				ShaderTypes.AddShaderType<FMeshProjectionHitProxyPS>();

				FMaterialShaders Shaders;
				if (!Material->TryGetShaders(ShaderTypes, VertexFactory->GetType(), Shaders))
				{
					return;
				}

				Shaders.TryGetVertexShader(PassShaders.VertexShader);
				Shaders.TryGetPixelShader(PassShaders.PixelShader);

				const FMeshDrawingPolicyOverrideSettings OverrideSettings = ComputeMeshOverrideSettings(MeshBatch);
				ERasterizerFillMode MeshFillMode = ComputeMeshFillMode(MeshBatch, *Material, OverrideSettings);
				ERasterizerCullMode MeshCullMode = ComputeMeshCullMode(MeshBatch, *Material, OverrideSettings);

				FMeshProjectionHitProxyShaderElementData ShaderElementData(MeshBatch.BatchHitProxyId);
				ShaderElementData.InitializeMeshMaterialData(ViewIfDynamicMeshCommand, PrimitiveSceneProxy, MeshBatch, StaticMeshId, false);

				const FMeshDrawCommandSortKey SortKey = CalculateMeshStaticSortKey(PassShaders.VertexShader, PassShaders.PixelShader);

				BuildMeshDrawCommands(
					MeshBatch,
					BatchElementMask,
					PrimitiveSceneProxy,
					*MaterialRenderProxy,
					*Material,
					DrawRenderState,
					PassShaders,
					MeshFillMode,
					MeshCullMode,
					SortKey,
					EMeshPassFeatures::Default,
					ShaderElementData);
			}
		}
	}

private:
	FMeshPassProcessorRenderState DrawRenderState;
};

//////////////////////////////////////////////////////////////////////////
// Normals Render Pass

class FMeshProjectionNormalsCreateRWTexturesCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMeshProjectionNormalsCreateRWTexturesCS);
	SHADER_USE_PARAMETER_STRUCT(FMeshProjectionNormalsCreateRWTexturesCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SceneColor)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SceneDepth)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D, SceneStencil)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, RWColor)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, RWDepth)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint>, RWStencil)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsPCPlatform(Parameters.Platform) && EnumHasAllFlags(Parameters.Flags, EShaderPermutationFlags::HasEditorOnlyData);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.CompilerFlags.Add(CFLAG_AllowTypedUAVLoads);
	}
};

IMPLEMENT_GLOBAL_SHADER(FMeshProjectionNormalsCreateRWTexturesCS, "/Plugin/nDisplay/Private/MeshProjectionNormalSmoothing.usf", "CreateRWTexturesCS", SF_Compute);

#define FILTER_KERNEL_SIZE 25

enum EMeshProjectionFilterType
{
	DepthDilate,
	Dilate,
	Blur
};

BEGIN_SHADER_PARAMETER_STRUCT(FMeshProjectionNormalsFilterParameters, )
	SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
	SHADER_PARAMETER_STRUCT(FScreenPassTextureViewportParameters, Input)
	SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D, SceneStencil)
	SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, RWColor)
	SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, RWDepth)
	SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint>, RWStencil)
	SHADER_PARAMETER_SCALAR_ARRAY(float, SpatialKernel, [FILTER_KERNEL_SIZE])
	SHADER_PARAMETER_SCALAR_ARRAY(float, InteriorSpatialKernel, [FILTER_KERNEL_SIZE])
	SHADER_PARAMETER(FVector2f, SampleDirection)
	SHADER_PARAMETER(FVector2f, SampleOffsetScale)
END_SHADER_PARAMETER_STRUCT()

template<EMeshProjectionFilterType FilterType>
class FMeshProjectionNormalsFilterCS : public FGlobalShader
{
public:
	using FParameters = FMeshProjectionNormalsFilterParameters;

	DECLARE_GLOBAL_SHADER(FMeshProjectionNormalsFilterCS);

	FMeshProjectionNormalsFilterCS() { }
	FMeshProjectionNormalsFilterCS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		BindForLegacyShaderParameters<FParameters>(this, Initializer.PermutationId, Initializer.ParameterMap, true);
	}

	static inline const FShaderParametersMetadata* GetRootParametersMetadata() { return FParameters::FTypeInfo::GetStructMetadata(); }

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsPCPlatform(Parameters.Platform) && EnumHasAllFlags(Parameters.Flags, EShaderPermutationFlags::HasEditorOnlyData);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.CompilerFlags.Add(CFLAG_AllowTypedUAVLoads);
	}
};

using FMeshProjectionNormalsDepthDilationCS = FMeshProjectionNormalsFilterCS<EMeshProjectionFilterType::DepthDilate>;
using FMeshProjectionNormalsDilationCS = FMeshProjectionNormalsFilterCS<EMeshProjectionFilterType::Dilate>;
using FMeshProjectionNormalsBlurCS = FMeshProjectionNormalsFilterCS<EMeshProjectionFilterType::Blur>;

IMPLEMENT_SHADER_TYPE(template<>, FMeshProjectionNormalsDepthDilationCS, TEXT("/Plugin/nDisplay/Private/MeshProjectionNormalSmoothing.usf"), TEXT("DepthDilationCS"), SF_Compute);
IMPLEMENT_SHADER_TYPE(template<>, FMeshProjectionNormalsDilationCS, TEXT("/Plugin/nDisplay/Private/MeshProjectionNormalSmoothing.usf"), TEXT("DilationCS"), SF_Compute);
IMPLEMENT_SHADER_TYPE(template<>, FMeshProjectionNormalsBlurCS, TEXT("/Plugin/nDisplay/Private/MeshProjectionNormalSmoothing.usf"), TEXT("BlurCS"), SF_Compute);

class FMeshProjectionNormalsOutputPS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMeshProjectionNormalsOutputPS);
	SHADER_USE_PARAMETER_STRUCT(FMeshProjectionNormalsOutputPS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT(FScreenPassTextureViewportParameters, Input)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, RWColor)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, RWDepth)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsPCPlatform(Parameters.Platform) && EnumHasAllFlags(Parameters.Flags, EShaderPermutationFlags::HasEditorOnlyData);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.CompilerFlags.Add(CFLAG_AllowTypedUAVLoads);
	}
};

IMPLEMENT_GLOBAL_SHADER(FMeshProjectionNormalsOutputPS, "/Plugin/nDisplay/Private/MeshProjectionNormalSmoothing.usf", "OutputNormalMapPS", SF_Pixel);

//////////////////////////////////////////////////////////////////////////
// Selection Outline Render Pass

class FMeshProjectionSelectionOutlinePS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMeshProjectionSelectionOutlinePS);
	SHADER_USE_PARAMETER_STRUCT(FMeshProjectionSelectionOutlinePS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_STRUCT(FScreenPassTextureViewportParameters, Color)
		SHADER_PARAMETER_STRUCT(FScreenPassTextureViewportParameters, Depth)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, ColorTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, ColorSampler)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, DepthTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, DepthSampler)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, EditorPrimitivesDepth)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D, EditorPrimitivesStencil)
		SHADER_PARAMETER(FVector3f, OutlineColor)
		SHADER_PARAMETER(float, SelectionHighlightIntensity)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		// Only PC platforms render editor primitives.
		return IsPCPlatform(Parameters.Platform) && EnumHasAllFlags(Parameters.Flags, EShaderPermutationFlags::HasEditorOnlyData);
	}
};

IMPLEMENT_GLOBAL_SHADER(FMeshProjectionSelectionOutlinePS, "/Plugin/nDisplay/Private/MeshProjectionSelectionOutline.usf", "Main", SF_Pixel);

template<EDisplayClusterMeshProjectionType ProjectionType> 
class FLightCardEditorSelectionPassProcessor : public FMeshPassProcessor
{
public:
	FLightCardEditorSelectionPassProcessor(const FScene* InScene, const FSceneView* InView, FMeshPassDrawListContext* InDrawListContext)
		: FMeshPassProcessor(InScene, GMaxRHIFeatureLevel, InView, InDrawListContext)
		, StencilValue(1)
	{
		DrawRenderState.SetDepthStencilState(TStaticDepthStencilState<true, CF_DepthNearOrEqual, true, CF_Always, SO_Keep, SO_Keep, SO_Replace>::GetRHI());
		DrawRenderState.SetBlendState(TStaticBlendStateWriteMask<CW_NONE, CW_NONE, CW_NONE, CW_NONE>::GetRHI());
	}

	virtual void AddMeshBatch(const FMeshBatch& RESTRICT MeshBatch, uint64 BatchElementMask, const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy, int32 StaticMeshId = -1) override final
	{
		const bool bDrawMeshBatch = MeshBatch.bUseForMaterial
			&& MeshBatch.bUseSelectionOutline
			&& PrimitiveSceneProxy
			&& PrimitiveSceneProxy->WantsSelectionOutline()
			&& (PrimitiveSceneProxy->IsSelected() || PrimitiveSceneProxy->IsHovered());

		if (bDrawMeshBatch)
		{
			const FMaterialRenderProxy* MaterialRenderProxy = MeshBatch.MaterialRenderProxy;
			const FMaterial* Material = MaterialRenderProxy->GetMaterialNoFallback(FeatureLevel);
			if (Material && Material->GetRenderingThreadShaderMap())
			{
				if (Material->WritesEveryPixel() && !Material->IsTwoSided() && !Material->MaterialModifiesMeshPosition_RenderThread())
				{
					// Default material doesn't handle masked, and doesn't have the correct bIsTwoSided setting.
					MaterialRenderProxy = UMaterial::GetDefaultMaterial(MD_Surface)->GetRenderProxy();
					check(MaterialRenderProxy);
					Material = MaterialRenderProxy->GetMaterialNoFallback(FeatureLevel);
					check(Material);
				}
				
				const FVertexFactory* VertexFactory = MeshBatch.VertexFactory;

				TMeshProcessorShaders<FMeshProjectionVS<ProjectionType>, FMeshProjectionDefaultPS> PassShaders;

				FMaterialShaderTypes ShaderTypes;
				ShaderTypes.AddShaderType<FMeshProjectionVS<ProjectionType>>();
				ShaderTypes.AddShaderType<FMeshProjectionDefaultPS>();

				FMaterialShaders Shaders;
				if (!Material->TryGetShaders(ShaderTypes, VertexFactory->GetType(), Shaders))
				{
					return;
				}

				Shaders.TryGetVertexShader(PassShaders.VertexShader);
				Shaders.TryGetPixelShader(PassShaders.PixelShader);

				const FMeshDrawingPolicyOverrideSettings OverrideSettings = ComputeMeshOverrideSettings(MeshBatch);
				ERasterizerFillMode MeshFillMode = ComputeMeshFillMode(MeshBatch, *Material, OverrideSettings);
				ERasterizerCullMode MeshCullMode = CM_None;

				DrawRenderState.SetStencilRef(StencilValue);

				FMeshMaterialShaderElementData ShaderElementData;
				ShaderElementData.InitializeMeshMaterialData(ViewIfDynamicMeshCommand, PrimitiveSceneProxy, MeshBatch, StaticMeshId, false);

				FMeshDrawCommandSortKey SortKey{};

				BuildMeshDrawCommands(
					MeshBatch,
					BatchElementMask,
					PrimitiveSceneProxy,
					*MaterialRenderProxy,
					*Material,
					DrawRenderState,
					PassShaders,
					MeshFillMode,
					MeshCullMode,
					SortKey,
					EMeshPassFeatures::Default,
					ShaderElementData);
			}
		}
	}

private:
	FMeshPassProcessorRenderState DrawRenderState;
	uint32 StencilValue;
};

namespace
{
	template<typename TSetupFunction>
	void DrawScreenPass(
		FRHICommandList& RHICmdList,
		const FSceneView& View,
		const FScreenPassTextureViewport& OutputViewport,
		const FScreenPassTextureViewport& InputViewport,
		const FScreenPassPipelineState& PipelineState,
		TSetupFunction SetupFunction)
	{
		PipelineState.Validate();

		const FIntRect InputRect = InputViewport.Rect;
		const FIntPoint InputSize = InputViewport.Extent;
		const FIntRect OutputRect = OutputViewport.Rect;
		const FIntPoint OutputSize = OutputRect.Size();

		RHICmdList.SetViewport(OutputRect.Min.X, OutputRect.Min.Y, 0.0f, OutputRect.Max.X, OutputRect.Max.Y, 1.0f);

		SetScreenPassPipelineState(RHICmdList, PipelineState);

		// Setting up buffers.
		SetupFunction(RHICmdList);

		FIntPoint LocalOutputPos(FIntPoint::ZeroValue);
		FIntPoint LocalOutputSize(OutputSize);
		EDrawRectangleFlags DrawRectangleFlags = EDRF_UseTriangleOptimization;

		DrawPostProcessPass(
			RHICmdList,
			LocalOutputPos.X, LocalOutputPos.Y, LocalOutputSize.X, LocalOutputSize.Y,
			InputRect.Min.X, InputRect.Min.Y, InputRect.Width(), InputRect.Height(),
			OutputSize,
			InputSize,
			PipelineState.VertexShader,
			View.StereoViewIndex,
			false,
			DrawRectangleFlags);
	}
} //! namespace

//////////////////////////////////////////////////////////////////////////
// FDisplayClusterMeshProjectionRenderer

/** A version of FSimpleElementCollector that has a hit proxy consumer, allowing support for adding hit proxies to the rendered elements */
class FMeshProjectionElementCollector : public FSimpleElementCollector
{
public:
	FMeshProjectionElementCollector(FHitProxyConsumer* InHitProxyConsumer)
		: FSimpleElementCollector()
		, HitProxyConsumer(InHitProxyConsumer)
	{ }

	virtual void SetHitProxy(HHitProxy* HitProxy) override
	{
		FSimpleElementCollector::SetHitProxy(HitProxy);

		if (HitProxyConsumer && HitProxy)
		{
			HitProxyConsumer->AddHitProxy(HitProxy);
		}
	}

private:
	FHitProxyConsumer* HitProxyConsumer;
};

bool FDisplayClusterMeshProjectionPrimitiveFilter::IsPrimitiveComponentFiltered(const UPrimitiveComponent* InPrimitiveComponent) const
{
	if (PrimitiveFilterDelegate.IsBound())
	{
		if (!PrimitiveFilterDelegate.Execute(InPrimitiveComponent))
		{
			return true;
		}
	}

	return false;
}

FVector FDisplayClusterMeshProjectionTransform::ProjectPosition(const FVector& WorldPosition) const
{
	FVector ProjectedPosition(WorldPosition);

	if (Projection != EDisplayClusterMeshProjectionType::Linear)
	{
		const FVector ViewPos = ViewMatrix.TransformPosition(WorldPosition);
		const FVector ProjectedViewPos = FDisplayClusterMeshProjectionRenderer::ProjectViewPosition(ViewPos, Projection);
		ProjectedPosition = InvViewMatrix.TransformPosition(ProjectedViewPos);
	}

	return ProjectedPosition;
}

FVector FDisplayClusterMeshProjectionTransform::UnprojectPosition(const FVector& ProjectedPosition) const
{
	FVector UnprojectedPosition(ProjectedPosition);

	if (Projection != EDisplayClusterMeshProjectionType::Linear)
	{
		const FVector ViewPos = ViewMatrix.TransformPosition(ProjectedPosition);
		const FVector ProjectedViewPos = FDisplayClusterMeshProjectionRenderer::UnprojectViewPosition(ViewPos, Projection);
		UnprojectedPosition = InvViewMatrix.TransformPosition(ProjectedViewPos);
	}

	return ProjectedPosition;
}

FDisplayClusterMeshProjectionRenderer::~FDisplayClusterMeshProjectionRenderer()
{
#if WITH_EDITOR
	for (TWeakObjectPtr<UPrimitiveComponent> PrimitiveComponent : PrimitiveComponents)
	{
		if (!PrimitiveComponent.IsValid())
		{
			continue;
		}

		// Unbind this since it will still have a raw pointer to this renderer
		PrimitiveComponent->SelectionOverrideDelegate.Unbind();
	}
#endif
}

FVector FDisplayClusterMeshProjectionRenderer::ProjectViewPosition(const FVector& ViewPosition, EDisplayClusterMeshProjectionType  ProjectionType)
{
	FVector ProjectedViewPosition(ViewPosition);

	if (ProjectionType == EDisplayClusterMeshProjectionType::Azimuthal)
	{
		const float Rho = ViewPosition.Length();
		const FVector UnitViewPos = ViewPosition.GetSafeNormal();
		const FVector2D PolarCoords = FVector2D(FMath::Acos(UnitViewPos.Z), FMath::Atan2(UnitViewPos.Y, UnitViewPos.X));
		const FVector PlanePos = FVector(PolarCoords.X * FMath::Cos(PolarCoords.Y), PolarCoords.X * FMath::Sin(PolarCoords.Y), 1);

		ProjectedViewPosition = PlanePos.GetSafeNormal() * Rho;
	}

	return ProjectedViewPosition;
}

FVector FDisplayClusterMeshProjectionRenderer::UnprojectViewPosition(const FVector& ProjectedViewPosition, EDisplayClusterMeshProjectionType  ProjectionType)
{
	FVector ViewPosition(ProjectedViewPosition);

	if (ProjectionType == EDisplayClusterMeshProjectionType::Azimuthal)
	{
		const float Rho = ProjectedViewPosition.Length();
		const FVector UnitViewPos = ProjectedViewPosition.GetSafeNormal();

		const FVector PlanePos = UnitViewPos / UnitViewPos.Z;
		const FVector2D PolarCoords = FVector2D(FMath::Sqrt(PlanePos.X * PlanePos.X + PlanePos.Y * PlanePos.Y), FMath::Atan2(PlanePos.Y, PlanePos.X));
		ViewPosition = FVector(FMath::Sin(PolarCoords.X) * FMath::Cos(PolarCoords.Y), FMath::Sin(PolarCoords.X) * FMath::Sin(PolarCoords.Y), FMath::Cos(PolarCoords.X)) * Rho;
	}

	return ViewPosition;
}

void FDisplayClusterMeshProjectionRenderer::AddActor(AActor* Actor)
{
	AddActor(Actor, [](const UPrimitiveComponent* PrimitiveComponent)
	{
		return !PrimitiveComponent->bHiddenInGame;
	});
}

void FDisplayClusterMeshProjectionRenderer::AddActor(AActor* Actor, const TFunctionRef<bool(const UPrimitiveComponent*)>& PrimitiveFilter)
{
	Actor->ForEachComponent<UPrimitiveComponent>(true, [&](UPrimitiveComponent* PrimitiveComponent)
	{
		if (PrimitiveFilter(PrimitiveComponent))
		{
			PrimitiveComponents.Add(PrimitiveComponent);

#if WITH_EDITOR
			PrimitiveComponent->SelectionOverrideDelegate = UPrimitiveComponent::FSelectionOverride::CreateRaw(this, &FDisplayClusterMeshProjectionRenderer::IsPrimitiveComponentSelected);
#endif
		}
	});
}

void FDisplayClusterMeshProjectionRenderer::RemoveActor(AActor* Actor)
{
	TArray<TWeakObjectPtr<UPrimitiveComponent>> ComponentsToRemove = PrimitiveComponents.FilterByPredicate([Actor](const TWeakObjectPtr<UPrimitiveComponent>& PrimitiveComponent)
	{
		return !PrimitiveComponent.IsValid() || PrimitiveComponent->GetOwner() == Actor;
	});

	for (const TWeakObjectPtr<UPrimitiveComponent>& PrimitiveComponent : ComponentsToRemove)
	{
#if WITH_EDITOR
		PrimitiveComponent->SelectionOverrideDelegate.Unbind();
#endif

		PrimitiveComponents.Remove(PrimitiveComponent);
	}
}

void FDisplayClusterMeshProjectionRenderer::ClearScene()
{
	for (const TWeakObjectPtr<UPrimitiveComponent>& PrimitiveComponent : PrimitiveComponents)
	{
#if WITH_EDITOR
		if (PrimitiveComponent.IsValid() && PrimitiveComponent->SelectionOverrideDelegate.IsBound())
		{
			PrimitiveComponent->SelectionOverrideDelegate.Unbind();
		}
#endif
	}

	PrimitiveComponents.Empty();
}

void FDisplayClusterMeshProjectionRenderer::Render(FCanvas* Canvas, FSceneInterface* Scene, const FSceneViewInitOptions& ViewInitOptions, const FEngineShowFlags& EngineShowFlags, EDisplayClusterMeshProjectionType ProjectionType)
{
	Canvas->Flush_GameThread();
	FRenderTarget* RenderTarget = Canvas->GetRenderTarget();
	const bool bIsHitTesting = Canvas->IsHitTesting();
	FHitProxyConsumer* HitProxyConsumer = Canvas->GetHitProxyConsumer();

	ENQUEUE_RENDER_COMMAND(FDrawProjectedMeshes)(
		[RenderTarget, Scene, ViewInitOptions, EngineShowFlags, bIsHitTesting, HitProxyConsumer, ProjectionType, this](FRHICommandListImmediate& RHICmdList)
		{
			FRDGBuilder GraphBuilder(RHICmdList);

			FSceneViewFamilyContext ViewFamily(FSceneViewFamily::ConstructionValues(
				RenderTarget,
				Scene,
				EngineShowFlags)
				.SetTime(FGameTime::GetTimeSinceAppStart())
				.SetGammaCorrection(1.0f));

			if (Scene)
			{
				Scene->IncrementFrameNumber();
				ViewFamily.FrameNumber = Scene->GetFrameNumber();
			}
			else
			{
				ViewFamily.FrameNumber = GFrameNumber;
			}

			ViewFamily.EngineShowFlags.SetHitProxies(bIsHitTesting);

			FScenePrimitiveRenderingContextScopeHelper ScenePrimitiveRenderingContextScopeHelper(GetRendererModule().BeginScenePrimitiveRendering(GraphBuilder, &ViewFamily));

			FSceneViewInitOptions NewInitOptions(ViewInitOptions);
			NewInitOptions.ViewFamily = &ViewFamily;

			GetRendererModule().CreateAndInitSingleView(RHICmdList, &ViewFamily, &NewInitOptions);
			FViewInfo* View = (FViewInfo*)ViewFamily.Views[0];

			FRDGTextureRef OutputTexture = GraphBuilder.RegisterExternalTexture(CreateRenderTarget(RenderTarget->GetRenderTargetTexture(), TEXT("ViewRenderTarget")));
			FRenderTargetBinding OutputRenderTargetBinding(OutputTexture, ERenderTargetLoadAction::ELoad);

			if (ViewFamily.EngineShowFlags.HitProxies)
			{
				FRDGTextureDesc Desc(FRDGTextureDesc::Create2D(OutputTexture->Desc.Extent, PF_B8G8R8A8, FClearValueBinding::Black, TexCreate_RenderTargetable | TexCreate_ShaderResource));
				FRDGTextureRef HitProxyTexture = GraphBuilder.CreateTexture(Desc, TEXT("DisplayClusterMeshProjection.HitProxyTexture"));

				const FRDGTextureDesc DepthDesc = FRDGTextureDesc::Create2D(OutputTexture->Desc.Extent, PF_DepthStencil, FClearValueBinding::DepthFar, TexCreate_DepthStencilTargetable | TexCreate_ShaderResource);
				FRDGTextureRef HitProxyDepthTexture = GraphBuilder.CreateTexture(DepthDesc, TEXT("DisplayClusterMeshProjection.HitProxyDepthTexture"));

				FRenderTargetBinding HitProxyRenderTargetBinding(HitProxyTexture, ERenderTargetLoadAction::EClear);
				FDepthStencilBinding HitProxyDepthStencilBinding(HitProxyDepthTexture, ERenderTargetLoadAction::EClear, ERenderTargetLoadAction::EClear, FExclusiveDepthStencil::DepthWrite_StencilWrite);

				AddHitProxyRenderPass(GraphBuilder, View, ProjectionType, HitProxyRenderTargetBinding, HitProxyDepthStencilBinding);

				// Copy the hit proxy buffer to the viewport family's render target
				{
					FCopyRectPS::FParameters* ScreenPassParameters = GraphBuilder.AllocParameters<FCopyRectPS::FParameters>();
					ScreenPassParameters->InputTexture = HitProxyTexture;
					ScreenPassParameters->InputSampler = TStaticSamplerState<>::GetRHI();
					ScreenPassParameters->RenderTargets[0] = OutputRenderTargetBinding;

					FGlobalShaderMap* GlobalShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
					TShaderMapRef<FScreenPassVS> ScreenPassVS(GlobalShaderMap);
					TShaderMapRef<FCopyRectPS> CopyPixelShader(GlobalShaderMap);

					FRHIBlendState* DefaultBlendState = FScreenPassPipelineState::FDefaultBlendState::GetRHI();
					const FScreenPassTextureViewport RegionViewport(OutputRenderTargetBinding.GetTexture());

					GraphBuilder.AddPass(
						RDG_EVENT_NAME("MeshProjectionRenderer::CopyHitProxyTexture"),
						ScreenPassParameters,
						ERDGPassFlags::Raster,
						[View, ScreenPassVS, CopyPixelShader, RegionViewport, ScreenPassParameters, DefaultBlendState](FRHICommandList& RHICmdList)
					{
						DrawScreenPass(
							RHICmdList,
							*View,
							RegionViewport,
							RegionViewport,
							FScreenPassPipelineState(ScreenPassVS, CopyPixelShader, DefaultBlendState),
							[&](FRHICommandList&)
						{
							SetShaderParameters(RHICmdList, CopyPixelShader, CopyPixelShader.GetPixelShader(), *ScreenPassParameters);
						});
					});
				}
			}
			else
			{
				FRDGTextureRef ColorTexture = GraphBuilder.CreateTexture(OutputTexture->Desc, TEXT("DisplayClusterMeshProjection.ColorTexture"));

				const FRDGTextureDesc DepthDesc = FRDGTextureDesc::Create2D(OutputTexture->Desc.Extent, PF_DepthStencil, FClearValueBinding::DepthFar, TexCreate_DepthStencilTargetable | TexCreate_ShaderResource);
				FRDGTextureRef DepthTexture = GraphBuilder.CreateTexture(DepthDesc, TEXT("DisplayClusterMeshProjection.DepthTexture"));

				FRenderTargetBinding ColorRenderTargetBinding(ColorTexture, ERenderTargetLoadAction::EClear);
				FDepthStencilBinding DepthStencilBinding(DepthTexture, ERenderTargetLoadAction::EClear, ERenderTargetLoadAction::ENoAction, FExclusiveDepthStencil::DepthWrite_StencilNop);

				AddBaseRenderPass(GraphBuilder, View, ProjectionType, ColorRenderTargetBinding, DepthStencilBinding);

				ColorRenderTargetBinding.SetLoadAction(ERenderTargetLoadAction::ELoad);
				DepthStencilBinding.SetDepthLoadAction(ERenderTargetLoadAction::ELoad);

				AddTranslucencyRenderPass(GraphBuilder, View, ProjectionType, ColorRenderTargetBinding, DepthStencilBinding);

#if WITH_EDITOR
				if (EngineShowFlags.SelectionOutline)
				{
					const FRDGTextureDesc SelectionDepthDesc = FRDGTextureDesc::Create2D(OutputTexture->Desc.Extent, PF_DepthStencil, FClearValueBinding::DepthFar, TexCreate_DepthStencilTargetable | TexCreate_ShaderResource);
					FRDGTextureRef SelectionDepthTexture = GraphBuilder.CreateTexture(SelectionDepthDesc, TEXT("DisplayClusterMeshProjection.SelectionDepthTexture"));
					FDepthStencilBinding SelectionDepthStencilBinding(SelectionDepthTexture, ERenderTargetLoadAction::EClear, ERenderTargetLoadAction::EClear, FExclusiveDepthStencil::DepthWrite_StencilWrite);

					AddSelectionDepthRenderPass(GraphBuilder, View, ProjectionType, SelectionDepthStencilBinding);
					AddSelectionOutlineScreenPass(GraphBuilder, View, OutputRenderTargetBinding, ColorTexture, DepthTexture, SelectionDepthTexture);
				}
				else
#endif
				// Copy the scene color to the output render target
				{
					FCopyRectPS::FParameters* ScreenPassParameters = GraphBuilder.AllocParameters<FCopyRectPS::FParameters>();
					ScreenPassParameters->InputTexture = ColorTexture;
					ScreenPassParameters->InputSampler = TStaticSamplerState<>::GetRHI();
					ScreenPassParameters->RenderTargets[0] = OutputRenderTargetBinding;

					FGlobalShaderMap* GlobalShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
					TShaderMapRef<FScreenPassVS> ScreenPassVS(GlobalShaderMap);
					TShaderMapRef<FCopyRectPS> CopyPixelShader(GlobalShaderMap);

					FRHIBlendState* DefaultBlendState = FScreenPassPipelineState::FDefaultBlendState::GetRHI();
					const FScreenPassTextureViewport RegionViewport(OutputRenderTargetBinding.GetTexture());

					GraphBuilder.AddPass(
						RDG_EVENT_NAME("MeshProjectionRenderer::CopyColorTexture"),
						ScreenPassParameters,
						ERDGPassFlags::Raster,
						[View, ScreenPassVS, CopyPixelShader, RegionViewport, ScreenPassParameters, DefaultBlendState](FRHICommandList& RHICmdList)
						{
							DrawScreenPass(
								RHICmdList,
								*View,
								RegionViewport,
								RegionViewport,
								FScreenPassPipelineState(ScreenPassVS, CopyPixelShader, DefaultBlendState),
								[&](FRHICommandList&)
								{
									SetShaderParameters(RHICmdList, CopyPixelShader, CopyPixelShader.GetPixelShader(), *ScreenPassParameters);
						});
					});
				}
			}
			
			FMeshProjectionElementCollector ElementCollector(HitProxyConsumer);
			if (RenderSimpleElementsDelegate.IsBound())
			{
				RenderSimpleElementsDelegate.Execute(View, &ElementCollector);
				AddSimpleElementPass(GraphBuilder, View, OutputRenderTargetBinding, ElementCollector);
			}

			GraphBuilder.Execute();
		});
}

void FDisplayClusterMeshProjectionRenderer::RenderNormals(FCanvas* Canvas,
	FSceneInterface* Scene,
	const FSceneViewInitOptions& ViewInitOptions,
	const FEngineShowFlags& EngineShowFlags,
	EDisplayClusterMeshProjectionType  ProjectionType,
	FDisplayClusterMeshProjectionPrimitiveFilter* PrimitiveFilter)
{
	Canvas->Flush_GameThread();
	FRenderTarget* RenderTarget = Canvas->GetRenderTarget();

	ENQUEUE_RENDER_COMMAND(FDrawProjectedMeshes)(
		[RenderTarget, Scene, ViewInitOptions, EngineShowFlags, ProjectionType, PrimitiveFilter, this](FRHICommandListImmediate& RHICmdList)
		{
			FRDGBuilder GraphBuilder(RHICmdList);

			FSceneViewFamilyContext ViewFamily(FSceneViewFamily::ConstructionValues(
				RenderTarget,
				Scene,
				EngineShowFlags)
				.SetTime(FGameTime::GetTimeSinceAppStart())
				.SetGammaCorrection(1.0f));

			if (Scene)
			{
				Scene->IncrementFrameNumber();
				ViewFamily.FrameNumber = Scene->GetFrameNumber();
			}
			else
			{
				ViewFamily.FrameNumber = GFrameNumber;
			}

			FScenePrimitiveRenderingContextScopeHelper ScenePrimitiveRenderingContextScopeHelper(GetRendererModule().BeginScenePrimitiveRendering(GraphBuilder, &ViewFamily));

			FSceneViewInitOptions NewInitOptions(ViewInitOptions);
			NewInitOptions.ViewFamily = &ViewFamily;

			GetRendererModule().CreateAndInitSingleView(RHICmdList, &ViewFamily, &NewInitOptions);
			FViewInfo* View = (FViewInfo*)ViewFamily.Views[0];

			FRDGTextureRef OutputTexture = GraphBuilder.RegisterExternalTexture(CreateRenderTarget(RenderTarget->GetRenderTargetTexture(), TEXT("ViewRenderTarget")));
			FRenderTargetBinding OutputRenderTargetBinding(OutputTexture, ERenderTargetLoadAction::ELoad);

			const FRDGTextureDesc NormalsDesc(FRDGTextureDesc::Create2D(OutputTexture->Desc.Extent, OutputTexture->Desc.Format, FClearValueBinding::Black, TexCreate_RenderTargetable | TexCreate_ShaderResource));
			FRDGTextureRef NormalsTexture = GraphBuilder.CreateTexture(NormalsDesc, TEXT("DisplayClusterMeshProjection.NormalsTexture"));

			const FRDGTextureDesc DepthDesc = FRDGTextureDesc::Create2D(OutputTexture->Desc.Extent, PF_DepthStencil, FClearValueBinding::DepthFar, TexCreate_DepthStencilTargetable | TexCreate_ShaderResource);
			FRDGTextureRef NormalsDepthTexture = GraphBuilder.CreateTexture(DepthDesc, TEXT("DisplayClusterMeshProjection.NormalsDepthTexture"));

			FRenderTargetBinding NormalsRenderTargetBinding(NormalsTexture, ERenderTargetLoadAction::EClear);
			FDepthStencilBinding NormalsDepthStencilBinding(NormalsDepthTexture, ERenderTargetLoadAction::EClear, ERenderTargetLoadAction::EClear, FExclusiveDepthStencil::DepthWrite_StencilWrite);

			AddNormalsRenderPass(GraphBuilder, View, ProjectionType, PrimitiveFilter, NormalsRenderTargetBinding, NormalsDepthStencilBinding);
			AddNormalsFilterPass(GraphBuilder, View, OutputRenderTargetBinding, NormalsTexture, NormalsDepthTexture);

			GraphBuilder.Execute();
		});
}

// Helper macros that generate appropriate switch cases for each projection type the mesh projection renderer supports, allowing new projection types to be easily added
#define PROJECTION_TYPE_CASE(FuncName, ProjectionType, ...) case ProjectionType: \
	FuncName<ProjectionType>(__VA_ARGS__); \
	break;

#define SWITCH_ON_PROJECTION_TYPE(FuncName, ProjectionType, ...) switch (ProjectionType) \
	{ \
	PROJECTION_TYPE_CASE(FuncName, EDisplayClusterMeshProjectionType::Azimuthal, __VA_ARGS__) \
	PROJECTION_TYPE_CASE(FuncName, EDisplayClusterMeshProjectionType::Linear, __VA_ARGS__) \
	}

void FDisplayClusterMeshProjectionRenderer::AddBaseRenderPass(FRDGBuilder& GraphBuilder,
	const FViewInfo* View, EDisplayClusterMeshProjectionType ProjectionType,
	FRenderTargetBinding& OutputRenderTargetBinding,
	FDepthStencilBinding& OutputDepthStencilBinding)
{
	FMeshProjectionPassParameters* MeshPassParameters = GraphBuilder.AllocParameters<FMeshProjectionPassParameters>();
	MeshPassParameters->View = View->ViewUniformBuffer;
	MeshPassParameters->InstanceCulling = FInstanceCullingContext::CreateDummyInstanceCullingUniformBuffer(GraphBuilder);
	MeshPassParameters->RenderTargets[0] = OutputRenderTargetBinding;
	MeshPassParameters->RenderTargets.DepthStencil = OutputDepthStencilBinding;

	GraphBuilder.AddPass(RDG_EVENT_NAME("MeshProjectionRenderer::Base"), MeshPassParameters, ERDGPassFlags::Raster | ERDGPassFlags::NeverCull, [View, ProjectionType, this](FRHICommandList& RHICmdList)
	{
		FIntRect ViewRect = View->UnscaledViewRect;
		RHICmdList.SetViewport(ViewRect.Min.X, ViewRect.Min.Y, 0.0f, ViewRect.Max.X, ViewRect.Max.Y, 1.0f);

		SWITCH_ON_PROJECTION_TYPE(RenderPrimitives_RenderThread, ProjectionType, View, RHICmdList, false);
	});
}

void FDisplayClusterMeshProjectionRenderer::AddTranslucencyRenderPass(FRDGBuilder& GraphBuilder,
	const FViewInfo* View, EDisplayClusterMeshProjectionType ProjectionType,
	FRenderTargetBinding& OutputRenderTargetBinding,
	FDepthStencilBinding& OutputDepthStencilBinding)
{
	FMeshProjectionPassParameters* MeshPassParameters = GraphBuilder.AllocParameters<FMeshProjectionPassParameters>();
	MeshPassParameters->View = View->ViewUniformBuffer;
	MeshPassParameters->InstanceCulling = FInstanceCullingContext::CreateDummyInstanceCullingUniformBuffer(GraphBuilder);
	MeshPassParameters->RenderTargets[0] = OutputRenderTargetBinding;
	MeshPassParameters->RenderTargets.DepthStencil = OutputDepthStencilBinding;

	GraphBuilder.AddPass(RDG_EVENT_NAME("MeshProjectionRenderer::Translucency"), MeshPassParameters, ERDGPassFlags::Raster | ERDGPassFlags::NeverCull, [View, ProjectionType, this](FRHICommandList& RHICmdList)
	{
		FIntRect ViewRect = View->UnscaledViewRect;
		RHICmdList.SetViewport(ViewRect.Min.X, ViewRect.Min.Y, 0.0f, ViewRect.Max.X, ViewRect.Max.Y, 1.0f);

		SWITCH_ON_PROJECTION_TYPE(RenderPrimitives_RenderThread, ProjectionType, View, RHICmdList, true);
	});
}

void FDisplayClusterMeshProjectionRenderer::AddHitProxyRenderPass(FRDGBuilder& GraphBuilder,
	const FViewInfo* View,
	EDisplayClusterMeshProjectionType ProjectionType,
	FRenderTargetBinding& OutputRenderTargetBinding,
	FDepthStencilBinding& OutputDepthStencilBinding)
{
	FMeshProjectionPassParameters* MeshPassParameters = GraphBuilder.AllocParameters<FMeshProjectionPassParameters>();
	MeshPassParameters->View = View->ViewUniformBuffer;
	MeshPassParameters->InstanceCulling = FInstanceCullingContext::CreateDummyInstanceCullingUniformBuffer(GraphBuilder);
	MeshPassParameters->RenderTargets[0] = OutputRenderTargetBinding;
	MeshPassParameters->RenderTargets.DepthStencil = OutputDepthStencilBinding;

	GraphBuilder.AddPass(RDG_EVENT_NAME("MeshProjectionRenderer::HitProxies"), MeshPassParameters, ERDGPassFlags::Raster | ERDGPassFlags::NeverCull, [View, ProjectionType, this](FRHICommandList& RHICmdList)
	{
		FIntRect ViewRect = View->UnscaledViewRect;
		RHICmdList.SetViewport(ViewRect.Min.X, ViewRect.Min.Y, 0.0f, ViewRect.Max.X, ViewRect.Max.Y, 1.0f);

		SWITCH_ON_PROJECTION_TYPE(RenderHitProxies_RenderThread, ProjectionType, View, RHICmdList);
	});
}

void FDisplayClusterMeshProjectionRenderer::AddNormalsRenderPass(FRDGBuilder& GraphBuilder,
	const FViewInfo* View,
	EDisplayClusterMeshProjectionType ProjectionType,
	FDisplayClusterMeshProjectionPrimitiveFilter* PrimitiveFilter,
	FRenderTargetBinding& OutputRenderTargetBinding,
	FDepthStencilBinding& OutputDepthStencilBinding)
{
	FMeshProjectionPassParameters* MeshPassParameters = GraphBuilder.AllocParameters<FMeshProjectionPassParameters>();
	MeshPassParameters->View = View->ViewUniformBuffer;
	MeshPassParameters->InstanceCulling = FInstanceCullingContext::CreateDummyInstanceCullingUniformBuffer(GraphBuilder);
	MeshPassParameters->RenderTargets[0] = OutputRenderTargetBinding;
	MeshPassParameters->RenderTargets.DepthStencil = OutputDepthStencilBinding;

	GraphBuilder.AddPass(RDG_EVENT_NAME("MeshProjectionRenderer::Normals"),
		MeshPassParameters,
		ERDGPassFlags::Raster | ERDGPassFlags::NeverCull,
		[View, ProjectionType, PrimitiveFilter, this](FRHICommandList& RHICmdList)
		{
			FIntRect ViewRect = View->UnscaledViewRect;
			RHICmdList.SetViewport(ViewRect.Min.X, ViewRect.Min.Y, 0.0f, ViewRect.Max.X, ViewRect.Max.Y, 1.0f);

			SWITCH_ON_PROJECTION_TYPE(RenderNormals_RenderThread, ProjectionType, View, RHICmdList, PrimitiveFilter);
		});
}

void GaussianSpatialFilter(float Sigma, TArray<float>& OutKernel)
{
	constexpr uint32 KernelSize = FILTER_KERNEL_SIZE;
	constexpr uint32 KernelRadius = (KernelSize - 1) / 2;

	OutKernel.Init(0.0, KernelSize);

	const float InvSigma = 1.f / Sigma;

	float Sum = 0.0f;
	for (int Index = 0; Index < KernelSize; ++Index)
	{
		int32 X = Index - KernelRadius;

		OutKernel[Index] = 0.39894 * FMath::Exp(-0.5f * (X * X) * InvSigma * InvSigma) * InvSigma;
		Sum += OutKernel[Index];
	}

	// Normalize the kernel so that there is no change in intensity of the filtered elements
	for (int Index = 0; Index < KernelSize; ++Index)
	{
		OutKernel[Index] /= Sum;
	}
}

template<EMeshProjectionFilterType FilterType>
void AddSeparableFilterPass(FRDGBuilder& GraphBuilder,
	const FViewInfo* View,
	FRDGTexture* SceneDepth,
	FRDGTextureUAV* RWColorUAV,
	FRDGTextureUAV* RWDepthUAV,
	FRDGTextureUAV* RWStencilUAV)
{
	TArray<float> SpatialKernel;
	TArray<float> InteriorSpatialKernel;

	GaussianSpatialFilter(100.0f, SpatialKernel);
	GaussianSpatialFilter(1.0f, InteriorSpatialKernel);

	const FScreenPassTextureViewport InputViewport(RWColorUAV->Desc.Texture);

	FMeshProjectionNormalsFilterParameters* HorizontalPassParameters = GraphBuilder.AllocParameters<FMeshProjectionNormalsFilterParameters>();
	HorizontalPassParameters->View = View->ViewUniformBuffer;
	HorizontalPassParameters->Input = GetScreenPassTextureViewportParameters(InputViewport);
	HorizontalPassParameters->SceneStencil = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::CreateWithPixelFormat(SceneDepth, PF_X24_G8));
	HorizontalPassParameters->RWColor = RWColorUAV;
	HorizontalPassParameters->RWDepth = RWDepthUAV;
	HorizontalPassParameters->RWStencil = RWStencilUAV;
	HorizontalPassParameters->SampleDirection = FVector2f(1.0f, 0.0f);
	HorizontalPassParameters->SampleOffsetScale = FVector2f(1.f);

	for (int Index = 0; Index < FILTER_KERNEL_SIZE; ++Index)
	{
		GET_SCALAR_ARRAY_ELEMENT(HorizontalPassParameters->SpatialKernel, Index) = SpatialKernel[Index];
		GET_SCALAR_ARRAY_ELEMENT(HorizontalPassParameters->InteriorSpatialKernel, Index) = InteriorSpatialKernel[Index];
	}

	TShaderMapRef<FMeshProjectionNormalsFilterCS<FilterType>> ComputeShader(View->ShaderMap);

	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("MeshProjectionRenderer::NormalsFilterHorizontal"),
		ComputeShader,
		HorizontalPassParameters,
		FComputeShaderUtils::GetGroupCount(InputViewport.Extent, FIntPoint(8, 8)));

	FMeshProjectionNormalsFilterParameters* VerticalPassParameters = GraphBuilder.AllocParameters<FMeshProjectionNormalsFilterParameters>();
	VerticalPassParameters->View = View->ViewUniformBuffer;
	VerticalPassParameters->Input = GetScreenPassTextureViewportParameters(InputViewport);
	VerticalPassParameters->SceneStencil = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::CreateWithPixelFormat(SceneDepth, PF_X24_G8));
	VerticalPassParameters->RWColor = RWColorUAV;
	VerticalPassParameters->RWDepth = RWDepthUAV;
	VerticalPassParameters->RWStencil = RWStencilUAV;
	VerticalPassParameters->SampleDirection = FVector2f(0.0f, 1.0f);
	VerticalPassParameters->SampleOffsetScale = FVector2f(1.f);

	for (int Index = 0; Index < FILTER_KERNEL_SIZE; ++Index)
	{
		GET_SCALAR_ARRAY_ELEMENT(VerticalPassParameters->SpatialKernel, Index) = SpatialKernel[Index];
		GET_SCALAR_ARRAY_ELEMENT(VerticalPassParameters->InteriorSpatialKernel, Index) = InteriorSpatialKernel[Index];
	}

	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("MeshProjectionRenderer::NormalsFilterVertical"),
		ComputeShader,
		VerticalPassParameters,
		FComputeShaderUtils::GetGroupCount(InputViewport.Extent, FIntPoint(8, 8)));
}

void FDisplayClusterMeshProjectionRenderer::AddNormalsFilterPass(FRDGBuilder& GraphBuilder,
	const FViewInfo* View,
	FRenderTargetBinding& OutputRenderTargetBinding,
	FRDGTexture* SceneColor,
	FRDGTexture* SceneDepth)
{
	// The normal map filter pass is made up of several separate passes:
	// * First, the scene color, depth, and stencil textures must be copied into UAV read-write textures for the compute shaders to operate on. This pass also
	//   converts the normal vectors from world space to radial space, so that the filtering operates on the relative normals instead of the absolute normals
	// * Second, the depth and normals are dilated so they bleed into empty space. There is a separate interior depth dilate pass which dilates closer overlapping
	//   screen depths into further away screens, so that when blurred, there is a continuous depth change between overlapping screens
	// * Third, the normals and depths are blurred using a Gaussian blur. There are two separate gaussian spatial filters used, one used for blurring empty space regions
	//   and one used for blurring the screens. The empty space filter has a large sigma, allowing a large blur, while the interior filter has a smaller sigma, so that
	//   the blurring doesn't cause too much feature loss
	// * Finally, the RW textures are rendered back to a render target. Here, the normals are placed into the RGB components, while the depth is placed in the A component

	const FRDGTextureDesc RWColorDesc(FRDGTextureDesc::Create2D(SceneColor->Desc.Extent, PF_B8G8R8A8, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV));
	FRDGTextureRef RWColor = GraphBuilder.CreateTexture(RWColorDesc, TEXT("DisplayClusterMeshProjection.NormalsRWTexture"));
	FRDGTextureUAVRef RWColorUAV = GraphBuilder.CreateUAV(RWColor);

	const FRDGTextureDesc RWDepthDesc = FRDGTextureDesc::Create2D(SceneDepth->Desc.Extent, PF_G16, FClearValueBinding::White, TexCreate_UAV | TexCreate_ShaderResource);
	FRDGTextureRef RWDepth = GraphBuilder.CreateTexture(RWDepthDesc, TEXT("DisplayClusterMeshProjection.RWDepth"));
	FRDGTextureUAVRef RWDepthUAV = GraphBuilder.CreateUAV(RWDepth);

	const FRDGTextureDesc RWStencilDesc = FRDGTextureDesc::Create2D(SceneDepth->Desc.Extent, PF_R32_UINT, FClearValueBinding::Black, TexCreate_UAV | TexCreate_ShaderResource);
	FRDGTextureRef RWStencil = GraphBuilder.CreateTexture(RWStencilDesc, TEXT("DisplayClusterMeshProjection.RWStencil"));
	FRDGTextureUAVRef RWStencilUAV = GraphBuilder.CreateUAV(RWStencil);

	// Copy the scene color and depth to RW buffers
	{
		FMeshProjectionNormalsCreateRWTexturesCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FMeshProjectionNormalsCreateRWTexturesCS::FParameters>();
		PassParameters->View = View->ViewUniformBuffer;
		PassParameters->SceneColor = SceneColor;
		PassParameters->SceneDepth = SceneDepth;
		PassParameters->SceneStencil = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::CreateWithPixelFormat(SceneDepth, PF_X24_G8));
		PassParameters->RWColor = RWColorUAV;
		PassParameters->RWDepth = RWDepthUAV;
		PassParameters->RWStencil = RWStencilUAV;

		TShaderMapRef<FMeshProjectionNormalsCreateRWTexturesCS> ComputeShader(View->ShaderMap);

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("MeshProjectionRenderer::CreateRWTextures"),
			ComputeShader,
			PassParameters,
			FComputeShaderUtils::GetGroupCount(SceneColor->Desc.Extent, FIntPoint(8, 8)));
	}

	static constexpr int32 DepthDilatePasses = 1;
	static constexpr int32 DilatePasses = 8;
	static constexpr int32 BlurPasses = 32;

	// Filter the RW buffers, dilating and blurring them appropriately
	for (int32 Index = 0; Index < DepthDilatePasses; Index++)
	{
		AddSeparableFilterPass<EMeshProjectionFilterType::DepthDilate>(GraphBuilder, View, SceneDepth, RWColorUAV, RWDepthUAV, RWStencilUAV);
	}

	for (int32 Index = 0; Index < DilatePasses; ++Index)
	{
		AddSeparableFilterPass<EMeshProjectionFilterType::Dilate>(GraphBuilder, View, SceneDepth, RWColorUAV, RWDepthUAV, RWStencilUAV);
	}

	for (int32 Index = 0; Index < BlurPasses; Index++)
	{
		AddSeparableFilterPass<EMeshProjectionFilterType::Blur>(GraphBuilder, View, SceneDepth, RWColorUAV, RWDepthUAV, RWStencilUAV);
	}

	// Copy the RW buffers to the output render target
	{
		const FScreenPassTextureViewport InputViewport(RWColor);
		const FScreenPassTextureViewport OutputViewport(OutputRenderTargetBinding.GetTexture());

		FMeshProjectionNormalsOutputPS::FParameters* ScreenPassParameters = GraphBuilder.AllocParameters<FMeshProjectionNormalsOutputPS::FParameters>();
		ScreenPassParameters->Input = GetScreenPassTextureViewportParameters(InputViewport);
		ScreenPassParameters->RWColor = RWColorUAV;
		ScreenPassParameters->RWDepth = RWDepthUAV;
		ScreenPassParameters->RenderTargets[0] = OutputRenderTargetBinding;

		TShaderMapRef<FScreenPassVS> ScreenPassVS(View->ShaderMap);
		TShaderMapRef<FMeshProjectionNormalsOutputPS> OutputNormalsPS(View->ShaderMap);

		FRHIBlendState* DefaultBlendState = FScreenPassPipelineState::FDefaultBlendState::GetRHI();

		GraphBuilder.AddPass(
			RDG_EVENT_NAME("MeshProjectionRenderer::OutputNormals"),
			ScreenPassParameters,
			ERDGPassFlags::Raster,
			[View, ScreenPassVS, OutputNormalsPS, InputViewport, OutputViewport, ScreenPassParameters, DefaultBlendState](FRHICommandList& RHICmdList)
		{
			DrawScreenPass(
				RHICmdList,
				*View,
				InputViewport,
				OutputViewport,
				FScreenPassPipelineState(ScreenPassVS, OutputNormalsPS, DefaultBlendState),
				[&](FRHICommandList&)
			{
				SetShaderParameters(RHICmdList, OutputNormalsPS, OutputNormalsPS.GetPixelShader(), *ScreenPassParameters);
			});
		});
	}
}

#if WITH_EDITOR
void FDisplayClusterMeshProjectionRenderer::AddSelectionDepthRenderPass(FRDGBuilder& GraphBuilder, 
		const FViewInfo* View,
		EDisplayClusterMeshProjectionType ProjectionType,
		FDepthStencilBinding& OutputDepthStencilBinding)
{
	FMeshProjectionPassParameters* SelectionPassParameters = GraphBuilder.AllocParameters<FMeshProjectionPassParameters>();
	SelectionPassParameters->View = View->ViewUniformBuffer;
	SelectionPassParameters->InstanceCulling = FInstanceCullingContext::CreateDummyInstanceCullingUniformBuffer(GraphBuilder);
	SelectionPassParameters->RenderTargets.DepthStencil = OutputDepthStencilBinding;

	GraphBuilder.AddPass(RDG_EVENT_NAME("MeshProjectionRenderer::SelectionDepth"), SelectionPassParameters, ERDGPassFlags::Raster | ERDGPassFlags::NeverCull, [View, ProjectionType, this](FRHICommandList& RHICmdList)
	{
		FIntRect ViewRect = View->UnscaledViewRect;
		RHICmdList.SetViewport(ViewRect.Min.X, ViewRect.Min.Y, 0.0f, ViewRect.Max.X, ViewRect.Max.Y, 1.0f);

		SWITCH_ON_PROJECTION_TYPE(RenderSelection_RenderThread, ProjectionType, View, RHICmdList);
	});
}

void FDisplayClusterMeshProjectionRenderer::AddSelectionOutlineScreenPass(FRDGBuilder& GraphBuilder,
	const FViewInfo* View,
	FRenderTargetBinding& OutputRenderTargetBinding,
	FRDGTexture* SceneColor,
	FRDGTexture* SceneDepth,
	FRDGTexture* SelectionDepth)
{
	const FScreenPassTextureViewport OutputViewport(OutputRenderTargetBinding.GetTexture());
	const FScreenPassTextureViewport ColorViewport(SceneColor);
	const FScreenPassTextureViewport DepthViewport(SceneDepth);

	FRHISamplerState* PointClampSampler = TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();

	FMeshProjectionSelectionOutlinePS::FParameters* ScreenPassParameters = GraphBuilder.AllocParameters<FMeshProjectionSelectionOutlinePS::FParameters>();
	ScreenPassParameters->RenderTargets[0] = OutputRenderTargetBinding;
	ScreenPassParameters->View = View->ViewUniformBuffer;
	ScreenPassParameters->Color = GetScreenPassTextureViewportParameters(ColorViewport);
	ScreenPassParameters->Depth = GetScreenPassTextureViewportParameters(DepthViewport);
	ScreenPassParameters->ColorTexture = SceneColor;
	ScreenPassParameters->ColorSampler = PointClampSampler;
	ScreenPassParameters->DepthTexture = SceneDepth;
	ScreenPassParameters->DepthSampler = PointClampSampler;
	ScreenPassParameters->EditorPrimitivesDepth = SelectionDepth;
	ScreenPassParameters->EditorPrimitivesStencil = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::CreateWithPixelFormat(SelectionDepth, PF_X24_G8));
	ScreenPassParameters->OutlineColor = FVector3f(View->SelectionOutlineColor);
	ScreenPassParameters->SelectionHighlightIntensity = GEngine->SelectionHighlightIntensity;

	TShaderMapRef<FScreenPassVS> ScreenPassVS(View->ShaderMap);
	TShaderMapRef<FMeshProjectionSelectionOutlinePS> SelectionOutlinePS(View->ShaderMap);

	FRHIBlendState* DefaultBlendState = FScreenPassPipelineState::FDefaultBlendState::GetRHI();

	GraphBuilder.AddPass(
		RDG_EVENT_NAME("MeshProjectionRenderer::SelectionScreen"),
		ScreenPassParameters,
		ERDGPassFlags::Raster,
		[View, ScreenPassVS, SelectionOutlinePS, OutputViewport, ScreenPassParameters, DefaultBlendState](FRHICommandList& RHICmdList)
	{
		DrawScreenPass(
			RHICmdList,
			*View,
			OutputViewport,
			OutputViewport,
			FScreenPassPipelineState(ScreenPassVS, SelectionOutlinePS, DefaultBlendState),
			[&](FRHICommandList&)
		{
			SetShaderParameters(RHICmdList, SelectionOutlinePS, SelectionOutlinePS.GetPixelShader(), *ScreenPassParameters);
		});
	});
}
#endif

void FDisplayClusterMeshProjectionRenderer::AddSimpleElementPass(FRDGBuilder& GraphBuilder,
	const FViewInfo* View,
	FRenderTargetBinding& OutputRenderTargetBinding,
	FSimpleElementCollector& ElementCollector)
{
	FMeshProjectionPassParameters* PassParameters = GraphBuilder.AllocParameters<FMeshProjectionPassParameters>();
	PassParameters->View = View->ViewUniformBuffer;
	PassParameters->InstanceCulling = FInstanceCullingContext::CreateDummyInstanceCullingUniformBuffer(GraphBuilder);
	PassParameters->RenderTargets[0] = OutputRenderTargetBinding;

	GraphBuilder.AddPass(
		RDG_EVENT_NAME("MeshProjectionRenderer::SimpleElements"),
		PassParameters,
		ERDGPassFlags::Raster,
		[this, View,  &ElementCollector](FRHICommandListImmediate& RHICmdList)
		{
			FIntRect ViewRect = View->UnscaledViewRect;
			RHICmdList.SetViewport(ViewRect.Min.X, ViewRect.Min.Y, 0.0f, ViewRect.Max.X, ViewRect.Max.Y, 1.0f);

			FMeshPassProcessorRenderState DrawRenderState;
			DrawRenderState.SetDepthStencilAccess(FExclusiveDepthStencil::DepthRead_StencilWrite);
			DrawRenderState.SetBlendState(TStaticBlendStateWriteMask<CW_RGBA, CW_RGBA, CW_RGBA, CW_RGBA>::GetRHI());
			DrawRenderState.SetDepthStencilState(TStaticDepthStencilState<false, CF_DepthNearOrEqual>::GetRHI());

			ElementCollector.DrawBatchedElements(RHICmdList, DrawRenderState, *View, EBlendModeFilter::OpaqueAndMasked, ESceneDepthPriorityGroup::SDPG_World);
			ElementCollector.DrawBatchedElements(RHICmdList, DrawRenderState, *View, EBlendModeFilter::OpaqueAndMasked, ESceneDepthPriorityGroup::SDPG_Foreground);
		}
	);
}

template<EDisplayClusterMeshProjectionType ProjectionType>
void FDisplayClusterMeshProjectionRenderer::RenderPrimitives_RenderThread(const FSceneView* View, FRHICommandList& RHICmdList, bool bTranslucencyPass)
{
	DrawDynamicMeshPass(*View, RHICmdList, [View, bTranslucencyPass, this](FDynamicPassMeshDrawListContext* DynamicMeshPassContext)
	{
		TArray<FPrimitiveSceneProxy*> PrimitiveSceneProxies;
		for (const TWeakObjectPtr<UPrimitiveComponent>& PrimitiveComponent : PrimitiveComponents)
		{
			if (PrimitiveComponent.IsValid() && PrimitiveComponent->SceneProxy)
			{
				PrimitiveSceneProxies.Add(PrimitiveComponent->SceneProxy);
			}
		}

		FLightCardEditorMeshPassProcessor<ProjectionType> MeshProcessor(nullptr, View, DynamicMeshPassContext, bTranslucencyPass);

		for (FPrimitiveSceneProxy* PrimitiveProxy : PrimitiveSceneProxies)
		{
			if (const FMeshBatch* MeshBatch = PrimitiveProxy->GetPrimitiveSceneInfo()->GetMeshBatch(PrimitiveProxy->GetPrimitiveSceneInfo()->StaticMeshes.Num() - 1))
			{
				MeshBatch->MaterialRenderProxy->UpdateUniformExpressionCacheIfNeeded(View->GetFeatureLevel());

				const uint64 BatchElementMask = ~0ull;
				MeshProcessor.AddMeshBatch(*MeshBatch, BatchElementMask, PrimitiveProxy);
			}
		}
	});
}

template<EDisplayClusterMeshProjectionType ProjectionType>
void FDisplayClusterMeshProjectionRenderer::RenderHitProxies_RenderThread(const FSceneView* View, FRHICommandList& RHICmdList)
{
	DrawDynamicMeshPass(*View, RHICmdList, [View, this](FDynamicPassMeshDrawListContext* DynamicMeshPassContext)
	{
		TArray<FPrimitiveSceneProxy*> PrimitiveSceneProxies;
		for (const TWeakObjectPtr<UPrimitiveComponent>& PrimitiveComponent : PrimitiveComponents)
		{
			if (PrimitiveComponent.IsValid() && PrimitiveComponent->SceneProxy)
			{
				PrimitiveSceneProxies.Add(PrimitiveComponent->SceneProxy);
			}
		}

		FLightCardEditorHitProxyMeshPassProcessor<ProjectionType> MeshProcessor(nullptr, View, DynamicMeshPassContext);

		for (FPrimitiveSceneProxy* PrimitiveProxy : PrimitiveSceneProxies)
		{
			if (const FMeshBatch* MeshBatch = PrimitiveProxy->GetPrimitiveSceneInfo()->GetMeshBatch(PrimitiveProxy->GetPrimitiveSceneInfo()->StaticMeshes.Num() - 1))
			{
				MeshBatch->MaterialRenderProxy->UpdateUniformExpressionCacheIfNeeded(View->GetFeatureLevel());

				const uint64 BatchElementMask = ~0ull;
				MeshProcessor.AddMeshBatch(*MeshBatch, BatchElementMask, PrimitiveProxy);
			}
		}
	});
}

template<EDisplayClusterMeshProjectionType ProjectionType>
void FDisplayClusterMeshProjectionRenderer::RenderNormals_RenderThread(const FSceneView* View, FRHICommandList& RHICmdList, FDisplayClusterMeshProjectionPrimitiveFilter* PrimitiveFilter)
{
	DrawDynamicMeshPass(*View, RHICmdList, [View, PrimitiveFilter, this](FDynamicPassMeshDrawListContext* DynamicMeshPassContext)
	{
		TArray<FPrimitiveSceneProxy*> PrimitiveSceneProxies;
		for (const TWeakObjectPtr<UPrimitiveComponent>& PrimitiveComponent : PrimitiveComponents)
		{
			if (PrimitiveComponent.IsValid() && PrimitiveComponent->SceneProxy)
			{
				if (PrimitiveFilter && PrimitiveFilter->IsPrimitiveComponentFiltered(PrimitiveComponent.Get()))
				{
					continue;
				}

				PrimitiveSceneProxies.Add(PrimitiveComponent->SceneProxy);
			}
		}

		FLightCardEditorMeshPassProcessor<ProjectionType, EMeshProjectionShaderOutput::Normals> MeshProcessor(nullptr, View, DynamicMeshPassContext);
		MeshProcessor.SetStencilValue(1);
		MeshProcessor.SetIgnoreTranslucency(true);

		for (FPrimitiveSceneProxy* PrimitiveProxy : PrimitiveSceneProxies)
		{
			if (const FMeshBatch* MeshBatch = PrimitiveProxy->GetPrimitiveSceneInfo()->GetMeshBatch(PrimitiveProxy->GetPrimitiveSceneInfo()->StaticMeshes.Num() - 1))
			{
				MeshBatch->MaterialRenderProxy->UpdateUniformExpressionCacheIfNeeded(View->GetFeatureLevel());

				const uint64 BatchElementMask = ~0ull;
				MeshProcessor.AddMeshBatch(*MeshBatch, BatchElementMask, PrimitiveProxy);
			}
		}
	});
}

#if WITH_EDITOR
template<EDisplayClusterMeshProjectionType ProjectionType>
void FDisplayClusterMeshProjectionRenderer::RenderSelection_RenderThread(const FSceneView* View, FRHICommandList& RHICmdList)
{
	DrawDynamicMeshPass(*View, RHICmdList, [View, this](FDynamicPassMeshDrawListContext* DynamicMeshPassContext)
	{
		TArray<FPrimitiveSceneProxy*> PrimitiveSceneProxies;
		for (const TWeakObjectPtr<UPrimitiveComponent>& PrimitiveComponent : PrimitiveComponents)
		{
			if (PrimitiveComponent.IsValid() && PrimitiveComponent->SceneProxy && PrimitiveComponent->SceneProxy->IsSelected())
			{
				PrimitiveSceneProxies.Add(PrimitiveComponent->SceneProxy);
			}
		}

		FLightCardEditorSelectionPassProcessor<ProjectionType> MeshProcessor(nullptr, View, DynamicMeshPassContext);

		for (FPrimitiveSceneProxy* PrimitiveProxy : PrimitiveSceneProxies)
		{
			if (const FMeshBatch* MeshBatch = PrimitiveProxy->GetPrimitiveSceneInfo()->GetMeshBatch(PrimitiveProxy->GetPrimitiveSceneInfo()->StaticMeshes.Num() - 1))
			{
				MeshBatch->MaterialRenderProxy->UpdateUniformExpressionCacheIfNeeded(View->GetFeatureLevel());

				const uint64 BatchElementMask = ~0ull;
				MeshProcessor.AddMeshBatch(*MeshBatch, BatchElementMask, PrimitiveProxy);
			}
		}
	});
}
#endif

bool FDisplayClusterMeshProjectionRenderer::IsPrimitiveComponentSelected(const UPrimitiveComponent* InPrimitiveComponent)
{
	if (ActorSelectedDelegate.IsBound())
	{
		return ActorSelectedDelegate.Execute(InPrimitiveComponent->GetOwner());
	}

	return false;
}

// Explicit template specializations for each projection type
#define PROJECTION_TYPE_TEMPLATE_SPECIALIZATION(FuncName, ProjectionType, ...) template void FDisplayClusterMeshProjectionRenderer::FuncName<ProjectionType>(__VA_ARGS__);
#define CREATE_PROJECTION_TYPE_TEMPLATE_SPECIALIZATIONS(FuncName, ...) \
	PROJECTION_TYPE_TEMPLATE_SPECIALIZATION(FuncName, EDisplayClusterMeshProjectionType::Linear, __VA_ARGS__) \
	PROJECTION_TYPE_TEMPLATE_SPECIALIZATION(FuncName, EDisplayClusterMeshProjectionType::Azimuthal, __VA_ARGS__)

CREATE_PROJECTION_TYPE_TEMPLATE_SPECIALIZATIONS(RenderPrimitives_RenderThread, const FSceneView* View, FRHICommandList& RHICmdList, bool bTranslucencyPass)
CREATE_PROJECTION_TYPE_TEMPLATE_SPECIALIZATIONS(RenderHitProxies_RenderThread, const FSceneView* View, FRHICommandList& RHICmdList)
CREATE_PROJECTION_TYPE_TEMPLATE_SPECIALIZATIONS(RenderNormals_RenderThread, const FSceneView* View, FRHICommandList& RHICmdList, FDisplayClusterMeshProjectionPrimitiveFilter* PrimitiveFilter)

#if WITH_EDITOR
CREATE_PROJECTION_TYPE_TEMPLATE_SPECIALIZATIONS(RenderSelection_RenderThread, const FSceneView* View, FRHICommandList& RHICmdList)
#endif