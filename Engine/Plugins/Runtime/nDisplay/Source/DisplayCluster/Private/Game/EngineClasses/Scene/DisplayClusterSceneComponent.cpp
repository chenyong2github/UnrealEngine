// Copyright Epic Games, Inc. All Rights Reserved.

#include "Components/DisplayClusterSceneComponent.h"

#include "Components/DisplayClusterRootComponent.h"

#include "Config/IPDisplayClusterConfigManager.h"
#include "Input/IPDisplayClusterInputManager.h"

#include "DisplayClusterConfigurationTypes.h"

#include "Misc/DisplayClusterGlobals.h"
#include "Misc/DisplayClusterHelpers.h"
#include "Misc/DisplayClusterLog.h"
#include "Misc/DisplayClusterStrings.h"


UDisplayClusterSceneComponent::UDisplayClusterSceneComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, TrackerChannel(-1)
{
	// Children of UDisplayClusterSceneComponent must always Tick to be able to process VRPN tracking
	PrimaryComponentTick.bCanEverTick = true;
}

void UDisplayClusterSceneComponent::BeginPlay()
{
	Super::BeginPlay();

	if (ConfigData)
	{
		FString  CfgParentId = ConfigData->ParentId;
		FVector  CfgLocation = ConfigData->Location;
		FRotator CfgRotation = ConfigData->Rotation;
		
		TrackerId      = ConfigData->TrackerId;
		TrackerChannel = ConfigData->TrackerChannel;

		// Take place in hierarchy
		if (!CfgParentId.IsEmpty())
		{
			UDisplayClusterRootComponent* const RootComp = Cast<UDisplayClusterRootComponent>(GetAttachParent());
			if (RootComp)
			{
				UDisplayClusterSceneComponent* const pComp = RootComp->GetComponentById(CfgParentId);
				if (pComp)
				{
					UE_LOG(LogDisplayClusterGame, Log, TEXT("Attaching %s to %s"), *GetId(), *CfgParentId);
					AttachToComponent(pComp, FAttachmentTransformRules(EAttachmentRule::KeepRelative, false));
				}
				else
				{
					UE_LOG(LogDisplayClusterGame, Warning, TEXT("Couldn't attach %s to %s"), *GetId(), *CfgParentId);
				}
			}
		}

		// Set up location and rotation
		SetRelativeLocationAndRotation(CfgLocation, CfgRotation);
	}
}

void UDisplayClusterSceneComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);
}

void UDisplayClusterSceneComponent::TickComponent( float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction )
{
	// Update transform if attached to a tracker
	if (!TrackerId.IsEmpty())
	{
		const IPDisplayClusterInputManager* const InputMgr = GDisplayCluster->GetPrivateInputMgr();
		if (InputMgr)
		{
			FVector Location;
			FQuat Rotation;
			const bool bLocAvail = InputMgr->GetTrackerLocation(TrackerId, TrackerChannel, Location);
			const bool bRotAvail = InputMgr->GetTrackerQuat(TrackerId, TrackerChannel, Rotation);

			if (bLocAvail && bRotAvail)
			{
				UE_LOG(LogDisplayClusterGame, Verbose, TEXT("%s[%s] update from tracker %s:%d - {loc %s} {quat %s}"),
					*GetName(), *GetId(), *TrackerId, TrackerChannel, *Location.ToString(), *Rotation.ToString());

				// Update transform
				this->SetRelativeLocationAndRotation(Location, Rotation);
				// Force child transforms update
				UpdateChildTransforms(EUpdateTransformFlags::PropagateFromParent);
			}
		}
	}

	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
}
