// Copyright Epic Games, Inc. All Rights Reserved.

#include "Policy/VIOSO/Windows/DisplayClusterProjectionVIOSOPolicy.h"

#include "DisplayClusterProjectionStrings.h"
#include "DisplayClusterProjectionLog.h"

#include "IDisplayCluster.h"
#include "Misc/DisplayClusterHelpers.h"
#include "Config/IDisplayClusterConfigManager.h"
#include "Game/IDisplayClusterGameManager.h"

#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"
#include "Engine/RendererSettings.h"

#include "Render/Viewport/IDisplayClusterViewportManager.h"
#include "Render/Viewport/IDisplayClusterViewport.h"
#include "Render/Viewport/IDisplayClusterViewportProxy.h"

#define VIOSO_USE_GRAPHICS_API_D3D11	1
#define VIOSO_USE_GRAPHICS_API_D3D12	1


//-------------------------------------------------------------------------------------------------
// D3D11
//-------------------------------------------------------------------------------------------------
#if VIOSO_USE_GRAPHICS_API_D3D11
#include "D3D11RHIPrivate.h"
#include "D3D11Util.h"
#endif // VIOSO_USE_GRAPHICS_API_D3D11

//-------------------------------------------------------------------------------------------------
// D3D12
//-------------------------------------------------------------------------------------------------
#if VIOSO_USE_GRAPHICS_API_D3D12
#define GetD3D11CubeFace GetD3D12CubeFace
#define VerifyD3D11Result VerifyD3D12Result
#define GetD3D11TextureFromRHITexture GetD3D12TextureFromRHITexture
#define FRingAllocation FRingAllocation_D3D12
#define GetRenderTargetFormat GetRenderTargetFormat_D3D12
#define ED3D11ShaderOffsetBuffer ED3D12ShaderOffsetBuffer
#define FindShaderResourceDXGIFormat FindShaderResourceDXGIFormat_D3D12
#define FindUnorderedAccessDXGIFormat FindUnorderedAccessDXGIFormat_D3D12
#define FindDepthStencilDXGIFormat FindDepthStencilDXGIFormat_D3D12
#define HasStencilBits HasStencilBits_D3D12
#define FVector4VertexDeclaration FVector4VertexDeclaration_D3D12
#define GLOBAL_CONSTANT_BUFFER_INDEX GLOBAL_CONSTANT_BUFFER_INDEX_D3D12
#define MAX_CONSTANT_BUFFER_SLOTS MAX_CONSTANT_BUFFER_SLOTS_D3D12
#define FD3DGPUProfiler FD3D12GPUProfiler
#define FRangeAllocator FRangeAllocator_D3D12

#include "D3D12RHIPrivate.h"
#include "D3D12Util.h"

#undef GetD3D11CubeFace
#undef VerifyD3D11Result
#undef GetD3D11TextureFromRHITexture
#undef FRingAllocation
#undef GetRenderTargetFormat
#undef ED3D11ShaderOffsetBuffer
#undef FindShaderResourceDXGIFormat
#undef FindUnorderedAccessDXGIFormat
#undef FindDepthStencilDXGIFormat
#undef HasStencilBits
#undef FVector4VertexDeclaration
#undef GLOBAL_CONSTANT_BUFFER_INDEX
#undef MAX_CONSTANT_BUFFER_SLOTS
#undef FD3DGPUProfiler
#undef FRangeAllocator

#endif // VIOSO_USE_GRAPHICS_API_D3D12

#if VIOSO_USE_GRAPHICS_API_D3D11
/**
 * Class for caching D3D11 changed values on scope
 */
class FD3D11ContextHelper
{
public:
	/** Initialization constructor: requires the device context. */
	FD3D11ContextHelper()
	{
		FMemory::Memzero(RenderTargetViews, sizeof(RenderTargetViews));
		FMemory::Memzero(Viewports, sizeof(Viewports));
		DepthStencilView = NULL;


		FD3D11DynamicRHI* d3d11RHI = static_cast<FD3D11DynamicRHI*>(GDynamicRHI);
		DeviceContext = d3d11RHI ? d3d11RHI->GetDeviceContext() : nullptr;

		if (DeviceContext)
		{
			ViewportsNum = MaxSimultaneousRenderTargets;
			DeviceContext->OMGetRenderTargets(MaxSimultaneousRenderTargets, &RenderTargetViews[0], &DepthStencilView);
			DeviceContext->RSGetViewports(&ViewportsNum, &Viewports[0]);
		}
	}

	/** Destructor. */
	~FD3D11ContextHelper()
	{
		if (DeviceContext)
		{
			// Flush tail commands
			DeviceContext->Flush();

			// Restore
			DeviceContext->OMSetRenderTargets(MaxSimultaneousRenderTargets, &RenderTargetViews[0], DepthStencilView);
			DeviceContext->RSSetViewports(ViewportsNum, &Viewports[0]);
		}

		// OMGetRenderTargets calls AddRef on each RTV/DSV it returns. We need
		// to make a corresponding call to Release.
		for (int32 TargetIndex = 0; TargetIndex < MaxSimultaneousRenderTargets; ++TargetIndex)
		{
			if (RenderTargetViews[TargetIndex] != nullptr)
			{
				RenderTargetViews[TargetIndex]->Release();
			}
		}

		if (DepthStencilView)
		{
			DepthStencilView->Release();
		}
	}

	inline bool AssignD3D11RenderTarget(FRHITexture2D* RenderTargetTexture)
	{
		if (DeviceContext)
		{
			// Set RTV
			FD3D11TextureBase* DestTextureD3D11RHI = static_cast<FD3D11TextureBase*>(RenderTargetTexture->GetTextureBaseRHI());
			ID3D11RenderTargetView* DestTextureRTV = DestTextureD3D11RHI->GetRenderTargetView(0, -1);
			DeviceContext->OMSetRenderTargets(1, &DestTextureRTV, nullptr);

			// Set viewport
			D3D11_VIEWPORT RenderViewportData;
			RenderViewportData.MinDepth = 0.0f;
			RenderViewportData.MaxDepth = 1.0f;
			RenderViewportData.TopLeftX = 0.0f;
			RenderViewportData.TopLeftY = 0.0f;
			RenderViewportData.Width = RenderTargetTexture->GetSizeX();
			RenderViewportData.Height = RenderTargetTexture->GetSizeY();
			DeviceContext->RSSetViewports(1, &RenderViewportData);

			// Clear RTV
			static FVector4 ClearColor(0, 0, 0, 1);
			DeviceContext->ClearRenderTargetView(DestTextureRTV, &ClearColor[0]);

			DeviceContext->Flush();

			return true;
		}

		return false;
	};

private:
	ID3D11DeviceContext*    DeviceContext;
	ID3D11RenderTargetView* RenderTargetViews[MaxSimultaneousRenderTargets];
	ID3D11DepthStencilView* DepthStencilView;
	D3D11_VIEWPORT          Viewports[MaxSimultaneousRenderTargets];
	uint32                  ViewportsNum;
};
#endif // VIOSO_USE_GRAPHICS_API_D3D11


//////////////////////////////////////////////////////////////////////////////////////////////
// FDisplayClusterProjectionVIOSOPolicy
//////////////////////////////////////////////////////////////////////////////////////////////
FDisplayClusterProjectionVIOSOPolicy::FDisplayClusterProjectionVIOSOPolicy(const FString& ProjectionPolicyId, const struct FDisplayClusterConfigurationProjection* InConfigurationProjectionPolicy)
	: FDisplayClusterProjectionPolicyBase(ProjectionPolicyId, InConfigurationProjectionPolicy)
{
	FString RHIName = GDynamicRHI->GetName();

	// Create warper for view
	if (RHIName == DisplayClusterProjectionStrings::rhi::D3D11)
	{
		RenderDevice = ERenderDevice::D3D11;
	}
	else
	if (RHIName == DisplayClusterProjectionStrings::rhi::D3D12)
	{
		RenderDevice = ERenderDevice::D3D12;
	}
	else
	{
		UE_LOG(LogDisplayClusterProjectionVIOSO, Error, TEXT("VIOSO warp projection not supported by '%s' rhi"),*RHIName);
	}
}

FDisplayClusterProjectionVIOSOPolicy::~FDisplayClusterProjectionVIOSOPolicy()
{
	ImplRelease();
}

bool FDisplayClusterProjectionVIOSOPolicy::HandleStartScene(class IDisplayClusterViewport* InViewport)
{
	check(IsInGameThread());	

	// Read VIOSO config data from nDisplay config file
	if (!ViosoConfigData.Initialize(GetParameters(), InViewport->GetId()))
	{
		if (!IsEditorOperationMode())
		{
			UE_LOG(LogDisplayClusterProjectionVIOSO, Error, TEXT("Couldn't read VIOSO configuration from the config file for viewport -'%s'"), *InViewport->GetId());
		}

		return false;
	}

	// Find origin component if it exists
	InitializeOriginComponent(InViewport, ViosoConfigData.OriginCompId);

	Views.AddDefaulted(2);
	//ViewportSize = InViewportSize;

	// Initialize data for all views
	FScopeLock lock(&DllAccessCS);
	for (FViewData& ViewIt : Views)
	{
		if (!ViewIt.Initialize(RenderDevice, ViosoConfigData))
		{
			UE_LOG(LogDisplayClusterProjectionVIOSO, Error, TEXT("VIOSO not initialized"));
			return false;
		}
	}

	UE_LOG(LogDisplayClusterProjectionVIOSO, Verbose, TEXT("VIOSO policy has been initialized: %s"), *ViosoConfigData.ToString());

	return true;
}

void FDisplayClusterProjectionVIOSOPolicy::HandleEndScene(class IDisplayClusterViewport* InViewport)
{
	check(IsInGameThread());

	ImplRelease();
}

void FDisplayClusterProjectionVIOSOPolicy::ImplRelease()
{
	ReleaseOriginComponent();

	// Destroy VIOSO for all views
	FScopeLock lock(&DllAccessCS);
	for (FViewData& ViewIt : Views)
	{
		ViewIt.DestroyVIOSO();
	}
}

bool FDisplayClusterProjectionVIOSOPolicy::CalculateView(IDisplayClusterViewport* InViewport, const uint32 InContextNum, FVector& InOutViewLocation, FRotator& InOutViewRotation, const FVector& ViewOffset, const float WorldToMeters, const float NCP, const float FCP)
{
	check(Views.Num() > (int)InContextNum);

	// Get view location in local space
	const USceneComponent* const OriginComp = GetOriginComp();
	const FTransform& World2LocalTransform = (OriginComp ? OriginComp->GetComponentTransform() : FTransform::Identity);

	// Get our base camera location and view offset in local space (MPCDI space)
	const FVector  LocalOrigin    = World2LocalTransform.InverseTransformPosition(InOutViewLocation - ViewOffset);
	const FVector  LocalEyeOrigin = World2LocalTransform.InverseTransformPosition(InOutViewLocation);
	const FRotator LocalRotator   = World2LocalTransform.InverseTransformRotation(InOutViewRotation.Quaternion()).Rotator();

	// Get view prj data from VIOSO
	FScopeLock lock(&DllAccessCS);
	if (!Views[InContextNum].UpdateVIOSO(InViewport, InContextNum, LocalEyeOrigin, LocalRotator, WorldToMeters, NCP, FCP))
	{
		if (Views[InContextNum].IsValid() && !FDisplayClusterProjectionPolicyBase::IsEditorOperationMode())
		{
			// Vioso api used, but failed inside math. The config base matrix or vioso geometry is invalid
			UE_LOG(LogDisplayClusterProjectionVIOSO, Error, TEXT("Couldn't Calculate View for VIOSO viewport '%s'"), *InViewport->GetId());
		}

		return false;
	}

	// Transform rotation to world space
	InOutViewRotation = World2LocalTransform.TransformRotation(Views[InContextNum].ViewRotation.Quaternion()).Rotator();
	InOutViewLocation = World2LocalTransform.TransformPosition(Views[InContextNum].ViewLocation);

	return true;
}

bool FDisplayClusterProjectionVIOSOPolicy::GetProjectionMatrix(class IDisplayClusterViewport* InViewport, const uint32 InContextNum, FMatrix& OutPrjMatrix)
{
	check(IsInGameThread());

	OutPrjMatrix = Views[InContextNum].ProjectionMatrix;
	
	return true;
}

bool FDisplayClusterProjectionVIOSOPolicy::IsWarpBlendSupported()
{
	return true;
}

void FDisplayClusterProjectionVIOSOPolicy::ApplyWarpBlend_RenderThread(FRHICommandListImmediate& RHICmdList, const class IDisplayClusterViewportProxy* InViewportProxy)
{
	if (!ImplApplyWarpBlend_RenderThread(RHICmdList, InViewportProxy))
	{
		// warp failed, just resolve texture to frame
		InViewportProxy->ResolveResources(RHICmdList, EDisplayClusterViewportResourceType::InputShaderResource, InViewportProxy->GetOutputResourceType());
	}
}

bool FDisplayClusterProjectionVIOSOPolicy::ImplApplyWarpBlend_RenderThread(FRHICommandListImmediate& RHICmdList, const class IDisplayClusterViewportProxy* InViewportProxy)
{
	// Get in\out remp resources ref from viewport
	TArray<FRHITexture2D*> InputTextures, OutputTextures;

	// Use for input first MipsShader texture if enabled in viewport render settings
	//@todo: test if domeprojection support mips textures as warp input
	//if (!InViewportProxy->GetResources_RenderThread(EDisplayClusterViewportResourceType::MipsShaderResource, InputTextures))
	{
		// otherwise inputshader texture
		if (!InViewportProxy->GetResources_RenderThread(EDisplayClusterViewportResourceType::InputShaderResource, InputTextures))
		{
			// no source textures
			return false;
		}
	}

	if (!InViewportProxy->GetResources_RenderThread(EDisplayClusterViewportResourceType::AdditionalTargetableResource, OutputTextures))
	{
		return false;
	}

	check(InputTextures.Num() == OutputTextures.Num());
	check(InViewportProxy->GetContexts_RenderThread().Num() == InputTextures.Num());

	// External SDK not use our RHI flow, call flush to finish resolve context image to input resource
	RHICmdList.ImmediateFlush(EImmediateFlushType::FlushRHIThreadFlushResources);


	TRACE_CPUPROFILER_EVENT_SCOPE(nDisplay VIOSO::Render);
	{
		FScopeLock lock(&DllAccessCS);

		for (int ContextNum = 0; ContextNum < InputTextures.Num(); ContextNum++)
		{
			if (!Views[ContextNum].RenderVIOSO_RenderThread(RHICmdList, InputTextures[ContextNum], OutputTextures[ContextNum], ViosoConfigData))
			{
				return false;
			}
		}
	}

	// resolve warp result images from temp targetable to FrameTarget
	return InViewportProxy->ResolveResources(RHICmdList, EDisplayClusterViewportResourceType::AdditionalTargetableResource, InViewportProxy->GetOutputResourceType());
}

//////////////////////////////////////////////////////////////////////////////////////////////
// FDisplayClusterProjectionVIOSOPolicy::FViewData
//////////////////////////////////////////////////////////////////////////////////////////////
bool FDisplayClusterProjectionVIOSOPolicy::FViewData::IsValid()
{
	return bDataInitialized && bInitialized && Warper.IsValid();
}

bool FDisplayClusterProjectionVIOSOPolicy::FViewData::Initialize(ERenderDevice InRenderDevice, const FViosoPolicyConfiguration& InConfigData)
{
	check(IsInGameThread());

	Warper.Release();

	// VIOSO requre render thread resources to initialize.
	// Store args, and initialize latter, on render thread
	RenderDevice = InRenderDevice;

	bDataInitialized = true;
	return true;
}

bool FDisplayClusterProjectionVIOSOPolicy::FViewData::UpdateVIOSO(IDisplayClusterViewport* InViewport, const uint32 InContextNum, const FVector& LocalLocation, const FRotator& LocalRotator, const float WorldToMeters, const float NCP, const float FCP)
{
	if (IsValid())
	{
		ViewLocation = LocalLocation;
		ViewRotation = LocalRotator;

		return Warper.CalculateViewProjection(InViewport, InContextNum, ViewLocation, ViewRotation, ProjectionMatrix, WorldToMeters, NCP, FCP);
	}

	return false;
}

bool FDisplayClusterProjectionVIOSOPolicy::FViewData::RenderVIOSO_RenderThread(FRHICommandListImmediate& RHICmdList, FRHITexture2D* ShaderResourceTexture, FRHITexture2D* RenderTargetTexture, const FViosoPolicyConfiguration& InConfigData)
{
	check(IsInRenderingThread());

	// Delayed vioso initialize on render thread
	bool bRequireInitialize = !bInitialized && !Warper.IsValid();

	if (bRequireInitialize || IsValid())
	{
		if (bRequireInitialize)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(nDisplay Vioso::Initialize);
			InitializeVIOSO(RenderTargetTexture, InConfigData);
		}

		if (IsValid())
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(nDisplay Vioso::Render);

			switch (RenderDevice)
			{
#ifdef VIOSO_USE_GRAPHICS_API_D3D11
			case ERenderDevice::D3D11:
			{
				FD3D11ContextHelper D3D11ContextHelper;
				ID3D11Texture2D* SourceTexture = static_cast<ID3D11Texture2D*>(ShaderResourceTexture->GetTexture2D()->GetNativeResource());
				if (D3D11ContextHelper.AssignD3D11RenderTarget(RenderTargetTexture) && Warper.Render(SourceTexture, VWB_STATEMASK_STANDARD))
				{
					return true;
				}
				break;
			}
#endif //VIOSO_USE_GRAPHICS_API_D3D11

#ifdef VIOSO_USE_GRAPHICS_API_D3D12
			case ERenderDevice::D3D12:
			{
				FD3D12Texture2D* SrcTexture2D = FD3D12DynamicRHI::ResourceCast(ShaderResourceTexture);
				FD3D12Texture2D* DestTexture2D = FD3D12DynamicRHI::ResourceCast(RenderTargetTexture);

				FD3D12RenderTargetView* RTV = DestTexture2D->GetRenderTargetView(0, 0);
				CD3DX12_CPU_DESCRIPTOR_HANDLE RTVHandle = RTV->GetView();

				FD3D12Device* Device = DestTexture2D->GetParentDevice();
				FD3D12CommandListHandle& hCommandList = Device->GetDefaultCommandContext().CommandListHandle;

				VWB_D3D12_RENDERINPUT RenderInput = {};
				RenderInput.textureResource = (ID3D12Resource*)SrcTexture2D->GetNativeResource();;
				RenderInput.renderTarget = (ID3D12Resource*)DestTexture2D->GetNativeResource();;
				RenderInput.rtvHandlePtr = RTVHandle.ptr;

				//experimental: add resource barrier
				hCommandList.AddPendingResourceBarrier(DestTexture2D->GetResource(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);

				if (Warper.Render(&RenderInput, VWB_STATEMASK_DEFAULT_D3D12))
				{
					//experimental: add resource barrier
					hCommandList.AddPendingResourceBarrier(DestTexture2D->GetResource(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);
					return true;
				}
				break;
			}
#endif // VIOSO_USE_GRAPHICS_API_D3D12

			default:
				break;
			}
		}
	}

	return false;
}

bool FDisplayClusterProjectionVIOSOPolicy::FViewData::InitializeVIOSO(FRHITexture2D* RenderTargetTexture, const FViosoPolicyConfiguration& InConfigData)
{
	if (!bDataInitialized)
	{
		return false;
	}

	if (bInitialized)
	{
		return true;
	}

	bInitialized = true;

	DestroyVIOSO();

	switch (RenderDevice)
	{

#ifdef VIOSO_USE_GRAPHICS_API_D3D11
	case ERenderDevice::D3D11:
	{
		auto UED3DDevice = static_cast<ID3D11Device*>(GDynamicRHI->RHIGetNativeDevice());

		FD3D11ContextHelper D3D11ContextHelper;
		if (D3D11ContextHelper.AssignD3D11RenderTarget(RenderTargetTexture) && Warper.Initialize(UED3DDevice, InConfigData))
		{
			return true;
		}
		break;
	}
#endif // VIOSO_USE_GRAPHICS_API_D3D11

#if VIOSO_USE_GRAPHICS_API_D3D12
	case ERenderDevice::D3D12:
	{
		FD3D12DynamicRHI* DynamicRHI = FD3D12DynamicRHI::GetD3DRHI();
		ID3D12CommandQueue* D3D12CommandQueue = DynamicRHI->RHIGetD3DCommandQueue();

		if (Warper.Initialize(D3D12CommandQueue, InConfigData))
		{
			return true;
		}
		break;
	}
#endif // VIOSO_USE_GRAPHICS_API_D3D12

	default:
		UE_LOG(LogDisplayClusterProjectionVIOSO, Error, TEXT("Unsupported render device for VIOSO"));
		break;
	}

	UE_LOG(LogDisplayClusterProjectionVIOSO, Error, TEXT("Couldn't initialize VIOSO internals"));
	return false;
}

void FDisplayClusterProjectionVIOSOPolicy::FViewData::DestroyVIOSO()
{
	if (IsValid())
	{
		Warper.Release();
	}
}
