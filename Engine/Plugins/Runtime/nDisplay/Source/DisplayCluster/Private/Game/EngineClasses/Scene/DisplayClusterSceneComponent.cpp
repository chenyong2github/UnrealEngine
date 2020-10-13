// Copyright Epic Games, Inc. All Rights Reserved.

#include "Components/DisplayClusterSceneComponent.h"

#include "Config/IPDisplayClusterConfigManager.h"
#include "Input/IPDisplayClusterInputManager.h"

#include "DisplayClusterRootActor.h"
#include "DisplayClusterConfigurationTypes.h"

#include "Misc/DisplayClusterGlobals.h"
#include "Misc/DisplayClusterHelpers.h"
#include "Misc/DisplayClusterLog.h"
#include "Misc/DisplayClusterStrings.h"
#include "Misc/DisplayClusterTypesConverter.h"

#if WITH_EDITOR
#include "Interfaces/IDisplayClusterConfiguratorToolkit.h"
#endif


UDisplayClusterSceneComponent::UDisplayClusterSceneComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, TrackerChannel(-1)
{
	// Children of UDisplayClusterSceneComponent must always Tick to be able to process VRPN tracking
	PrimaryComponentTick.bCanEverTick = true;
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
				UE_LOG(LogDisplayClusterGame, Verbose, TEXT("%s update from tracker %s:%d - {loc %s} {quat %s}"),
					*GetName(), *TrackerId, TrackerChannel, *Location.ToString(), *Rotation.ToString());

				// Update transform
				this->SetRelativeLocationAndRotation(Location, Rotation);
				// Force child transforms update
				UpdateChildTransforms(EUpdateTransformFlags::PropagateFromParent);
			}
		}
	}

	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
}

void UDisplayClusterSceneComponent::ApplyConfigurationData()
{
	if (ConfigData)
	{
		TrackerId = ConfigData->TrackerId;
		TrackerChannel = ConfigData->TrackerChannel;

		// Take place in hierarchy
		if (!ConfigData->ParentId.IsEmpty())
		{
			ADisplayClusterRootActor* const RootActor = Cast<ADisplayClusterRootActor>(GetOwner());
			if (RootActor)
			{
				UDisplayClusterSceneComponent* const ParentComp = RootActor->GetComponentById(ConfigData->ParentId);
				if (ParentComp)
				{
					UE_LOG(LogDisplayClusterGame, Log, TEXT("Attaching %s to %s"), *GetName(), *ParentComp->GetName());
					AttachToComponent(ParentComp, FAttachmentTransformRules(EAttachmentRule::KeepRelative, false));
				}
				else
				{
					UE_LOG(LogDisplayClusterGame, Warning, TEXT("Couldn't attach %s to %s"), *GetName(), *ConfigData->ParentId);
				}
			}
		}

		// Set up location and rotation
		SetRelativeLocationAndRotation(ConfigData->Location, ConfigData->Rotation);
	}
}

#if WITH_EDITOR
UObject* UDisplayClusterSceneComponent::GetObject() const
{
	return ConfigData;
}

bool UDisplayClusterSceneComponent::IsSelected()
{
	ADisplayClusterRootActor* RootActor = Cast<ADisplayClusterRootActor>(GetOwner());
	if (RootActor)
	{
		TSharedPtr<IDisplayClusterConfiguratorToolkit> Toolkit = RootActor->GetToolkit().Pin();
		if (Toolkit.IsValid())
		{
			const TArray<UObject*>& SelectedObjects = Toolkit->GetSelectedObjects();

			UObject* const* SelectedObject = SelectedObjects.FindByPredicate([this](const UObject* InObject)
			{
				return InObject == GetObject();
			});

			return SelectedObject != nullptr;
		}
	}

	return false;
}
#endif
