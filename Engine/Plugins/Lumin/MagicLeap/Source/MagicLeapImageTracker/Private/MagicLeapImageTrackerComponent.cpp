// Copyright Epic Games, Inc. All Rights Reserved.

#include "MagicLeapImageTrackerComponent.h"
#include "IMagicLeapPlugin.h"
#include "MagicLeapImageTrackerModule.h"
#include "MagicLeapImageTrackerRunnable.h"
#include "Misc/Guid.h"
#if WITH_EDITOR
#include "Editor.h"
#endif

UMagicLeapImageTrackerComponent::UMagicLeapImageTrackerComponent()
: AxisOrientation(EMagicLeapImageTargetOrientation::ForwardAxisAsNormal)
, bTargetSet(false)
, LastStatus(EMagicLeapImageTargetStatus::NotTracked)
#if WITH_EDITOR
, TextureBeforeEdit(nullptr)
#endif // WITH_EDITOR
{
	// Make sure this component ticks
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;

	bAutoActivate = true;
}

void UMagicLeapImageTrackerComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	RemoveTargetAsync();
	Super::EndPlay(EndPlayReason);
}

void UMagicLeapImageTrackerComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!bTargetSet)
	{
		UTexture2D* TempTargetImageTexture = TargetImageTexture;
		// Because SetTargetAsync() can also be called from outside this component,
		// it checks if the input texture is same as the existing TargetImageTexture and skips setting the target
		// if its the same. So, we reset TargetImageTexture to null here to bypass that check.
		TargetImageTexture = nullptr;
		bTargetSet = SetTargetAsync(TempTargetImageTexture);
	}
	else
	{
		FMagicLeapImageTargetState TargetState;
		GetMagicLeapImageTrackerModule().GetTargetState(Name, true /* bProvideTransformInTrackingSpace */, TargetState);
		// GetTargetState() reports orientation with ForwardAxisAsNormal
		if (AxisOrientation == EMagicLeapImageTargetOrientation::UpAxisAsNormal)
		{
			FQuat CurrentOrientation = TargetState.Rotation.Quaternion();
			// Rotate -180 degrees around Z. This makes Y axis point to our Right, X is our Forward, Z is Up.
			CurrentOrientation = CurrentOrientation * FQuat(FVector(0, 0, 1), -PI);
			// Rotate -90 degrees around Y. This makes Z axis point to our Back, X is our Up, Y is Right.
			CurrentOrientation = CurrentOrientation * FQuat(FVector(0, 1, 0), -PI / 2);
			TargetState.Rotation = CurrentOrientation.Rotator();
		}

		switch (TargetState.TrackingStatus)
		{
			case EMagicLeapImageTargetStatus::NotTracked:
			{
				if (LastStatus != TargetState.TrackingStatus)
				{
					OnImageTargetLost.Broadcast();
				}
				break;
			}
			case EMagicLeapImageTargetStatus::Tracked:
			{
				// set transform before firing the "found" event
				SetRelativeLocationAndRotation(TargetState.Location, TargetState.Rotation);

				if (LastStatus != TargetState.TrackingStatus)
				{
					OnImageTargetFound.Broadcast();
				}

				break;
			}
			case EMagicLeapImageTargetStatus::Unreliable:
			{
				const FVector LastTrackedLocation = GetComponentLocation();
				const FRotator LastTrackedRotation = GetComponentRotation();
				if (bUseUnreliablePose)
				{
					SetRelativeLocationAndRotation(TargetState.Location, TargetState.Rotation);
				}

				// Developer can choose whether to use this unreliable pose or not.
				OnImageTargetUnreliableTracking.Broadcast(LastTrackedLocation, LastTrackedRotation, TargetState.Location, TargetState.Rotation);

				break;
			}
		}

		LastStatus = TargetState.TrackingStatus;
	}
}

bool UMagicLeapImageTrackerComponent::SetTargetAsync(UTexture2D* ImageTarget)
{
	if (ImageTarget == nullptr)
	{
		UE_LOG(LogMagicLeapImageTracker, Warning, TEXT("ImageTarget is NULL!."));
		return false;
	}

	if (ImageTarget && !(ImageTarget->GetPixelFormat() == EPixelFormat::PF_R8G8B8A8 || ImageTarget->GetPixelFormat() == EPixelFormat::PF_B8G8R8A8))
	{
		UE_LOG(LogMagicLeapImageTracker, Error, TEXT("Cannot set texture %s as it uses an invalid pixel format!  Valid formats are R8B8G8A8 or B8G8R8A8"), *ImageTarget->GetName());
		return false;
	}

	if (ImageTarget == TargetImageTexture)
	{
		UE_LOG(LogMagicLeapImageTracker, Error, TEXT("Skipped setting %s as it is already being used as the current image target"), *ImageTarget->GetName());
		return false;
	}

	TargetImageTexture = ImageTarget;
	if (Name.Len() == 0)
	{
		Name = FGuid::NewGuid().ToString();
	}

	FMagicLeapImageTargetSettings TargetSettings;
	TargetSettings.ImageTexture = TargetImageTexture;
	TargetSettings.Name = Name;
	TargetSettings.LongerDimension = LongerDimension;
	TargetSettings.bIsStationary = bIsStationary;
	// TODO : expose option to pause tracking for this specific target
	TargetSettings.bIsEnabled = true;

	GetMagicLeapImageTrackerModule().SetTargetAsync(TargetSettings, OnSetImageTargetSucceeded, OnSetImageTargetFailed);

	return true;
}

bool UMagicLeapImageTrackerComponent::RemoveTargetAsync()
{
	bTargetSet = false;
	return GetMagicLeapImageTrackerModule().RemoveTargetAsync(Name);
}

#if WITH_EDITOR
void UMagicLeapImageTrackerComponent::PreEditChange(FProperty* PropertyAboutToChange)
{
	if ((PropertyAboutToChange != nullptr) && (PropertyAboutToChange->GetFName() == GET_MEMBER_NAME_CHECKED(UMagicLeapImageTrackerComponent, TargetImageTexture)))
	{
		TextureBeforeEdit = TargetImageTexture;
	}

	Super::PreEditChange(PropertyAboutToChange);
}

void UMagicLeapImageTrackerComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	const FName PropertyName = (PropertyChangedEvent.Property != nullptr) ? PropertyChangedEvent.Property->GetFName() : NAME_None;

	if (PropertyName == GET_MEMBER_NAME_CHECKED(UMagicLeapImageTrackerComponent, TargetImageTexture))
	{
		if (TargetImageTexture && !(TargetImageTexture->GetPixelFormat() == EPixelFormat::PF_R8G8B8A8 || TargetImageTexture->GetPixelFormat() == EPixelFormat::PF_B8G8R8A8))
		{
			UE_LOG(LogMagicLeapImageTracker, Error, TEXT("[UImageTrackerComponent] Cannot set texture %s as it uses an invalid pixel format!  Valid formats are R8B8G8A8 or B8G8R8A8"), *TargetImageTexture->GetName());
			TargetImageTexture = TextureBeforeEdit;
		}
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif
