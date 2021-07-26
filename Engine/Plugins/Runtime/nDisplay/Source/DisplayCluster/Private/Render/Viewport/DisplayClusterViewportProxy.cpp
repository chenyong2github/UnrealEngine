// Copyright Epic Games, Inc. All Rights Reserved.

#include "Render/Viewport/DisplayClusterViewportProxy.h"

#include "Render/Viewport/DisplayClusterViewportManagerProxy.h"
#include "Render/Viewport/DisplayClusterViewport.h"

#include "Render/Viewport/Containers/DisplayClusterViewport_PostRenderSettings.h"
#include "Render/Viewport/RenderTarget/DisplayClusterRenderTargetResource.h"
#include "Render/Viewport/RenderFrame/DisplayClusterRenderFrameSettings.h"
#include "Render/Viewport/Configuration/DisplayClusterViewportConfiguration.h"

#include "Render/Projection/IDisplayClusterProjectionPolicy.h"

#if WITH_EDITOR
#include "DisplayClusterRootActor.h"
#endif

#include "RHIStaticStates.h"

#include "RenderResource.h"
#include "RenderingThread.h"
#include "CommonRenderResources.h"
#include "PixelShaderUtils.h"

#include "GlobalShader.h"
#include "ShaderParameters.h"
#include "ShaderParameterUtils.h"

#include "ScreenRendering.h"
#include "PostProcess/SceneFilterRendering.h"
#include "PostProcessParameters.h"

#include "IDisplayClusterShaders.h"

static TAutoConsoleVariable<int> CVarDisplayClusterRenderOverscanResolve(
	TEXT("nDisplay.render.overscan.resolve"),
	1,
	TEXT("Allow resolve overscan internal rect to output backbuffer.\n")
	TEXT(" 0 - to disable.\n"),
	ECVF_RenderThreadSafe
);

FDisplayClusterViewportProxy::FDisplayClusterViewportProxy(const FDisplayClusterViewportManagerProxy& InOwner, const FDisplayClusterViewport& RenderViewport)
	: ViewportId(RenderViewport.ViewportId)
	, RenderSettings(RenderViewport.RenderSettings)
	, ProjectionPolicy(RenderViewport.UninitializedProjectionPolicy)
	, Owner(InOwner)
	, ShadersAPI(IDisplayClusterShaders::Get())
{
	check(ProjectionPolicy.IsValid());
}

FDisplayClusterViewportProxy::~FDisplayClusterViewportProxy()
{
}

const IDisplayClusterViewportManagerProxy& FDisplayClusterViewportProxy::GetOwner() const
{
	return Owner;
}

bool ImplGetTextureResources_RenderThread(const TArray<FDisplayClusterTextureResource*>& InResources, TArray<FRHITexture2D*>& OutResources)
{
	check(OutResources.Num() == 0);

	if (InResources.Num() > 0)
	{
		OutResources.AddDefaulted(InResources.Num());

		for (int i = 0; i < OutResources.Num(); i++)
		{
			OutResources[i] = InResources[i]->GetTextureResource();
		}

		return true;
	}

	return false;
}


bool ImplGetTextureResources_RenderThread(const TArray<FTextureRenderTargetResource*>& InResources, TArray<FRHITexture2D*>& OutResources)
{
	check(OutResources.Num() == 0);

	if (InResources.Num() > 0)
	{
		OutResources.AddDefaulted(InResources.Num());

		for (int i = 0; i < OutResources.Num(); i++)
		{
			OutResources[i] = InResources[i]->GetTexture2DRHI();
		}

		return true;
	}

	return false;
}

//  Return viewport scene proxy resources by type
bool FDisplayClusterViewportProxy::GetResources_RenderThread(const EDisplayClusterViewportResourceType InResourceType, TArray<FRHITexture2D*>& OutResources) const
{
	check(IsInRenderingThread());

	OutResources.Empty();

	switch (InResourceType)
	{
	case EDisplayClusterViewportResourceType::InternalRenderTargetResource:
	{
		if (Contexts.Num() > 0)
		{
			OutResources.AddDefaulted(Contexts.Num());
			for (int ContextIt = 0; ContextIt < Contexts.Num(); ContextIt++)
			{
				//Support Override:
				if (Contexts[ContextIt].bDisableRender)
				{
					if (!PostRenderSettings.Replace.TextureRHI.IsValid())
					{
						OutResources.Empty();
						return false;
					}

					OutResources[ContextIt] = PostRenderSettings.Replace.TextureRHI->GetTexture2D();
				}
				else
				{
					FDisplayClusterRenderTargetResource* Input = RenderTargets[ContextIt];
					if (Input == nullptr || Input->IsInitialized() == false)
					{
						OutResources.Empty();
						return false;
					}

					OutResources[ContextIt] = Input->GetRenderTargetTexture();
				}
			}
		}

		return OutResources.Num() > 0;
	}

	case EDisplayClusterViewportResourceType::InputShaderResource:
		return ImplGetTextureResources_RenderThread(InputShaderResources, OutResources);

	case EDisplayClusterViewportResourceType::AdditionalTargetableResource:
		return ImplGetTextureResources_RenderThread(AdditionalTargetableResources, OutResources);

	case EDisplayClusterViewportResourceType::MipsShaderResource:
		return ImplGetTextureResources_RenderThread(MipsShaderResources, OutResources);

	case EDisplayClusterViewportResourceType::OutputFrameTargetableResource:
		return ImplGetTextureResources_RenderThread(OutputFrameTargetableResources, OutResources);

	case EDisplayClusterViewportResourceType::AdditionalFrameTargetableResource:
		return ImplGetTextureResources_RenderThread(AdditionalFrameTargetableResources, OutResources);

#if WITH_EDITOR
		// Support preview:
	case EDisplayClusterViewportResourceType::OutputPreviewTargetableResource:
	{
		// Get external resource:
		if (OutputPreviewTargetableResource.IsValid())
		{
			FRHITexture* Texture = OutputPreviewTargetableResource;
			FRHITexture2D* Texture2D = static_cast<FRHITexture2D*>(Texture);
			if (Texture2D != nullptr)
			{
				OutResources.Add(Texture2D);
				return true;
			}
		}
		break;
	}
#endif

	default:
		break;
	}

	return false;
}

EDisplayClusterViewportResourceType FDisplayClusterViewportProxy::GetOutputResourceType() const
{
#if WITH_EDITOR
	if (Owner.GetRenderFrameSettings_RenderThread().RenderMode == EDisplayClusterRenderFrameMode::PreviewMono)
	{
		return EDisplayClusterViewportResourceType::OutputPreviewTargetableResource;
	}
#endif

	return EDisplayClusterViewportResourceType::OutputFrameTargetableResource;
}

bool FDisplayClusterViewportProxy::GetResourcesWithRects_RenderThread(const EDisplayClusterViewportResourceType InResourceType, TArray<FRHITexture2D*>& OutResources, TArray<FIntRect>& OutResourceRects) const
{
	check(IsInRenderingThread());

	if (!GetResources_RenderThread(InResourceType, OutResources))
	{
		return false;
	}

	// Collect all resource rects:
	OutResourceRects.AddDefaulted(OutResources.Num());

	switch (InResourceType)
	{
	case EDisplayClusterViewportResourceType::InternalRenderTargetResource:
		for (int ContextIt = 0; ContextIt < OutResourceRects.Num(); ContextIt++)
		{
			if (Contexts[ContextIt].bDisableRender)
			{
				// Get image from Override
				OutResourceRects[ContextIt] = PostRenderSettings.Replace.Rect;
			}
			else
			{
				OutResourceRects[ContextIt] = Contexts[ContextIt].RenderTargetRect;
			}
		}
		break;
	case EDisplayClusterViewportResourceType::OutputFrameTargetableResource:
	case EDisplayClusterViewportResourceType::AdditionalFrameTargetableResource:
		for (int ContextIt = 0; ContextIt < OutResourceRects.Num(); ContextIt++)
		{
			OutResourceRects[ContextIt] = Contexts[ContextIt].FrameTargetRect;
		}
		break;
	default:
		for (int ContextIt = 0; ContextIt < OutResourceRects.Num(); ContextIt++)
		{
			OutResourceRects[ContextIt] = FIntRect(FIntPoint(0, 0), OutResources[ContextIt]->GetSizeXY());
		}
	}

	return true;
}

// Apply postprocess, generate mips, etc from settings in FDisplayClusterViewporDeferredUpdateSettings
void FDisplayClusterViewportProxy::UpdateDeferredResources(FRHICommandListImmediate& RHICmdList) const
{
	check(IsInRenderingThread());

	FDisplayClusterViewportProxy const * SourceProxy = this;
	EDisplayClusterViewportResourceType SourceType = EDisplayClusterViewportResourceType::InternalRenderTargetResource;

	if (RenderSettings.OverrideViewportId.IsEmpty() == false)
	{
		// link resources from other viewport
		//@todo: implement correct sorting order for rendering in frame manager
		//@todo: Implement stereo supporting, now mono only

		FDisplayClusterViewportProxy const* OverrideViewportProxy = Owner.ImplFindViewport_RenderThread(RenderSettings.OverrideViewportId);
		if (OverrideViewportProxy)
		{
			SourceProxy = OverrideViewportProxy;
			// Get after postprocessing
			SourceType = EDisplayClusterViewportResourceType::InputShaderResource;
		}
	}
	else
	{
		if (RenderSettings.bSkipRendering)
		{
			//@todo: support skip rendering
			return;
		}
	}

	// Pass 0: Resolve from RTT region to separated viewport context resource:
	ImplResolveResources(RHICmdList, SourceProxy, SourceType, EDisplayClusterViewportResourceType::InputShaderResource);

	// Pass 1: Generate blur postprocess effect for render target texture rect for all contexts
	if (PostRenderSettings.PostprocessBlur.IsEnabled())
	{
		TArray<FRHITexture2D*> InShaderResources;
		TArray<FRHITexture2D*> OutTargetableResources;
		if (GetResources_RenderThread(EDisplayClusterViewportResourceType::InputShaderResource, InShaderResources) && GetResources_RenderThread(EDisplayClusterViewportResourceType::AdditionalTargetableResource, OutTargetableResources))
		{
			// Render postprocess blur:
			for (int ContextNum = 0; ContextNum < InShaderResources.Num(); ContextNum++)
			{
				ShadersAPI.RenderPostprocess_Blur(RHICmdList, InShaderResources[ContextNum], OutTargetableResources[ContextNum], PostRenderSettings.PostprocessBlur);
			}

			// Copy result back to input
			ResolveResources(RHICmdList, EDisplayClusterViewportResourceType::AdditionalTargetableResource, EDisplayClusterViewportResourceType::InputShaderResource);
		}
	}

	// Pass 2: Create mips texture and generate mips from render target rect for all contexts
	if (PostRenderSettings.GenerateMips.IsEnabled())
	{
		TArray<FRHITexture2D*> InOutMipsResources;
		if (GetResources_RenderThread(EDisplayClusterViewportResourceType::MipsShaderResource, InOutMipsResources))
		{
			// Copy input image to layer0 on mips texture
			ResolveResources(RHICmdList, EDisplayClusterViewportResourceType::InputShaderResource, EDisplayClusterViewportResourceType::MipsShaderResource);

			// Generate mips
			for (FRHITexture2D*& ResourceIt : InOutMipsResources)
			{
				ShadersAPI.GenerateMips(RHICmdList, ResourceIt, PostRenderSettings.GenerateMips);
			}
		}
	}
}

static void DirectCopyTextureImpl_RenderThread(FRHICommandListImmediate& RHICmdList, FRHITexture* SrcTexture, FRHITexture* DstTexture, const FIntRect& SrcRect, const FIntRect& DstRect)
{
	// Copy with resolved params
	FResolveParams Params;

	Params.DestArrayIndex = 0;
	Params.SourceArrayIndex = 0;

	Params.Rect.X1 = SrcRect.Min.X;
	Params.Rect.Y1 = SrcRect.Min.Y;
	Params.Rect.X2 = SrcRect.Max.X;
	Params.Rect.Y2 = SrcRect.Max.Y;

	Params.DestRect.X1 = DstRect.Min.X;
	Params.DestRect.Y1 = DstRect.Min.Y;
	Params.DestRect.X2 = DstRect.Max.X;
	Params.DestRect.Y2 = DstRect.Max.Y;

	RHICmdList.CopyToResolveTarget(SrcTexture, DstTexture, Params);
}

static void ResampleCopyTextureImpl_RenderThread(FRHICommandListImmediate& RHICmdList, FRHITexture* SrcTexture, FRHITexture* DstTexture, const FIntRect& SrcRect, const FIntRect& DstRect)
{
	// Texture format mismatch, use a shader to do the copy.
	// #todo-renderpasses there's no explicit resolve here? Do we need one?
	FRHIRenderPassInfo RPInfo(DstTexture, ERenderTargetActions::Load_Store);
	RHICmdList.BeginRenderPass(RPInfo, TEXT("DisaplyClusterRender_ResampleTexture"));
	{
		FIntVector SrcSizeXYZ = SrcTexture->GetSizeXYZ();
		FIntVector DstSizeXYZ = DstTexture->GetSizeXYZ();

		FIntPoint SrcSize(SrcSizeXYZ.X, SrcSizeXYZ.Y);
		FIntPoint DstSize(DstSizeXYZ.X, DstSizeXYZ.Y);

		RHICmdList.SetViewport(0.f, 0.f, 0.0f, DstSize.X, DstSize.Y, 1.0f);

		FGraphicsPipelineStateInitializer GraphicsPSOInit;
		RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
		GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();
		GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
		GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();

		FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
		TShaderMapRef<FScreenVS> VertexShader(ShaderMap);
		TShaderMapRef<FScreenPS> PixelShader(ShaderMap);

		GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
		GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
		GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
		GraphicsPSOInit.PrimitiveType = PT_TriangleList;

		SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

		if (SrcRect.Size() != DstRect.Size())
		{
			PixelShader->SetParameters(RHICmdList, TStaticSamplerState<SF_Bilinear>::GetRHI(), SrcTexture);
		}
		else
		{
			PixelShader->SetParameters(RHICmdList, TStaticSamplerState<SF_Point>::GetRHI(), SrcTexture);
		}

		// Set up vertex uniform parameters for scaling and biasing the rectangle.
		// Note: Use DrawRectangle in the vertex shader to calculate the correct vertex position and uv.
		FDrawRectangleParameters Parameters;
		{
			Parameters.PosScaleBias = FVector4(DstRect.Size().X, DstRect.Size().Y, DstRect.Min.X, DstRect.Min.Y);
			Parameters.UVScaleBias = FVector4(SrcRect.Size().X, SrcRect.Size().Y, SrcRect.Min.X, SrcRect.Min.Y);
			Parameters.InvTargetSizeAndTextureSize = FVector4(1.0f / DstSize.X, 1.0f / DstSize.Y, 1.0f / SrcSize.X, 1.0f / SrcSize.Y);

			SetUniformBufferParameterImmediate(RHICmdList, VertexShader.GetVertexShader(), VertexShader->GetUniformBufferParameter<FDrawRectangleParameters>(), Parameters);
		}

		FPixelShaderUtils::DrawFullscreenQuad(RHICmdList, 1);
	}
	RHICmdList.EndRenderPass();
}

void ImplResolveResource(FRHICommandListImmediate& RHICmdList, FRHITexture2D* InputResource, const FIntRect& InputRect, FRHITexture2D* OutputResource, const FIntRect& OutputRect, bool bOutputIsMipsResource)
{
	check(InputResource);
	check(OutputResource);

	if (bOutputIsMipsResource)
	{
		// Special copy for output texture with NumMips (copy to dest mips = 0)
		// Copy first Mip from SrcTexture to CameraRTT(other mips is black)

		FRHICopyTextureInfo CopyInfo;
		CopyInfo.Size = FIntVector(InputRect.Width(), InputRect.Height(), 0);
		CopyInfo.SourcePosition.X = InputRect.Min.X;
		CopyInfo.SourcePosition.Y = InputRect.Min.Y;

		CopyInfo.DestMipIndex = 0;
		CopyInfo.DestPosition.X = OutputRect.Min.X;
		CopyInfo.DestPosition.Y = OutputRect.Min.Y;

		RHICmdList.CopyTexture(InputResource, OutputResource, CopyInfo);
	}
	else
	{
		if (InputRect.Size() != OutputRect.Size() || InputResource->GetFormat() != OutputResource->GetFormat())
		{
			// Resample size with shader
			ResampleCopyTextureImpl_RenderThread(RHICmdList, InputResource, OutputResource, InputRect, OutputRect);
		}
		else
		{
			// Use RHICmdList.CopyToResolveTarget inside
			DirectCopyTextureImpl_RenderThread(RHICmdList, InputResource, OutputResource, InputRect, OutputRect);
		}
	}
}

// Resolve resource contexts
bool FDisplayClusterViewportProxy::ResolveResources(FRHICommandListImmediate& RHICmdList, const EDisplayClusterViewportResourceType InputResourceType, const EDisplayClusterViewportResourceType OutputResourceType) const
{
	return ImplResolveResources(RHICmdList, this, InputResourceType, OutputResourceType);
}

bool FDisplayClusterViewportProxy::ImplResolveResources(FRHICommandListImmediate& RHICmdList, FDisplayClusterViewportProxy const* SourceProxy, const EDisplayClusterViewportResourceType InputResourceType, const EDisplayClusterViewportResourceType OutputResourceType) const
{
	check(IsInRenderingThread());

	if (InputResourceType == EDisplayClusterViewportResourceType::MipsShaderResource) {
		// RenderTargetMips not allowved for resolve op
		return false;
	}

	bool bOutputIsMipsResource = OutputResourceType == EDisplayClusterViewportResourceType::MipsShaderResource;

	TArray<FRHITexture2D*> InputResources, OutputResources;
	TArray<FIntRect> InputResourcesRect, OutputResourcesRect;
	if (SourceProxy->GetResourcesWithRects_RenderThread(InputResourceType, InputResources, InputResourcesRect) && GetResourcesWithRects_RenderThread(OutputResourceType, OutputResources, OutputResourcesRect))
	{
		int InputAmmount = FMath::Min(InputResources.Num(), OutputResources.Num());
		for (int InputIt = 0; InputIt < InputAmmount; InputIt++)
		{
			FIntRect SourceRect = InputResourcesRect[InputIt];

			// When resolving without warp, apply overscan
			switch (InputResourceType)
			{
			case EDisplayClusterViewportResourceType::InternalRenderTargetResource:
				if (OverscanSettings.bIsEnabled && CVarDisplayClusterRenderOverscanResolve.GetValueOnRenderThread() != 0)
				{
					// Support overscan crop
					SourceRect = OverscanSettings.OverscanPixels.GetInnerRect(SourceRect);
				}
				break;
			default:
				break;
			}

			if ((InputIt + 1) == InputAmmount)
			{
				// last input mono -> stereo outputs
				for (int OutputIt = InputIt; OutputIt < OutputResources.Num(); OutputIt++)
				{
					ImplResolveResource(RHICmdList, InputResources[InputIt], SourceRect, OutputResources[OutputIt], OutputResourcesRect[OutputIt], bOutputIsMipsResource);
				}
				break;
			}
			else
			{
				ImplResolveResource(RHICmdList, InputResources[InputIt], SourceRect, OutputResources[InputIt], OutputResourcesRect[InputIt], bOutputIsMipsResource);
			}
		}

		return true;
	}

	return false;
}
