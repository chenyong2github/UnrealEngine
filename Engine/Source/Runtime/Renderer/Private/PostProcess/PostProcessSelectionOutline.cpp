// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#if WITH_EDITOR

#include "PostProcess/PostProcessSelectionOutline.h"
#include "PostProcess/PostProcessCompositeEditorPrimitives.h"
#include "SceneTextureParameters.h"
#include "CanvasTypes.h"
#include "RenderTargetTemp.h"
#include "ClearQuad.h"

namespace
{
class FSelectionOutlinePS : public FEditorPrimitiveShader
{
public:
	DECLARE_GLOBAL_SHADER(FSelectionOutlinePS);
	SHADER_USE_PARAMETER_STRUCT(FSelectionOutlinePS, FEditorPrimitiveShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_STRUCT(FScreenPassTextureViewportParameters, Color)
		SHADER_PARAMETER_STRUCT(FScreenPassTextureViewportParameters, Depth)
		SHADER_PARAMETER_STRUCT(FScreenPassTextureViewportTransform, ColorToDepth)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, ColorTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, ColorSampler)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, DepthTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, DepthSampler)
		SHADER_PARAMETER_RDG_TEXTURE(, EditorPrimitivesDepth)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(, EditorPrimitivesStencil)
		SHADER_PARAMETER(FVector, OutlineColor)
		SHADER_PARAMETER(float, SelectionHighlightIntensity)
		SHADER_PARAMETER(FVector, SubduedOutlineColor)
		SHADER_PARAMETER(float, BSPSelectionIntensity)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()
};

IMPLEMENT_GLOBAL_SHADER(FSelectionOutlinePS, "/Engine/Private/PostProcessSelectionOutline.usf", "MainPS", SF_Pixel);
} //! namespace

FScreenPassTexture AddSelectionOutlinePass(FRDGBuilder& GraphBuilder, const FViewInfo& View, const FSelectionOutlineInputs& Inputs)
{
	check(Inputs.SceneColor.IsValid());
	check(Inputs.SceneDepth.IsValid());

	RDG_EVENT_SCOPE(GraphBuilder, "EditorSelectionOutlines");

	uint32 MsaaSampleCount = 0;

	// Patch uniform buffers with updated state for rendering the outline mesh draw commands.
	{
		FScene* Scene = View.Family->Scene->GetRenderScene();

		FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(GraphBuilder.RHICmdList);

		MsaaSampleCount = SceneContext.GetEditorMSAACompositingSampleCount();

		UpdateEditorPrimitiveView(Scene->UniformBuffers, SceneContext, View, Inputs.SceneColor.ViewRect);

		FSceneTexturesUniformParameters SceneTextureParameters;
		SetupSceneTextureUniformParameters(SceneContext, View.FeatureLevel, ESceneTextureSetupMode::None, SceneTextureParameters);
		Scene->UniformBuffers.EditorSelectionPassUniformBuffer.UpdateUniformBufferImmediate(SceneTextureParameters);
	}

	FRDGTextureRef DepthStencilTexture = nullptr;

	// Generate custom depth / stencil for outline shapes.
	{
		{
			FRDGTextureDesc DepthStencilDesc = Inputs.SceneColor.Texture->Desc;
			DepthStencilDesc.Reset();
			DepthStencilDesc.Format = PF_DepthStencil;
			DepthStencilDesc.Flags = TexCreate_None;

			// This is a reversed Z depth surface, so 0.0f is the far plane.
			DepthStencilDesc.ClearValue = FClearValueBinding((float)ERHIZBuffer::FarPlane, 0);

			// Mark targetable as TexCreate_ShaderResource because we actually do want to sample from the unresolved MSAA target in this case.
			DepthStencilDesc.TargetableFlags = TexCreate_DepthStencilTargetable | TexCreate_ShaderResource;
			DepthStencilDesc.NumSamples = MsaaSampleCount;
			DepthStencilDesc.bForceSharedTargetAndShaderResource = true;

			DepthStencilTexture = GraphBuilder.CreateTexture(DepthStencilDesc, TEXT("SelectionOutline"));
		}

		FRenderTargetParameters* PassParameters = GraphBuilder.AllocParameters<FRenderTargetParameters>();
		PassParameters->RenderTargets.DepthStencil = FDepthStencilBinding(
			DepthStencilTexture,
			ERenderTargetLoadAction::EClear,
			ERenderTargetLoadAction::EClear,
			FExclusiveDepthStencil::DepthWrite_StencilWrite);

		const FScreenPassTextureViewport SceneColorViewport(Inputs.SceneColor);

		GraphBuilder.AddPass(
			RDG_EVENT_NAME("OutlineDepth %dx%d", SceneColorViewport.Rect.Width(), SceneColorViewport.Rect.Height()),
			PassParameters,
			ERDGPassFlags::Raster,
			[&View, SceneColorViewport](FRHICommandList& RHICmdList)
		{
			RHICmdList.SetViewport(SceneColorViewport.Rect.Min.X, SceneColorViewport.Rect.Min.Y, 0.0f, SceneColorViewport.Rect.Max.X, SceneColorViewport.Rect.Max.Y, 1.0f);

			// Run selection pass on static elements
			View.ParallelMeshDrawCommandPasses[EMeshPass::EditorSelection].DispatchDraw(nullptr, RHICmdList);

			// to get an outline around the objects if it's partly outside of the screen
			{
				FIntRect InnerRect = SceneColorViewport.Rect;

				// 1 as we have an outline that is that thick
				InnerRect.InflateRect(-1);

				// top
				RHICmdList.SetScissorRect(true, SceneColorViewport.Rect.Min.X, SceneColorViewport.Rect.Min.Y, SceneColorViewport.Rect.Max.X, InnerRect.Min.Y);
				DrawClearQuad(RHICmdList, false, FLinearColor(), true, (float)ERHIZBuffer::FarPlane, true, 0, SceneColorViewport.Extent, FIntRect());
				// bottom
				RHICmdList.SetScissorRect(true, SceneColorViewport.Rect.Min.X, InnerRect.Max.Y, SceneColorViewport.Rect.Max.X, SceneColorViewport.Rect.Max.Y);
				DrawClearQuad(RHICmdList, false, FLinearColor(), true, (float)ERHIZBuffer::FarPlane, true, 0, SceneColorViewport.Extent, FIntRect());
				// left
				RHICmdList.SetScissorRect(true, SceneColorViewport.Rect.Min.X, SceneColorViewport.Rect.Min.Y, InnerRect.Min.X, SceneColorViewport.Rect.Max.Y);
				DrawClearQuad(RHICmdList, false, FLinearColor(), true, (float)ERHIZBuffer::FarPlane, true, 0, SceneColorViewport.Extent, FIntRect());
				// right
				RHICmdList.SetScissorRect(true, InnerRect.Max.X, SceneColorViewport.Rect.Min.Y, SceneColorViewport.Rect.Max.X, SceneColorViewport.Rect.Max.Y);
				DrawClearQuad(RHICmdList, false, FLinearColor(), true, (float)ERHIZBuffer::FarPlane, true, 0, SceneColorViewport.Extent, FIntRect());

				RHICmdList.SetScissorRect(false, 0, 0, 0, 0);
			}
		});
	}

	FScreenPassRenderTarget Output = Inputs.OverrideOutput;

	if (!Output.IsValid())
	{
		Output = FScreenPassRenderTarget::CreateFromInput(GraphBuilder, Inputs.SceneColor, View.GetOverwriteLoadAction(), TEXT("SelectionOutlineColor"));
	}

	// Render selection outlines.
	{
		const FScreenPassTextureViewport OutputViewport(Output);
		const FScreenPassTextureViewport ColorViewport(Inputs.SceneColor);
		const FScreenPassTextureViewport DepthViewport(Inputs.SceneDepth);

		FRHISamplerState* PointClampSampler = TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();

		FSelectionOutlinePS::FParameters* PassParameters = GraphBuilder.AllocParameters<FSelectionOutlinePS::FParameters>();
		PassParameters->RenderTargets[0] = Output.GetRenderTargetBinding();
		PassParameters->View = View.ViewUniformBuffer;
		PassParameters->Color = GetScreenPassTextureViewportParameters(ColorViewport);
		PassParameters->Depth = GetScreenPassTextureViewportParameters(DepthViewport);
		PassParameters->ColorToDepth = GetScreenPassTextureViewportTransform(PassParameters->Color, PassParameters->Depth);
		PassParameters->ColorTexture = Inputs.SceneColor.Texture;
		PassParameters->ColorSampler = PointClampSampler;
		PassParameters->DepthTexture = Inputs.SceneDepth.Texture;
		PassParameters->DepthSampler = PointClampSampler;
		PassParameters->EditorPrimitivesDepth = DepthStencilTexture;
		PassParameters->EditorPrimitivesStencil = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::CreateWithPixelFormat(DepthStencilTexture, PF_X24_G8));
		PassParameters->OutlineColor = FVector(View.SelectionOutlineColor);
		PassParameters->SelectionHighlightIntensity = GEngine->SelectionHighlightIntensity;
		PassParameters->SubduedOutlineColor = FVector(View.SubduedSelectionOutlineColor);
		PassParameters->BSPSelectionIntensity = GEngine->BSPSelectionHighlightIntensity;

		FSelectionOutlinePS::FPermutationDomain PermutationVector;
		PermutationVector.Set<FSelectionOutlinePS::FSampleCountDimension>(MsaaSampleCount);

		TShaderMapRef<FSelectionOutlinePS> PixelShader(View.ShaderMap, PermutationVector);

		AddDrawScreenPass(
			GraphBuilder,
			RDG_EVENT_NAME("OutlineColor %dx%d", OutputViewport.Rect.Width(), OutputViewport.Rect.Height()),
			View,
			OutputViewport,
			ColorViewport,
			*PixelShader,
			PassParameters);
	}

	return MoveTemp(Output);
}

FRenderingCompositeOutputRef AddSelectionOutlinePass(FRenderingCompositionGraph& Graph, FRenderingCompositeOutputRef Input)
{
	FRenderingCompositePass* Pass = Graph.RegisterPass(
		new(FMemStack::Get()) TRCPassForRDG<1, 1>(
			[](FRenderingCompositePass* InPass, FRenderingCompositePassContext& InContext)
	{
		FRDGBuilder GraphBuilder(InContext.RHICmdList);

		FRDGTextureRef SceneColorTexture = InPass->CreateRDGTextureForRequiredInput(GraphBuilder, ePId_Input0, TEXT("SceneColor"));
		const FIntRect SceneColorViewRect = InContext.GetSceneColorDestRect(InPass);

		const FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(GraphBuilder.RHICmdList);
		FRDGTextureRef SceneDepthTexture = GraphBuilder.RegisterExternalTexture(SceneContext.SceneDepthZ, TEXT("SceneDepthZ"));

		FSelectionOutlineInputs Inputs;
		Inputs.SceneColor.Texture = SceneColorTexture;
		Inputs.SceneColor.ViewRect = SceneColorViewRect;
		Inputs.SceneDepth.Texture = SceneDepthTexture;
		Inputs.SceneDepth.ViewRect = InContext.View.ViewRect;

		if (FRDGTextureRef OutputTexture = InPass->FindRDGTextureForOutput(GraphBuilder, ePId_Output0, TEXT("BackBuffer")))
		{
			Inputs.OverrideOutput.Texture = OutputTexture;
			Inputs.OverrideOutput.ViewRect = InContext.GetSceneColorDestRect(InPass->GetOutput(ePId_Output0)->PooledRenderTarget->GetRenderTargetItem());
			Inputs.OverrideOutput.LoadAction = InContext.View.IsFirstInFamily() ? ERenderTargetLoadAction::EClear : ERenderTargetLoadAction::ELoad;
		}

		FScreenPassTexture Outputs = AddSelectionOutlinePass(GraphBuilder, InContext.View, Inputs);

		InPass->ExtractRDGTextureForOutput(GraphBuilder, ePId_Output0, Outputs.Texture);

		GraphBuilder.Execute();
	}));
	Pass->SetInput(ePId_Input0, Input);
	return Pass;
}

#endif