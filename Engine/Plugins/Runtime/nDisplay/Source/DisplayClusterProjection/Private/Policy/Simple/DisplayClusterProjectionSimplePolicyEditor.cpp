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
	FString InScreenId;
	if (!DisplayClusterHelpers::map::template ExtractValue(ConfigData.Parameters, DisplayClusterProjectionStrings::cfg::simple::Screen, InScreenId))
	{
		return false;
	}

	RootActor = InRootActor;
	ScreenId  = InScreenId;

	return HasScreenComponent();
}


void FDisplayClusterProjectionSimplePolicy::InitializePreview(UDisplayClusterProjectionPolicyParameters* PolicyParameters)
{
	UDisplayClusterProjectionPolicySimpleParameters* SimplePolicyParams = Cast<UDisplayClusterProjectionPolicySimpleParameters>(PolicyParameters);
	if (SimplePolicyParams && SimplePolicyParams->HasScreenComponent())
	{
		ViewData.AddDefaulted(1);
		ScreenComp = SimplePolicyParams->GetScreenComponent();
	}
}

UDisplayClusterProjectionPolicyParameters* FDisplayClusterProjectionSimplePolicy::CreateParametersObject(UObject* Owner)
{
	return NewObject<UDisplayClusterProjectionPolicySimpleParameters>(Owner);
}

UMeshComponent* FDisplayClusterProjectionSimplePolicy::BuildMeshPreview(UDisplayClusterProjectionPolicyParameters* PolicyParameters)
{
	if(UDisplayClusterProjectionPolicySimpleParameters* SimplePolicyParams = Cast<UDisplayClusterProjectionPolicySimpleParameters>(PolicyParameters))
	{
		if (UDisplayClusterScreenComponent* ScreenComponent = SimplePolicyParams->GetScreenComponent())
		{
			return ScreenComponent->VisScreenComponent;
		}
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
			if (SimplePolicyParams->HasScreenComponent())
			{
				FDisplayClusterRenderingParameters PreviewParameters;

				ScreenComp = SimplePolicyParams->GetScreenComponent();
				
				if (ScreenComp == nullptr)
				{
					// TODO: Verify deleting components in the editor cleans up this policy.
					return;
				}
				
				PreviewParameters.ViewLocation = Camera->GetComponentLocation();
				PreviewParameters.ViewRotation = ScreenComp->GetComponentRotation();
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
