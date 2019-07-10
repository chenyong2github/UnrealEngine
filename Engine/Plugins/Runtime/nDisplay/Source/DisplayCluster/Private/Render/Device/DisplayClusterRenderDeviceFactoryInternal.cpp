// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Render/Device/DisplayClusterRenderDeviceFactoryInternal.h"

#include "Render/Device/Monoscopic/DisplayClusterDeviceMonoscopicDX11.h"
#include "Render/Device/Monoscopic/DisplayClusterDeviceMonoscopicDX12.h"
#include "Render/Device/Monoscopic/DisplayClusterDeviceMonoscopicOpenGL.h"
#include "Render/Device/QuadBufferStereo/DisplayClusterDeviceQuadBufferStereoDX11.h"
#include "Render/Device/QuadBufferStereo/DisplayClusterDeviceQuadBufferStereoDX12.h"
#include "Render/Device/QuadBufferStereo/DisplayClusterDeviceQuadBufferStereoOpengl.h"
#include "Render/Device/SideBySide/DisplayClusterDeviceSideBySideDX11.h"
#include "Render/Device/SideBySide/DisplayClusterDeviceSideBySideDX12.h"
#include "Render/Device/SideBySide/DisplayClusterDeviceSideBySideOpenGL.h"
#include "Render/Device/TopBottom/DisplayClusterDeviceTopBottomDX11.h"
#include "Render/Device/TopBottom/DisplayClusterDeviceTopBottomDX12.h"
#include "Render/Device/TopBottom/DisplayClusterDeviceTopBottomOpenGL.h"

#include "DisplayClusterLog.h"
#include "DisplayClusterStrings.h"


FDisplayClusterRenderDeviceFactoryInternal::FDisplayClusterRenderDeviceFactoryInternal()
{
}

FDisplayClusterRenderDeviceFactoryInternal::~FDisplayClusterRenderDeviceFactoryInternal()
{
}

TSharedPtr<IDisplayClusterRenderDevice, ESPMode::ThreadSafe> FDisplayClusterRenderDeviceFactoryInternal::Create(const FString& InDeviceType, const FString& InRHIName)
{
	// Monoscopic
	if (InDeviceType.Compare(DisplayClusterStrings::args::dev::Mono, ESearchCase::IgnoreCase) == 0)
	{
		if (InRHIName.Compare(DisplayClusterStrings::rhi::D3D11, ESearchCase::IgnoreCase) == 0)
		{
			UE_LOG(LogDisplayClusterRender, Log, TEXT("Instantiating DX11 monoscopic device..."));
			return MakeShareable(new FDisplayClusterDeviceMonoscopicDX11);
		}
		else if (InRHIName.Compare(DisplayClusterStrings::rhi::D3D12, ESearchCase::IgnoreCase) == 0)
		{
			UE_LOG(LogDisplayClusterRender, Log, TEXT("Instantiating DX12 monoscopic device..."));
			return MakeShareable(new FDisplayClusterDeviceMonoscopicDX12);
		}
		else if (InRHIName.Compare(DisplayClusterStrings::rhi::OpenGL, ESearchCase::IgnoreCase) == 0)
		{
			UE_LOG(LogDisplayClusterRender, Log, TEXT("Instantiating OpenGL monoscopic device..."));
			return MakeShareable(new FDisplayClusterDeviceMonoscopicOpenGL);
		}
	}
	// Quad buffer stereo
	else if (InDeviceType.Compare(DisplayClusterStrings::args::dev::QBS, ESearchCase::IgnoreCase) == 0)
	{
		if (InRHIName.Compare(DisplayClusterStrings::rhi::D3D11, ESearchCase::IgnoreCase) == 0)
		{
			UE_LOG(LogDisplayClusterRender, Log, TEXT("Instantiating D3D11 quad buffer stereo device..."));
			return MakeShareable(new FDisplayClusterDeviceQuadBufferStereoDX11);
		}
		else if (InRHIName.Compare(DisplayClusterStrings::rhi::D3D12, ESearchCase::IgnoreCase) == 0)
		{
			UE_LOG(LogDisplayClusterRender, Log, TEXT("Instantiating D3D12 quad buffer stereo device..."));
			return MakeShareable(new FDisplayClusterDeviceQuadBufferStereoDX12);
		}
		else if (InRHIName.Compare(DisplayClusterStrings::rhi::OpenGL, ESearchCase::IgnoreCase) == 0)
		{
			UE_LOG(LogDisplayClusterRender, Log, TEXT("Instantiating OpenGL quad buffer stereo device..."));
			return  MakeShareable(new FDisplayClusterDeviceQuadBufferStereoOpenGL);
		}
	}
	// Side-by-side
	else if (InDeviceType.Compare(DisplayClusterStrings::args::dev::SbS, ESearchCase::IgnoreCase) == 0)
	{
		if (InRHIName.Compare(DisplayClusterStrings::rhi::D3D11, ESearchCase::IgnoreCase) == 0)
		{
			UE_LOG(LogDisplayClusterRender, Log, TEXT("Instantiating D3D11 side-by-side stereo device..."));
			return MakeShareable(new FDisplayClusterDeviceSideBySideDX11);
		}
		else if (InRHIName.Compare(DisplayClusterStrings::rhi::D3D12, ESearchCase::IgnoreCase) == 0)
		{
			UE_LOG(LogDisplayClusterRender, Log, TEXT("Instantiating D3D12 side-by-side stereo device..."));
			return MakeShareable(new FDisplayClusterDeviceSideBySideDX12);
		}
		else if (InRHIName.Compare(DisplayClusterStrings::rhi::OpenGL, ESearchCase::IgnoreCase) == 0)
		{
			UE_LOG(LogDisplayClusterRender, Log, TEXT("Instantiating OpenGL side-by-side stereo device..."));
			return MakeShareable(new FDisplayClusterDeviceSideBySideOpenGL);
		}
	}
	// Top-bottom
	else if (InDeviceType.Compare(DisplayClusterStrings::args::dev::TB, ESearchCase::IgnoreCase) == 0)
	{
		if (InRHIName.Compare(DisplayClusterStrings::rhi::D3D11, ESearchCase::IgnoreCase) == 0)
		{
			UE_LOG(LogDisplayClusterRender, Log, TEXT("Instantiating D3D11 top-bottom stereo device..."));
			return MakeShareable(new FDisplayClusterDeviceTopBottomDX11);
		}
		else if (InRHIName.Compare(DisplayClusterStrings::rhi::D3D12, ESearchCase::IgnoreCase) == 0)
		{
			UE_LOG(LogDisplayClusterRender, Log, TEXT("Instantiating D3D12 top-bottom stereo device..."));
			return MakeShareable(new FDisplayClusterDeviceTopBottomDX12);
		}
		else if (InRHIName.Compare(DisplayClusterStrings::rhi::OpenGL, ESearchCase::IgnoreCase) == 0)
		{
			UE_LOG(LogDisplayClusterRender, Log, TEXT("Instantiating OpenGL top-bottom stereo device..."));
			return MakeShareable(new FDisplayClusterDeviceTopBottomOpenGL);
		}
	}

	UE_LOG(LogDisplayClusterRender, Warning, TEXT("An internal rendering device factory couldn't create a device %s:%s"), *InRHIName, *InDeviceType);

	return nullptr;
}
