// Copyright Epic Games, Inc. All Rights Reserved.

#include "GpuDebugRendering.h"
#include "SceneRendering.h"
#include "GlobalShader.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "Containers/ResourceArray.h"
#include "CommonRenderResources.h"

namespace ShaderDrawDebug 
{
	// Console variables
	static TAutoConsoleVariable<int32> CVarShaderDrawEnable(
		TEXT("r.ShaderDrawDebug"),
		1,
		TEXT("ShaderDrawDebug debugging toggle.\n"),
		ECVF_Cheat | ECVF_RenderThreadSafe);

	static TAutoConsoleVariable<int32> CVarShaderDrawMaxElementCount(
		TEXT("r.ShaderDrawDebug.MaxElementCount"),
		10000,
		TEXT("ShaderDraw output buffer size in element.\n"),
		ECVF_Cheat | ECVF_RenderThreadSafe);

	static TAutoConsoleVariable<int32> CVarShaderDrawLock(
		TEXT("r.ShaderDrawDebug.Lock"),
		0,
		TEXT("Lock the shader draw buffer.\n"),
		ECVF_Cheat | ECVF_RenderThreadSafe);

	bool IsShaderDrawDebugEnabled()
	{
#if WITH_EDITOR
		return CVarShaderDrawEnable.GetValueOnAnyThread() > 0;
#else
		return false;
#endif
	}

	bool IsShaderDrawLocked()
	{
#if WITH_EDITOR
		return CVarShaderDrawLock.GetValueOnAnyThread() > 0;
#else
		return false;
#endif
	}

	static bool IsShaderDrawDebugEnabled(const EShaderPlatform Platform)
	{
		return IsFeatureLevelSupported(Platform, ERHIFeatureLevel::SM5) && IsPCPlatform(Platform);
	}

	bool IsShaderDrawDebugEnabled(const FViewInfo& View)
	{
		return IsShaderDrawDebugEnabled() && IsShaderDrawDebugEnabled(View.GetShaderPlatform());
	}

	static uint32 GetMaxShaderDrawElementCount()
	{
		return uint32(FMath::Max(1, CVarShaderDrawMaxElementCount.GetValueOnRenderThread()));
	}

	struct ShaderDrawDebugElement
	{
		uint32 Pos[3];
		uint32 Color[2];
	};

	// This needs to be allocated per view, or move into a more persistent place
	struct FLockedData
	{
		FRWBufferStructured Buffer;
		FRWBuffer IndirectBuffer;
		bool bIsLocked = false;
	};

	static FLockedData LockedData = {};

	//////////////////////////////////////////////////////////////////////////

	class FShaderDrawDebugClearCS : public FGlobalShader
	{
		DECLARE_GLOBAL_SHADER(FShaderDrawDebugClearCS);
		SHADER_USE_PARAMETER_STRUCT(FShaderDrawDebugClearCS, FGlobalShader);

		BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer, DataBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer, IndirectBuffer)
		END_SHADER_PARAMETER_STRUCT()

		static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
		{
			return IsShaderDrawDebugEnabled(Parameters.Platform);
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

		class FBufferType : SHADER_PERMUTATION_INT("PERMUTATION_BUFFER_TYPE", 2);
		using FPermutationDomain = TShaderPermutationDomain<FBufferType>;

		BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
			SHADER_PARAMETER(uint32, NumElements)
			SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, InBuffer)
			SHADER_PARAMETER_UAV(RWBuffer, OutBuffer)

			SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer, InStructuredBuffer)
			SHADER_PARAMETER_UAV(RWStructuredBuffer, OutStructuredBuffer)
		END_SHADER_PARAMETER_STRUCT()

		static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
		{
			return IsShaderDrawDebugEnabled(Parameters.Platform);
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

		class FInputType : SHADER_PERMUTATION_INT("PERMUTATION_INPUT_TYPE", 2);
		using FPermutationDomain = TShaderPermutationDomain<FInputType>;

		BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
			SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
			SHADER_PARAMETER_SRV(StructuredBuffer, LockedShaderDrawDebugPrimitive)
			SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer, ShaderDrawDebugPrimitive)
			SHADER_PARAMETER_RDG_BUFFER(StructuredBuffer, IndirectBuffer)
		END_SHADER_PARAMETER_STRUCT()

		static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
		{
			return IsShaderDrawDebugEnabled(Parameters.Platform);
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
			SHADER_PARAMETER(float, ColorScale)
			RENDER_TARGET_BINDING_SLOTS()
		END_SHADER_PARAMETER_STRUCT()

		using FPermutationDomain = TShaderPermutationDomain<>;

		static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
		{
			return IsShaderDrawDebugEnabled(Parameters.Platform);
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

	BEGIN_SHADER_PARAMETER_STRUCT(ShaderDrawVSPSParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FShaderDrawDebugVS::FParameters, ShaderDrawVSParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(FShaderDrawDebugPS::FParameters, ShaderDrawPSParameters)
	END_SHADER_PARAMETER_STRUCT()

	//////////////////////////////////////////////////////////////////////////

	void BeginView(FRHICommandListImmediate& InRHICmdList, FViewInfo& View)
	{
		if (!IsShaderDrawDebugEnabled(View))
		{
			return;
		}

		FRDGBuilder GraphBuilder(InRHICmdList);
		FRDGBufferRef DataBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(ShaderDrawDebugElement), GetMaxShaderDrawElementCount()), TEXT("ShaderDrawDataBuffer"));
		FRDGBufferRef IndirectBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc<FRHIDrawIndirectParameters>(1), TEXT("ShaderDrawDataIndirectBuffer"));

		FShaderDrawDebugClearCS::FParameters* Parameters = GraphBuilder.AllocParameters<FShaderDrawDebugClearCS::FParameters>();
		Parameters->DataBuffer = GraphBuilder.CreateUAV(DataBuffer);
		Parameters->IndirectBuffer = GraphBuilder.CreateUAV(IndirectBuffer);

		TShaderMapRef<FShaderDrawDebugClearCS> ComputeShader(View.ShaderMap);

		// Note: we do not call ClearUnusedGraphResources here as we want to for the allocation of DataBuffer
		GraphBuilder.AddPass(
			RDG_EVENT_NAME("ShaderDrawClear"),
			Parameters,
			ERDGPassFlags::Compute,
			[Parameters, ComputeShader](FRHICommandList& RHICmdList)
		{
			FComputeShaderUtils::Dispatch(RHICmdList, *ComputeShader, *Parameters, FIntVector(1,1,1));
		});

		GraphBuilder.QueueBufferExtraction(DataBuffer, &View.ShaderDrawData.Buffer, FRDGResourceState::EAccess::Write, FRDGResourceState::EPipeline::Compute);
		GraphBuilder.QueueBufferExtraction(IndirectBuffer, &View.ShaderDrawData.IndirectBuffer, FRDGResourceState::EAccess::Write, FRDGResourceState::EPipeline::Compute);

		GraphBuilder.Execute();

		if (IsShaderDrawLocked() && !LockedData.bIsLocked)
		{
			LockedData.Buffer.Initialize(sizeof(ShaderDrawDebugElement), GetMaxShaderDrawElementCount(), 0U, TEXT("ShaderDrawDataBuffer"));
			LockedData.IndirectBuffer.Initialize(sizeof(uint32), 4, PF_R32_UINT, BUF_DrawIndirect, TEXT("ShaderDrawDataIndirectBuffer"));
		}

		View.ShaderDrawData.CursorPosition = View.CursorPos;
	}

	void DrawView(FRDGBuilder& GraphBuilder, const FViewInfo& View, FRDGTextureRef OutputTexture, FRDGTextureRef DepthTexture)
	{
		if (!IsShaderDrawDebugEnabled(View))
		{
			return;
		}

		auto RunPass = [&](
			bool bIsBehindDepth, 
			bool bUseRdgInput,
			FRDGBufferRef DataBuffer, 
			FRDGBufferRef IndirectBuffer, 
			FShaderResourceViewRHIRef LockedDataBuffer,
			FRHIVertexBuffer* LockedIndirectBuffer)
		{
			FShaderDrawDebugVS::FPermutationDomain PermutationVector;
			PermutationVector.Set<FShaderDrawDebugVS::FInputType>(bUseRdgInput ? 0 : 1);
			TShaderMapRef<FShaderDrawDebugVS> VertexShader(View.ShaderMap, PermutationVector);
			TShaderMapRef<FShaderDrawDebugPS> PixelShader(View.ShaderMap);

			ShaderDrawVSPSParameters* PassParameters = GraphBuilder.AllocParameters<ShaderDrawVSPSParameters>();
			PassParameters->ShaderDrawPSParameters.RenderTargets[0] = FRenderTargetBinding(OutputTexture, ERenderTargetLoadAction::ELoad);
			PassParameters->ShaderDrawPSParameters.RenderTargets.DepthStencil = FDepthStencilBinding(DepthTexture, ERenderTargetLoadAction::ELoad, FExclusiveDepthStencil::DepthRead_StencilNop);
			PassParameters->ShaderDrawPSParameters.ColorScale = bIsBehindDepth ? 0.4f : 1.0f;	// When debug primitive are behind the depth buffer, make them look darker.
			PassParameters->ShaderDrawVSParameters.View = View.ViewUniformBuffer;
			if (bUseRdgInput)
			{
				PassParameters->ShaderDrawVSParameters.ShaderDrawDebugPrimitive = GraphBuilder.CreateSRV(DataBuffer);
				PassParameters->ShaderDrawVSParameters.IndirectBuffer = IndirectBuffer;
			}
			else
			{
				PassParameters->ShaderDrawVSParameters.LockedShaderDrawDebugPrimitive = LockedDataBuffer;
			}

			ValidateShaderParameters(*PixelShader, PassParameters->ShaderDrawPSParameters);
			ClearUnusedGraphResources(*PixelShader, &PassParameters->ShaderDrawPSParameters);
			ValidateShaderParameters(*VertexShader, PassParameters->ShaderDrawVSParameters);
			ClearUnusedGraphResources(*VertexShader, &PassParameters->ShaderDrawVSParameters);

			GraphBuilder.AddPass(
				RDG_EVENT_NAME("ShaderDrawDebug"),
				PassParameters,
				ERDGPassFlags::Raster,
				[VertexShader, PixelShader, PassParameters, IndirectBuffer, LockedIndirectBuffer, bIsBehindDepth, bUseRdgInput](FRHICommandListImmediate& RHICmdListImmediate)
			{
				FGraphicsPipelineStateInitializer GraphicsPSOInit;
				RHICmdListImmediate.ApplyCachedRenderTargets(GraphicsPSOInit);
				GraphicsPSOInit.DepthStencilState = bIsBehindDepth ? TStaticDepthStencilState<false, CF_DepthFarther>::GetRHI() : TStaticDepthStencilState<false, CF_DepthNearOrEqual>::GetRHI();
				GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGBA, BO_Add, BF_One, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_One>::GetRHI(); // Premultiplied-alpha composition
				GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None, true>::GetRHI();
				GraphicsPSOInit.PrimitiveType = PT_LineList;
				GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GEmptyVertexDeclaration.VertexDeclarationRHI;
				GraphicsPSOInit.BoundShaderState.VertexShaderRHI = GETSAFERHISHADER_VERTEX(*VertexShader);
				GraphicsPSOInit.BoundShaderState.PixelShaderRHI = GETSAFERHISHADER_PIXEL(*PixelShader);
				SetGraphicsPipelineState(RHICmdListImmediate, GraphicsPSOInit);

				SetShaderParameters(RHICmdListImmediate, *VertexShader, VertexShader->GetVertexShader(), PassParameters->ShaderDrawVSParameters);
				SetShaderParameters(RHICmdListImmediate, *PixelShader, PixelShader->GetPixelShader(), PassParameters->ShaderDrawPSParameters);


				if (bUseRdgInput)
				{
					RHICmdListImmediate.DrawPrimitiveIndirect(IndirectBuffer->GetIndirectRHICallBuffer(), 0);
				}
				else
				{
					RHICmdListImmediate.DrawPrimitiveIndirect(LockedIndirectBuffer, 0);
				}
			});
		};

		FRDGBufferRef DataBuffer = GraphBuilder.RegisterExternalBuffer(View.ShaderDrawData.Buffer, TEXT("ShaderDrawDebugDataBuffer"));
		FRDGBufferRef IndirectBuffer = GraphBuilder.RegisterExternalBuffer(View.ShaderDrawData.IndirectBuffer, TEXT("ShaderDrawDebugIndirectDataBuffer"));
		{
			RunPass(true,  true, DataBuffer, IndirectBuffer, nullptr, nullptr);	// Render what is behind the depth buffer
			RunPass(false, true, DataBuffer, IndirectBuffer, nullptr, nullptr); // Render what is in front of the depth buffer
		}

		if (LockedData.bIsLocked)
		{
			FShaderResourceViewRHIRef LockedDataBuffer = LockedData.Buffer.SRV;
			FRHIVertexBuffer* LockedIndirectBuffer = LockedData.IndirectBuffer.Buffer;
			RunPass(false, false, nullptr, nullptr, LockedDataBuffer, LockedIndirectBuffer); // Render what is in front of the depth buffer
			RunPass(true,  false, nullptr, nullptr, LockedDataBuffer, LockedIndirectBuffer); // Render what is behind the depth buffer
		}

		if (IsShaderDrawLocked() && !LockedData.bIsLocked)
		{
			{
				const uint32 NumElements = LockedData.Buffer.NumBytes / sizeof(ShaderDrawDebugElement);

				FShaderDrawDebugCopyCS::FPermutationDomain PermutationVector;
				PermutationVector.Set<FShaderDrawDebugCopyCS::FBufferType>(0);
				TShaderMapRef<FShaderDrawDebugCopyCS> ComputeShader(View.ShaderMap, PermutationVector);

				FShaderDrawDebugCopyCS::FParameters* Parameters = GraphBuilder.AllocParameters<FShaderDrawDebugCopyCS::FParameters>();
				Parameters->NumElements = NumElements;
				Parameters->InStructuredBuffer = GraphBuilder.CreateSRV(DataBuffer);
				Parameters->OutStructuredBuffer = LockedData.Buffer.UAV;
				FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("ShaderDrawDebugCopy"), *ComputeShader, Parameters, FIntVector(FMath::CeilToInt(NumElements / 1024.f), 1, 1));
			}
			{
				FShaderDrawDebugCopyCS::FPermutationDomain PermutationVector;
				PermutationVector.Set<FShaderDrawDebugCopyCS::FBufferType>(1);
				TShaderMapRef<FShaderDrawDebugCopyCS> ComputeShader(View.ShaderMap, PermutationVector);

				FShaderDrawDebugCopyCS::FParameters* Parameters = GraphBuilder.AllocParameters<FShaderDrawDebugCopyCS::FParameters>();
				Parameters->InBuffer = GraphBuilder.CreateSRV(IndirectBuffer);
				Parameters->OutBuffer = LockedData.IndirectBuffer.UAV;
				FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("ShaderDrawDebugCopy"), *ComputeShader, Parameters, FIntVector(1, 1, 1));
			}
		}
	}

	void EndView(FViewInfo& View)
	{
		if (!IsShaderDrawDebugEnabled(View))
		{
			return;
		}

		if (IsShaderDrawLocked() && !LockedData.bIsLocked)
		{
			LockedData.bIsLocked = true;
		}

		if (!IsShaderDrawLocked() && LockedData.bIsLocked)
		{
			LockedData.Buffer.Release();
			LockedData.IndirectBuffer.Release();
			LockedData.bIsLocked = false;
		}
	}

	void SetParameters(FRDGBuilder& GraphBuilder, const FShaderDrawDebugData& Data, FShaderDrawDebugParameters& OutParameters)
	{
		FRDGBufferRef DataBuffer = GraphBuilder.RegisterExternalBuffer(Data.Buffer, TEXT("ShaderDrawDebugDataBuffer"));
		FRDGBufferRef IndirectBuffer = GraphBuilder.RegisterExternalBuffer(Data.IndirectBuffer, TEXT("ShaderDrawDebugIndirectDataBuffer"));

		OutParameters.ShaderDrawCursorPos = Data.CursorPosition;
		OutParameters.ShaderDrawMaxElementCount = GetMaxShaderDrawElementCount();
		OutParameters.OutShaderDrawPrimitive = GraphBuilder.CreateUAV(DataBuffer);
		OutParameters.OutputShaderDrawIndirect = GraphBuilder.CreateUAV(IndirectBuffer);
	}
}
