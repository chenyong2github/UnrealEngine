// Copyright Epic Games, Inc. All Rights Reserved.

#include "Policy/EasyBlend/DisplayClusterProjectionEasyBlendPolicyBase.h"

#include "DisplayClusterProjectionLog.h"
#include "DisplayClusterProjectionStrings.h"

#include "DisplayClusterUtils/DisplayClusterCommonHelpers.h"

#include "IDisplayCluster.h"
#include "Config/IDisplayClusterConfigManager.h"
#include "Game/IDisplayClusterGameManager.h"

#include "DisplayClusterRootComponent.h"
#include "DisplayClusterScreenComponent.h"


FDisplayClusterProjectionEasyBlendPolicyBase::FDisplayClusterProjectionEasyBlendPolicyBase(const FString& ViewportId)
	: FDisplayClusterProjectionPolicyBase(ViewportId)
{
}

FDisplayClusterProjectionEasyBlendPolicyBase::~FDisplayClusterProjectionEasyBlendPolicyBase()
{
}


//////////////////////////////////////////////////////////////////////////////////////////////
// IDisplayClusterProjectionPolicy
//////////////////////////////////////////////////////////////////////////////////////////////
void FDisplayClusterProjectionEasyBlendPolicyBase::StartScene(UWorld* World)
{
	check(IsInGameThread());

	// The game side of the nDisplay has been initialized by the nDisplay Game Manager already
	// so we can extend it by our projection related functionality/components/etc.

	// Find origin component if it exists
	InitializeOriginComponent(OriginCompId);
}

void FDisplayClusterProjectionEasyBlendPolicyBase::EndScene()
{
	check(IsInGameThread());
}

bool FDisplayClusterProjectionEasyBlendPolicyBase::HandleAddViewport(const FIntPoint& ViewportSize, const uint32 ViewsAmount)
{
	check(IsInGameThread())

	FString File;

	// Read easyblend config data from nDisplay config file
	if (!ReadConfigData(GetViewportId(), File, OriginCompId, EasyBlendScale))
	{
		UE_LOG(LogDisplayClusterProjectionEasyBlend, Error, TEXT("Couldn't read EasyBlend configuration from the config file"));
		return false;
	}

	FString FullFilePath = DisplayClusterHelpers::config::GetFullPath(File);
	if (!FPaths::FileExists(FullFilePath))
	{
		//! Handle error: EasyBlend configuration file not found
		return false;
	}

	// Create and store nDisplay-to-EasyBlend viewport adapter
	TSharedPtr<FDisplayClusterProjectionEasyBlendViewAdapterBase> NewViewAdapter = CreateViewAdapter(FDisplayClusterProjectionEasyBlendViewAdapterBase::FInitParams{ ViewportSize, ViewsAmount });
	if (!NewViewAdapter || !NewViewAdapter->Initialize(FullFilePath))
	{
		UE_LOG(LogDisplayClusterProjectionEasyBlend, Error, TEXT("An error occurred during EasyBlend viewport adapter initialization"));
		return false;
	}

	// Store the adapter
	ViewAdapter = NewViewAdapter;

	UE_LOG(LogDisplayClusterProjectionEasyBlend, Log, TEXT("An EasyBlend viewport adapter has been initialized"));

	return true;
}

void FDisplayClusterProjectionEasyBlendPolicyBase::HandleRemoveViewport()
{
	check(IsInGameThread());

	UE_LOG(LogDisplayClusterProjectionEasyBlend, Log, TEXT("Removing viewport '%s'"), *GetViewportId());
}

bool FDisplayClusterProjectionEasyBlendPolicyBase::CalculateView(const uint32 ViewIdx, FVector& InOutViewLocation, FRotator& InOutViewRotation, const FVector& ViewOffset, const float WorldToMeters, const float NCP, const float FCP)
{
	check(IsInGameThread());

	if (!ViewAdapter.IsValid())
	{
		return false;
	}

	const float WorldScale = WorldToMeters / 100.f;

	// Get origin component
	const USceneComponent* const OriginComp = GetOriginComp();
	check(OriginComp);

	// Get world-origin matrix
	const FTransform& World2LocalTransform = (OriginComp != nullptr ? OriginComp->GetComponentTransform() : FTransform::Identity);

	// Calculate view location in origin space
	FVector  OriginSpaceViewLocation = World2LocalTransform.InverseTransformPosition(InOutViewLocation);

	// Apply EasyBlend scale depending on the measurement units used in calibration
	OriginSpaceViewLocation = (OriginSpaceViewLocation / 100 / EasyBlendScale);

	// Forward data to the RHI dependend EasyBlend implementation
	FRotator OriginSpaceViewRotation = FRotator::ZeroRotator;
	if (!ViewAdapter->CalculateView(ViewIdx, OriginSpaceViewLocation, OriginSpaceViewRotation, ViewOffset, WorldScale, NCP, FCP))
	{
		UE_LOG(LogDisplayClusterProjectionEasyBlend, Warning, TEXT("Couldn't compute view info for <%s> viewport"), *GetViewportId());
		return false;
	}

	// Convert rotation back from origin to world space
	InOutViewRotation = World2LocalTransform.TransformRotation(OriginSpaceViewRotation.Quaternion()).Rotator();

	return true;
}

bool FDisplayClusterProjectionEasyBlendPolicyBase::GetProjectionMatrix(const uint32 ViewIdx, FMatrix& OutPrjMatrix)
{
	check(IsInGameThread());

	if (!ViewAdapter.IsValid())
	{
		return false;
	}

	// Pass request to the adapter
	return ViewAdapter->GetProjectionMatrix(ViewIdx, OutPrjMatrix);
}

bool FDisplayClusterProjectionEasyBlendPolicyBase::IsWarpBlendSupported()
{
	return true;
}

void FDisplayClusterProjectionEasyBlendPolicyBase::ApplyWarpBlend_RenderThread(const uint32 ViewIdx, FRHICommandListImmediate& RHICmdList, FRHITexture2D* SrcTexture, const FIntRect& ViewportRect)
{
	check(IsInRenderingThread());

	if (ViewAdapter.IsValid())
	{
		ViewAdapter->ApplyWarpBlend_RenderThread(ViewIdx, RHICmdList, SrcTexture, ViewportRect);
	}
}

//////////////////////////////////////////////////////////////////////////////////////////////
// FDisplayClusterProjectionEasyBlendPolicy
//////////////////////////////////////////////////////////////////////////////////////////////
bool FDisplayClusterProjectionEasyBlendPolicyBase::ReadConfigData(const FString& InViewportId, FString& OutFile, FString& OutOrigin, float& OutGeometryScale)
{
	// Get projection settings of the specified viewport
	FDisplayClusterConfigProjection CfgProjection;
	if (!DisplayClusterHelpers::config::GetViewportProjection(InViewportId, CfgProjection))
	{
		UE_LOG(LogDisplayClusterProjectionEasyBlend, Error, TEXT("Couldn't obtain projection info for <%s> viewport"), *InViewportId);
		return false;
	}

	// EasyBlend file (mandatory)
	if (!DisplayClusterHelpers::str::ExtractValue(CfgProjection.Params, DisplayClusterStrings::cfg::data::projection::easyblend::File, OutFile))
	{
		UE_LOG(LogDisplayClusterProjectionEasyBlend, Error, TEXT("Parameter <%s> not found in the config file"), DisplayClusterStrings::cfg::data::projection::easyblend::File);
		return false;
	}
	else
	{
		UE_LOG(LogDisplayClusterProjectionEasyBlend, Log, TEXT("Found <%s> parameter for projection %s - %s"), DisplayClusterStrings::cfg::data::projection::easyblend::File, *CfgProjection.Id, *OutFile);
	}
	
	// Origin node (optional)
	if (DisplayClusterHelpers::str::ExtractValue(CfgProjection.Params, DisplayClusterStrings::cfg::data::projection::easyblend::Origin, OutOrigin))
	{
		UE_LOG(LogDisplayClusterProjectionEasyBlend, Log, TEXT("Found <%s> parameter for projection %s - %s"), *CfgProjection.Id, *OutOrigin);
	}
	else
	{
		UE_LOG(LogDisplayClusterProjectionEasyBlend, Log, TEXT("No <%s> parameter found for projection %s"), DisplayClusterStrings::cfg::data::projection::easyblend::Origin, *CfgProjection.Id);
	}

	// Geometry scale (optional)
	if (DisplayClusterHelpers::str::ExtractValue(CfgProjection.Params, DisplayClusterStrings::cfg::data::projection::easyblend::Scale, OutGeometryScale))
	{
		UE_LOG(LogDisplayClusterProjectionEasyBlend, Log, TEXT("Found <%s> parameter for projection %s - %f"), DisplayClusterStrings::cfg::data::projection::easyblend::Scale, *CfgProjection.Id, OutGeometryScale);
	}
	else
	{
		UE_LOG(LogDisplayClusterProjectionEasyBlend, Log, TEXT("No <%s> parameter found for projection %s"), DisplayClusterStrings::cfg::data::projection::easyblend::Scale, *CfgProjection.Id);
	}

	return true;
}
