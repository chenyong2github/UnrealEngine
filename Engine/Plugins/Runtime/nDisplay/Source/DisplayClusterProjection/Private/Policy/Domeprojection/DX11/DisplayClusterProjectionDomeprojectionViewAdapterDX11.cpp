// Copyright Epic Games, Inc. All Rights Reserved.

#include "Policy/Domeprojection/DX11/DisplayClusterProjectionDomeprojectionViewAdapterDX11.h"
#include "Policy/Domeprojection/DX11/DisplayClusterProjectionDomeprojectionLibraryDX11.h"

#include "DisplayClusterProjectionLog.h"
#include "Misc/DisplayClusterHelpers.h"

#include "RHI.h"
#include "RHIResources.h"
#include "RHIUtilities.h"

#include "Windows/D3D11RHI/Private/D3D11RHIPrivate.h"

#include "Engine/GameViewportClient.h"
#include "Engine/Engine.h"
#include "Engine/RendererSettings.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

#define DP_PLUGIN_ID 12


FDisplayClusterProjectionDomeprojectionViewAdapterDX11::FDisplayClusterProjectionDomeprojectionViewAdapterDX11(const FDisplayClusterProjectionDomeprojectionViewAdapterBase::FInitParams& InitParams)
	: FDisplayClusterProjectionDomeprojectionViewAdapterBase(InitParams)
	, bIsRenderResourcesInitialized(false)
	, Context(nullptr)
{
	check(InitParams.NumViews > 0);

	Views.AddDefaulted(InitParams.NumViews);
	for (auto& View : Views)
	{
		View.ViewportSize = InitParams.ViewportSize;
	}
}

FDisplayClusterProjectionDomeprojectionViewAdapterDX11::~FDisplayClusterProjectionDomeprojectionViewAdapterDX11()
{
	DisplayClusterProjectionDomeprojectionLibraryDX11::dpDestroyContextFunc(Context);
}

bool FDisplayClusterProjectionDomeprojectionViewAdapterDX11::Initialize(const FString& File)
{
	// Initialize Domeprojection DLL API
	if (!DisplayClusterProjectionDomeprojectionLibraryDX11::Initialize())
	{
		UE_LOG(LogDisplayClusterProjectionDomeprojection, Error, TEXT("Couldn't link to the Domeprojection DLL"));
		return false;
	}

	check(GDynamicRHI);
	FD3D11DynamicRHI* d3d11RHI = static_cast<FD3D11DynamicRHI*>(GDynamicRHI);

	dpResult Result = DisplayClusterProjectionDomeprojectionLibraryDX11::dpCreateContextFunc(&Context, d3d11RHI->GetDevice(), DP_PLUGIN_ID);
	if (Result != dpNoError)
	{
		UE_LOG(LogDisplayClusterProjectionDomeprojection, Error, TEXT("Couldn't initialize Domeprojection context"));
		return false;
	}

	// Check if configuration file exists
	if (!FPaths::FileExists(File))
	{
		UE_LOG(LogDisplayClusterProjectionDomeprojection, Error, TEXT("File '%s' not found"), *File);
		return false;
	}

	// Load the configuration
	Result = DisplayClusterProjectionDomeprojectionLibraryDX11::dpLoadConfigurationFromFileFunc(Context, TCHAR_TO_ANSI(*File));
	if (Result != dpNoError)
	{
		UE_LOG(LogDisplayClusterProjectionDomeprojection, Error, TEXT("Loading configuration from %s failed with %d"), *File, Result);
		return false;
	}

	return true;
}

// Location/Rotation inside the function is in Domeprojection space
bool FDisplayClusterProjectionDomeprojectionViewAdapterDX11::CalculateView(const uint32 ViewIdx, const uint32 Channel, FVector& InOutViewLocation, FRotator& InOutViewRotation, const FVector& ViewOffset, const float WorldToMeters, const float NCP, const float FCP)
{
	check(Views.Num() > (int)ViewIdx);

	ZNear = NCP;
	ZFar  = FCP;

	const float WorldScale = WorldToMeters / 1000.0f; // we use mm

	dpVec3f Eyepoint(InOutViewLocation.Y / WorldScale, InOutViewLocation.Z / WorldScale, -InOutViewLocation.X / WorldScale);
	dpVec3f Orientation;

	{
		FScopeLock lock(&DllAccessCS);

		check(GDynamicRHI);
		FD3D11DynamicRHI* d3d11RHI = static_cast<FD3D11DynamicRHI*>(GDynamicRHI);

		check(DisplayClusterProjectionDomeprojectionLibraryDX11::dpSetActiveChannelFunc);
		DisplayClusterProjectionDomeprojectionLibraryDX11::dpSetActiveChannelFunc(Context, Channel, d3d11RHI->GetDevice(), Views[ViewIdx].ViewportSize.X, Views[ViewIdx].ViewportSize.Y);

		check(DisplayClusterProjectionDomeprojectionLibraryDX11::dpPreDrawFunc);
		DisplayClusterProjectionDomeprojectionLibraryDX11::dpPreDrawFunc(Context, Eyepoint, &Views[ViewIdx].Camera);

		check(DisplayClusterProjectionDomeprojectionLibraryDX11::dpGetOrientationFunc);
		DisplayClusterProjectionDomeprojectionLibraryDX11::dpGetOrientationFunc(Views[ViewIdx].Camera.dir, Views[ViewIdx].Camera.up, &Orientation);
	}

	// Forward location to a caller
	InOutViewLocation = FVector(-Views[ViewIdx].Camera.position.z * WorldScale, Views[ViewIdx].Camera.position.x * WorldScale, Views[ViewIdx].Camera.position.y * WorldScale);

	// Forward view rotation to a caller
	InOutViewRotation = FRotator(Orientation.y, Orientation.x, -Orientation.z);

	return true;
}

bool FDisplayClusterProjectionDomeprojectionViewAdapterDX11::GetProjectionMatrix(const uint32 ViewIdx, const uint32 Channel, FMatrix& OutPrjMatrix)
{
	check(Views.Num() > (int)ViewIdx);

	// Build Projection matrix:
	const float Left   = Views[ViewIdx].Camera.tanLeft;
	const float Right  = Views[ViewIdx].Camera.tanRight;
	const float Bottom = Views[ViewIdx].Camera.tanBottom;
	const float Top    = Views[ViewIdx].Camera.tanTop;

	OutPrjMatrix = DisplayClusterHelpers::math::GetProjectionMatrixFromOffsets(Left, Right, Top, Bottom, ZNear, ZFar);

	return true;
}

bool FDisplayClusterProjectionDomeprojectionViewAdapterDX11::ApplyWarpBlend_RenderThread(const uint32 ViewIdx, const uint32 Channel, FRHICommandListImmediate& RHICmdList, FRHITexture2D* SrcTexture, const FIntRect& ViewportRect)
{
	check(IsInRenderingThread());
	check(Views.Num() > (int)ViewIdx);

	if (!InitializeResources_RenderThread())
	{
		return false;
	}

	if (!GEngine)
	{
		return false;
	}

	FD3D11DynamicRHI* d3d11RHI = static_cast<FD3D11DynamicRHI*>(GDynamicRHI);
	FViewport* MainViewport = GEngine->GameViewport->Viewport;
	if (d3d11RHI==nullptr || MainViewport==nullptr)
	{
		return false;
	}

	// Copy the requested region to a temporary texture
	LoadViewportTexture_RenderThread(ViewIdx, RHICmdList, SrcTexture, ViewportRect);

	// Prepare the textures
	FD3D11TextureBase* DstTextureRHI = static_cast<FD3D11TextureBase*>(Views[ViewIdx].TargetableTexture->GetTextureBaseRHI());
	FD3D11TextureBase* SrcTextureRHI = static_cast<FD3D11TextureBase*>(Views[ViewIdx].ShaderResourceTexture->GetTextureBaseRHI());

	ID3D11RenderTargetView* DstTextureRTV = DstTextureRHI->GetRenderTargetView(0, -1);

	ID3D11Texture2D * DstTextureD3D11 = static_cast<ID3D11Texture2D*>(DstTextureRHI->GetResource());
	ID3D11ShaderResourceView * SrcTextureD3D11 = static_cast<ID3D11ShaderResourceView*>(SrcTextureRHI->GetShaderResourceView());

	D3D11_VIEWPORT RenderViewportData;
	RenderViewportData.MinDepth = 0.0f;
	RenderViewportData.MaxDepth = 1.0f;
	RenderViewportData.Width = static_cast<float>(GetViewportSize().X);
	RenderViewportData.Height = static_cast<float>(GetViewportSize().Y);
	RenderViewportData.TopLeftX = 0.0f;
	RenderViewportData.TopLeftY = 0.0f;

	d3d11RHI->GetDeviceContext()->RSSetViewports(1, &RenderViewportData);
	d3d11RHI->GetDeviceContext()->OMSetRenderTargets(1, &DstTextureRTV, nullptr);
	d3d11RHI->GetDeviceContext()->Flush();

	// perform warp/blend
	{
		FScopeLock lock(&DllAccessCS);

		check(DisplayClusterProjectionDomeprojectionLibraryDX11::dpPostDrawFunc);
		dpResult Result = DisplayClusterProjectionDomeprojectionLibraryDX11::dpPostDrawFunc(Context, SrcTextureD3D11, d3d11RHI->GetDeviceContext());
		if (Result != dpNoError)
		{
			UE_LOG(LogDisplayClusterProjectionDomeprojection, Error, TEXT("Domeprojection couldn't perform rendering operation"));
			return false;
		}
	}

	// Copy results back to our render target
	SaveViewportTexture_RenderThread(ViewIdx, RHICmdList, SrcTexture, ViewportRect);

	return true;
}

bool FDisplayClusterProjectionDomeprojectionViewAdapterDX11::InitializeResources_RenderThread()
{
	check(IsInRenderingThread());

	if (!bIsRenderResourcesInitialized)
	{
		FScopeLock lock(&RenderingResourcesInitializationCS);
		if (!bIsRenderResourcesInitialized)
		{
			static const TConsoleVariableData<int32>* CVarDefaultBackBufferPixelFormat = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.DefaultBackBufferPixelFormat"));
			static const EPixelFormat SceneTargetFormat = EDefaultBackBufferPixelFormat::Convert2PixelFormat(EDefaultBackBufferPixelFormat::FromInt(CVarDefaultBackBufferPixelFormat->GetValueOnRenderThread()));

			check(GDynamicRHI);
			check(GEngine);
			check(GEngine->GameViewport);

			FD3D11DynamicRHI* d3d11RHI = static_cast<FD3D11DynamicRHI*>(GDynamicRHI);
			FViewport* MainViewport = GEngine->GameViewport->Viewport;

			if (d3d11RHI && MainViewport)
			{
				FD3D11Device*                 Device = d3d11RHI->GetDevice();
				FD3D11DeviceContext*   DeviceContext = d3d11RHI->GetDeviceContext();

				check(Device);
				check(DeviceContext);

				FD3D11Viewport* Viewport = static_cast<FD3D11Viewport*>(MainViewport->GetViewportRHI().GetReference());
				IDXGISwapChain*  SwapChain = (IDXGISwapChain*)Viewport->GetSwapChain();

				check(Viewport);
				check(SwapChain);

				// Create RT texture for viewport warp
				for (int32 i = 0; i < Views.Num(); i++)
				{
					FRHIResourceCreateInfo CreateInfo;
					FTexture2DRHIRef DummyTexRef;

					RHICreateTargetableShaderResource2D(GetViewportSize().X, GetViewportSize().Y, SceneTargetFormat, 1, TexCreate_None, TexCreate_RenderTargetable, true, CreateInfo, Views[i].TargetableTexture, Views[i].ShaderResourceTexture);

					// Initialize Domeprojection internals
					check(DisplayClusterProjectionDomeprojectionLibraryDX11::dpSetActiveChannelFunc);
					dpResult Result = DisplayClusterProjectionDomeprojectionLibraryDX11::dpSetActiveChannelFunc(Context, i, Device, GetViewportSize().X, GetViewportSize().Y);
					if (Result != dpNoError)
					{
						UE_LOG(LogDisplayClusterProjectionDomeprojection, Error, TEXT("Couldn't initialize Domeprojection Device/DeviceContext/SwapChain"));
						return false;
					}
				}

				// Here we set initialization flag. In case we couldn't initialize Domeprojection device objects, we won't do it again.
				// However, the per-view bIsInitialized flag must be tested before call any Domeprojection function.
				bIsRenderResourcesInitialized = true;
			}
		}
	}

	return bIsRenderResourcesInitialized;
}

void FDisplayClusterProjectionDomeprojectionViewAdapterDX11::LoadViewportTexture_RenderThread(const uint32 ViewIdx, FRHICommandListImmediate& RHICmdList, FRHITexture2D* SrcTexture, const FIntRect& ViewportRect)
{
	check(IsInRenderingThread());

	FResolveParams copyParams;
	copyParams.DestArrayIndex = 0;
	copyParams.SourceArrayIndex = 0;

	copyParams.Rect.X1 = ViewportRect.Min.X;
	copyParams.Rect.X2 = ViewportRect.Max.X;

	copyParams.Rect.Y1 = ViewportRect.Min.Y;
	copyParams.Rect.Y2 = ViewportRect.Max.Y;

	copyParams.DestRect.X1 = 0;
	copyParams.DestRect.X2 = GetViewportSize().X;

	copyParams.DestRect.Y1 = 0;
	copyParams.DestRect.Y2 = GetViewportSize().Y;

	RHICmdList.CopyToResolveTarget(SrcTexture, Views[ViewIdx].ShaderResourceTexture, copyParams);

	RHICmdList.ImmediateFlush(EImmediateFlushType::FlushRHIThreadFlushResources);
}

void FDisplayClusterProjectionDomeprojectionViewAdapterDX11::SaveViewportTexture_RenderThread(const uint32 ViewIdx, FRHICommandListImmediate& RHICmdList, FRHITexture2D* DstTexture, const FIntRect& ViewportRect)
{
	check(IsInRenderingThread());

	RHICmdList.ImmediateFlush(EImmediateFlushType::FlushRHIThreadFlushResources);

	FResolveParams copyParams;
	copyParams.DestArrayIndex = 0;
	copyParams.SourceArrayIndex = 0;

	copyParams.Rect.X1 = 0;
	copyParams.Rect.X2 = GetViewportSize().X;
	copyParams.Rect.Y1 = 0;
	copyParams.Rect.Y2 = GetViewportSize().Y;

	copyParams.DestRect.X1 = ViewportRect.Min.X;
	copyParams.DestRect.X2 = ViewportRect.Max.X;

	copyParams.DestRect.Y1 = ViewportRect.Min.Y;
	copyParams.DestRect.Y2 = ViewportRect.Max.Y;

	RHICmdList.CopyToResolveTarget(Views[ViewIdx].TargetableTexture, DstTexture, copyParams);
}
