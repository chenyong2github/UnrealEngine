// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "MagicLeapImageTrackerComponent.h"
#include "IMagicLeapPlugin.h"
#include "MagicLeapImageTrackerModule.h"
#include "MagicLeapImageTrackerRunnable.h"
#if WITH_EDITOR
#include "Editor.h"
#endif

UMagicLeapImageTrackerComponent::UMagicLeapImageTrackerComponent()
: bIsTracking(false)
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

	if (!bIsTracking)
	{
		UTexture2D* TempTargetImageTexture = TargetImageTexture;
		TargetImageTexture = nullptr;
		bIsTracking = SetTargetAsync(TempTargetImageTexture);
	}
	else
	{
		FVector Location;
		FRotator Rotation;
		if (GetMagicLeapImageTrackerModule().TryGetRelativeTransform(Name, Location, Rotation))
		{
			this->SetRelativeLocationAndRotation(Location, Rotation);
		}
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
	if (Name.Len() == 0) Name = GetFullGroupName(true);

	FMagicLeapImageTrackerTarget Target;
	Target.Name = Name;
#if WITH_MLSDK
	Target.Settings.longer_dimension = LongerDimension / IMagicLeapPlugin::Get().GetWorldToMetersScale();
	Target.Settings.is_stationary = bIsStationary;
#endif // WITH_MLSDK
	Target.Texture = TargetImageTexture;
	Target.bUseUnreliablePose = bUseUnreliablePose;
	Target.OnSetImageTargetSucceeded = OnSetImageTargetSucceeded;
	Target.OnSetImageTargetFailed = OnSetImageTargetFailed;
	Target.OnImageTargetFound = OnImageTargetFound;
	Target.OnImageTargetLost = OnImageTargetLost;
	Target.OnImageTargetUnreliableTracking = OnImageTargetUnreliableTracking;

	GetMagicLeapImageTrackerModule().SetTargetAsync(Target);
	return true;
}

bool UMagicLeapImageTrackerComponent::RemoveTargetAsync()
{
	bIsTracking = false;
	return GetMagicLeapImageTrackerModule().RemoveTargetAsync(Name);
}

#if WITH_EDITOR
void UMagicLeapImageTrackerComponent::PreEditChange(UProperty* PropertyAboutToChange)
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
