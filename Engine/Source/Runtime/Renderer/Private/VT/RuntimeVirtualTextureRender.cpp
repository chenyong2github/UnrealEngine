// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "VT/RuntimeVirtualTextureRender.h"

#include "Components/RuntimeVirtualTextureComponent.h"
#include "GlobalShader.h"
#include "GPUScene.h"
#include "MaterialShader.h"
#include "MeshPassProcessor.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RenderUtils.h"
#include "ScenePrivate.h"
#include "SceneRenderTargets.h"
#include "ShaderBaseClasses.h"
#include "VT/RuntimeVirtualTexture.h"
#include "VT/RuntimeVirtualTextureSceneProxy.h"
#include "MeshPassProcessor.inl"


namespace RuntimeVirtualTexture
{
	/** Mesh material shader for writing to the virtual texture. */
	class FShader_VirtualTextureMaterialDraw : public FMeshMaterialShader
	{
	public:
		typedef FRenderTargetParameters FParameters;

		static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
		{
			return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5) && 
				(Parameters.Material->GetMaterialDomain() == MD_RuntimeVirtualTexture || Parameters.Material->HasRuntimeVirtualTextureOutput());
		}

		static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
		{
			FMeshMaterialShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
			OutEnvironment.SetDefine(TEXT("VIRTUAL_TEXTURE_PAGE_RENDER"), 1);
		}

		FShader_VirtualTextureMaterialDraw()
		{}

		FShader_VirtualTextureMaterialDraw(const FMeshMaterialShaderType::CompiledShaderInitializerType& Initializer)
			: FMeshMaterialShader(Initializer)
		{
			// Ensure FMeshMaterialShader::PassUniformBuffer is bound (although currently unused)
			PassUniformBuffer.Bind(Initializer.ParameterMap, FSceneTexturesUniformParameters::StaticStructMetadata.GetShaderVariableName());
		}

		template <typename TRHICmdList>
		void SetParameters(TRHICmdList& RHICmdList, FSceneView const& View, FMaterialRenderProxy const& MaterialProxy)
		{
			FMeshMaterialShader::SetParameters(
				RHICmdList,
				GetPixelShader(),
				&MaterialProxy,
				*MaterialProxy.GetMaterial(View.FeatureLevel),
				View,
				View.ViewUniformBuffer,
				ESceneTextureSetupMode::All);
		}
	};


	/** Specialization for ERuntimeVirtualTextureMaterialType::BaseColor */
	class FMaterialPolicy_BaseColor
	{
	public:
		static void ModifyCompilationEnvironment(FShaderCompilerEnvironment& OutEnvironment)
		{
			OutEnvironment.SetDefine(TEXT("OUT_BASECOLOR"), 1);
		}

		static FRHIBlendState* GetBlendState()
		{
			return TStaticBlendState< CW_RGBA, BO_Add, BF_One, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_One >::GetRHI();
		}
	};

	/** Specialization for ERuntimeVirtualTextureMaterialType::BaseColor_Normal_Specular */
	class FMaterialPolicy_BaseColorNormalSpecular
	{
	public:
		static void ModifyCompilationEnvironment(FShaderCompilerEnvironment& OutEnvironment)
		{
			OutEnvironment.SetDefine(TEXT("OUT_BASECOLOR_NORMAL_SPECULAR"), 1);
		}

		static FRHIBlendState* GetBlendState()
		{
			return TStaticBlendState<
				CW_RGBA, BO_Add, BF_One, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_One,
				CW_RGBA, BO_Add, BF_One, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_One,
				CW_RGBA, BO_Add, BF_One, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_One >::GetRHI();
		}
	};

	/** Specialization for ERuntimeVirtualTextureMaterialType::WorldHeight */
	class FMaterialPolicy_WorldHeight
	{
	public:
		static void ModifyCompilationEnvironment(FShaderCompilerEnvironment& OutEnvironment)
		{
			OutEnvironment.SetDefine(TEXT("OUT_WORLDHEIGHT"), 1);
			OutEnvironment.SetRenderTargetOutputFormat(0, PF_R32_FLOAT);
		}

		static FRHIBlendState* GetBlendState()
		{
			return TStaticBlendState< CW_RED, BO_Max, BF_One, BF_One, BO_Add, BF_One, BF_One >::GetRHI();
		}
	};


	/** Vertex shader derivation of material shader. Templated on policy for virtual texture layout. */
	template< class MaterialPolicy >
	class FShader_VirtualTextureMaterialDraw_VS : public FShader_VirtualTextureMaterialDraw
	{
	public:
		DECLARE_SHADER_TYPE(FShader_VirtualTextureMaterialDraw_VS, MeshMaterial);

		FShader_VirtualTextureMaterialDraw_VS()
		{}

		FShader_VirtualTextureMaterialDraw_VS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
			: FShader_VirtualTextureMaterialDraw(Initializer)
		{}

		static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
		{
			FShader_VirtualTextureMaterialDraw::ModifyCompilationEnvironment(Parameters, OutEnvironment);
			MaterialPolicy::ModifyCompilationEnvironment(OutEnvironment);
		}
	};

	/** Pixel shader derivation of material shader. Templated on policy for virtual texture layout. */
	template< class MaterialPolicy >
	class FShader_VirtualTextureMaterialDraw_PS : public FShader_VirtualTextureMaterialDraw
	{
	public:
		DECLARE_SHADER_TYPE(FShader_VirtualTextureMaterialDraw_VS< MaterialPolicy >, MeshMaterial);

		FShader_VirtualTextureMaterialDraw_PS()
		{}

		FShader_VirtualTextureMaterialDraw_PS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
			: FShader_VirtualTextureMaterialDraw(Initializer)
		{}

		static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
		{
			FShader_VirtualTextureMaterialDraw::ModifyCompilationEnvironment(Parameters, OutEnvironment);
			MaterialPolicy::ModifyCompilationEnvironment(OutEnvironment);
		}
	};

	// If we change this macro or add additional policy types then we need to update GetRuntimeVirtualTextureShaderTypes() in LandscapeRender.cpp
	// That code is used to filter out unnecessary shader variations
#define IMPLEMENT_VIRTUALTEXTURE_SHADER_TYPE(PolicyType, PolicyName) \
	typedef FShader_VirtualTextureMaterialDraw_VS<PolicyType> TVirtualTextureVS##PolicyName; \
	IMPLEMENT_MATERIAL_SHADER_TYPE(template<>,TVirtualTextureVS##PolicyName, TEXT("/Engine/Private/VirtualTextureMaterial.usf"), TEXT("MainVS"), SF_Vertex); \
	typedef FShader_VirtualTextureMaterialDraw_PS<PolicyType> TVirtualTexturePS##PolicyName; \
	IMPLEMENT_MATERIAL_SHADER_TYPE(template<>,TVirtualTexturePS##PolicyName, TEXT("/Engine/Private/VirtualTextureMaterial.usf"), TEXT("MainPS"), SF_Pixel);

	IMPLEMENT_VIRTUALTEXTURE_SHADER_TYPE(FMaterialPolicy_BaseColor, BaseColor);
	IMPLEMENT_VIRTUALTEXTURE_SHADER_TYPE(FMaterialPolicy_BaseColorNormalSpecular, BaseColorNormalSpecular);
	IMPLEMENT_VIRTUALTEXTURE_SHADER_TYPE(FMaterialPolicy_WorldHeight, WorldHeight);

	/** Mesh processor for rendering static meshes to the virtual texture */
	class FRuntimeVirtualTextureMeshProcessor : public FMeshPassProcessor
	{
	public:
		FRuntimeVirtualTextureMeshProcessor(const FScene* InScene, const FSceneView* InView, FMeshPassDrawListContext* InDrawListContext)
			: FMeshPassProcessor(InScene, InScene->GetFeatureLevel(), InView, InDrawListContext)
		{
			DrawRenderState.SetViewUniformBuffer(Scene->UniformBuffers.VirtualTextureViewUniformBuffer);
			DrawRenderState.SetInstancedViewUniformBuffer(Scene->UniformBuffers.InstancedViewUniformBuffer);
			DrawRenderState.SetDepthStencilState(TStaticDepthStencilState<false, CF_Always>::GetRHI());
		}

	private:
		template<class MaterialPolicy>
		void Process(
			const FMeshBatch& MeshBatch,
			uint64 BatchElementMask,
			int32 StaticMeshId,
			const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy,
			const FMaterialRenderProxy& RESTRICT MaterialRenderProxy,
			const FMaterial& RESTRICT MaterialResource)
		{
			const FVertexFactory* VertexFactory = MeshBatch.VertexFactory;

			TMeshProcessorShaders<
				FShader_VirtualTextureMaterialDraw_VS< MaterialPolicy >,
				FBaseHS,
				FBaseDS,
				FShader_VirtualTextureMaterialDraw_PS< MaterialPolicy > > Shaders;

			Shaders.VertexShader = MaterialResource.GetShader< FShader_VirtualTextureMaterialDraw_VS< MaterialPolicy > >(VertexFactory->GetType());
			Shaders.PixelShader = MaterialResource.GetShader< FShader_VirtualTextureMaterialDraw_PS< MaterialPolicy > >(VertexFactory->GetType());

			DrawRenderState.SetBlendState(MaterialPolicy::GetBlendState());

			ERasterizerFillMode MeshFillMode = ComputeMeshFillMode(MeshBatch, MaterialResource);
			ERasterizerCullMode MeshCullMode = ComputeMeshCullMode(MeshBatch, MaterialResource);

			FMeshMaterialShaderElementData ShaderElementData;
			ShaderElementData.InitializeMeshMaterialData(ViewIfDynamicMeshCommand, PrimitiveSceneProxy, MeshBatch, StaticMeshId, false);

			FMeshDrawCommandSortKey SortKey;
			SortKey.Translucent.MeshIdInPrimitive = MeshBatch.MeshIdInPrimitive;
			SortKey.Translucent.Distance = 0;
			SortKey.Translucent.Priority = (uint16)((int32)PrimitiveSceneProxy->GetTranslucencySortPriority() - (int32)SHRT_MIN);

			BuildMeshDrawCommands(
				MeshBatch,
				BatchElementMask,
				PrimitiveSceneProxy,
				MaterialRenderProxy,
				MaterialResource,
				DrawRenderState,
				Shaders,
				MeshFillMode,
				MeshCullMode,
				SortKey,
				EMeshPassFeatures::Default,
				ShaderElementData);
		}

	public:
		virtual void AddMeshBatch(const FMeshBatch& RESTRICT MeshBatch, uint64 BatchElementMask, const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy, int32 StaticMeshId = -1) override final
		{
			if (MeshBatch.bRenderToVirtualTexture)
			{
				const FMaterialRenderProxy* FallbackMaterialRenderProxyPtr = nullptr;
				const FMaterial& Material = MeshBatch.MaterialRenderProxy->GetMaterialWithFallback(FeatureLevel, FallbackMaterialRenderProxyPtr);
				const FMaterialRenderProxy& MaterialRenderProxy = FallbackMaterialRenderProxyPtr ? *FallbackMaterialRenderProxyPtr : *MeshBatch.MaterialRenderProxy;

				if (Material.GetMaterialDomain() == MD_RuntimeVirtualTexture || Material.HasRuntimeVirtualTextureOutput_RenderThread())
				{
					switch ((ERuntimeVirtualTextureMaterialType)MeshBatch.RuntimeVirtualTextureMaterialType)
					{
					case ERuntimeVirtualTextureMaterialType::BaseColor:
						Process<FMaterialPolicy_BaseColor>(MeshBatch, BatchElementMask, StaticMeshId, PrimitiveSceneProxy, MaterialRenderProxy, Material);
						break;
					case ERuntimeVirtualTextureMaterialType::BaseColor_Normal_Specular:
					case ERuntimeVirtualTextureMaterialType::BaseColor_Normal_Specular_YCoCg:
						Process<FMaterialPolicy_BaseColorNormalSpecular>(MeshBatch, BatchElementMask, StaticMeshId, PrimitiveSceneProxy, MaterialRenderProxy, Material);
						break;
					case ERuntimeVirtualTextureMaterialType::WorldHeight:
						Process<FMaterialPolicy_WorldHeight>(MeshBatch, BatchElementMask, StaticMeshId, PrimitiveSceneProxy, MaterialRenderProxy, Material);
						break;
					default:
						break;
					}
				}
			}
		}

	private:
		FMeshPassProcessorRenderState DrawRenderState;
	};


	/** Registration for virtual texture command caching pass */
	FMeshPassProcessor* CreateRuntimeVirtualTexturePassProcessor(const FScene* Scene, const FSceneView* InViewIfDynamicMeshCommand, FMeshPassDrawListContext* InDrawListContext)
	{
		return new(FMemStack::Get()) FRuntimeVirtualTextureMeshProcessor(Scene, InViewIfDynamicMeshCommand, InDrawListContext);
	}

	FRegisterPassProcessorCreateFunction RegisterVirtualTexturePass(&CreateRuntimeVirtualTexturePassProcessor, EShadingPath::Deferred, EMeshPass::VirtualTexture, EMeshPassFlags::CachedMeshCommands);


	/** Collect meshes and draw. */
	void DrawMeshes(FRHICommandListImmediate& RHICmdList, FScene const* Scene, FViewInfo const* View, ERuntimeVirtualTextureMaterialType MaterialType, uint32 RuntimeVirtualTextureMask, uint8 vLevel, uint8 MaxLevel)
	{
		// Cached draw command collectors
		const FCachedPassMeshDrawList& SceneDrawList = Scene->CachedDrawLists[EMeshPass::VirtualTexture];
		TArray<FVisibleMeshDrawCommand, TInlineAllocator<256>> CachedDrawCommands;

		// Uncached mesh processor
		FDynamicMeshDrawCommandStorage MeshDrawCommandStorage;
		FMeshCommandOneFrameArray AllocatedCommands;
		FGraphicsMinimalPipelineStateSet GraphicsMinimalPipelineStateSet;
		FDynamicPassMeshDrawListContext DynamicMeshPassContext(MeshDrawCommandStorage, AllocatedCommands, GraphicsMinimalPipelineStateSet);
		FRuntimeVirtualTextureMeshProcessor MeshProcessor(Scene, View, &DynamicMeshPassContext);

		// Pre-calculate view factors used for culling
		const float RcpWorldSize = 1.f / (View->ViewMatrices.GetInvProjectionMatrix().M[0][0]);
		const float WorldToPixel = View->ViewRect.Width() * RcpWorldSize;

		// Iterate over scene and collect visible virtual texture draw commands for this view
		//todo: Consider a broad phase (quad tree etc?) here. (But only if running over PrimitiveVirtualTextureFlags shows up as a bottleneck.)
		for (int32 PrimitiveIndex = 0; PrimitiveIndex < Scene->Primitives.Num(); ++PrimitiveIndex)
		{
			const FPrimitiveVirtualTextureFlags Flags = Scene->PrimitiveVirtualTextureFlags[PrimitiveIndex];
			if (!Flags.bRenderToVirtualTexture)
			{
				continue;
			}
			if ((Flags.RuntimeVirtualTextureMask & RuntimeVirtualTextureMask) == 0)
			{
				continue;
			}

			//todo[vt]: In our case we know that frustum is an oriented box so investigate cheaper test for intersecting that
			const FSphere SphereBounds = Scene->PrimitiveBounds[PrimitiveIndex].BoxSphereBounds.GetSphere();
			if (!View->ViewFrustum.IntersectSphere(SphereBounds.Center, SphereBounds.W))
			{
				continue;
			}

			// Cull primitives according to mip level or pixel coverage
			const FPrimitiveVirtualTextureLodInfo LodInfo = Scene->PrimitiveVirtualTextureLod[PrimitiveIndex];
			if (LodInfo.CullMethod == 0)
			{
				if (MaxLevel - vLevel < LodInfo.CullValue)
				{
					continue;
				}
			}
			else
			{
				// Note that we use 2^MinPixelCoverage as that scales linearly with mip extents
				int32 PixelCoverage = FMath::FloorToInt(2.f * SphereBounds.W * WorldToPixel);
				if (PixelCoverage < (1 << LodInfo.CullValue))
				{
					continue;
				}
			}

			FPrimitiveSceneInfo* PrimitiveSceneInfo = Scene->Primitives[PrimitiveIndex];

			// Calculate Lod for current mip
			const float AreaRatio = 2.f * SphereBounds.W * RcpWorldSize;
			const int32 CurFirstLODIdx = PrimitiveSceneInfo->Proxy->GetCurrentFirstLODIdx_RenderThread();
			const int32 MinLODIdx = FMath::Max((int32)LodInfo.MinLod, CurFirstLODIdx);
			const int32 LodBias = (int32)LodInfo.LodBias - FPrimitiveVirtualTextureLodInfo::LodBiasOffset;
			const int32 LodIndex = FMath::Clamp<int32>(LodBias - FMath::FloorToInt(FMath::Log2(AreaRatio)), MinLODIdx, LodInfo.MaxLod);

			// Process meshes
			for (int32 MeshIndex = 0; MeshIndex < PrimitiveSceneInfo->StaticMeshes.Num(); ++MeshIndex)
			{
				FStaticMeshBatchRelevance const& StaticMeshRelevance = PrimitiveSceneInfo->StaticMeshRelevances[MeshIndex];
				if (StaticMeshRelevance.bRenderToVirtualTexture && StaticMeshRelevance.LODIndex == LodIndex && StaticMeshRelevance.RuntimeVirtualTextureMaterialType == (uint32)MaterialType)
				{
					bool bCachedDraw = false;
					if (StaticMeshRelevance.bSupportsCachingMeshDrawCommands)
					{
						// Use cached draw command
						const int32 StaticMeshCommandInfoIndex = StaticMeshRelevance.GetStaticMeshCommandInfoIndex(EMeshPass::VirtualTexture);
						if (StaticMeshCommandInfoIndex >= 0)
						{
							FCachedMeshDrawCommandInfo& CachedMeshDrawCommand = PrimitiveSceneInfo->StaticMeshCommandInfos[StaticMeshCommandInfoIndex];

							const FMeshDrawCommand* MeshDrawCommand = CachedMeshDrawCommand.StateBucketId >= 0
								? &Scene->CachedMeshDrawCommandStateBuckets[FSetElementId::FromInteger(CachedMeshDrawCommand.StateBucketId)].MeshDrawCommand
								: &SceneDrawList.MeshDrawCommands[CachedMeshDrawCommand.CommandIndex];

							FVisibleMeshDrawCommand NewVisibleMeshDrawCommand;
							NewVisibleMeshDrawCommand.Setup(
								MeshDrawCommand,
								PrimitiveIndex,
								PrimitiveIndex,
								CachedMeshDrawCommand.StateBucketId,
								CachedMeshDrawCommand.MeshFillMode,
								CachedMeshDrawCommand.MeshCullMode,
								CachedMeshDrawCommand.SortKey);

							CachedDrawCommands.Add(NewVisibleMeshDrawCommand);
							bCachedDraw = true;
						}
					}

					if (!bCachedDraw)
					{
						// No cached draw command was available. Process the mesh batch.
						uint64 BatchElementMask = ~0ull;
						MeshProcessor.AddMeshBatch(PrimitiveSceneInfo->StaticMeshes[MeshIndex], BatchElementMask, Scene->PrimitiveSceneProxies[PrimitiveIndex]);
					}
				}
			}
		}

		// Combine cached and uncached draw command lists
		const int32 NumCachedCommands = CachedDrawCommands.Num();
		if (NumCachedCommands > 0)
		{
			const int32 CurrentCount = AllocatedCommands.Num();
			AllocatedCommands.AddUninitialized(NumCachedCommands);
			FMemory::Memcpy(&AllocatedCommands[CurrentCount], &CachedDrawCommands[0], NumCachedCommands * sizeof(CachedDrawCommands[0]));
		}

		// Sort and submit
		if (AllocatedCommands.Num() > 0)
		{
			FRHIVertexBuffer* PrimitiveIdsBuffer;
			const bool bDynamicInstancing = IsDynamicInstancingEnabled(View->FeatureLevel);
			const uint32 InstanceFactor = 1;

			SortAndMergeDynamicPassMeshDrawCommands(View->FeatureLevel, AllocatedCommands, MeshDrawCommandStorage, PrimitiveIdsBuffer, InstanceFactor);
			SubmitMeshDrawCommands(AllocatedCommands, GraphicsMinimalPipelineStateSet, PrimitiveIdsBuffer, 0, bDynamicInstancing, InstanceFactor, RHICmdList);
		}
	}


	/** BC Compression compute shader */
	class FShader_VirtualTextureCompress : public FGlobalShader
	{
	public:
		BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
			SHADER_PARAMETER(FIntVector4, DestRect)
			SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, RenderTexture0)
			SHADER_PARAMETER_SAMPLER(SamplerState, TextureSampler0)
			SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, RenderTexture1)
			SHADER_PARAMETER_SAMPLER(SamplerState, TextureSampler1)
			SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, RenderTexture2)
			SHADER_PARAMETER_SAMPLER(SamplerState, TextureSampler2)
			SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture<uint2>, OutCompressTexture0_u2)
			SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture<uint4>, OutCompressTexture0_u4)
			SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture<uint4>, OutCompressTexture1)
			SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture<uint2>, OutCompressTexture2)
		END_SHADER_PARAMETER_STRUCT()

		static bool ShouldCompilePermutation(FGlobalShaderPermutationParameters const& Parameters)
		{
			return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
		}

		FShader_VirtualTextureCompress()
		{}

		FShader_VirtualTextureCompress(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
			: FGlobalShader(Initializer)
		{
			Bindings.BindForLegacyShaderParameters(this, Initializer.ParameterMap, *FParameters::FTypeInfo::GetStructMetadata());
		}
	};

	template< ERuntimeVirtualTextureMaterialType MaterialType >
	class FShader_VirtualTextureCompress_CS : public FShader_VirtualTextureCompress
	{
	public:
		typedef FShader_VirtualTextureCompress_CS< MaterialType > ClassName; // typedef is only so that we can use in DECLARE_SHADER_TYPE macro
		DECLARE_SHADER_TYPE( ClassName, Global );

		FShader_VirtualTextureCompress_CS()
		{}

		FShader_VirtualTextureCompress_CS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
			: FShader_VirtualTextureCompress(Initializer)
		{}
	};

	IMPLEMENT_SHADER_TYPE(template<>, FShader_VirtualTextureCompress_CS< ERuntimeVirtualTextureMaterialType::BaseColor >, TEXT("/Engine/Private/VirtualTextureCompress.usf"), TEXT("CompressBaseColorCS"), SF_Compute);
	IMPLEMENT_SHADER_TYPE(template<>, FShader_VirtualTextureCompress_CS< ERuntimeVirtualTextureMaterialType::BaseColor_Normal_Specular >, TEXT("/Engine/Private/VirtualTextureCompress.usf"), TEXT("CompressBaseColorNormalSpecularCS"), SF_Compute);
	IMPLEMENT_SHADER_TYPE(template<>, FShader_VirtualTextureCompress_CS< ERuntimeVirtualTextureMaterialType::BaseColor_Normal_Specular_YCoCg >, TEXT("/Engine/Private/VirtualTextureCompress.usf"), TEXT("CompressBaseColorNormalSpecularYCoCgCS"), SF_Compute);


	/** Add the BC compression pass to the graph. */
	template< ERuntimeVirtualTextureMaterialType MaterialType >
	void AddCompressPass(FRDGBuilder& GraphBuilder, ERHIFeatureLevel::Type FeatureLevel, FShader_VirtualTextureCompress::FParameters* Parameters, FIntVector GroupCount)
	{
		TShaderMap<FGlobalShaderType>* GlobalShaderMap = GetGlobalShaderMap(FeatureLevel);
		TShaderMapRef< FShader_VirtualTextureCompress_CS< MaterialType > > ComputeShader(GlobalShaderMap);

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("VirtualTextureCompress"),
			*ComputeShader, Parameters, GroupCount);
	}

	/** Set up the BC compression pass for the given MaterialType. */
	void AddCompressPass(FRDGBuilder& GraphBuilder, ERHIFeatureLevel::Type FeatureLevel, FShader_VirtualTextureCompress::FParameters* Parameters, FIntPoint TextureSize, ERuntimeVirtualTextureMaterialType MaterialType)
	{
		const FIntVector GroupCount(((TextureSize.X / 4) + 7) / 8, ((TextureSize.Y / 4) + 7) / 8, 1);

		// Dispatch using the shader variation for our MaterialType
		switch (MaterialType)
		{
		case ERuntimeVirtualTextureMaterialType::BaseColor:
			AddCompressPass<ERuntimeVirtualTextureMaterialType::BaseColor>(GraphBuilder, FeatureLevel, Parameters, GroupCount);
			break;
		case ERuntimeVirtualTextureMaterialType::BaseColor_Normal_Specular:
			AddCompressPass<ERuntimeVirtualTextureMaterialType::BaseColor_Normal_Specular>(GraphBuilder, FeatureLevel, Parameters, GroupCount);
			break;
		case ERuntimeVirtualTextureMaterialType::BaseColor_Normal_Specular_YCoCg:
			AddCompressPass<ERuntimeVirtualTextureMaterialType::BaseColor_Normal_Specular_YCoCg>(GraphBuilder, FeatureLevel, Parameters, GroupCount);
			break;
		}
	}


	/** Copy shaders are used when compression is disabled. These are used to ensure that the channel layout is the same as with compression. */
	class FShader_VirtualTextureCopy : public FGlobalShader
	{
	public:
		BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
			RENDER_TARGET_BINDING_SLOTS()
			SHADER_PARAMETER(FIntVector4, DestRect)
			SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, RenderTexture0)
			SHADER_PARAMETER_SAMPLER(SamplerState, TextureSampler0)
			SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, RenderTexture1)
			SHADER_PARAMETER_SAMPLER(SamplerState, TextureSampler1)
			SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, RenderTexture2)
			SHADER_PARAMETER_SAMPLER(SamplerState, TextureSampler2)
		END_SHADER_PARAMETER_STRUCT()

		static bool ShouldCompilePermutation(FGlobalShaderPermutationParameters const& Parameters)
		{
			return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
		}

		FShader_VirtualTextureCopy()
		{}

		FShader_VirtualTextureCopy(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
			: FGlobalShader(Initializer)
		{
			Bindings.BindForLegacyShaderParameters(this, Initializer.ParameterMap, *FParameters::FTypeInfo::GetStructMetadata());
		}
	};

	class FShader_VirtualTextureCopy_VS : public FShader_VirtualTextureCopy
	{
	public:
		DECLARE_SHADER_TYPE(FShader_VirtualTextureCopy_VS, Global);

		FShader_VirtualTextureCopy_VS()
		{}

		FShader_VirtualTextureCopy_VS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
			: FShader_VirtualTextureCopy(Initializer)
		{}
	};

	IMPLEMENT_SHADER_TYPE(, FShader_VirtualTextureCopy_VS, TEXT("/Engine/Private/VirtualTextureCompress.usf"), TEXT("CopyVS"), SF_Vertex);

	template< ERuntimeVirtualTextureMaterialType MaterialType >
	class FShader_VirtualTextureCopy_PS : public FShader_VirtualTextureCopy
	{
	public:
		typedef FShader_VirtualTextureCopy_PS< MaterialType > ClassName; // typedef is only so that we can use in DECLARE_SHADER_TYPE macro
		DECLARE_SHADER_TYPE(ClassName, Global);

		FShader_VirtualTextureCopy_PS()
		{}

		FShader_VirtualTextureCopy_PS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
			: FShader_VirtualTextureCopy(Initializer)
		{}
	};

	IMPLEMENT_SHADER_TYPE(template<>, FShader_VirtualTextureCopy_PS< ERuntimeVirtualTextureMaterialType::BaseColor >, TEXT("/Engine/Private/VirtualTextureCompress.usf"), TEXT("CopyBaseColorNormalSpecularPS"), SF_Pixel);
	IMPLEMENT_SHADER_TYPE(template<>, FShader_VirtualTextureCopy_PS< ERuntimeVirtualTextureMaterialType::BaseColor_Normal_Specular >, TEXT("/Engine/Private/VirtualTextureCompress.usf"), TEXT("CopyBaseColorNormalSpecularPS"), SF_Pixel);
	IMPLEMENT_SHADER_TYPE(template<>, FShader_VirtualTextureCopy_PS< ERuntimeVirtualTextureMaterialType::BaseColor_Normal_Specular_YCoCg >, TEXT("/Engine/Private/VirtualTextureCompress.usf"), TEXT("CopyBaseColorNormalSpecularYCoCgPS"), SF_Pixel);
	IMPLEMENT_SHADER_TYPE(template<>, FShader_VirtualTextureCopy_PS< ERuntimeVirtualTextureMaterialType::WorldHeight >, TEXT("/Engine/Private/VirtualTextureCompress.usf"), TEXT("CopyWorldHeightPS"), SF_Pixel);


	/** Add the copy pass to the graph. */
	template< ERuntimeVirtualTextureMaterialType MaterialType >
	void AddCopyPass(FRDGBuilder& GraphBuilder, ERHIFeatureLevel::Type FeatureLevel, FShader_VirtualTextureCopy::FParameters* Parameters, FIntPoint TextureSize)
	{
		TShaderMap<FGlobalShaderType>* GlobalShaderMap = GetGlobalShaderMap(FeatureLevel);
		TShaderMapRef< FShader_VirtualTextureCopy_VS > VertexShader(GlobalShaderMap);
		TShaderMapRef< FShader_VirtualTextureCopy_PS< MaterialType > > PixelShader(GlobalShaderMap);

		GraphBuilder.AddPass(
			RDG_EVENT_NAME("VirtualTextureCopy"),
			Parameters,
			ERDGPassFlags::Raster,
			[VertexShader, PixelShader, Parameters, TextureSize](FRHICommandListImmediate& RHICmdList)
		{
			FGraphicsPipelineStateInitializer GraphicsPSOInit;
			RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
			GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
			GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGBA, BO_Add, BF_One, BF_Zero, BO_Add, BF_One, BF_Zero>::GetRHI();
			GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
			GraphicsPSOInit.PrimitiveType = PT_TriangleList;
			GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GetVertexDeclarationFVector4();
			GraphicsPSOInit.BoundShaderState.VertexShaderRHI = GETSAFERHISHADER_VERTEX(*VertexShader);
			GraphicsPSOInit.BoundShaderState.PixelShaderRHI = GETSAFERHISHADER_PIXEL(*PixelShader);
			SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

			SetShaderParameters(RHICmdList, *VertexShader, VertexShader->GetVertexShader(), *Parameters);
			SetShaderParameters(RHICmdList, *PixelShader, PixelShader->GetPixelShader(), *Parameters);

			RHICmdList.SetViewport(0.0f, 0.0f, 0.0f, TextureSize[0], TextureSize[1], 1.0f);
			RHICmdList.DrawIndexedPrimitive(GTwoTrianglesIndexBuffer.IndexBufferRHI, 0, 0, 4, 0, 2, 1);
		});
	}

	/** Set up the copy pass for the given MaterialType. */
	void AddCopyPass(FRDGBuilder& GraphBuilder, ERHIFeatureLevel::Type FeatureLevel, FShader_VirtualTextureCopy::FParameters* Parameters, FIntPoint TextureSize, ERuntimeVirtualTextureMaterialType MaterialType)
	{
		switch (MaterialType)
		{
		case ERuntimeVirtualTextureMaterialType::BaseColor_Normal_Specular:
			AddCopyPass<ERuntimeVirtualTextureMaterialType::BaseColor_Normal_Specular>(GraphBuilder, FeatureLevel, Parameters, TextureSize);
			break;
		case ERuntimeVirtualTextureMaterialType::BaseColor_Normal_Specular_YCoCg:
			AddCopyPass<ERuntimeVirtualTextureMaterialType::BaseColor_Normal_Specular_YCoCg>(GraphBuilder, FeatureLevel, Parameters, TextureSize);
			break;
		}
	}

	/** Set up the copy pass for the given MaterialType. */
	void AddCopyThumbnailPass(FRDGBuilder& GraphBuilder, ERHIFeatureLevel::Type FeatureLevel, FShader_VirtualTextureCopy::FParameters* Parameters, FIntPoint TextureSize, ERuntimeVirtualTextureMaterialType MaterialType)
	{
		switch (MaterialType)
		{
		case ERuntimeVirtualTextureMaterialType::BaseColor:
		case ERuntimeVirtualTextureMaterialType::BaseColor_Normal_Specular:
		case ERuntimeVirtualTextureMaterialType::BaseColor_Normal_Specular_YCoCg:
			AddCopyPass<ERuntimeVirtualTextureMaterialType::BaseColor>(GraphBuilder, FeatureLevel, Parameters, TextureSize);
			break;
		case ERuntimeVirtualTextureMaterialType::WorldHeight:
			AddCopyPass<ERuntimeVirtualTextureMaterialType::WorldHeight>(GraphBuilder, FeatureLevel, Parameters, TextureSize);
			break;
		}
	}


	/** Structure to localize the setup of our render graph based on the virtual texture setup. */
	struct FRenderGraphSetup
	{
		FRenderGraphSetup(FRDGBuilder& GraphBuilder, ERuntimeVirtualTextureMaterialType MaterialType, FRHITexture2D* OutputTexture0, FIntPoint TextureSize, bool bIsThumbnails)
		{
			bRenderPass = OutputTexture0 != nullptr;
			bCopyThumbnailPass = bRenderPass && bIsThumbnails;
			bCompressPass = bRenderPass && !bCopyThumbnailPass && (OutputTexture0->GetFormat() == PF_DXT1 || OutputTexture0->GetFormat() == PF_DXT5 || OutputTexture0->GetFormat() == PF_BC5);
			bCopyPass = bRenderPass && !bCopyThumbnailPass && !bCompressPass && (MaterialType == ERuntimeVirtualTextureMaterialType::BaseColor_Normal_Specular || MaterialType == ERuntimeVirtualTextureMaterialType::BaseColor_Normal_Specular_YCoCg);
		
			switch (MaterialType)
			{
			case ERuntimeVirtualTextureMaterialType::BaseColor:
				if (bRenderPass)
				{
					OutputAlias0 = RenderTexture0 = GraphBuilder.CreateTexture(FPooledRenderTargetDesc::Create2DDesc(TextureSize, PF_B8G8R8A8, FClearValueBinding::Black, TexCreate_SRGB, TexCreate_RenderTargetable | TexCreate_ShaderResource, false), TEXT("RenderTexture0"));
				}
				if (bCompressPass)
				{
					OutputAlias0 = CompressTexture0_u2 = GraphBuilder.CreateTexture(FPooledRenderTargetDesc::Create2DDesc(TextureSize / 4, PF_R32G32_UINT, FClearValueBinding::None, TexCreate_None, TexCreate_UAV, false), TEXT("CompressTexture0"));
				}
				if (bCopyThumbnailPass)
				{
					OutputAlias0 = CopyTexture0 = GraphBuilder.CreateTexture(FPooledRenderTargetDesc::Create2DDesc(TextureSize, PF_B8G8R8A8, FClearValueBinding::Black, TexCreate_SRGB, TexCreate_RenderTargetable | TexCreate_ShaderResource, false), TEXT("CopyTexture0"));
				}
				break;
			case ERuntimeVirtualTextureMaterialType::BaseColor_Normal_Specular:
				if (bRenderPass)
				{
					OutputAlias0 = RenderTexture0 = GraphBuilder.CreateTexture(FPooledRenderTargetDesc::Create2DDesc(TextureSize, PF_B8G8R8A8, FClearValueBinding::Black, TexCreate_SRGB, TexCreate_RenderTargetable | TexCreate_ShaderResource, false), TEXT("RenderTexture0"));
					RenderTexture1 = GraphBuilder.CreateTexture(FPooledRenderTargetDesc::Create2DDesc(TextureSize, PF_B8G8R8A8, FClearValueBinding::DefaultNormal8Bit, TexCreate_None, TexCreate_RenderTargetable | TexCreate_ShaderResource, false), TEXT("RenderTexture1"));
					RenderTexture2 = GraphBuilder.CreateTexture(FPooledRenderTargetDesc::Create2DDesc(TextureSize, PF_B8G8R8A8, FClearValueBinding::DefaultNormal8Bit, TexCreate_None, TexCreate_RenderTargetable | TexCreate_ShaderResource, false), TEXT("RenderTexture2"));
				}
				if (bCompressPass)
				{
					OutputAlias0 = CompressTexture0_u4 = GraphBuilder.CreateTexture(FPooledRenderTargetDesc::Create2DDesc(TextureSize / 4, PF_R32G32B32A32_UINT, FClearValueBinding::None, TexCreate_None, TexCreate_UAV, false), TEXT("CompressTexture0"));
					OutputAlias1 = CompressTexture1 = GraphBuilder.CreateTexture(FPooledRenderTargetDesc::Create2DDesc(TextureSize / 4, PF_R32G32B32A32_UINT, FClearValueBinding::None, TexCreate_None, TexCreate_UAV, false), TEXT("CompressTexture1"));
				}
				if (bCopyPass)
				{
					OutputAlias0 = CopyTexture0 = GraphBuilder.CreateTexture(FPooledRenderTargetDesc::Create2DDesc(TextureSize, PF_B8G8R8A8, FClearValueBinding::Black, TexCreate_SRGB, TexCreate_RenderTargetable | TexCreate_ShaderResource, false), TEXT("CopyTexture0"));
					OutputAlias1 = CopyTexture1 = GraphBuilder.CreateTexture(FPooledRenderTargetDesc::Create2DDesc(TextureSize, PF_B8G8R8A8, FClearValueBinding::Black, TexCreate_None, TexCreate_RenderTargetable | TexCreate_ShaderResource, false), TEXT("CopyTexture1"));
				}
				if (bCopyThumbnailPass)
				{
					OutputAlias0 = CopyTexture0 = GraphBuilder.CreateTexture(FPooledRenderTargetDesc::Create2DDesc(TextureSize, PF_B8G8R8A8, FClearValueBinding::Black, TexCreate_SRGB, TexCreate_RenderTargetable | TexCreate_ShaderResource, false), TEXT("CopyTexture0"));
				}
				break;
			case ERuntimeVirtualTextureMaterialType::BaseColor_Normal_Specular_YCoCg:
				if (bRenderPass)
				{
					OutputAlias0 = RenderTexture0 = GraphBuilder.CreateTexture(FPooledRenderTargetDesc::Create2DDesc(TextureSize, PF_B8G8R8A8, FClearValueBinding::Black, TexCreate_SRGB, TexCreate_RenderTargetable | TexCreate_ShaderResource, false), TEXT("RenderTexture0"));
					RenderTexture1 = GraphBuilder.CreateTexture(FPooledRenderTargetDesc::Create2DDesc(TextureSize, PF_B8G8R8A8, FClearValueBinding::DefaultNormal8Bit, TexCreate_None, TexCreate_RenderTargetable | TexCreate_ShaderResource, false), TEXT("RenderTexture1"));
					RenderTexture2 = GraphBuilder.CreateTexture(FPooledRenderTargetDesc::Create2DDesc(TextureSize, PF_B8G8R8A8, FClearValueBinding::DefaultNormal8Bit, TexCreate_None, TexCreate_RenderTargetable | TexCreate_ShaderResource, false), TEXT("RenderTexture2"));
				}
				if (bCompressPass)
				{
					OutputAlias0 = CompressTexture0_u4 = GraphBuilder.CreateTexture(FPooledRenderTargetDesc::Create2DDesc(TextureSize / 4, PF_R32G32B32A32_UINT, FClearValueBinding::None, TexCreate_None, TexCreate_UAV, false), TEXT("CompressTexture0"));
					OutputAlias1 = CompressTexture1 = GraphBuilder.CreateTexture(FPooledRenderTargetDesc::Create2DDesc(TextureSize / 4, PF_R32G32B32A32_UINT, FClearValueBinding::None, TexCreate_None, TexCreate_UAV, false), TEXT("CompressTexture1"));
					OutputAlias2 = CompressTexture2 = GraphBuilder.CreateTexture(FPooledRenderTargetDesc::Create2DDesc(TextureSize / 4, PF_R32G32_UINT, FClearValueBinding::None, TexCreate_None, TexCreate_UAV, false), TEXT("CompressTexture2"));
				}
				if (bCopyPass)
				{
					OutputAlias0 = CopyTexture0 = GraphBuilder.CreateTexture(FPooledRenderTargetDesc::Create2DDesc(TextureSize, PF_B8G8R8A8, FClearValueBinding::Black, TexCreate_None, TexCreate_RenderTargetable | TexCreate_ShaderResource, false), TEXT("CopyTexture0"));
					OutputAlias1 = CopyTexture1 = GraphBuilder.CreateTexture(FPooledRenderTargetDesc::Create2DDesc(TextureSize, PF_B8G8R8A8, FClearValueBinding::Black, TexCreate_None, TexCreate_RenderTargetable | TexCreate_ShaderResource, false), TEXT("CopyTexture1"));
					OutputAlias2 = CopyTexture2 = GraphBuilder.CreateTexture(FPooledRenderTargetDesc::Create2DDesc(TextureSize, PF_B8G8R8A8, FClearValueBinding::Black, TexCreate_None, TexCreate_RenderTargetable | TexCreate_ShaderResource, false), TEXT("CopyTexture2"));
				}
				if (bCopyThumbnailPass)
				{
					OutputAlias0 = CopyTexture0 = GraphBuilder.CreateTexture(FPooledRenderTargetDesc::Create2DDesc(TextureSize, PF_B8G8R8A8, FClearValueBinding::Black, TexCreate_SRGB, TexCreate_RenderTargetable | TexCreate_ShaderResource, false), TEXT("CopyTexture0"));
				}
				break;
			case ERuntimeVirtualTextureMaterialType::WorldHeight:
				if (bRenderPass)
				{
					OutputAlias0 = RenderTexture0 = GraphBuilder.CreateTexture(FPooledRenderTargetDesc::Create2DDesc(TextureSize, PF_G16, FClearValueBinding::Black, TexCreate_None, TexCreate_RenderTargetable | TexCreate_ShaderResource, false), TEXT("RenderTexture0"));
				}
				if (bCopyThumbnailPass)
				{
					OutputAlias0 = CopyTexture0 = GraphBuilder.CreateTexture(FPooledRenderTargetDesc::Create2DDesc(TextureSize, PF_B8G8R8A8, FClearValueBinding::Black, TexCreate_None, TexCreate_RenderTargetable | TexCreate_ShaderResource, false), TEXT("CopyTexture0"));
				}
				break;
			}
		}

		/** Flags to express what passes we need for this virtual texture layout. */
		bool bRenderPass = false;
		bool bCompressPass = false;
		bool bCopyPass = false;
		bool bCopyThumbnailPass = false;

		/** Render graph textures needed for this virtual texture layout. */
		FRDGTextureRef RenderTexture0 = nullptr;
		FRDGTextureRef RenderTexture1 = nullptr;
		FRDGTextureRef RenderTexture2 = nullptr;
		FRDGTextureRef CompressTexture0_u2 = nullptr;
		FRDGTextureRef CompressTexture0_u4 = nullptr;
		FRDGTextureRef CompressTexture1 = nullptr;
		FRDGTextureRef CompressTexture2 = nullptr;
		FRDGTextureRef CopyTexture0 = nullptr;
		FRDGTextureRef CopyTexture1 = nullptr;
		FRDGTextureRef CopyTexture2 = nullptr;

		/** Aliases to one of the render/compress/copy textures. This is what we will Copy into the final physical texture. */
		//todo[vt]: On platforms that support direct aliasing we can not set these and compress direct to the final destination
		FRDGTextureRef OutputAlias0 = nullptr;
		FRDGTextureRef OutputAlias1 = nullptr;
		FRDGTextureRef OutputAlias2 = nullptr;
	};


	bool IsSceneReadyToRender(FScene* Scene)
	{
		// Test scene is loaded and has been updated once by main rendering passes
		// This function gets called on the main thread, so accessing scene frame number is not strictly thread safe, but we can probably
		// assume that frame number is always increasing, and so the test is conservative
		return Scene != nullptr && Scene->GetRenderScene() != nullptr && Scene->GetRenderScene()->GetFrameNumber() > 1;
	}

	void RenderPage(
		FRHICommandListImmediate& RHICmdList,
		FScene* Scene,
		uint32 RuntimeVirtualTextureMask,
		ERuntimeVirtualTextureMaterialType MaterialType,
		bool bClearTextures,
		bool bIsThumbnails,
		FRHITexture2D* OutputTexture0,
		FRHIUnorderedAccessView* OutputUAV0,
		FBox2D const& DestBox0,
		FRHITexture2D* OutputTexture1,
		FRHIUnorderedAccessView* OutputUAV1,
		FBox2D const& DestBox1,
		FRHITexture2D* OutputTexture2, 
		FRHIUnorderedAccessView* OutputUAV2,
		FBox2D const& DestBox2,
		FTransform const& UVToWorld,
		FBox2D const& UVRange,
		uint8 vLevel,
		uint8 MaxLevel,
		ERuntimeVirtualTextureDebugType DebugType)
	{
		SCOPED_DRAW_EVENT(RHICmdList, VirtualTextureDynamicCache);

		// Initialize a temporary view required for the material render pass
		//todo[vt]: Some of this, such as ViewRotationMatrix, can be computed once in the Finalizer and passed down.
		//todo[vt]: Have specific shader variations and setup for different output texture configs
		FSceneViewFamily::ConstructionValues ViewFamilyInit(nullptr, nullptr, FEngineShowFlags(ESFIM_Game));
		ViewFamilyInit.SetWorldTimes(0.0f, 0.0f, 0.0f);
		FSceneViewFamilyContext ViewFamily(ViewFamilyInit);

		FSceneViewInitOptions ViewInitOptions;
		ViewInitOptions.ViewFamily = &ViewFamily;

		const FIntPoint TextureSize = (DestBox0.Max - DestBox0.Min).IntPoint();
		ViewInitOptions.SetViewRectangle(FIntRect(FIntPoint(0, 0), TextureSize));

		const FVector UVCenter = FVector(UVRange.GetCenter(), 0.f);
		const FVector CameraLookAt = UVToWorld.TransformPosition(UVCenter);
		const float BoundBoxHalfZ = UVToWorld.GetScale3D().Z;
		const FVector CameraPos = CameraLookAt + BoundBoxHalfZ * UVToWorld.GetUnitAxis(EAxis::Z);
		ViewInitOptions.ViewOrigin = CameraPos;

		const float OrthoWidth = UVToWorld.GetScaledAxis(EAxis::X).Size() * UVRange.GetExtent().X;
		const float OrthoHeight = UVToWorld.GetScaledAxis(EAxis::Y).Size() * UVRange.GetExtent().Y;

		const FTransform WorldToUVRotate(UVToWorld.GetRotation().Inverse());
		ViewInitOptions.ViewRotationMatrix = WorldToUVRotate.ToMatrixNoScale() * FMatrix(
			FPlane(1, 0, 0, 0),
			FPlane(0, -1, 0, 0),
			FPlane(0, 0, -1, 0),
			FPlane(0, 0, 0, 1));

		const float NearPlane = 0;
		const float FarPlane = BoundBoxHalfZ * 2.f;
		const float ZScale = 1.0f / (FarPlane - NearPlane);
		const float ZOffset = -NearPlane;
		ViewInitOptions.ProjectionMatrix = FReversedZOrthoMatrix(OrthoWidth, OrthoHeight, ZScale, ZOffset);

		ViewInitOptions.BackgroundColor = FLinearColor::Black;
		ViewInitOptions.OverlayColor = FLinearColor::White;

		FViewInfo* View = new FViewInfo(ViewInitOptions);
		ViewFamily.Views.Add(View);

		FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);
		View->ViewRect = View->UnconstrainedViewRect;
		View->CachedViewUniformShaderParameters = MakeUnique<FViewUniformShaderParameters>();
		View->SetupUniformBufferParameters(SceneContext, nullptr, 0, *View->CachedViewUniformShaderParameters);
		View->CachedViewUniformShaderParameters->WorldToVirtualTexture = WorldToUVRotate.ToMatrixNoScale();
		View->CachedViewUniformShaderParameters->VirtualTextureParams = FVector4((float)vLevel, DebugType == ERuntimeVirtualTextureDebugType::Debug ? 1.f : 0.f, OrthoWidth / (float)TextureSize.X, OrthoHeight / (float)TextureSize.Y);
		View->ViewUniformBuffer = TUniformBufferRef<FViewUniformShaderParameters>::CreateUniformBufferImmediate(*View->CachedViewUniformShaderParameters, UniformBuffer_SingleFrame);
		UploadDynamicPrimitiveShaderDataForView(RHICmdList, *(const_cast<FScene*>(Scene)), *View);
		Scene->UniformBuffers.VirtualTextureViewUniformBuffer.UpdateUniformBufferImmediate(*View->CachedViewUniformShaderParameters);

		// Build graph
		FMemMark Mark(FMemStack::Get());
		FRDGBuilder GraphBuilder(RHICmdList);
		FRenderGraphSetup GraphSetup(GraphBuilder, MaterialType, OutputTexture0, TextureSize, bIsThumbnails);

		// Draw Pass
		if (GraphSetup.bRenderPass)
		{
			ERenderTargetLoadAction LoadAction = bClearTextures ? ERenderTargetLoadAction::EClear : ERenderTargetLoadAction::ENoAction;
			FShader_VirtualTextureMaterialDraw::FParameters* PassParameters = GraphBuilder.AllocParameters<FShader_VirtualTextureMaterialDraw::FParameters>();
			PassParameters->RenderTargets[0] = GraphSetup.RenderTexture0 ? FRenderTargetBinding(GraphSetup.RenderTexture0, LoadAction) : FRenderTargetBinding();
			PassParameters->RenderTargets[1] = GraphSetup.RenderTexture1 ? FRenderTargetBinding(GraphSetup.RenderTexture1, LoadAction) : FRenderTargetBinding();
			PassParameters->RenderTargets[2] = GraphSetup.RenderTexture2 ? FRenderTargetBinding(GraphSetup.RenderTexture2, LoadAction) : FRenderTargetBinding();

			GraphBuilder.AddPass(
				RDG_EVENT_NAME("VirtualTextureDraw"),
				PassParameters,
				ERDGPassFlags::Raster,
				[Scene, View, MaterialType, RuntimeVirtualTextureMask, vLevel, MaxLevel](FRHICommandListImmediate& RHICmdListImmediate)
			{
				DrawMeshes(RHICmdListImmediate, Scene, View, MaterialType, RuntimeVirtualTextureMask, vLevel, MaxLevel);
			});
		}

		// Compression Pass
		if (GraphSetup.bCompressPass)
		{
			FShader_VirtualTextureCompress::FParameters* PassParameters = GraphBuilder.AllocParameters<FShader_VirtualTextureCompress::FParameters>();
			PassParameters->DestRect = FIntVector4(0, 0, TextureSize.X, TextureSize.Y);
			PassParameters->RenderTexture0 = GraphSetup.RenderTexture0;
			PassParameters->TextureSampler0 = TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
			PassParameters->RenderTexture1 = GraphSetup.RenderTexture1;
			PassParameters->TextureSampler1 = TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
			PassParameters->RenderTexture2 = GraphSetup.RenderTexture2;
			PassParameters->TextureSampler2 = TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
			PassParameters->OutCompressTexture0_u2 = GraphSetup.CompressTexture0_u2 ? GraphBuilder.CreateUAV(FRDGTextureUAVDesc(GraphSetup.CompressTexture0_u2)) : nullptr;
			PassParameters->OutCompressTexture0_u4 = GraphSetup.CompressTexture0_u4 ? GraphBuilder.CreateUAV(FRDGTextureUAVDesc(GraphSetup.CompressTexture0_u4)) : nullptr;
			PassParameters->OutCompressTexture1 = GraphSetup.CompressTexture1 ? GraphBuilder.CreateUAV(FRDGTextureUAVDesc(GraphSetup.CompressTexture1)) : nullptr;
			PassParameters->OutCompressTexture2 = GraphSetup.CompressTexture2 ? GraphBuilder.CreateUAV(FRDGTextureUAVDesc(GraphSetup.CompressTexture2)) : nullptr;

			AddCompressPass(GraphBuilder, View->GetFeatureLevel(), PassParameters, TextureSize, MaterialType);
		}
		
		// Copy Pass
		if (GraphSetup.bCopyPass || GraphSetup.bCopyThumbnailPass)
		{
			FShader_VirtualTextureCopy::FParameters* PassParameters = GraphBuilder.AllocParameters<FShader_VirtualTextureCopy::FParameters>();
			PassParameters->RenderTargets[0] = GraphSetup.CopyTexture0 ? FRenderTargetBinding(GraphSetup.CopyTexture0, ERenderTargetLoadAction::ENoAction) : FRenderTargetBinding();
			PassParameters->RenderTargets[1] = GraphSetup.CopyTexture1 ? FRenderTargetBinding(GraphSetup.CopyTexture1, ERenderTargetLoadAction::ENoAction) : FRenderTargetBinding();
			PassParameters->RenderTargets[2] = GraphSetup.CopyTexture2 ? FRenderTargetBinding(GraphSetup.CopyTexture2, ERenderTargetLoadAction::ENoAction) : FRenderTargetBinding();
			PassParameters->DestRect = FIntVector4(0, 0, TextureSize.X, TextureSize.Y);
			PassParameters->RenderTexture0 = GraphSetup.RenderTexture0;
			PassParameters->TextureSampler0 = TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
			PassParameters->RenderTexture1 = GraphSetup.RenderTexture1;
			PassParameters->TextureSampler1 = TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
			PassParameters->RenderTexture2 = GraphSetup.RenderTexture2;
			PassParameters->TextureSampler2 = TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();

			if (GraphSetup.bCopyPass)
			{
				AddCopyPass(GraphBuilder, View->GetFeatureLevel(), PassParameters, TextureSize, MaterialType);
			}
			else
			{
				AddCopyThumbnailPass(GraphBuilder, View->GetFeatureLevel(), PassParameters, TextureSize, MaterialType);
			}
		}

		// Set up the output to capture
		TRefCountPtr<IPooledRenderTarget> GraphOutputTexture0;
		FIntVector GraphOutputSize0;
		if (GraphSetup.OutputAlias0 != nullptr)
		{
			GraphBuilder.QueueTextureExtraction(GraphSetup.OutputAlias0, &GraphOutputTexture0);
			GraphOutputSize0 = GraphSetup.OutputAlias0->Desc.GetSize();
		}

		TRefCountPtr<IPooledRenderTarget> GraphOutputTexture1;
		FIntVector GraphOutputSize1;
		if (GraphSetup.OutputAlias1 != nullptr)
		{
			GraphBuilder.QueueTextureExtraction(GraphSetup.OutputAlias1, &GraphOutputTexture1);
			GraphOutputSize1 = GraphSetup.OutputAlias1->Desc.GetSize();
		}

		TRefCountPtr<IPooledRenderTarget> GraphOutputTexture2;
		FIntVector GraphOutputSize2;
		if (GraphSetup.OutputAlias2 != nullptr)
		{
			GraphBuilder.QueueTextureExtraction(GraphSetup.OutputAlias2, &GraphOutputTexture2);
			GraphOutputSize2 = GraphSetup.OutputAlias2->Desc.GetSize();
		}

		// Execute the graph
		GraphBuilder.Execute();

		// Copy to final destination
		if (GraphSetup.OutputAlias0 != nullptr && OutputTexture0 != nullptr)
		{
			FRHICopyTextureInfo Info;
			Info.Size = GraphOutputSize0;
			Info.DestPosition = FIntVector(DestBox0.Min.X, DestBox0.Min.Y, 0);

			RHICmdList.CopyTexture(GraphOutputTexture0->GetRenderTargetItem().ShaderResourceTexture->GetTexture2D(), OutputTexture0->GetTexture2D(), Info);
		}

		if (GraphSetup.OutputAlias1 != nullptr && OutputTexture1 != nullptr)
		{
			FRHICopyTextureInfo Info;
			Info.Size = GraphOutputSize1;
			Info.DestPosition = FIntVector(DestBox1.Min.X, DestBox1.Min.Y, 0);

			RHICmdList.CopyTexture(GraphOutputTexture1->GetRenderTargetItem().ShaderResourceTexture->GetTexture2D(), OutputTexture1->GetTexture2D(), Info);
		}

		if (GraphSetup.OutputAlias2 != nullptr && OutputTexture2 != nullptr)
		{
			FRHICopyTextureInfo Info;
			Info.Size = GraphOutputSize2;
			Info.DestPosition = FIntVector(DestBox2.Min.X, DestBox2.Min.Y, 0);

			RHICmdList.CopyTexture(GraphOutputTexture2->GetRenderTargetItem().ShaderResourceTexture->GetTexture2D(), OutputTexture2->GetTexture2D(), Info);
		}

		View->CachedViewUniformShaderParameters.Reset();
	}

	void RenderPages(FRHICommandListImmediate& RHICmdList, FRenderPageBatchDesc const& InDesc)
	{
		SCOPED_DRAW_EVENT(RHICmdList, RuntimeVirtualTextureRenderPages);
		check(InDesc.NumPageDescs <= EMaxRenderPageBatch);

		// Make sure GPUScene is up to date. 
		// Usually this is a no-op since we updated before calling, but RuntimeVirtualTexture::BuildStreamedMips() needs this.
		UpdateGPUScene(RHICmdList, *InDesc.Scene->GetRenderScene());

		for (int32 PageIndex = 0; PageIndex < InDesc.NumPageDescs; ++PageIndex)
		{
			FRenderPageDesc const& PageDesc = InDesc.PageDescs[PageIndex];

			RenderPage(
				RHICmdList,
				InDesc.Scene,
				InDesc.RuntimeVirtualTextureMask,
				InDesc.MaterialType,
				InDesc.bClearTextures,
				InDesc.bIsThumbnails,
				InDesc.Targets[0].Texture, InDesc.Targets[0].UAV, PageDesc.DestBox[0],
				InDesc.Targets[1].Texture, InDesc.Targets[1].UAV, PageDesc.DestBox[1],
				InDesc.Targets[2].Texture, InDesc.Targets[2].UAV, PageDesc.DestBox[2],
				InDesc.UVToWorld,
				PageDesc.UVRange,
				PageDesc.vLevel,
				InDesc.MaxLevel,
				InDesc.DebugType);
		}
	}


	uint32 GetRuntimeVirtualTextureSceneIndex_GameThread(class URuntimeVirtualTextureComponent* InComponent)
	{
		int32 SceneIndex = 0;
		ENQUEUE_RENDER_COMMAND(GetSceneIndexCommand)(
			[&SceneIndex, InComponent](FRHICommandListImmediate& RHICmdList)
		{
			if (InComponent->GetScene() != nullptr)
			{
				FScene* Scene = InComponent->GetScene()->GetRenderScene();
				if (Scene != nullptr && InComponent->SceneProxy != nullptr)
				{
					SceneIndex = Scene->GetRuntimeVirtualTextureSceneIndex(InComponent->SceneProxy->ProducerId);
				}
			}
		});
		FlushRenderingCommands();
		return SceneIndex;
	}
}
