// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "DefaultStereoLayers.h"
#include "HeadMountedDisplayBase.h"

#include "EngineModule.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "RendererInterface.h"
#include "StereoLayerRendering.h"
#include "RHIStaticStates.h"
#include "StaticBoundShaderState.h"
#include "PipelineStateCache.h"
#include "ClearQuad.h"
#include "SceneView.h"
#include "CommonRenderResources.h"
#include "IXRLoadingScreen.h"

namespace 
{

	/*=============================================================================
	*
	* Helper functions
	*
	*/

	//=============================================================================
	static FMatrix ConvertTransform(const FTransform& In)
	{

		const FQuat InQuat = In.GetRotation();
		FQuat OutQuat(-InQuat.Y, -InQuat.Z, -InQuat.X, -InQuat.W);

		const FVector InPos = In.GetTranslation();
		FVector OutPos(InPos.Y, InPos.Z, InPos.X);

		const FVector InScale = In.GetScale3D();
		FVector OutScale(InScale.Y, InScale.Z, InScale.X);

		return FTransform(OutQuat, OutPos, OutScale).ToMatrixWithScale() * FMatrix(
			FPlane(0, 1, 0, 0),
			FPlane(0, 0, 1, 0),
			FPlane(1, 0, 0, 0),
			FPlane(0, 0, 0, 1));
	}

}

FDefaultStereoLayers::FDefaultStereoLayers(const FAutoRegister& AutoRegister, FHeadMountedDisplayBase* InHMDDevice) 
	: FSceneViewExtensionBase(AutoRegister)
	, HMDDevice(InHMDDevice)
{

}

//=============================================================================
void FDefaultStereoLayers::StereoLayerRender(FRHICommandListImmediate& RHICmdList, const TArray<uint32> & LayersToRender, const FLayerRenderParams& RenderParams) const
{
	check(IsInRenderingThread());
	if (!LayersToRender.Num())
	{
		return;
	}

	IRendererModule& RendererModule = GetRendererModule();
	using TOpaqueBlendState = TStaticBlendState<CW_RGBA, BO_Add, BF_One, BF_Zero, BO_Add, BF_One, BF_Zero>;
	using TAlphaBlendState = TStaticBlendState<CW_RGBA, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_One, BF_InverseSourceAlpha>;

	// Set render state
	FGraphicsPipelineStateInitializer GraphicsPSOInit;
	RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);

	GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None, true, false>::GetRHI();
	GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
	RHICmdList.SetScissorRect(false, 0, 0, 0, 0);
	RHICmdList.SetViewport(RenderParams.Viewport.Min.X, RenderParams.Viewport.Min.Y, 0, RenderParams.Viewport.Max.X, RenderParams.Viewport.Max.Y, 1.0f);

	// Set initial shader state
	auto ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
	TShaderMapRef<FStereoLayerVS> VertexShader(ShaderMap);
	TShaderMapRef<FStereoLayerPS> PixelShader(ShaderMap);
	TShaderMapRef<FStereoLayerPS_External> PixelShader_External(ShaderMap);

	GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
	GraphicsPSOInit.BoundShaderState.VertexShaderRHI = GETSAFERHISHADER_VERTEX(*VertexShader);

	GraphicsPSOInit.PrimitiveType = PT_TriangleList;

	// Force initialization of pipeline state on first iteration:
	bool bLastWasOpaque = (RenderThreadLayers[LayersToRender[0]].Flags & LAYER_FLAG_TEX_NO_ALPHA_CHANNEL) == 0;
	bool bLastWasExternal = (RenderThreadLayers[LayersToRender[0]].Flags & LAYER_FLAG_TEX_EXTERNAL) == 0;

	// For each layer
	for (uint32 LayerIndex : LayersToRender)
	{
		const FLayerDesc& Layer = RenderThreadLayers[LayerIndex];
		check(Layer.IsVisible());
		const bool bIsOpaque = (Layer.Flags & LAYER_FLAG_TEX_NO_ALPHA_CHANNEL) != 0;
		const bool bIsExternal = (Layer.Flags & LAYER_FLAG_TEX_EXTERNAL) != 0;
		bool bPipelineStateNeedsUpdate = false;

		if (bIsOpaque != bLastWasOpaque)
		{
			bLastWasOpaque = bIsOpaque;
			GraphicsPSOInit.BlendState = bIsOpaque ? TOpaqueBlendState::GetRHI() : TAlphaBlendState::GetRHI();
			bPipelineStateNeedsUpdate = true;
		}

		if (bIsExternal != bLastWasExternal)
		{
			bLastWasExternal = bIsExternal;
			GraphicsPSOInit.BoundShaderState.PixelShaderRHI = bIsExternal ? GETSAFERHISHADER_PIXEL(*PixelShader_External) : GETSAFERHISHADER_PIXEL(*PixelShader);
			bPipelineStateNeedsUpdate = true;
		}

		if (bPipelineStateNeedsUpdate)
		{
			// Updater render state
			SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);
		}

		FMatrix LayerMatrix = ConvertTransform(Layer.Transform);

		FVector2D QuadSize = Layer.QuadSize * 0.5f;
		if (Layer.Flags & LAYER_FLAG_QUAD_PRESERVE_TEX_RATIO)
		{
			const FRHITexture2D* Tex2D = Layer.Texture->GetTexture2D();
			if (Tex2D)
			{
				const float SizeX = Tex2D->GetSizeX();
				const float SizeY = Tex2D->GetSizeY();
				if (SizeX != 0)
				{
					const float AspectRatio = SizeY / SizeX;
					QuadSize.Y = QuadSize.X * AspectRatio;
				}
			}
		}

		// Set shader uniforms
		VertexShader->SetParameters(
			RHICmdList,
			QuadSize,
			Layer.UVRect,
			RenderParams.RenderMatrices[static_cast<int>(Layer.PositionType)],
			LayerMatrix);

		PixelShader->SetParameters(
			RHICmdList,
			TStaticSamplerState<SF_Trilinear>::GetRHI(),
			Layer.Texture);

		const FIntPoint TargetSize = RenderParams.Viewport.Size();
		// Draw primitive
		RendererModule.DrawRectangle(
			RHICmdList,
			0.0f, 0.0f,
			TargetSize.X, TargetSize.Y,
			0.0f, 0.0f,
			1.0f, 1.0f,
			TargetSize,
			FIntPoint(1, 1),
			*VertexShader
		);
	}
}

void FDefaultStereoLayers::PreRenderViewFamily_RenderThread(FRHICommandListImmediate& RHICmdList, FSceneViewFamily& InViewFamily)
{
	check(IsInRenderingThread());

	if (!GetStereoLayersDirty())
	{
		return;
	}

	CopyLayers(RenderThreadLayers);

	// Sort layers
	SortedSceneLayers.Reset();
	SortedOverlayLayers.Reset();
	uint32 LayerCount = RenderThreadLayers.Num();
	for (uint32 LayerIndex = 0; LayerIndex < LayerCount; ++LayerIndex)
	{
		const auto& Layer = RenderThreadLayers[LayerIndex];
		if (!Layer.IsVisible())
		{
			continue;
		}
		if (Layer.PositionType == ELayerType::FaceLocked)
		{
			SortedOverlayLayers.Add(LayerIndex);
		}
		else
		{
			SortedSceneLayers.Add(LayerIndex);
		}
	}

	auto SortLayersPredicate = [&](const uint32& A, const uint32& B)
	{
		return RenderThreadLayers[A].Priority < RenderThreadLayers[B].Priority;
	};
	SortedSceneLayers.Sort(SortLayersPredicate);
	SortedOverlayLayers.Sort(SortLayersPredicate);
}


void FDefaultStereoLayers::PostRenderView_RenderThread(FRHICommandListImmediate& RHICmdList, FSceneView& InView)
{
	if (!HMDDevice->DeviceIsStereoEyeView(InView))
	{
		return;
	}

	FViewMatrices ModifiedViewMatrices = InView.ViewMatrices;
	ModifiedViewMatrices.HackRemoveTemporalAAProjectionJitter();
	const FMatrix& ProjectionMatrix = ModifiedViewMatrices.GetProjectionMatrix();
	const FMatrix& ViewProjectionMatrix = ModifiedViewMatrices.GetViewProjectionMatrix();

	// Calculate a view matrix that only adjusts for eye position, ignoring head position, orientation and world position.
	FVector EyeShift;
	FQuat EyeOrientation;
	HMDDevice->GetRelativeEyePose(IXRTrackingSystem::HMDDeviceId, InView.StereoPass, EyeOrientation, EyeShift);

	FMatrix EyeMatrix = FTranslationMatrix(-EyeShift) * FInverseRotationMatrix(EyeOrientation.Rotator()) * FMatrix(
		FPlane(0, 0, 1, 0),
		FPlane(1, 0, 0, 0),
		FPlane(0, 1, 0, 0),
		FPlane(0, 0, 0, 1));

	FQuat HmdOrientation = HmdTransform.GetRotation();
	FVector HmdLocation = HmdTransform.GetTranslation();
	FMatrix TrackerMatrix = FTranslationMatrix(-HmdLocation) * FInverseRotationMatrix(HmdOrientation.Rotator()) * EyeMatrix;

	FLayerRenderParams RenderParams{
		InView.UnscaledViewRect, // Viewport
		{
			ViewProjectionMatrix,				// WorldLocked,
			TrackerMatrix * ProjectionMatrix,	// TrackerLocked,
			EyeMatrix * ProjectionMatrix		// FaceLocked
		}
	};
	
	FTexture2DRHIRef RenderTarget = HMDDevice->GetSceneLayerTarget_RenderThread(InView.StereoPass, RenderParams.Viewport);
	if (!RenderTarget.IsValid())
	{
		RenderTarget = InView.Family->RenderTarget->GetRenderTargetTexture();
	}

	FRHIRenderPassInfo RPInfo(RenderTarget, ERenderTargetActions::Load_Store);
	RHICmdList.BeginRenderPass(RPInfo, TEXT("StereoLayerRender"));
	RHICmdList.SetViewport(RenderParams.Viewport.Min.X, RenderParams.Viewport.Min.Y, 0, RenderParams.Viewport.Max.X, RenderParams.Viewport.Max.Y, 1.0f);

	if (bSplashIsShown || !IsBackgroundLayerVisible())
	{
		DrawClearQuad(RHICmdList, FLinearColor::Black);
	}

	StereoLayerRender(RHICmdList, SortedSceneLayers, RenderParams);
	
	// Optionally render face-locked layers into a non-reprojected target if supported by the HMD platform
	FTexture2DRHIRef OverlayRenderTarget = HMDDevice->GetOverlayLayerTarget_RenderThread(InView.StereoPass, RenderParams.Viewport);
	if (OverlayRenderTarget.IsValid())
	{
		RHICmdList.EndRenderPass();

		FRHIRenderPassInfo RPInfoOverlayRenderTarget(OverlayRenderTarget, ERenderTargetActions::Load_Store);
		RHICmdList.BeginRenderPass(RPInfoOverlayRenderTarget, TEXT("StereoLayerRenderIntoOverlay"));

		DrawClearQuad(RHICmdList, FLinearColor(0.0f, 0.0f, 0.0f, 0.0f));
		RHICmdList.SetViewport(RenderParams.Viewport.Min.X, RenderParams.Viewport.Min.Y, 0, RenderParams.Viewport.Max.X, RenderParams.Viewport.Max.Y, 1.0f);
	}

	StereoLayerRender(RHICmdList, SortedOverlayLayers, RenderParams);

	RHICmdList.EndRenderPass();
}

bool FDefaultStereoLayers::IsActiveThisFrame(class FViewport* InViewport) const
{
	return GEngine && GEngine->IsStereoscopic3D(InViewport);
}

void FDefaultStereoLayers::SetupViewFamily(FSceneViewFamily& InViewFamily)
{
	// Initialize HMD position.
	FQuat HmdOrientation = FQuat::Identity;
	FVector HmdPosition = FVector::ZeroVector;
	HMDDevice->GetCurrentPose(IXRTrackingSystem::HMDDeviceId, HmdOrientation, HmdPosition);
	HmdTransform = FTransform(HmdOrientation, HmdPosition);
}

void FDefaultStereoLayers::GetAllocatedTexture(uint32 LayerId, FTextureRHIRef &Texture, FTextureRHIRef &LeftTexture)
{
	Texture = LeftTexture = nullptr;
	FLayerDesc* LayerFound = nullptr;

	if (IsInRenderingThread())
	{
		for (int32 LayerIndex = 0; LayerIndex < RenderThreadLayers.Num(); LayerIndex++)
		{
			if (RenderThreadLayers[LayerIndex].GetLayerId() == LayerId)
			{
				LayerFound = &RenderThreadLayers[LayerIndex];
			}
		}
	}

	else
	{
		// Only supporting the use of this function on RenderingThread.
		check(false);
		return;
	}

	if (LayerFound && LayerFound->Texture)
	{
		switch (LayerFound->ShapeType)
		{
		case IStereoLayers::CubemapLayer:
			Texture = LayerFound->Texture->GetTextureCube();
			LeftTexture = LayerFound->LeftTexture ? LayerFound->LeftTexture->GetTextureCube() : nullptr;			
			break;

		case IStereoLayers::CylinderLayer:
		case IStereoLayers::QuadLayer:
			Texture = LayerFound->Texture->GetTexture2D();
			LeftTexture = LayerFound->LeftTexture ? LayerFound->LeftTexture->GetTexture2D() : nullptr;
			break;

		default:
			break;
		}
	}
}
