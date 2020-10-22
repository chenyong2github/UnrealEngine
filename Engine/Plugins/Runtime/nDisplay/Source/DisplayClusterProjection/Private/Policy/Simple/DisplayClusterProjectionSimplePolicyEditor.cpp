// Copyright Epic Games, Inc. All Rights Reserved.

#include "Policy/Simple/DisplayClusterProjectionSimplePolicy.h"

#include "Misc/DisplayClusterHelpers.h"

#include "DisplayClusterConfigurationTypes.h"
#include "DisplayClusterProjectionStrings.h"

#include "DisplayClusterRootActor.h"
#include "Components/DisplayClusterCameraComponent.h"
#include "Components/DisplayClusterScreenComponent.h"
#include "Components/StaticMeshComponent.h"

#if WITH_EDITOR
#include "IDisplayClusterRendering.h"
#endif


UDisplayClusterProjectionPolicySimpleParameters::UDisplayClusterProjectionPolicySimpleParameters()
	: Super()
#if WITH_EDITOR
	, RootActor(nullptr)
	, Screen(nullptr)
#endif
{
}

#if WITH_EDITOR
bool UDisplayClusterProjectionPolicySimpleParameters::Parse(ADisplayClusterRootActor* InRootActor, const FDisplayClusterConfigurationProjection& ConfigData)
{
	FScopeLock Lock(&InternalsSyncScope);

	if (RootActor)
	{
		return false;
	}

	// Get assigned screen ID
	FString ScreenId;
	if (!DisplayClusterHelpers::map::template ExtractValue(ConfigData.Parameters, DisplayClusterProjectionStrings::cfg::simple::Screen, ScreenId))
	{
		return false;
	}

	RootActor = InRootActor;
	Screen    = InRootActor->GetScreenById(ScreenId);

	return Screen != nullptr;
}


void FDisplayClusterProjectionSimplePolicy::InitializePreview(UDisplayClusterProjectionPolicyParameters* PolicyParameters)
{
	UDisplayClusterProjectionPolicySimpleParameters* SimplePolicyParams = Cast<UDisplayClusterProjectionPolicySimpleParameters>(PolicyParameters);
	if (SimplePolicyParams && SimplePolicyParams->Screen)
	{
		ViewData.AddDefaulted(1);
		ScreenComp = SimplePolicyParams->Screen;
	}
}

UDisplayClusterProjectionPolicyParameters* FDisplayClusterProjectionSimplePolicy::CreateParametersObject(UObject* Owner)
{
	return NewObject<UDisplayClusterProjectionPolicySimpleParameters>(Owner);
}

UMeshComponent* FDisplayClusterProjectionSimplePolicy::BuildMeshPreview(UDisplayClusterProjectionPolicyParameters* PolicyParameters)
{
	UDisplayClusterProjectionPolicySimpleParameters* SimplePolicyParams = Cast<UDisplayClusterProjectionPolicySimpleParameters>(PolicyParameters);
	if (SimplePolicyParams && SimplePolicyParams->Screen)
	{
		return SimplePolicyParams->Screen->VisScreenComponent;
	}

	return nullptr;
}

void FDisplayClusterProjectionSimplePolicy::RenderFrame(USceneComponent* Camera, UDisplayClusterProjectionPolicyParameters* PolicyParameters, FTextureRenderTargetResource* RenderTarget, FIntRect RenderRegion, bool bApplyWarpBlend)
{
	if (RenderTarget && PolicyParameters && ViewData.Num() > 0)
	{
		UDisplayClusterProjectionPolicySimpleParameters* SimplePolicyParams = Cast<UDisplayClusterProjectionPolicySimpleParameters>(PolicyParameters);
		if (SimplePolicyParams)
		{
			FScopeLock Lock(&SimplePolicyParams->InternalsSyncScope);
			if (SimplePolicyParams->Screen)
			{
				FDisplayClusterRenderingParameters PreviewParameters;

				ScreenComp = SimplePolicyParams->Screen;

				PreviewParameters.ViewLocation = Camera->GetComponentLocation();
				PreviewParameters.ViewRotation = SimplePolicyParams->Screen->GetComponentRotation();
				PreviewParameters.RenderTarget = RenderTarget;
				PreviewParameters.RenderTargetRect = RenderRegion;
				PreviewParameters.ProjectionPolicy = this;
				PreviewParameters.ProjectionMatrix = FMatrix::Identity;
				PreviewParameters.bAllowWarpBlend = false;
				PreviewParameters.HiddenActors.Add(SimplePolicyParams->RootActor);

				UWorld* World = Camera->GetWorld();
				PreviewParameters.Scene = World->Scene;

				if (CalculateView(0, PreviewParameters.ViewLocation, PreviewParameters.ViewRotation, FVector::ZeroVector, World->GetWorldSettings()->WorldToMeters, 1.f, 1.f))
				{
					if (GetProjectionMatrix(0, PreviewParameters.ProjectionMatrix))
					{
						IDisplayClusterRendering::Get().RenderSceneToTexture(PreviewParameters);
					}
				}
			}
		}
	}
}

#endif
