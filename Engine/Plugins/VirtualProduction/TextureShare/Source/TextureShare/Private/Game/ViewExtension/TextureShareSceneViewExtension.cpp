// Copyright Epic Games, Inc. All Rights Reserved.

#include "Game/ViewExtension/TextureShareSceneViewExtension.h"

#include "Object/TextureShareObject.h"
#include "Object/TextureShareObjectProxy.h"

#include "Module/TextureShareLog.h"
#include "Misc/TextureShareStrings.h"

#include "RHI.h"
#include "RHICommandList.h"
#include "RHIResources.h"
#include "SceneView.h"

#include "PostProcess/SceneRenderTargets.h"

//////////////////////////////////////////////////////////////////////////////////////////////
namespace TextureShareSceneViewExtensionHelpers
{
	static void GetTextureShareCoreSceneViewMatrices(const FViewMatrices& InViewMatrices, FTextureShareCoreSceneViewMatrices& OutViewMatrices)
	{
		OutViewMatrices.ProjectionMatrix = InViewMatrices.GetProjectionMatrix();
		OutViewMatrices.ProjectionNoAAMatrix = InViewMatrices.GetProjectionNoAAMatrix();
		OutViewMatrices.ViewMatrix = InViewMatrices.GetViewMatrix();
		OutViewMatrices.ViewProjectionMatrix = InViewMatrices.GetViewProjectionMatrix();
		OutViewMatrices.TranslatedViewProjectionMatrix = InViewMatrices.GetTranslatedViewProjectionMatrix();
		OutViewMatrices.PreViewTranslation = InViewMatrices.GetPreViewTranslation();
		OutViewMatrices.ViewOrigin = InViewMatrices.GetViewOrigin();
		OutViewMatrices.ProjectionScale = InViewMatrices.GetProjectionScale();
		OutViewMatrices.TemporalAAProjectionJitter = InViewMatrices.GetTemporalAAJitter();
		OutViewMatrices.ScreenScale = InViewMatrices.GetScreenScale();
	}

	static void GetTextureShareCoreSceneView(const FSceneView& InSceneView, FTextureShareCoreSceneView& OutSceneView)
	{
		GetTextureShareCoreSceneViewMatrices(InSceneView.ViewMatrices, OutSceneView.ViewMatrices);

		OutSceneView.UnscaledViewRect = InSceneView.UnscaledViewRect;
		OutSceneView.UnconstrainedViewRect = InSceneView.UnconstrainedViewRect;
		OutSceneView.ViewLocation = InSceneView.ViewLocation;
		OutSceneView.ViewRotation = InSceneView.ViewRotation;
		OutSceneView.BaseHmdOrientation = InSceneView.BaseHmdOrientation;
		OutSceneView.BaseHmdLocation = InSceneView.BaseHmdLocation;
		OutSceneView.WorldToMetersScale = InSceneView.WorldToMetersScale;

		OutSceneView.StereoViewIndex = InSceneView.StereoViewIndex;
		OutSceneView.PrimaryViewIndex = InSceneView.PrimaryViewIndex;

		OutSceneView.FOV = InSceneView.FOV;
		OutSceneView.DesiredFOV = InSceneView.DesiredFOV;
	}

	static void GetTextureShareCoreSceneGameTime(const FGameTime& InGameTime, FTextureShareCoreSceneGameTime& OutGameTime)
	{
		OutGameTime.RealTimeSeconds = InGameTime.GetRealTimeSeconds();
		OutGameTime.WorldTimeSeconds = InGameTime.GetWorldTimeSeconds();
		OutGameTime.DeltaRealTimeSeconds = InGameTime.GetDeltaRealTimeSeconds();
		OutGameTime.DeltaWorldTimeSeconds = InGameTime.GetDeltaWorldTimeSeconds();
	}

	static void GetTextureShareCoreSceneViewFamily(const FSceneViewFamily& InViewFamily, FTextureShareCoreSceneViewFamily& OutViewFamily)
	{
		GetTextureShareCoreSceneGameTime(InViewFamily.Time, OutViewFamily.GameTime);

		OutViewFamily.FrameNumber = InViewFamily.FrameNumber;
		OutViewFamily.bIsHDR = InViewFamily.bIsHDR;
		OutViewFamily.GammaCorrection = InViewFamily.GammaCorrection;
		OutViewFamily.SecondaryViewFraction = InViewFamily.SecondaryViewFraction;
	}

	BEGIN_SHADER_PARAMETER_STRUCT(FSendTextureParameters, )
	RDG_TEXTURE_ACCESS(Texture, ERHIAccess::CopySrc)
	END_SHADER_PARAMETER_STRUCT()

	BEGIN_SHADER_PARAMETER_STRUCT(FReceiveTextureParameters, )
	RDG_TEXTURE_ACCESS(Texture, ERHIAccess::CopyDest)
	END_SHADER_PARAMETER_STRUCT()

	static void SendRDGTexture(const TSharedPtr<ITextureShareObjectProxy, ESPMode::ThreadSafe> ObjectProxy, FRDGBuilder& GraphBuilder, FRDGTextureRef InTextureRef, const int32 InTextureGPUIndex, const FTextureShareCoreResourceDesc& InResourceDesc, const FIntRect& InViewRect)
	{
		FSendTextureParameters* PassParameters = GraphBuilder.AllocParameters<FSendTextureParameters>();
		PassParameters->Texture = InTextureRef;

		GraphBuilder.AddPass(
			RDG_EVENT_NAME("TextureShare_SendRDGTexture_%s", *InResourceDesc.ResourceName),
			PassParameters,
			ERDGPassFlags::Copy | ERDGPassFlags::NeverCull,
			[ObjectProxy, InTextureRef, InTextureGPUIndex, InResourceDesc, InViewRect](FRHICommandListImmediate& RHICmdList)
		{
			ObjectProxy->ShareResource_RenderThread(RHICmdList, InResourceDesc, InTextureRef->GetRHI(), InTextureGPUIndex, &InViewRect);
		});
	}

};
using namespace TextureShareSceneViewExtensionHelpers;

//////////////////////////////////////////////////////////////////////////////////////////////
// FTextureShareSceneViewExtension
//////////////////////////////////////////////////////////////////////////////////////////////
void FTextureShareSceneViewExtension::GetTextureShareCoreSceneViewData_RenderThread(const FSceneViewFamily& InViewFamily, const FSceneView& InSceneView, FTextureShareCoreSceneViewData& OutSceneViewData) const
{
	GetTextureShareCoreSceneView(InSceneView, OutSceneViewData.View);
	GetTextureShareCoreSceneViewFamily(InViewFamily, OutSceneViewData.ViewFamily);
}

void FTextureShareSceneViewExtension::GetSceneViewData_RenderThread(const FSceneView& InSceneView, const FTextureShareCoreViewDesc& InViewDesc)
{
	// Create new data container for viewport eye
	FTextureShareCoreSceneViewData SceneViewData(InViewDesc);

	// Get view eye data
	GetTextureShareCoreSceneViewData_RenderThread(*ViewFamily_RenderThread, InSceneView, SceneViewData);

	// Save scene viewport eye data
	ObjectProxy->GetCoreProxyData_RenderThread().SceneData.Add(SceneViewData);
}

void FTextureShareSceneViewExtension::ShareSceneViewColors_RenderThread(FRDGBuilder& GraphBuilder, const FSceneTextures& SceneTextures, const FSceneView& InSceneView, const FTextureShareCoreViewDesc& InViewDesc)
{
	int32 SceneViewGPUIndex = -1;

#if WITH_MGPU
	if (ViewFamily_RenderThread->bMultiGPUForkAndJoin)
	{
		SceneViewGPUIndex = InSceneView.GPUMask.GetFirstIndex();
	}
#endif

	const auto AddShareTexturePass = [&](const TCHAR* InTextureName, FRDGTextureRef InTextureRef)
	{
		if (HasBeenProduced(InTextureRef))
		{
			FTextureShareCoreResourceDesc InResourceDesc(InTextureName, InViewDesc, ETextureShareTextureOp::Read);
			// Share only if the resource is requested from a remote process
			if (const FTextureShareCoreResourceRequest* ExistResourceRequest = ObjectProxy->GetData_RenderThread().FindResourceRequest(InResourceDesc))
			{
				SendRDGTexture(ObjectProxy, GraphBuilder, InTextureRef, SceneViewGPUIndex, InResourceDesc, InSceneView.UnconstrainedViewRect);
			}
		}
	};

	AddShareTexturePass(TextureShareStrings::SceneTextures::SceneColor, SceneTextures.Color.Resolve);

	AddShareTexturePass(TextureShareStrings::SceneTextures::SceneDepth, SceneTextures.Depth.Resolve);
	AddShareTexturePass(TextureShareStrings::SceneTextures::SmallDepthZ, SceneTextures.SmallDepth);

	AddShareTexturePass(TextureShareStrings::SceneTextures::GBufferA, SceneTextures.GBufferA);
	AddShareTexturePass(TextureShareStrings::SceneTextures::GBufferB, SceneTextures.GBufferB);
	AddShareTexturePass(TextureShareStrings::SceneTextures::GBufferC, SceneTextures.GBufferC);
	AddShareTexturePass(TextureShareStrings::SceneTextures::GBufferD, SceneTextures.GBufferD);
	AddShareTexturePass(TextureShareStrings::SceneTextures::GBufferE, SceneTextures.GBufferE);
	AddShareTexturePass(TextureShareStrings::SceneTextures::GBufferF, SceneTextures.GBufferF);
}

//////////////////////////////////////////////////////////////////////////////////////////////
FTextureShareSceneViewExtension::FTextureShareSceneViewExtension(const FAutoRegister& AutoRegister, const TSharedRef<ITextureShareObjectProxy, ESPMode::ThreadSafe>& InObjectProxy, FViewport* InLinkedViewport)
	: FSceneViewExtensionBase(AutoRegister)
	, LinkedViewport(InLinkedViewport)
	, ObjectProxy(InObjectProxy)
{ }

FTextureShareSceneViewExtension::~FTextureShareSceneViewExtension()
{ }

//////////////////////////////////////////////////////////////////////////////////////////////
void FTextureShareSceneViewExtension::Initialize(const TSharedPtr<FTextureShareSceneViewExtension, ESPMode::ThreadSafe> InViewExtension)
{
	FScopeLock Lock(&DataCS);

	// finally restore the current value
	if (InViewExtension.IsValid())
	{
		PreRenderViewFamilyFunction = InViewExtension->PreRenderViewFamilyFunction;
		PostRenderViewFamilyFunction = InViewExtension->PostRenderViewFamilyFunction;

		OnBackBufferReadyToPresentFunction = InViewExtension->OnBackBufferReadyToPresentFunction;
	}
}

//////////////////////////////////////////////////////////////////////////////////////////////
FViewport* FTextureShareSceneViewExtension::GetLinkedViewport() const
{
	FScopeLock Lock(&DataCS);
	return LinkedViewport;
}

void FTextureShareSceneViewExtension::SetLinkedViewport(FViewport* InLinkedViewport)
{
	FScopeLock Lock(&DataCS);
	LinkedViewport = InLinkedViewport;
}

bool FTextureShareSceneViewExtension::IsStereoRenderingAllowed() const
{
	return LinkedViewport && LinkedViewport->IsStereoRenderingAllowed();
}

void FTextureShareSceneViewExtension::SetPreRenderViewFamilyFunction(TFunctionTextureShareViewExtension* In)
{
	FScopeLock Lock(&DataCS);

	PreRenderViewFamilyFunction.Reset();
	if (In)
	{
		PreRenderViewFamilyFunction = *In;
	}
}

void FTextureShareSceneViewExtension::SetPostRenderViewFamilyFunction(TFunctionTextureShareViewExtension* In)
{
	FScopeLock Lock(&DataCS);

	PostRenderViewFamilyFunction.Reset();
	if (In)
	{
		PostRenderViewFamilyFunction = *In;
	}
}

void FTextureShareSceneViewExtension::SetOnBackBufferReadyToPresentFunction(TFunctionTextureShareOnBackBufferReadyToPresent* In)
{
	FScopeLock Lock(&DataCS);

	OnBackBufferReadyToPresentFunction.Reset();
	if (In)
	{
		OnBackBufferReadyToPresentFunction = *In;
	}
}

void FTextureShareSceneViewExtension::SetEnableObjectProxySync(bool bInEnabled)
{
	FScopeLock Lock(&DataCS);

	bEnableObjectProxySync = bInEnabled;
}

bool FTextureShareSceneViewExtension::IsActiveThisFrame_Internal(const FSceneViewExtensionContext& Context) const
{
	FScopeLock Lock(&DataCS);

	if (!bEnabled)
	{
		return false;
	}

	return (LinkedViewport == Context.Viewport);
}

void FTextureShareSceneViewExtension::SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView)
{
	FScopeLock Lock(&DataCS);

}

void FTextureShareSceneViewExtension::PreRenderViewFamily_RenderThread(FRHICommandListImmediate& RHICmdList, FSceneViewFamily& InViewFamily)
{
	FScopeLock Lock(&DataCS);

	// be default scene view capturing is disabled
	ViewFamily_RenderThread = nullptr;

	if (bEnabled_RenderThread)
	{
		ViewFamily_RenderThread = &InViewFamily;
	}

	// Handle view extension functor callbacks
	if (PreRenderViewFamilyFunction)
	{
		PreRenderViewFamilyFunction(RHICmdList, *this);
	}
}

void FTextureShareSceneViewExtension::OnResolvedSceneColor_RenderThread(FRDGBuilder& GraphBuilder, const FSceneTextures& SceneTextures)
{
	FScopeLock Lock(&DataCS);

	if (ViewFamily_RenderThread && bEnabled_RenderThread)
	{
		// Send all  textures from viewfamily views
		for (const FSceneView* SceneViewIt : ViewFamily_RenderThread->Views)
		{
			if (SceneViewIt)
			{
				if (const FTextureShareSceneViewInfo* ViewInfo = ObjectProxy->GetData_RenderThread().Views.Find(SceneViewIt->StereoViewIndex, SceneViewIt->StereoPass))
				{
					// Always get scene view data
					GetSceneViewData_RenderThread(*SceneViewIt, ViewInfo->ViewDesc);

					// Send scene textures on request
					ShareSceneViewColors_RenderThread(GraphBuilder, SceneTextures, *SceneViewIt, ViewInfo->ViewDesc);
				}
			}
		}
	}
}

void FTextureShareSceneViewExtension::PostRenderViewFamily_RenderThread(FRHICommandListImmediate& RHICmdList, FSceneViewFamily& InViewFamily)
{
	FScopeLock Lock(&DataCS);

	if (ViewFamily_RenderThread && bEnabled_RenderThread)
	{
		for (const FSceneView* SceneViewIt : InViewFamily.Views)
		{
			if (SceneViewIt)
			{
				if (const FTextureShareSceneViewInfo* ViewInfo = ObjectProxy->GetData_RenderThread().Views.Find(SceneViewIt->StereoViewIndex, SceneViewIt->StereoPass))
				{
					// Share only if the resource is requested from a remote process
					if (const FTextureShareCoreResourceRequest* ExistResourceRequest = ObjectProxy->GetData_RenderThread().FindResourceRequest(FTextureShareCoreResourceDesc(TextureShareStrings::SceneTextures::FinalColor, ViewInfo->ViewDesc, ETextureShareTextureOp::Undefined)))
					{
						int32 SceneViewGPUIndex = -1;
#if WITH_MGPU
						if (ViewFamily_RenderThread->bMultiGPUForkAndJoin)
						{
							SceneViewGPUIndex = SceneViewIt->GPUMask.GetFirstIndex();
						}
#endif
						FTexture2DRHIRef RenderTargetTexture = InViewFamily.RenderTarget->GetRenderTargetTexture();
						if (RenderTargetTexture.IsValid())
						{
							// Send
							const FTextureShareCoreResourceDesc SendResourceDesc(TextureShareStrings::SceneTextures::FinalColor, ViewInfo->ViewDesc, ETextureShareTextureOp::Read);
							ObjectProxy->ShareResource_RenderThread(RHICmdList, SendResourceDesc, RenderTargetTexture, SceneViewGPUIndex, &SceneViewIt->UnscaledViewRect);

							if (bEnableObjectProxySync)
							{
								// Receive
								const FTextureShareCoreResourceDesc ReceiveResourceDesc(TextureShareStrings::SceneTextures::FinalColor, ViewInfo->ViewDesc, ETextureShareTextureOp::Write, ETextureShareSyncStep::FrameSceneFinalColorEnd);
								if (ObjectProxy->ShareResource_RenderThread(RHICmdList, ReceiveResourceDesc, RenderTargetTexture, SceneViewGPUIndex, &SceneViewIt->UnscaledViewRect))
								{
									ObjectProxy->FrameSync_RenderThread(RHICmdList, ETextureShareSyncStep::FrameSceneFinalColorEnd);
								}
							}
						}
					}
				}
			}
		}
	}

	// Handle view extension functor callbacks
	if (PostRenderViewFamilyFunction)
	{
		PostRenderViewFamilyFunction(RHICmdList, *this);
	}

	// Clear stored value
	ViewFamily_RenderThread = nullptr;
}

void FTextureShareSceneViewExtension::OnBackBufferReadyToPresent_RenderThread(SWindow& SlateWindow, const FTexture2DRHIRef& InBackbuffer)
{
	FScopeLock Lock(&DataCS);

	// Handle view extension functor callbacks
	if (OnBackBufferReadyToPresentFunction)
	{
		FRHICommandListImmediate& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();
		OnBackBufferReadyToPresentFunction(RHICmdList, *this, InBackbuffer);
	}
}
