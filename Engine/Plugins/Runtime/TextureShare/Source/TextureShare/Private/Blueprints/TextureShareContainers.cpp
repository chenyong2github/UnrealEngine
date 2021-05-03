// Copyright Epic Games, Inc. All Rights Reserved.

#include "Blueprints/TextureShareContainers.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/Texture2D.h"
#include "Rendering/Texture2DResource.h"

bool FTextureShareBPTexture2D::IsValid() const
{
	return RTT || Texture;
}

FIntPoint FTextureShareBPTexture2D::GetSizeXY() const
{
	if (RTT)
	{ 
		return FIntPoint(RTT->SizeX, RTT->SizeY); 
	}

	if (Texture)
	{ 
		return FIntPoint(Texture->GetSizeX(), Texture->GetSizeY()); 
	}

	return FIntPoint(0, 0);
}

EPixelFormat FTextureShareBPTexture2D::GetFormat() const
{
	if (RTT)
	{
		return RTT->GetFormat();
	}

	if (Texture)
	{
		return Texture->GetPixelFormat();
	}

	return EPixelFormat::PF_Unknown;
}

bool FTextureShareBPTexture2D::GetTexture2DRHI_RenderThread(FTexture2DRHIRef& OutTexture2DRHIRef) const
{
	if (RTT)
	{
		FTextureRenderTargetResource*     SrcRTTRes = RTT ? RTT->GetRenderTargetResource() : nullptr;
		FTextureRenderTarget2DResource* SrcRTTRes2D = SrcRTTRes ? ((FTextureRenderTarget2DResource*)SrcRTTRes) : nullptr;
		if (SrcRTTRes2D)
		{
			OutTexture2DRHIRef = SrcRTTRes2D->GetTextureRHI();
			return OutTexture2DRHIRef.IsValid();
		}

		return false;
	}

	if (Texture)
	{
		FTexture2DResource* Texture2DResource = Texture ? (FTexture2DResource*)Texture->Resource : nullptr;
		if (Texture2DResource)
		{
			OutTexture2DRHIRef = Texture2DResource->GetTexture2DRHI();
			return OutTexture2DRHIRef.IsValid();
		}
	}

	return false;
}

FTextureShareAdditionalData FTextureShareBPAdditionalData::operator*() const
{
	// Forward data from BP
	FTextureShareAdditionalData OutData;

	// Frame info
	OutData.FrameNumber = FrameNumber;

	// Projection matrix
	OutData.PrjMatrix    = PrjMatrix;
	
	// View info
	FTransform ViewTransform(ViewRotation, ViewLocation, ViewScale);
	OutData.ViewMatrix = ViewTransform.ToMatrixWithScale();
	OutData.ViewLocation = ViewLocation;
	OutData.ViewRotation = ViewRotation;
	OutData.ViewScale = ViewScale;

	return OutData;
}

static TMap<ETextureShareBPSyncConnect, ETextureShareSyncConnect> InitBPSyncConnect()
{
	TMap<ETextureShareBPSyncConnect, ETextureShareSyncConnect> RetVal;
	
	RetVal.Add(ETextureShareBPSyncConnect::None, ETextureShareSyncConnect::None);
	RetVal.Add(ETextureShareBPSyncConnect::SyncSession, ETextureShareSyncConnect::SyncSession);
	RetVal.Add(ETextureShareBPSyncConnect::Default, ETextureShareSyncConnect::Default);
	
	return RetVal;
}

static TMap<ETextureShareBPSyncFrame, ETextureShareSyncFrame> InitBPSyncFrame()
{
	TMap<ETextureShareBPSyncFrame, ETextureShareSyncFrame> RetVal;

	RetVal.Add(ETextureShareBPSyncFrame::None, ETextureShareSyncFrame::None);
	RetVal.Add(ETextureShareBPSyncFrame::FrameSync, ETextureShareSyncFrame::FrameSync);
	RetVal.Add(ETextureShareBPSyncFrame::Default, ETextureShareSyncFrame::Default);

	return RetVal;
}


static TMap<ETextureShareBPSyncSurface, ETextureShareSyncSurface> InitBPSyncSurface()
{
	TMap<ETextureShareBPSyncSurface, ETextureShareSyncSurface> RetVal;

	RetVal.Add(ETextureShareBPSyncSurface::None, ETextureShareSyncSurface::None);
	RetVal.Add(ETextureShareBPSyncSurface::SyncRead, ETextureShareSyncSurface::SyncRead);
	RetVal.Add(ETextureShareBPSyncSurface::SyncPairingRead, ETextureShareSyncSurface::SyncPairingRead);
	RetVal.Add(ETextureShareBPSyncSurface::Default, ETextureShareSyncSurface::Default);

	return RetVal;
}

static TMap<ETextureShareBPSyncConnect, ETextureShareSyncConnect> BPSyncConnect = InitBPSyncConnect();
static TMap<ETextureShareBPSyncFrame, ETextureShareSyncFrame>     BPSyncFrame   = InitBPSyncFrame();
static TMap<ETextureShareBPSyncSurface, ETextureShareSyncSurface> BPSyncSurface = InitBPSyncSurface();

FTextureShareBPSyncPolicy::FTextureShareBPSyncPolicy(const FTextureShareSyncPolicy& Init)
{
	const ETextureShareBPSyncConnect* ConnectionValue = BPSyncConnect.FindKey(Init.ConnectionSync);
	Connection = ConnectionValue ? *ConnectionValue : ETextureShareBPSyncConnect::Default;

	const ETextureShareBPSyncFrame* FrameValue = BPSyncFrame.FindKey(Init.FrameSync);
	Frame = FrameValue ? *FrameValue : ETextureShareBPSyncFrame::Default;

	const ETextureShareBPSyncSurface* TextureValue = BPSyncSurface.FindKey(Init.TextureSync);
	Texture = TextureValue ? *TextureValue : ETextureShareBPSyncSurface::Default;
}

FTextureShareSyncPolicy FTextureShareBPSyncPolicy::operator*() const
{
	FTextureShareSyncPolicy RetVal;
	RetVal.ConnectionSync = BPSyncConnect[Connection];
	RetVal.FrameSync      = BPSyncFrame[Frame];
	RetVal.TextureSync    = BPSyncSurface[Texture];

	return RetVal;
}

FTextureShareBPTimeOut::FTextureShareBPTimeOut(const FTextureShareTimeOut& Init)
	: ConnectionSync(Init.ConnectionSync)
	, FrameSync(Init.FrameSync)
	, TexturePairingSync(Init.TexturePairingSync)
	, TextureResourceSync(Init.TextureResourceSync)
	, TextureSync(Init.TextureSync)
{}

FTextureShareTimeOut FTextureShareBPTimeOut::operator*() const
{
	FTextureShareTimeOut OutData;

	OutData.ConnectionSync = ConnectionSync;
	OutData.FrameSync = FrameSync;
	OutData.TexturePairingSync = TexturePairingSync;
	OutData.TextureResourceSync = TextureResourceSync;
	OutData.TextureSync = TextureSync;

	return OutData;
}

FTextureShareBPSyncPolicySettings::FTextureShareBPSyncPolicySettings(const FTextureShareSyncPolicySettings& Init)
	: DefaultSyncPolicy(Init.DefaultSyncPolicy)
	, TimeOut(Init.TimeOut)
{}

FTextureShareSyncPolicySettings FTextureShareBPSyncPolicySettings::operator*() const
{
	FTextureShareSyncPolicySettings OutData;

	OutData.DefaultSyncPolicy = *DefaultSyncPolicy;
	OutData.TimeOut = *TimeOut;

	return OutData;
}

