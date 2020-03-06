// Copyright Epic Games, Inc. All Rights Reserved.

#include "LuminARImageTracker.h"
#include "IMagicLeapImageTrackerModule.h"
#include "LuminARTrackingSystem.h"

FLuminARImageTracker::FLuminARImageTracker(FLuminARImplementation& InARSystemSupport)
: ILuminARTracker(InARSystemSupport)
{}

FLuminARImageTracker::~FLuminARImageTracker()
{
	TrackedTargetNames.Empty();
}

void FLuminARImageTracker::CreateEntityTracker()
{
	IMagicLeapImageTrackerModule& ImageTrackerModule = IMagicLeapImageTrackerModule::Get();
	bool bImageTrackerEnabled = ImageTrackerModule.GetImageTrackerEnabled();
	if (!bImageTrackerEnabled)
	{
		ImageTrackerModule.SetImageTrackerEnabled(true);
	}
}

void FLuminARImageTracker::DestroyEntityTracker()
{
	IMagicLeapImageTrackerModule::Get().DestroyTracker();
	TrackedTargetNames.Empty();
}

void FLuminARImageTracker::OnStartGameFrame()
{
	TArray<FGuid> ImageGuids;
	TrackedTargetNames.GenerateKeyArray(ImageGuids);

	for (const FGuid& ImageID : ImageGuids)
	{
		UARTrackedGeometry* ARTrackedGeometry = ARSystemSupport->GetOrCreateTrackableFromHandle<UARTrackedImage>(ImageID);

		if (ARTrackedGeometry && ARTrackedGeometry->GetTrackingState() != EARTrackingState::StoppedTracking)
		{
			const FString* TargetName = GetTargetNameFromHandle(ImageID);
			EARTrackingState NewState = EARTrackingState::NotTracking;

			if (TargetName)
			{
				NewState = IMagicLeapImageTrackerModule::Get().IsTracked(*TargetName) ? EARTrackingState::Tracking : EARTrackingState::NotTracking;
			}

			ARTrackedGeometry->SetTrackingState(NewState);

			if (NewState == EARTrackingState::Tracking)
			{
				FLuminARTrackableResource* TrackableResource = static_cast<FLuminARTrackableResource*>(ARTrackedGeometry->GetNativeResource());
				TrackableResource->UpdateGeometryData(ARSystemSupport);
			}
		}
	}
}

void FLuminARImageTracker::OnSetImageTargetSucceeded(FMagicLeapImageTrackerTarget& Target)
{
#if WITH_MLSDK
	TrackedTargetNames.Add(MagicLeap::MLHandleToFGuid(Target.Handle), Target.Name);
#endif
}

bool FLuminARImageTracker::IsHandleTracked(const FGuid& Handle) const
{
	return TrackedTargetNames.Contains(Handle);
}

const FString* FLuminARImageTracker::GetTargetNameFromHandle(const FGuid& Handle) const
{
	if (IsHandleTracked(Handle))
	{
		return TrackedTargetNames.Find(Handle);
	}

	return nullptr;
}

UARTrackedGeometry* FLuminARImageTracker::CreateTrackableObject()
{
	return NewObject<UARTrackedImage>();
}

IARRef* FLuminARImageTracker::CreateNativeResource(const FGuid& Handle, UARTrackedGeometry* TrackableObject)
{
	return new FLuminARTrackedImageResource(Handle, TrackableObject, *this);
}

void FLuminARTrackedImageResource::UpdateGeometryData(FLuminARImplementation* InARSystemSupport)
{
	FLuminARTrackableResource::UpdateGeometryData(InARSystemSupport);

	UARTrackedImage* Image = CastChecked<UARTrackedImage>(TrackedGeometry);

	if (!InARSystemSupport || TrackedGeometry->GetTrackingState() == EARTrackingState::StoppedTracking)
	{
		return;
	}

	IMagicLeapImageTrackerModule& ImageTrackerModule = IMagicLeapImageTrackerModule::Get();
	FVector Position;
	FRotator Orientation;
	const FString* TargetName = Tracker.GetTargetNameFromHandle(TrackableHandle);
	if (!ImageTrackerModule.TryGetRelativeTransform(*TargetName, Position, Orientation))
	{
		return;
	}
	UARCandidateImage* DetectedImage = InARSystemSupport->GetCandidateImage(*TargetName);
	if (DetectedImage == nullptr)
	{
		return;
	}

	const FTransform LocalToTrackingTransform(Orientation, Position);

	const uint32 FrameNum = InARSystemSupport->GetFrameNum();
	const int64 TimeStamp = InARSystemSupport->GetCameraTimestamp();

	Image->UpdateTrackedGeometry(InARSystemSupport->GetARSystem(), FrameNum, static_cast<double>(TimeStamp), LocalToTrackingTransform, InARSystemSupport->GetARSystem()->GetAlignmentTransform(), FVector2D(0.f), DetectedImage);
	Image->SetDebugName(FName(*GetNativeHandle().ToString()));
}
