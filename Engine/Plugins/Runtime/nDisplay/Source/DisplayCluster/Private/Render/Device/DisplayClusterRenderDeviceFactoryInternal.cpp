// Copyright Epic Games, Inc. All Rights Reserved.

#include "Render/Device/DisplayClusterRenderDeviceFactoryInternal.h"

#include "Render/Device/Monoscopic/DisplayClusterDeviceMonoscopicDX11.h"
#include "Render/Device/Monoscopic/DisplayClusterDeviceMonoscopicDX12.h"
#include "Render/Device/QuadBufferStereo/DisplayClusterDeviceQuadBufferStereoDX11.h"
#include "Render/Device/QuadBufferStereo/DisplayClusterDeviceQuadBufferStereoDX12.h"
#include "Render/Device/SideBySide/DisplayClusterDeviceSideBySideDX11.h"
#include "Render/Device/SideBySide/DisplayClusterDeviceSideBySideDX12.h"
#include "Render/Device/TopBottom/DisplayClusterDeviceTopBottomDX11.h"
#include "Render/Device/TopBottom/DisplayClusterDeviceTopBottomDX12.h"

#include "Misc/DisplayClusterLog.h"
#include "Misc/DisplayClusterStrings.h"


FDisplayClusterRenderDeviceFactoryInternal::FDisplayClusterRenderDeviceFactoryInternal()
{
}

FDisplayClusterRenderDeviceFactoryInternal::~FDisplayClusterRenderDeviceFactoryInternal()
{
}

TSharedPtr<IDisplayClusterRenderDevice, ESPMode::ThreadSafe> FDisplayClusterRenderDeviceFactoryInternal::Create(const FString& InDeviceType, const FString& InRHIName)
{
	// Monoscopic
	if (InDeviceType.Equals(DisplayClusterStrings::args::dev::Mono, ESearchCase::IgnoreCase))
	{
		if (InRHIName.Equals(DisplayClusterStrings::rhi::D3D11, ESearchCase::IgnoreCase))
		{
			UE_LOG(LogDisplayClusterRender, Log, TEXT("Instantiating DX11 monoscopic device..."));
			return MakeShared<FDisplayClusterDeviceMonoscopicDX11, ESPMode::ThreadSafe>();
		}
		else if (InRHIName.Equals(DisplayClusterStrings::rhi::D3D12, ESearchCase::IgnoreCase))
		{
			UE_LOG(LogDisplayClusterRender, Log, TEXT("Instantiating DX12 monoscopic device..."));
			return MakeShared<FDisplayClusterDeviceMonoscopicDX12, ESPMode::ThreadSafe>();
		}
	}
	// Quad buffer stereo
	else if (InDeviceType.Equals(DisplayClusterStrings::args::dev::QBS, ESearchCase::IgnoreCase))
	{
		if (InRHIName.Equals(DisplayClusterStrings::rhi::D3D11, ESearchCase::IgnoreCase))
		{
			UE_LOG(LogDisplayClusterRender, Log, TEXT("Instantiating D3D11 quad buffer stereo device..."));
			return MakeShared<FDisplayClusterDeviceQuadBufferStereoDX11, ESPMode::ThreadSafe>();
		}
		else if (InRHIName.Equals(DisplayClusterStrings::rhi::D3D12, ESearchCase::IgnoreCase))
		{
			UE_LOG(LogDisplayClusterRender, Log, TEXT("Instantiating D3D12 quad buffer stereo device..."));
			return MakeShared<FDisplayClusterDeviceQuadBufferStereoDX12, ESPMode::ThreadSafe>();
		}
	}
	// Side-by-side
	else if (InDeviceType.Equals(DisplayClusterStrings::args::dev::SbS, ESearchCase::IgnoreCase))
	{
		if (InRHIName.Equals(DisplayClusterStrings::rhi::D3D11, ESearchCase::IgnoreCase))
		{
			UE_LOG(LogDisplayClusterRender, Log, TEXT("Instantiating D3D11 side-by-side stereo device..."));
			return MakeShared<FDisplayClusterDeviceSideBySideDX11, ESPMode::ThreadSafe>();
		}
		else if (InRHIName.Equals(DisplayClusterStrings::rhi::D3D12, ESearchCase::IgnoreCase))
		{
			UE_LOG(LogDisplayClusterRender, Log, TEXT("Instantiating D3D12 side-by-side stereo device..."));
			return MakeShared<FDisplayClusterDeviceSideBySideDX12, ESPMode::ThreadSafe>();
		}
	}
	// Top-bottom
	else if (InDeviceType.Equals(DisplayClusterStrings::args::dev::TB, ESearchCase::IgnoreCase))
	{
		if (InRHIName.Equals(DisplayClusterStrings::rhi::D3D11, ESearchCase::IgnoreCase))
		{
			UE_LOG(LogDisplayClusterRender, Log, TEXT("Instantiating D3D11 top-bottom stereo device..."));
			return MakeShared<FDisplayClusterDeviceTopBottomDX11, ESPMode::ThreadSafe>();
		}
		else if (InRHIName.Equals(DisplayClusterStrings::rhi::D3D12, ESearchCase::IgnoreCase))
		{
			UE_LOG(LogDisplayClusterRender, Log, TEXT("Instantiating D3D12 top-bottom stereo device..."));
			return MakeShared<FDisplayClusterDeviceTopBottomDX12, ESPMode::ThreadSafe>();
		}
	}

	UE_LOG(LogDisplayClusterRender, Warning, TEXT("An internal rendering device factory couldn't create a device %s:%s"), *InRHIName, *InDeviceType);

	return nullptr;
}
