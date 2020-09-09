// Copyright Epic Games, Inc. All Rights Reserved.

#include "LuminARImageTracker.h"
#include "IMagicLeapImageTrackerModule.h"
#include "LuminARTrackingSystem.h"

FLuminARImageTracker::FLuminARImageTracker(FLuminARImplementation& InARSystemSupport)
: ILuminARTracker(InARSystemSupport)
, bAttemptedTrackerCreation(false)
{
	SuccessDelegate.BindRaw(this, &FLuminARImageTracker::OnSetImageTargetSucceeded);
}

FLuminARImageTracker::~FLuminARImageTracker()
{
	TrackedTargetNames.Empty();
}

void FLuminARImageTracker::CreateEntityTracker()
{
	AddPredefinedCandidateImages();
}

void FLuminARImageTracker::DestroyEntityTracker()
{
	IMagicLeapImageTrackerModule::Get().DestroyTracker();
	TrackedTargetNames.Empty();
	bAttemptedTrackerCreation = false;
}

void FLuminARImageTracker::OnStartGameFrame()
{
	FMagicLeapImageTargetState TargetState;
	for (const auto& TargetPair : TrackedTargetNames)
	{
		UARTrackedGeometry* ARTrackedGeometry = ARSystemSupport->GetOrCreateTrackableFromHandle<UARTrackedImage>(TargetPair.Key);

		if (ARTrackedGeometry && ARTrackedGeometry->GetTrackingState() != EARTrackingState::StoppedTracking)
		{
			IMagicLeapImageTrackerModule::Get().GetTargetState(TargetPair.Value->GetFriendlyName(), true, TargetState);

			EARTrackingState NewState = (TargetState.TrackingStatus == EMagicLeapImageTargetStatus::NotTracked) ? EARTrackingState::NotTracking : EARTrackingState::Tracking;

			ARTrackedGeometry->SetTrackingState(NewState);

			// TODO: this whole system of ARTrackedGeometry, TrackedResource, UpdateGeometryData, TrackingState etc etc is very convoluted. Simplify and document.
			if (NewState == EARTrackingState::Tracking)
			{
				FLuminARTrackedImageResource* TrackableResource = static_cast<FLuminARTrackedImageResource*>(ARTrackedGeometry->GetNativeResource());
				TrackableResource->UpdateTrackerData(ARSystemSupport, TargetState);
			}
		}
	}
}

bool FLuminARImageTracker::IsHandleTracked(const FGuid& Handle) const
{
	return TrackedTargetNames.Contains(Handle);
}

UARTrackedGeometry* FLuminARImageTracker::CreateTrackableObject()
{
	return NewObject<UARTrackedImage>();
}

IARRef* FLuminARImageTracker::CreateNativeResource(const FGuid& Handle, UARTrackedGeometry* TrackableObject)
{
	return new FLuminARTrackedImageResource(Handle, TrackableObject, *this, TrackedTargetNames[Handle]);
}

void FLuminARImageTracker::AddCandidateImageForTracking(UARCandidateImage* NewCandidateImage)
{
	// This delays image tracker creation until first candidate image is added.
	EnableImageTracker();

	FMagicLeapImageTargetSettings Target;
	Target.ImageTexture = NewCandidateImage->GetCandidateTexture();
	Target.Name = NewCandidateImage->GetFriendlyName();

	const float ImageWidth = NewCandidateImage->GetPhysicalWidth();
	const float ImageHeight = NewCandidateImage->GetPhysicalHeight();
	Target.LongerDimension = (ImageWidth > ImageHeight ? ImageWidth : ImageHeight);

	ULuminARCandidateImage* LuminCandidateImage = Cast<ULuminARCandidateImage>(NewCandidateImage);
	// If the candidate image isn't a ULuminARCandidateImage there is no data about whether the image is stationary so assume false, per the is_stationary documentation
	Target.bIsStationary = LuminCandidateImage ? LuminCandidateImage->GetImageIsStationary() : false;
	Target.bIsEnabled = true;

	IMagicLeapImageTrackerModule::Get().SetTargetAsync(Target, SuccessDelegate, FMagicLeapSetImageTargetCompletedStaticDelegate());
}

UClass* FLuminARImageTracker::GetARComponentClass(const UARSessionConfig& SessionConfig)
{
	return SessionConfig.GetImageComponentClass();
}

void FLuminARImageTracker::OnSetImageTargetSucceeded(const FString& TargetName)
{
	const FGuid TargetHandle = IMagicLeapImageTrackerModule::Get().GetTargetHandle(TargetName);
	const UARSessionConfig& ARSessionConfig = ARSystemSupport->GetARSystem()->AccessSessionConfig();
	const TArray<UARCandidateImage*> CandidateImages = ARSessionConfig.GetCandidateImageList();

	for (UARCandidateImage* CandidateImage : CandidateImages)
	{
		if (TargetName.Compare(CandidateImage->GetFriendlyName()) == 0)
		{
			TrackedTargetNames.Add(TargetHandle, CandidateImage);
			break;
		}
	}
}

void FLuminARImageTracker::AddPredefinedCandidateImages()
{
	const UARSessionConfig& ARSessionConfig = ARSystemSupport->GetARSystem()->AccessSessionConfig();
	const TArray<UARCandidateImage*> CandidateImages = ARSessionConfig.GetCandidateImageList();

	for (UARCandidateImage* CandidateImage : CandidateImages)
	{
		AddCandidateImageForTracking(CandidateImage);
	}
}

void FLuminARImageTracker::EnableImageTracker()
{
	// TODO : attempt tracker creation only once per ARSession. Fix bug in IMagicLeapImageTrackerModule to account for multipe consecutive calls to Get/SetImageTrackerEnabled()
	if (!bAttemptedTrackerCreation)
	{
		bAttemptedTrackerCreation = true;
		IMagicLeapImageTrackerModule& ImageTrackerModule = IMagicLeapImageTrackerModule::Get();
		const bool bImageTrackerEnabled = ImageTrackerModule.GetImageTrackerEnabled();
		if (!bImageTrackerEnabled)
		{
			ImageTrackerModule.SetImageTrackerEnabled(true);
		}
	}
}

void FLuminARTrackedImageResource::UpdateTrackerData(FLuminARImplementation* InARSystemSupport, const FMagicLeapImageTargetState& TargetState)
{
	if (InARSystemSupport == nullptr || CandidateImage == nullptr || TrackedGeometry->GetTrackingState() == EARTrackingState::StoppedTracking)
	{
		return;
	}

	UpdateGeometryData(InARSystemSupport);

	const ULuminARCandidateImage* LuminCandidateImage = Cast<ULuminARCandidateImage>(CandidateImage);

	// Account for unreliable pose
	if (TargetState.TrackingStatus == EMagicLeapImageTargetStatus::Unreliable)
	{
		if (LuminCandidateImage != nullptr)
		{
			if (!LuminCandidateImage->GetUseUnreliablePose())
			{
				return;
			}
		}
		else
		{
			const ULuminARSessionConfig* LuminARSessionConfig = Cast<ULuminARSessionConfig>(&InARSystemSupport->GetARSystem()->AccessSessionConfig());
			if (LuminARSessionConfig != nullptr)
			{
				if (!LuminARSessionConfig->bDefaultUseUnreliablePose)
				{
					return;
				}
			}
		}
	}

	FTransform LocalToTrackingTransform(TargetState.Rotation, TargetState.Location);
	// If its a normal ARCandidateImage or if the axis orientation is not ForwardAxisAsNormal
	if (!(LuminCandidateImage != nullptr && LuminCandidateImage->GetAxisOrientation() == EMagicLeapImageTargetOrientation::ForwardAxisAsNormal))
	{
		// Rotate -180 degrees around Z. This makes Y axis point to our Right, X is our Forward, Z is Up.
		LocalToTrackingTransform.ConcatenateRotation(FQuat(FVector(0, 0, 1), -PI));
		// Rotate -90 degrees around Y. This makes Z axis point to our Back, X is our Up, Y is Right.
		LocalToTrackingTransform.ConcatenateRotation(FQuat(FVector(0, 1, 0), -PI/2));
	}

	const uint32 FrameNum = InARSystemSupport->GetFrameNum();
	const int64 TimeStamp = InARSystemSupport->GetCameraTimestamp();

	UARTrackedImage* Image = CastChecked<UARTrackedImage>(TrackedGeometry);
	Image->UpdateTrackedGeometry(InARSystemSupport->GetARSystem(), FrameNum, static_cast<double>(TimeStamp), LocalToTrackingTransform, InARSystemSupport->GetARSystem()->GetAlignmentTransform(), FVector2D(0.f), CandidateImage);
	Image->SetDebugName(FName(*(CandidateImage->GetFriendlyName())));
}
