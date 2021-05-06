// Copyright Epic Games, Inc. All Rights Reserved.

#include "Render/Viewport/Containers/ImplDisplayClusterViewport_Overscan.h"
#include "Render/Viewport/DisplayClusterViewport.h"



static TAutoConsoleVariable<int> CVarDisplayClusterRenderOverscanEnable(
	TEXT("nDisplay.render.overscan.enable"),
	1,
	TEXT("Enable overscan feature.\n")
	TEXT(" 0 - to disable.\n"),
	ECVF_RenderThreadSafe
);

///////////////////////////////////////////////////////////////////////////////////////
//          FImplDisplayClusterViewport_Overscan
///////////////////////////////////////////////////////////////////////////////////////
inline float ImplClampPercent(float InValue)
{
	// @todo: check range, now limits is maximal 0..100%
	// change max value 1 to lower
	static const float MaxOverscanValue = 1.f;

	return FMath::Clamp(InValue, 0.f, MaxOverscanValue);
}

bool FImplDisplayClusterViewport_Overscan::UpdateProjectionAngles(float& InOutLeft, float& InOutRight, float& InOutTop, float& InOutBottom)
{
	if (RuntimeSettings.bIsEnabled)
	{
		float dh = InOutRight - InOutLeft;
		float dv = InOutTop - InOutBottom;

		InOutLeft   -= dh * RuntimeSettings.OverscanPercent.Left;
		InOutRight  += dh * RuntimeSettings.OverscanPercent.Right;
		InOutBottom -= dv * RuntimeSettings.OverscanPercent.Bottom;
		InOutTop    += dv * RuntimeSettings.OverscanPercent.Top;

		return true;
	}

	return false;
}

void FImplDisplayClusterViewport_Overscan::Update(FDisplayClusterViewport& Viewport, FIntRect& InOutRenderTargetRect)
{
	// Disable overscane feature from console
	if (CVarDisplayClusterRenderOverscanEnable.GetValueOnGameThread() == 0)
	{
		return;
	}

	const FIntPoint Size = InOutRenderTargetRect.Size();

	switch (OverscanSettings.Mode)
	{
	case EDisplayClusterViewport_OverscanMode::Percent:
	{
		RuntimeSettings.bIsEnabled = true;

		RuntimeSettings.OverscanPercent.Left   = ImplClampPercent(OverscanSettings.Left);
		RuntimeSettings.OverscanPercent.Right  = ImplClampPercent(OverscanSettings.Right);
		RuntimeSettings.OverscanPercent.Top    = ImplClampPercent(OverscanSettings.Top);
		RuntimeSettings.OverscanPercent.Bottom = ImplClampPercent(OverscanSettings.Bottom);
		break;
	}

	case EDisplayClusterViewport_OverscanMode::Pixels:
	{
		RuntimeSettings.bIsEnabled = true;

		RuntimeSettings.OverscanPercent.Left   = ImplClampPercent(OverscanSettings.Left   / Size.X);
		RuntimeSettings.OverscanPercent.Right  = ImplClampPercent(OverscanSettings.Right  / Size.X);
		RuntimeSettings.OverscanPercent.Top    = ImplClampPercent(OverscanSettings.Top    / Size.Y);
		RuntimeSettings.OverscanPercent.Bottom = ImplClampPercent(OverscanSettings.Bottom / Size.Y);

		break;
	}

	default:
		break;
	}

	// Update RTT size for overscan
	if (RuntimeSettings.bIsEnabled)
	{
		// Calc pixels from percent
		RuntimeSettings.OverscanPixels.Left   = Size.X * RuntimeSettings.OverscanPercent.Left;
		RuntimeSettings.OverscanPixels.Right  = Size.X * RuntimeSettings.OverscanPercent.Right;
		RuntimeSettings.OverscanPixels.Top    = Size.Y * RuntimeSettings.OverscanPercent.Top;
		RuntimeSettings.OverscanPixels.Bottom = Size.Y * RuntimeSettings.OverscanPercent.Bottom;

		FIntPoint OverscanSize = Size + RuntimeSettings.OverscanPixels.Size();
		FIntPoint ValidOverscanSize = Viewport.GetValidRect(FIntRect(FIntPoint(0, 0), OverscanSize), TEXT("Overscan")).Size();

		if (OverscanSize != ValidOverscanSize)
		{
			// can't use overscan with extra size, disable oversize
			OverscanSettings.bOversize = false;
		}

		if (OverscanSettings.bOversize)
		{
			InOutRenderTargetRect.Max = OverscanSize;
		}
		else
		{
			float scaleX = float(Size.X) / OverscanSize.X;
			float scaleY = float(Size.Y) / OverscanSize.Y;

			RuntimeSettings.OverscanPixels.Left   *= scaleX;
			RuntimeSettings.OverscanPixels.Right  *= scaleX;
			RuntimeSettings.OverscanPixels.Top    *= scaleY;
			RuntimeSettings.OverscanPixels.Bottom *= scaleY;
		}
	}
}

