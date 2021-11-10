// Copyright Epic Games, Inc. All Rights Reserved.

#include "Policy/EasyBlend/DisplayClusterProjectionEasyBlendPolicyBase.h"

#include "DisplayClusterProjectionLog.h"
#include "DisplayClusterProjectionStrings.h"

#include "Misc/DisplayClusterHelpers.h"

#include "IDisplayCluster.h"
#include "Game/IDisplayClusterGameManager.h"

#include "Components/DisplayClusterScreenComponent.h"

#include "Render/Viewport/IDisplayClusterViewport.h"
#include "Render/Viewport/IDisplayClusterViewportProxy.h"

FDisplayClusterProjectionEasyBlendPolicyBase::FDisplayClusterProjectionEasyBlendPolicyBase(const FString& ProjectionPolicyId, const struct FDisplayClusterConfigurationProjection* InConfigurationProjectionPolicy)
	: FDisplayClusterProjectionPolicyBase(ProjectionPolicyId, InConfigurationProjectionPolicy)
{
}


//////////////////////////////////////////////////////////////////////////////////////////////
// IDisplayClusterProjectionPolicy
//////////////////////////////////////////////////////////////////////////////////////////////
bool FDisplayClusterProjectionEasyBlendPolicyBase::HandleStartScene(class IDisplayClusterViewport* InViewport)
{
	check(IsInGameThread());

	if (!IsEasyBlendRenderingEnabled())
	{
		static bool bEasyBlendInitializeOnce = false;
		if (!bEasyBlendInitializeOnce)
		{
			bEasyBlendInitializeOnce = true;
			UE_LOG(LogDisplayClusterProjectionEasyBlend, Error, TEXT("An error occurred during EasyBlend initialization : current UE render device not supported"));
		}

		return false;
	}

	if (bInitializeOnce)
	{
		return false;
	}

	bInitializeOnce = true;

	// The game side of the nDisplay has been initialized by the nDisplay Game Manager already
	// so we can extend it by our projection related functionality/components/etc.	

	// Read easyblend config data from nDisplay config file
	FString FilePath;
	if (!ReadConfigData(InViewport->GetId(), FilePath, OriginCompId, EasyBlendScale))
	{
		if (!IsEditorOperationMode())
		{
			UE_LOG(LogDisplayClusterProjectionEasyBlend, Error, TEXT("Couldn't read EasyBlend configuration from the config file"));
		}
		return false;
	}

	if (FilePath.IsEmpty())
	{
		if (!IsEditorOperationMode())
		{
			UE_LOG(LogDisplayClusterProjectionEasyBlend, Error, TEXT("EasyBlend file '' not found (Empty)"));
		}

		return false;
	}

	// Find origin component if it exists
	InitializeOriginComponent(InViewport, OriginCompId);

	FString FullPath = DisplayClusterHelpers::filesystem::GetFullPathForConfigResource(FilePath);
	if (!FPaths::FileExists(FullPath))
	{
		UE_LOG(LogDisplayClusterProjectionEasyBlend, Error, TEXT("EasyBlend file '%s' not found"), *FullPath);
		return false;
	}

	const uint32 MaxViewsAmmount = 2;

	// Create and store nDisplay-to-EasyBlend viewport adapter
	ViewAdapter = CreateViewAdapter(FDisplayClusterProjectionEasyBlendViewAdapterBase::FInitParams{ MaxViewsAmmount });
	if (!ViewAdapter || !ViewAdapter->Initialize(FullPath))
	{
		UE_LOG(LogDisplayClusterProjectionEasyBlend, Error, TEXT("An error occurred during EasyBlend viewport adapter initialization"));
		return false;
	}

	UE_LOG(LogDisplayClusterProjectionEasyBlend, Verbose, TEXT("An EasyBlend viewport adapter has been initialized"));
	return true;
}

void FDisplayClusterProjectionEasyBlendPolicyBase::HandleEndScene(class IDisplayClusterViewport* InViewport)
{
	check(IsInGameThread());

	ReleaseOriginComponent();
}

void FDisplayClusterProjectionEasyBlendPolicyBase::ApplyWarpBlend_RenderThread(FRHICommandListImmediate& RHICmdList, const IDisplayClusterViewportProxy* InViewportProxy)
{
	check(IsInRenderingThread());

	if (ViewAdapter.IsValid() && IsEasyBlendRenderingEnabled())
	{
		if (!ViewAdapter->ApplyWarpBlend_RenderThread(RHICmdList, InViewportProxy))
		{
			// Warpbled failed, just copy unwarped rtt for frame output
			InViewportProxy->ResolveResources(RHICmdList, EDisplayClusterViewportResourceType::InputShaderResource, InViewportProxy->GetOutputResourceType());
		}
	}
}

bool FDisplayClusterProjectionEasyBlendPolicyBase::CalculateView(IDisplayClusterViewport* InViewport, const uint32 InContextNum, FVector& InOutViewLocation, FRotator& InOutViewRotation, const FVector& ViewOffset, const float WorldToMeters, const float NCP, const float FCP)
{
	check(IsInGameThread());

	if (!ViewAdapter.IsValid() || !IsEasyBlendRenderingEnabled())
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
	FVector OriginSpaceViewLocation = World2LocalTransform.InverseTransformPosition(InOutViewLocation);

	// Apply EasyBlend scale depending on the measurement units used in calibration
	OriginSpaceViewLocation = (OriginSpaceViewLocation / 100 / EasyBlendScale);

	// Forward data to the RHI dependend EasyBlend implementation
	FRotator OriginSpaceViewRotation = FRotator::ZeroRotator;
	if (!ViewAdapter->CalculateView(InViewport, InContextNum, OriginSpaceViewLocation, OriginSpaceViewRotation, ViewOffset, WorldScale, NCP, FCP))
	{
		if (!IsEditorOperationMode())
		{
			UE_LOG(LogDisplayClusterProjectionEasyBlend, Warning, TEXT("Couldn't compute view info for <%s> viewport"), *InViewport->GetId());
		}
		return false;
	}

	// Convert rotation back from origin to world space
	InOutViewRotation = World2LocalTransform.TransformRotation(OriginSpaceViewRotation.Quaternion()).Rotator();

	return true;
}

bool FDisplayClusterProjectionEasyBlendPolicyBase::GetProjectionMatrix(IDisplayClusterViewport* InViewport, const uint32 InContextNum, FMatrix& OutPrjMatrix)
{
	check(IsInGameThread());

	if (!ViewAdapter.IsValid() || !IsEasyBlendRenderingEnabled())
	{
		return false;
	}

	// Pass request to the adapter
	return ViewAdapter->GetProjectionMatrix(InViewport, InContextNum, OutPrjMatrix);
}

//////////////////////////////////////////////////////////////////////////////////////////////
// FDisplayClusterProjectionEasyBlendPolicy
//////////////////////////////////////////////////////////////////////////////////////////////
bool FDisplayClusterProjectionEasyBlendPolicyBase::ReadConfigData(const FString& InViewportId, FString& OutFile, FString& OutOrigin, float& OutGeometryScale)
{
	// EasyBlend file (mandatory)
	if (DisplayClusterHelpers::map::template ExtractValueFromString(GetParameters(), DisplayClusterProjectionStrings::cfg::easyblend::File, OutFile))
	{
		UE_LOG(LogDisplayClusterProjectionEasyBlend, Verbose, TEXT("Viewport <%s>: Projection parameter '%s' - '%s'"), *InViewportId, DisplayClusterProjectionStrings::cfg::easyblend::File, *OutFile);
	}
	else
	{
		if (!IsEditorOperationMode())
		{
			UE_LOG(LogDisplayClusterProjectionEasyBlend, Error, TEXT("Viewport <%s>: Projection parameter '%s' not found"), *InViewportId, DisplayClusterProjectionStrings::cfg::easyblend::File);
		}
		return false;
	}
	
	// Origin node (optional)
	if (DisplayClusterHelpers::map::template ExtractValueFromString(GetParameters(), DisplayClusterProjectionStrings::cfg::easyblend::Origin, OutOrigin))
	{
		UE_LOG(LogDisplayClusterProjectionEasyBlend, Verbose, TEXT("Viewport <%s>: Projection parameter '%s' - '%s'"), *InViewportId, DisplayClusterProjectionStrings::cfg::easyblend::Origin, *OutOrigin);
	}
	else
	{
		if (!IsEditorOperationMode())
		{
			UE_LOG(LogDisplayClusterProjectionEasyBlend, Log, TEXT("Viewport <%s>: Projection parameter '%s' not found"), *InViewportId, DisplayClusterProjectionStrings::cfg::easyblend::Origin);
		}
	}

	// Geometry scale (optional)
	if (DisplayClusterHelpers::map::template ExtractValueFromString(GetParameters(), DisplayClusterProjectionStrings::cfg::easyblend::Scale, OutGeometryScale))
	{
		UE_LOG(LogDisplayClusterProjectionEasyBlend, Verbose, TEXT("Viewport <%s>: Projection parameter '%s' - %f"), *InViewportId, DisplayClusterProjectionStrings::cfg::easyblend::Scale, OutGeometryScale);
	}
	else
	{
		if (!IsEditorOperationMode())
		{
			UE_LOG(LogDisplayClusterProjectionEasyBlend, Log, TEXT("Viewport <%s>: Projection parameter '%s' not found"), *InViewportId, DisplayClusterProjectionStrings::cfg::easyblend::Scale);
		}
	}

	return true;
}
