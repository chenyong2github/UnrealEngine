// Copyright Epic Games, Inc. All Rights Reserved.

#include "Policy/Domeprojection/Windows/DX11/DisplayClusterProjectionDomeprojectionViewAdapterDX11.h"
#include "Policy/Domeprojection/Windows/DX11/DisplayClusterProjectionDomeprojectionLibraryDX11.h"
#include "Policy/DisplayClusterProjectionPolicyBase.h"

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

#include "Render/Viewport/IDisplayClusterViewport.h"
#include "Render/Viewport/IDisplayClusterViewportProxy.h"

#define DP_PLUGIN_ID 12

FDisplayClusterProjectionDomeprojectionViewAdapterDX11::FDisplayClusterProjectionDomeprojectionViewAdapterDX11(const FDisplayClusterProjectionDomeprojectionViewAdapterBase::FInitParams& InitParams)
	: FDisplayClusterProjectionDomeprojectionViewAdapterBase(InitParams)
{
	check(InitParams.NumViews > 0);

	Views.AddDefaulted(InitParams.NumViews);
}

FDisplayClusterProjectionDomeprojectionViewAdapterDX11::~FDisplayClusterProjectionDomeprojectionViewAdapterDX11()
{
	for (FViewData& It : Views)
	{
		It.Release(DllAccessCS);
	}

	Views.Empty();
}

bool FDisplayClusterProjectionDomeprojectionViewAdapterDX11::Initialize(const FString& InFile)
{
	bool bResult = true;

	for (FViewData& ViewIt : Views)
	{
		if (!ViewIt.Initialize(InFile, DllAccessCS))
		{
			bResult = false;
		}
	}

	return bResult;
}

// Location/Rotation inside the function is in Domeprojection space
bool FDisplayClusterProjectionDomeprojectionViewAdapterDX11::CalculateView(IDisplayClusterViewport* InViewport, const uint32 InContextNum, const uint32 Channel, FVector& InOutViewLocation, FRotator& InOutViewRotation, const FVector& ViewOffset, const float WorldToMeters, const float NCP, const float FCP)
{
	check(GDynamicRHI);

	FD3D11DynamicRHI* d3d11RHI = static_cast<FD3D11DynamicRHI*>(GDynamicRHI);
	ID3D11Device* D3D11Device = d3d11RHI ? d3d11RHI->GetDevice() : nullptr;
	if (!D3D11Device)
	{
		return false;
	}

	if (InContextNum >= (uint32)(Views.Num()))
	{
		Views.AddDefaulted(InContextNum - Views.Num() + 1);
	}

	ZNear = NCP;
	ZFar  = FCP;

	const auto InContext = InViewport->GetContexts()[InContextNum];
	const auto InViewportSize = InContext.ContextSize;

	const float WorldScale = WorldToMeters / 1000.0f; // we use mm

	dpVec3f Eyepoint(InOutViewLocation.Y / WorldScale, InOutViewLocation.Z / WorldScale, -InOutViewLocation.X / WorldScale);
	dpVec3f Orientation;

	{
		FScopeLock lock(&DllAccessCS);

		check(DisplayClusterProjectionDomeprojectionLibraryDX11::dpSetActiveChannelFunc);
		DisplayClusterProjectionDomeprojectionLibraryDX11::dpSetActiveChannelFunc(Views[InContextNum].Context, Channel, D3D11Device, InViewportSize.X, InViewportSize.Y);

		check(DisplayClusterProjectionDomeprojectionLibraryDX11::dpPreDrawFunc);
		DisplayClusterProjectionDomeprojectionLibraryDX11::dpPreDrawFunc(Views[InContextNum].Context, Eyepoint, &Views[InContextNum].Camera);

		check(DisplayClusterProjectionDomeprojectionLibraryDX11::dpGetOrientationFunc);
		DisplayClusterProjectionDomeprojectionLibraryDX11::dpGetOrientationFunc(Views[InContextNum].Camera.dir, Views[InContextNum].Camera.up, &Orientation);
	}

	// Forward location to a caller
	InOutViewLocation = FVector(-Views[InContextNum].Camera.position.z * WorldScale, Views[InContextNum].Camera.position.x * WorldScale, Views[InContextNum].Camera.position.y * WorldScale);

	// Forward view rotation to a caller
	InOutViewRotation = FRotator(Orientation.y, Orientation.x, -Orientation.z);

	return true;
}

bool FDisplayClusterProjectionDomeprojectionViewAdapterDX11::GetProjectionMatrix(class IDisplayClusterViewport* InViewport, const uint32 InContextNum, const uint32 Channel, FMatrix& OutPrjMatrix)
{
	check(Views.Num() > (int)InContextNum);

	// Build Projection matrix:
	const float Left   = Views[InContextNum].Camera.tanLeft;
	const float Right  = Views[InContextNum].Camera.tanRight;
	const float Bottom = Views[InContextNum].Camera.tanBottom;
	const float Top    = Views[InContextNum].Camera.tanTop;

	InViewport->CalculateProjectionMatrix(InContextNum, Left, Right, Top, Bottom, ZNear, ZFar, false);
	OutPrjMatrix = InViewport->GetContexts()[InContextNum].ProjectionMatrix;

	return true;
}

bool FDisplayClusterProjectionDomeprojectionViewAdapterDX11::ImplApplyWarpBlend_RenderThread(FRHICommandListImmediate& RHICmdList, int InContextNum, const uint32 Channel, FRHITexture2D* InputTextures, FRHITexture2D* OutputTextures)
{
	if (GD3D11RHI == nullptr)
	{
		return false;
	}

	// Prepare the textures
	FD3D11TextureBase* SrcTextureRHI = static_cast<FD3D11TextureBase*>(InputTextures->GetTextureBaseRHI());
	FD3D11TextureBase* DstTextureRHI = static_cast<FD3D11TextureBase*>(OutputTextures->GetTextureBaseRHI());
	if (!(SrcTextureRHI && DstTextureRHI))
	{
		return false;
	}

	ID3D11ShaderResourceView* SrcTextureSRVD3D11 = static_cast<ID3D11ShaderResourceView*>(SrcTextureRHI->GetShaderResourceView());
	ID3D11RenderTargetView* DstTextureRTV = DstTextureRHI->GetRenderTargetView(0, -1);
	if (!(SrcTextureSRVD3D11 && DstTextureRTV))
	{
		return false;
	}

	const FIntPoint InViewportSize = InputTextures->GetSizeXY();

	D3D11_VIEWPORT RenderViewportData;
	RenderViewportData.MinDepth = 0.0f;
	RenderViewportData.MaxDepth = 1.0f;
	RenderViewportData.Width = static_cast<float>(InViewportSize.X);
	RenderViewportData.Height = static_cast<float>(InViewportSize.Y);
	RenderViewportData.TopLeftX = 0.0f;
	RenderViewportData.TopLeftY = 0.0f;

	FD3D11Device* D3D11Device = GD3D11RHI->GetDevice();
	FD3D11DeviceContext* DeviceContext = GD3D11RHI->GetDeviceContext();
	if (D3D11Device && DeviceContext)
	{
		DeviceContext->RSSetViewports(1, &RenderViewportData);
		DeviceContext->OMSetRenderTargets(1, &DstTextureRTV, nullptr);
		DeviceContext->Flush();

		// perform warp/blend
		{
			FScopeLock lock(&DllAccessCS);

			check(DisplayClusterProjectionDomeprojectionLibraryDX11::dpPostDrawFunc);
			dpResult Result = DisplayClusterProjectionDomeprojectionLibraryDX11::dpPostDrawFunc(Views[InContextNum].Context, SrcTextureSRVD3D11, DeviceContext);

			if (Result != dpNoError)
			{
				UE_LOG(LogDisplayClusterProjectionDomeprojection, Error, TEXT("Domeprojection couldn't perform rendering operation"));
				return false;
			}

			return true;
		}
	}

	UE_LOG(LogDisplayClusterProjectionDomeprojection, Error, TEXT("UE couldn't perform domeprojection rendering on current render device"));
	return false;
}

bool FDisplayClusterProjectionDomeprojectionViewAdapterDX11::ApplyWarpBlend_RenderThread(FRHICommandListImmediate& RHICmdList, const class IDisplayClusterViewportProxy* InViewportProxy, const uint32 Channel)
{
	check(IsInRenderingThread());

	if (!GEngine)
	{
		return false;
	}

	// Get in\out remp resources ref from viewport
	TArray<FRHITexture2D*> InputTextures, OutputTextures;

	if (!InViewportProxy->GetResources_RenderThread(EDisplayClusterViewportResourceType::InputShaderResource, InputTextures))
	{
		// no source textures
		return false;
	}

	if (!InViewportProxy->GetResources_RenderThread(EDisplayClusterViewportResourceType::AdditionalTargetableResource, OutputTextures))
	{
		return false;
	}

	check(InputTextures.Num() == OutputTextures.Num());
	check(InViewportProxy->GetContexts_RenderThread().Num() == InputTextures.Num());

	// External SDK not use our RHI flow, call flush to finish resolve context image to input resource
	RHICmdList.ImmediateFlush(EImmediateFlushType::FlushRHIThreadFlushResources);

	TRACE_CPUPROFILER_EVENT_SCOPE(nDisplay EasyBlend::Render);
	for (int ContextNum = 0; ContextNum < InputTextures.Num(); ContextNum++)
	{
		if (!ImplApplyWarpBlend_RenderThread(RHICmdList, ContextNum, Channel, InputTextures[ContextNum], OutputTextures[ContextNum]))
		{
			return false;
		}
	}

	// resolve warp result images from temp targetable to FrameTarget
	return InViewportProxy->ResolveResources(RHICmdList, EDisplayClusterViewportResourceType::AdditionalTargetableResource, InViewportProxy->GetOutputResourceType());
}

void FDisplayClusterProjectionDomeprojectionViewAdapterDX11::FViewData::Release(FCriticalSection& DllAccessCS)
{
	if (Context != nullptr)
	{
		FScopeLock lock(&DllAccessCS);
		DisplayClusterProjectionDomeprojectionLibraryDX11::dpDestroyContextFunc(Context);
		Context = nullptr;
	}
}

bool FDisplayClusterProjectionDomeprojectionViewAdapterDX11::FViewData::Initialize(const FString& InFile, FCriticalSection& DllAccessCS)
{
	// Initialize Domeprojection DLL API
	FScopeLock lock(&DllAccessCS);

	if (!DisplayClusterProjectionDomeprojectionLibraryDX11::Initialize())
	{
		if (!FDisplayClusterProjectionPolicyBase::IsEditorOperationMode())
		{
			UE_LOG(LogDisplayClusterProjectionDomeprojection, Error, TEXT("Couldn't link to the Domeprojection DLL"));
		}

		return false;
	}

	if (InFile.IsEmpty())
	{
		if (!FDisplayClusterProjectionPolicyBase::IsEditorOperationMode())
		{
			UE_LOG(LogDisplayClusterProjectionDomeprojection, Error, TEXT("File name is empty"));
		}

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
	if (!FPaths::FileExists(InFile))
	{
		UE_LOG(LogDisplayClusterProjectionDomeprojection, Error, TEXT("File '%s' not found"), *InFile);
		return false;
	}

	// Load the configuration
	Result = DisplayClusterProjectionDomeprojectionLibraryDX11::dpLoadConfigurationFromFileFunc(Context, TCHAR_TO_ANSI(*InFile));
	if (Result != dpNoError)
	{
		UE_LOG(LogDisplayClusterProjectionDomeprojection, Error, TEXT("Loading configuration from %s failed with %d"), *InFile, Result);
		return false;
	}

	return true;
}
