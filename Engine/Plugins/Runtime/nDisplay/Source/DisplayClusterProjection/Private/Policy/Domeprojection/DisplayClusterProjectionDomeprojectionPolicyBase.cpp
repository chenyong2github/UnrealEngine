// Copyright Epic Games, Inc. All Rights Reserved.

#include "Policy/Domeprojection/DisplayClusterProjectionDomeprojectionPolicyBase.h"

#include "DisplayClusterProjectionLog.h"
#include "DisplayClusterProjectionStrings.h"

#include "Misc/DisplayClusterHelpers.h"

#include "IDisplayCluster.h"
#include "Config/IDisplayClusterConfigManager.h"
#include "Game/IDisplayClusterGameManager.h"


FDisplayClusterProjectionDomeprojectionPolicyBase::FDisplayClusterProjectionDomeprojectionPolicyBase(const FString& ViewportId, const TMap<FString, FString>& Parameters)
	: FDisplayClusterProjectionPolicyBase(ViewportId, Parameters)
{
}


//////////////////////////////////////////////////////////////////////////////////////////////
// IDisplayClusterProjectionPolicy
//////////////////////////////////////////////////////////////////////////////////////////////
void FDisplayClusterProjectionDomeprojectionPolicyBase::StartScene(UWorld* World)
{
	check(IsInGameThread());

	// The game side of the nDisplay has been initialized by the nDisplay Game Manager already
	// so we can extend it by our projection related functionality/components/etc.

	// Find origin component if it exists
	InitializeOriginComponent(OriginCompId);
}

void FDisplayClusterProjectionDomeprojectionPolicyBase::EndScene()
{
	check(IsInGameThread());

	ReleaseOriginComponent();
}

bool FDisplayClusterProjectionDomeprojectionPolicyBase::HandleAddViewport(const FIntPoint& ViewportSize, const uint32 ViewsAmount)
{
	check(IsInGameThread())

	// Read domeprojection config data from nDisplay config file
	FString File;
	if (!ReadConfigData(GetViewportId(), File, OriginCompId, DomeprojectionChannel))
	{
		UE_LOG(LogDisplayClusterProjectionDomeprojection, Error, TEXT("Couldn't read Domeprojection configuration from the config file"));
		return false;
	}

	const FString FullFilePath = DisplayClusterHelpers::filesystem::GetFullPathForConfigResource(File);
	if (!FPaths::FileExists(FullFilePath))
	{
		UE_LOG(LogDisplayClusterProjectionDomeprojection, Error, TEXT("Couldn't read Domeprojection configuration file: %s"), *FullFilePath);
		return false;
	}

	// Create and store nDisplay-to-Domeprojection viewport adapter
	ViewAdapter = CreateViewAdapter(FDisplayClusterProjectionDomeprojectionViewAdapterBase::FInitParams{ ViewportSize, ViewsAmount });
	if (!(ViewAdapter && ViewAdapter->Initialize(FullFilePath)))
	{
		UE_LOG(LogDisplayClusterProjectionDomeprojection, Error, TEXT("An error occurred during Domeprojection viewport adapter initialization"));
		return false;
	}

	UE_LOG(LogDisplayClusterProjectionDomeprojection, Log, TEXT("A Domeprojection viewport adapter has been initialized"));

	return true;
}

void FDisplayClusterProjectionDomeprojectionPolicyBase::HandleRemoveViewport()
{
	check(IsInGameThread());

	UE_LOG(LogDisplayClusterProjectionDomeprojection, Log, TEXT("Removing viewport '%s'"), *GetViewportId());
}

bool FDisplayClusterProjectionDomeprojectionPolicyBase::CalculateView(const uint32 ViewIdx, FVector& InOutViewLocation, FRotator& InOutViewRotation, const FVector& ViewOffset, const float WorldToMeters, const float NCP, const float FCP)
{
	check(IsInGameThread());

	if (!ViewAdapter.IsValid())
	{
		return false;
	}

	// Get origin component
	const USceneComponent* const OriginComp = GetOriginComp();
	check(OriginComp);

	// Get world-origin matrix
	const FTransform& World2LocalTransform = (OriginComp != nullptr ? OriginComp->GetComponentTransform() : FTransform::Identity);

	// Calculate view location in origin space
	FVector OriginSpaceViewLocation = World2LocalTransform.InverseTransformPosition(InOutViewLocation);

	// Forward data to the RHI dependend Domeprojection implementation
	FRotator OriginSpaceViewRotation = FRotator::ZeroRotator;
	if (!ViewAdapter->CalculateView(ViewIdx, DomeprojectionChannel, OriginSpaceViewLocation, OriginSpaceViewRotation, ViewOffset, WorldToMeters, NCP, FCP))
	{
		UE_LOG(LogDisplayClusterProjectionDomeprojection, Warning, TEXT("Couldn't compute view info for <%s> viewport"), *GetViewportId());
		return false;
	}

	// Convert rotation back from origin to world space
	InOutViewRotation = World2LocalTransform.TransformRotation(OriginSpaceViewRotation.Quaternion()).Rotator();

	// Convert location back from origin to world space
	InOutViewLocation = World2LocalTransform.TransformPosition(OriginSpaceViewLocation);

	return true;
}

bool FDisplayClusterProjectionDomeprojectionPolicyBase::GetProjectionMatrix(const uint32 ViewIdx, FMatrix& OutPrjMatrix)
{
	check(IsInGameThread());

	if (!ViewAdapter.IsValid())
	{
		return false;
	}

	// Pass request to the adapter
	return ViewAdapter->GetProjectionMatrix(ViewIdx, DomeprojectionChannel, OutPrjMatrix);
}

bool FDisplayClusterProjectionDomeprojectionPolicyBase::IsWarpBlendSupported()
{
	return true;
}

void FDisplayClusterProjectionDomeprojectionPolicyBase::ApplyWarpBlend_RenderThread(const uint32 ViewIdx, FRHICommandListImmediate& RHICmdList, FRHITexture2D* SrcTexture, const FIntRect& ViewportRect)
{
	check(IsInRenderingThread());

	if (ViewAdapter.IsValid())
	{
		ViewAdapter->ApplyWarpBlend_RenderThread(ViewIdx, DomeprojectionChannel, RHICmdList, SrcTexture, ViewportRect);
	}
}

//////////////////////////////////////////////////////////////////////////////////////////////
// FDisplayClusterProjectionDomeprojectionPolicy
//////////////////////////////////////////////////////////////////////////////////////////////
bool FDisplayClusterProjectionDomeprojectionPolicyBase::ReadConfigData(const FString& InViewportId, FString& OutFile, FString& OutOrigin, uint32& OutChannel)
{
	// Domeprojection file (mandatory)
	if (DisplayClusterHelpers::map::ExtractValue(GetParameters(), DisplayClusterProjectionStrings::cfg::domeprojection::File, OutFile))
	{
		UE_LOG(LogDisplayClusterProjectionDomeprojection, Log, TEXT("Viewport <%s>: Projection parameter '%s' - '%s'"), *InViewportId, DisplayClusterProjectionStrings::cfg::domeprojection::File, *OutFile);
	}
	else
	{
		UE_LOG(LogDisplayClusterProjectionDomeprojection, Error, TEXT("Viewport <%s>: Projection parameter '%s' not found"), *InViewportId, DisplayClusterProjectionStrings::cfg::domeprojection::File);
		return false;
	}

	// Channel (mandatory)
	if(DisplayClusterHelpers::map::ExtractValueFromString(GetParameters(), DisplayClusterProjectionStrings::cfg::domeprojection::Channel, OutChannel))
	{
		UE_LOG(LogDisplayClusterProjectionDomeprojection, Log, TEXT("Viewport <%s>: Projection parameter '%s' - '%d'"), *InViewportId, DisplayClusterProjectionStrings::cfg::domeprojection::Channel, OutChannel);
	}
	else
	{
		UE_LOG(LogDisplayClusterProjectionDomeprojection, Error, TEXT("Viewport <%s>: Parameter <%s> not found in the config file"), *InViewportId, DisplayClusterProjectionStrings::cfg::domeprojection::Channel);
		return false;
	}

	// Origin node (optional)
	if (DisplayClusterHelpers::map::ExtractValue(GetParameters(), DisplayClusterProjectionStrings::cfg::domeprojection::Origin, OutOrigin))
	{
		UE_LOG(LogDisplayClusterProjectionDomeprojection, Log, TEXT("Viewport <%s>: Projection parameter '%s' - '%s'"), *InViewportId, DisplayClusterProjectionStrings::cfg::domeprojection::Origin, *OutOrigin);
	}
	else
	{
		UE_LOG(LogDisplayClusterProjectionDomeprojection, Log, TEXT("Viewport <%s>: No <%s> parameter found for projection %s"), *InViewportId, DisplayClusterProjectionStrings::cfg::domeprojection::Origin, *OutOrigin);
	}

	return true;
}
