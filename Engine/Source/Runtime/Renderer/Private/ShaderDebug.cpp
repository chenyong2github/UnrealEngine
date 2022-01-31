// Copyright Epic Games, Inc. All Rights Reserved.

#include "ShaderDebug.h"
#include "SceneRendering.h"
#include "GlobalShader.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "Containers/ResourceArray.h"
#include "CommonRenderResources.h"
#include "ScenePrivate.h"

namespace ShaderDrawDebug 
{
	// Console variables
	static int32 GShaderDrawDebug_Enable = 1;
	static FAutoConsoleVariableRef CVarShaderDrawEnable(
		TEXT("r.ShaderDrawDebug"),
		GShaderDrawDebug_Enable,
		TEXT("ShaderDrawDebug debugging toggle.\n"),
		ECVF_Cheat | ECVF_RenderThreadSafe);

	static int32 GShaderDrawDebug_MaxElementCount = 1;
	static FAutoConsoleVariableRef CVarShaderDrawMaxElementCount(
		TEXT("r.ShaderDrawDebug.MaxElementCount"),
		GShaderDrawDebug_MaxElementCount,
		TEXT("ShaderDraw output buffer size in element.\n"),
		ECVF_Cheat | ECVF_RenderThreadSafe);

	static TAutoConsoleVariable<int32> CVarShaderDrawLock(
		TEXT("r.ShaderDrawDebug.Lock"),
		0,
		TEXT("Lock the shader draw buffer.\n"),
		ECVF_Cheat | ECVF_RenderThreadSafe);

	static FViewInfo* GDefaultView = nullptr;
	static uint32 GElementRequestCount = 0;


	bool IsEnabled()
	{
		return GShaderDrawDebug_Enable > 0;
	}

	static bool IsShaderDrawLocked()
	{
		return CVarShaderDrawLock.GetValueOnRenderThread() > 0;
	}

	bool IsSupported(const EShaderPlatform Platform)
	{
		return RHISupportsComputeShaders(Platform) && !IsHlslccShaderPlatform(Platform);
	}

	void SetEnabled(bool bInEnabled)
	{
		GShaderDrawDebug_Enable = bInEnabled ? 1 : 0;
	}

	void SetMaxElementCount(uint32 MaxCount)
	{
		GShaderDrawDebug_MaxElementCount = FMath::Max3(1024, GShaderDrawDebug_MaxElementCount, int32(MaxCount));
	}

	void RequestSpaceForElements(uint32 MaxElementCount)
	{
		GElementRequestCount += MaxElementCount;
	}

	bool IsEnabled(const FViewInfo& View)
	{
		return IsEnabled() && IsSupported(View.GetShaderPlatform());
	}

	//////////////////////////////////////////////////////////////////////////

	class FShaderDrawDebugClearCS : public FGlobalShader
	{
		DECLARE_GLOBAL_SHADER(FShaderDrawDebugClearCS);
		SHADER_USE_PARAMETER_STRUCT(FShaderDrawDebugClearCS, FGlobalShader);

		BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
			SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer, RWElementBuffer)
		END_SHADER_PARAMETER_STRUCT()

		static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
		{
			return IsSupported(Parameters.Platform);
		}

		static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
		{
			FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
			OutEnvironment.SetDefine(TEXT("GPU_DEBUG_RENDERING"), 1);
			OutEnvironment.SetDefine(TEXT("GPU_DEBUG_RENDERING_CLEAR_CS"), 1);
		}
	};

	IMPLEMENT_GLOBAL_SHADER(FShaderDrawDebugClearCS, "/Engine/Private/ShaderDrawDebug.usf", "ShaderDrawDebugClearCS", SF_Compute);
	
	//////////////////////////////////////////////////////////////////////////

	class FShaderDrawDebugCopyCS : public FGlobalShader
	{
		DECLARE_GLOBAL_SHADER(FShaderDrawDebugCopyCS);
		SHADER_USE_PARAMETER_STRUCT(FShaderDrawDebugCopyCS, FGlobalShader);

		BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
			SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer, ElementBuffer)
			SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer, RWIndirectArgs)
		END_SHADER_PARAMETER_STRUCT()

		static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
		{
			return IsSupported(Parameters.Platform);
		}

		static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
		{
			FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
			OutEnvironment.SetDefine(TEXT("GPU_DEBUG_RENDERING"), 1);
			OutEnvironment.SetDefine(TEXT("GPU_DEBUG_RENDERING_COPY_CS"), 1);
		}
	};

	IMPLEMENT_GLOBAL_SHADER(FShaderDrawDebugCopyCS, "/Engine/Private/ShaderDrawDebug.usf", "ShaderDrawDebugCopyCS", SF_Compute);

	//////////////////////////////////////////////////////////////////////////

	class FShaderDrawDebugVS : public FGlobalShader
	{
		DECLARE_GLOBAL_SHADER(FShaderDrawDebugVS);
		SHADER_USE_PARAMETER_STRUCT(FShaderDrawDebugVS, FGlobalShader);

		BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
			SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
			SHADER_PARAMETER_SRV(StructuredBuffer, LockedShaderDrawDebugPrimitive)
			SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer, ShaderDrawDebugPrimitive)
			RDG_BUFFER_ACCESS(IndirectBuffer, ERHIAccess::IndirectArgs)
		END_SHADER_PARAMETER_STRUCT()

		static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
		{
			return IsSupported(Parameters.Platform);
		}

		static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
		{
			FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
			OutEnvironment.SetDefine(TEXT("GPU_DEBUG_RENDERING"), 1);
			OutEnvironment.SetDefine(TEXT("GPU_DEBUG_RENDERING_VS"), 1);
			OutEnvironment.SetDefine(TEXT("GPU_DEBUG_RENDERING_PS"), 0);
		}
	};

	IMPLEMENT_GLOBAL_SHADER(FShaderDrawDebugVS, "/Engine/Private/ShaderDrawDebug.usf", "ShaderDrawDebugVS", SF_Vertex);

	//////////////////////////////////////////////////////////////////////////

	class FShaderDrawDebugPS : public FGlobalShader
	{
		DECLARE_GLOBAL_SHADER(FShaderDrawDebugPS);
		SHADER_USE_PARAMETER_STRUCT(FShaderDrawDebugPS, FGlobalShader);
		
		BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
			SHADER_PARAMETER_RDG_TEXTURE(Texture2D, DepthTexture)
			SHADER_PARAMETER(FIntPoint, DepthTextureResolution)
			SHADER_PARAMETER(FVector2f, DepthTextureInvResolution)
			SHADER_PARAMETER_SAMPLER(SamplerState, DepthSampler)
			RENDER_TARGET_BINDING_SLOTS()
		END_SHADER_PARAMETER_STRUCT()

		using FPermutationDomain = TShaderPermutationDomain<>;

		static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
		{
			return IsSupported(Parameters.Platform);
		}

		static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
		{
			FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
			OutEnvironment.SetDefine(TEXT("GPU_DEBUG_RENDERING"), 1);
			OutEnvironment.SetDefine(TEXT("GPU_DEBUG_RENDERING_VS"), 0);
			OutEnvironment.SetDefine(TEXT("GPU_DEBUG_RENDERING_PS"), 1);
		}
	};

	IMPLEMENT_GLOBAL_SHADER(FShaderDrawDebugPS, "/Engine/Private/ShaderDrawDebug.usf", "ShaderDrawDebugPS", SF_Pixel);

	BEGIN_SHADER_PARAMETER_STRUCT(FShaderDrawVSPSParameters , )
		SHADER_PARAMETER_STRUCT_INCLUDE(FShaderDrawDebugVS::FParameters, ShaderDrawVSParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(FShaderDrawDebugPS::FParameters, ShaderDrawPSParameters)
	END_SHADER_PARAMETER_STRUCT()

	//////////////////////////////////////////////////////////////////////////
		
	static void AddShaderDrawDebugClearPass(FRDGBuilder& GraphBuilder, const FViewInfo& View, FRDGBufferRef& DataBuffer)
	{
		FShaderDrawDebugClearCS::FParameters* Parameters = GraphBuilder.AllocParameters<FShaderDrawDebugClearCS::FParameters>();
		Parameters->RWElementBuffer = GraphBuilder.CreateUAV(DataBuffer);

		TShaderMapRef<FShaderDrawDebugClearCS> ComputeShader(View.ShaderMap);
		ClearUnusedGraphResources(ComputeShader, Parameters);
		GraphBuilder.AddPass(
			RDG_EVENT_NAME("ShaderDebug::Clear"),
			Parameters,
			ERDGPassFlags::Compute,
		[Parameters, ComputeShader](FRHICommandList& RHICmdList)
		{
			FComputeShaderUtils::Dispatch(RHICmdList, ComputeShader, *Parameters, FIntVector(1, 1, 1));
		});
	}

	static void AddShaderDrawDebugCopyPass(FRDGBuilder& GraphBuilder, const FViewInfo& View, FRDGBufferRef& DataBuffer, FRDGBufferRef& IndirectBuffer)
	{
		FShaderDrawDebugCopyCS::FParameters* Parameters = GraphBuilder.AllocParameters<FShaderDrawDebugCopyCS::FParameters>();
		Parameters->ElementBuffer = GraphBuilder.CreateSRV(DataBuffer);
		Parameters->RWIndirectArgs = GraphBuilder.CreateUAV(IndirectBuffer, PF_R32_UINT);

		TShaderMapRef<FShaderDrawDebugCopyCS> ComputeShader(View.ShaderMap);
		ClearUnusedGraphResources(ComputeShader, Parameters);
		GraphBuilder.AddPass(
			RDG_EVENT_NAME("ShaderDebug::CopyArgs"),
			Parameters,
			ERDGPassFlags::Compute,
		[Parameters, ComputeShader](FRHICommandList& RHICmdList)
		{
			FComputeShaderUtils::Dispatch(RHICmdList, ComputeShader, *Parameters, FIntVector(1, 1, 1));
		});
	}

	static void InternalDrawView(
		FRDGBuilder& GraphBuilder,
		const FViewInfo& View,
		FRDGBufferRef DataBuffer,
		FRDGTextureRef OutputTexture,
		FRDGTextureRef DepthTexture)
	{
		FRDGBufferRef IndirectBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc<FRHIDrawIndirectParameters>(1), TEXT("ShaderDraw.IndirectBuffer"), ERDGBufferFlags::None);
		AddShaderDrawDebugCopyPass(GraphBuilder, View, DataBuffer, IndirectBuffer);

		TShaderMapRef<FShaderDrawDebugVS> VertexShader(View.ShaderMap);
		TShaderMapRef<FShaderDrawDebugPS> PixelShader(View.ShaderMap);

		FShaderDrawVSPSParameters* PassParameters = GraphBuilder.AllocParameters<FShaderDrawVSPSParameters >();
		PassParameters->ShaderDrawPSParameters.RenderTargets[0] = FRenderTargetBinding(OutputTexture, ERenderTargetLoadAction::ELoad);
		//PassParameters->ShaderDrawPSParameters.RenderTargets.DepthStencil = FDepthStencilBinding(DepthTexture, ERenderTargetLoadAction::ELoad, FExclusiveDepthStencil::DepthRead_StencilNop);
		PassParameters->ShaderDrawPSParameters.DepthTexture = DepthTexture;
		PassParameters->ShaderDrawPSParameters.DepthTextureResolution = FIntPoint(DepthTexture->Desc.Extent.X, DepthTexture->Desc.Extent.Y);
		PassParameters->ShaderDrawPSParameters.DepthTextureInvResolution = FVector2D(1.f / DepthTexture->Desc.Extent.X, 1.f / DepthTexture->Desc.Extent.Y);
		PassParameters->ShaderDrawPSParameters.DepthSampler = TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
		PassParameters->ShaderDrawVSParameters.View = View.ViewUniformBuffer;
		PassParameters->ShaderDrawVSParameters.ShaderDrawDebugPrimitive = GraphBuilder.CreateSRV(DataBuffer);
		PassParameters->ShaderDrawVSParameters.IndirectBuffer = IndirectBuffer;

		ValidateShaderParameters(PixelShader, PassParameters->ShaderDrawPSParameters);
		ClearUnusedGraphResources(PixelShader, &PassParameters->ShaderDrawPSParameters, { IndirectBuffer });
		ValidateShaderParameters(VertexShader, PassParameters->ShaderDrawVSParameters);
		ClearUnusedGraphResources(VertexShader, &PassParameters->ShaderDrawVSParameters, { IndirectBuffer });

		const FIntRect Viewport = View.ViewRect;
		GraphBuilder.AddPass(
			RDG_EVENT_NAME("ShaderDebug::Draw"),
			PassParameters,
			ERDGPassFlags::Raster,
			[VertexShader, PixelShader, PassParameters, IndirectBuffer, Viewport](FRHICommandList& RHICmdList)
			{
				// Marks the indirect draw parameter as used by the pass, given it's not used directly by any of the shaders.
				PassParameters->ShaderDrawVSParameters.IndirectBuffer->MarkResourceAsUsed();

				FGraphicsPipelineStateInitializer GraphicsPSOInit;
				RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
				GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
				GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGBA, BO_Add, BF_One, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_One>::GetRHI(); // Premultiplied-alpha composition
				GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None, true>::GetRHI();
				GraphicsPSOInit.PrimitiveType = PT_LineList;
				GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GEmptyVertexDeclaration.VertexDeclarationRHI;
				GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
				GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
				SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit, 0);

				RHICmdList.SetViewport(Viewport.Min.X, Viewport.Min.Y, 0.0f, Viewport.Max.X, Viewport.Max.Y, 1.0f);
				SetShaderParameters(RHICmdList, VertexShader, VertexShader.GetVertexShader(), PassParameters->ShaderDrawVSParameters);
				SetShaderParameters(RHICmdList, PixelShader, PixelShader.GetPixelShader(), PassParameters->ShaderDrawPSParameters);

				// Marks the indirect draw parameter as used by the pass, given it's not used directly by any of the shaders.
				FRHIBuffer* IndirectBufferRHI = PassParameters->ShaderDrawVSParameters.IndirectBuffer->GetIndirectRHICallBuffer();
				check(IndirectBufferRHI != nullptr);
				RHICmdList.DrawPrimitiveIndirect(IndirectBufferRHI, 0);
			});
	}

	void BeginView(FRDGBuilder& GraphBuilder, FViewInfo& View)
	{
		View.ShaderDrawData = FShaderDrawDebugData();
		View.ShaderDrawData.ShaderDrawTranslatedWorldOffset = View.ViewMatrices.GetPreViewTranslation();
		View.ShaderDrawData.CursorPosition = View.CursorPos;
		View.ShaderDrawData.MaxElementCount = 0;

		if (IsEnabled(View))
		{
			// Update max element count from the request count and reset
			GShaderDrawDebug_MaxElementCount = FMath::Max3(1, GShaderDrawDebug_MaxElementCount, int32(GElementRequestCount));
			View.ShaderDrawData.MaxElementCount = GShaderDrawDebug_MaxElementCount;
			GElementRequestCount = 0;

			FSceneViewState* ViewState = View.ViewState;
			const bool bLockBufferThisFrame = IsShaderDrawLocked() && ViewState && !ViewState->ShaderDrawDebugStateData.bIsLocked;
			ERDGBufferFlags Flags = bLockBufferThisFrame ? ERDGBufferFlags::MultiFrame : ERDGBufferFlags::None;

			View.ShaderDrawData.Buffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(4, 8*View.ShaderDrawData.MaxElementCount), TEXT("ShaderDraw.DataBuffer"), Flags);
			AddShaderDrawDebugClearPass(GraphBuilder, View, View.ShaderDrawData.Buffer);

			if (IsShaderDrawLocked() && ViewState && !ViewState->ShaderDrawDebugStateData.bIsLocked)
			{
				ViewState->ShaderDrawDebugStateData.Buffer = GraphBuilder.ConvertToExternalBuffer(View.ShaderDrawData.Buffer);
				ViewState->ShaderDrawDebugStateData.bIsLocked = true;
			}

			if (!IsShaderDrawLocked() && ViewState && ViewState->ShaderDrawDebugStateData.bIsLocked)
			{
				ViewState->ShaderDrawDebugStateData.Buffer = nullptr;
				ViewState->ShaderDrawDebugStateData.bIsLocked = false;
			}

			// Invalid to call begin twice for the same view.
			check(GDefaultView != &View);
			if (GDefaultView == nullptr)
			{
				GDefaultView = &View;
			}
		}
		else
		{
			// Default resources
			View.ShaderDrawData.Buffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(4, 8), TEXT("ShaderDraw.DataBuffer(Dummy)"), ERDGBufferFlags::None);
		}
	}

	void DrawView(FRDGBuilder& GraphBuilder, const FViewInfo& View, FRDGTextureRef OutputTexture, FRDGTextureRef DepthTexture)
	{
		if (!IsEnabled(View))
		{
			return;
		}

		{
			FRDGBufferRef DataBuffer = View.ShaderDrawData.Buffer;
			InternalDrawView(GraphBuilder, View, DataBuffer, OutputTexture, DepthTexture);
		}

		if (View.ViewState && View.ViewState->ShaderDrawDebugStateData.bIsLocked)
		{
			FRDGBufferRef DataBuffer = GraphBuilder.RegisterExternalBuffer(View.ViewState->ShaderDrawDebugStateData.Buffer);
			InternalDrawView(GraphBuilder, View, DataBuffer, OutputTexture, DepthTexture);
		}
	}

	void EndView(FViewInfo& View)
	{
		if (!IsEnabled(View))
		{
			return;
		}

		GDefaultView = nullptr;
	}

	void SetParameters(FRDGBuilder& GraphBuilder, const FShaderDrawDebugData& Data, FShaderParameters& OutParameters)
	{
		OutParameters.ShaderDrawCursorPos				= Data.CursorPosition;
		OutParameters.ShaderDrawMaxElementCount			= Data.MaxElementCount;
		OutParameters.ShaderDrawTranslatedWorldOffset	= Data.ShaderDrawTranslatedWorldOffset;

		check(Data.Buffer != nullptr);
		OutParameters.OutShaderDrawPrimitive = GraphBuilder.CreateUAV(Data.Buffer);		
	}

	void SetParameters(FRDGBuilder& GraphBuilder, const FViewInfo& View, FShaderParameters& OutParameters)
	{
		SetParameters(GraphBuilder, View.ShaderDrawData, OutParameters);
	}

	// Returns true if the default view exists and has shader debug rendering enabled (this needs to be checked before using a permutation that requires the shader draw parameters)
	bool IsDefaultViewEnabled()
	{
		return GDefaultView != nullptr && IsEnabled(*GDefaultView);
	}

	void SetParameters(FRDGBuilder& GraphBuilder, FShaderParameters& OutParameters)
	{
		if (GDefaultView != nullptr)
		{
			SetParameters(GraphBuilder, GDefaultView->ShaderDrawData, OutParameters);
		}
	}
}
