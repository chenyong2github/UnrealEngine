// Copyright Epic Games, Inc. All Rights Reserved.

#include "XRRenderTargetManager.h"
#include "Slate/SceneViewport.h"
#include "Widgets/SViewport.h"
#include "IHeadMountedDisplay.h"
#include "IXRTrackingSystem.h"
#include "Engine/Engine.h"
#include "RenderUtils.h"
#include "XRRenderBridge.h"

void FXRRenderTargetManager::CalculateRenderTargetSize(const class FViewport& Viewport, uint32& InOutSizeX, uint32& InOutSizeY)
{
	check(IsInGameThread() || IsInRenderingThread());

	if (GEngine && GEngine->XRSystem.IsValid())
	{
		IHeadMountedDisplay* const HMDDevice = GEngine->XRSystem->GetHMDDevice();
		if (HMDDevice)
		{
			const FIntPoint IdealRenderTargetSize = HMDDevice->GetIdealRenderTargetSize();
			const float PixelDensity = HMDDevice->GetPixelDenity();

			FIntPoint DensityAdjustedTargetSize =
			{
				FMath::CeilToInt(IdealRenderTargetSize.X * PixelDensity),
				FMath::CeilToInt(IdealRenderTargetSize.Y * PixelDensity)
			};

			// We need a custom quantized width here because if we have an atlased texture, each half needs to be aligned.
			// We could modify or overload QuantizeSceneBufferSize, but this is the only call point that needs to fix up 
			// the pixel-density-adjusted width. Strata requires DivBy8, hence aligning to 16.
			// TODO: Would be nice if we could offload the alignment to QSBS by passing in number of atlased views
			constexpr uint32 Mask16 = ~(16 - 1);
			DensityAdjustedTargetSize.X = (DensityAdjustedTargetSize.X + (16 - 1)) & Mask16;

			QuantizeSceneBufferSize(DensityAdjustedTargetSize, DensityAdjustedTargetSize);

			InOutSizeX = DensityAdjustedTargetSize.X;
			InOutSizeY = DensityAdjustedTargetSize.Y;

			check(InOutSizeX != 0 && InOutSizeY != 0);
		}
	}
}

bool FXRRenderTargetManager::NeedReAllocateViewportRenderTarget(const FViewport& Viewport)
{
	check(IsInGameThread());

	if (!ShouldUseSeparateRenderTarget()) // or should this be a check instead, as it is only called when ShouldUseSeparateRenderTarget() returns true?
	{
		return false;
	}

	const FIntPoint ViewportSize = Viewport.GetSizeXY();
	const FIntPoint RenderTargetSize = Viewport.GetRenderTargetTextureSizeXY();

	uint32 NewSizeX = ViewportSize.X;
	uint32 NewSizeY = ViewportSize.Y;
	CalculateRenderTargetSize(Viewport, NewSizeX, NewSizeY);

	return (NewSizeX != RenderTargetSize.X || NewSizeY != RenderTargetSize.Y);
}

void FXRRenderTargetManager::UpdateViewportRHIBridge(bool bUseSeparateRenderTarget, const class FViewport& Viewport, FRHIViewport* const ViewportRHI)
{
	FXRRenderBridge* Bridge = GetActiveRenderBridge_GameThread(bUseSeparateRenderTarget);
	if (Bridge != nullptr)
	{
		Bridge->UpdateViewport(Viewport, ViewportRHI);	
	}
	
	ViewportRHI->SetCustomPresent(Bridge);
}

void FXRRenderTargetManager::UpdateViewport(bool bUseSeparateRenderTarget, const class FViewport& Viewport, class SViewport* ViewportWidget /*= nullptr*/)
{
	check(IsInGameThread());

	if (GIsEditor && ViewportWidget != nullptr && !ViewportWidget->IsStereoRenderingAllowed())
	{
		return;
	}

	FRHIViewport* const ViewportRHI = Viewport.GetViewportRHI().GetReference();
	if (!ViewportRHI)
	{
		return;
	}

	if (ViewportWidget)
	{
		UpdateViewportWidget(bUseSeparateRenderTarget, Viewport, ViewportWidget);
	}

	if (!ShouldUseSeparateRenderTarget())
	{
		if ((!bUseSeparateRenderTarget || GIsEditor) && ViewportRHI)
		{
			ViewportRHI->SetCustomPresent(nullptr);
		}
		return;
	}

	UpdateViewportRHIBridge(bUseSeparateRenderTarget, Viewport, ViewportRHI);
}
