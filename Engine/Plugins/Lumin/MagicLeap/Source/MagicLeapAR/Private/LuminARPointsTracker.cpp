// Copyright Epic Games, Inc. All Rights Reserved.

#include "LuminARPointsTracker.h"
#include "MagicLeapARPinFunctionLibrary.h"
#include "LuminARTrackingSystem.h"

FLuminARPointsTracker::FLuminARPointsTracker(FLuminARImplementation& InARSystemSupport)
: ILuminARTracker(InARSystemSupport)
{}

void FLuminARPointsTracker::CreateEntityTracker()
{
	UMagicLeapARPinFunctionLibrary::CreateTracker();
}

void FLuminARPointsTracker::DestroyEntityTracker()
{
	UMagicLeapARPinFunctionLibrary::DestroyTracker();
	TrackedPoints.Empty();
}

void FLuminARPointsTracker::OnStartGameFrame()
{
	if (UMagicLeapARPinFunctionLibrary::IsTrackerValid())
	{
		TArray<FGuid> Points;
		EMagicLeapPassableWorldError Result = UMagicLeapARPinFunctionLibrary::GetAvailableARPins(-1, Points);
		if (Result == EMagicLeapPassableWorldError::None)
		{
			// Remove points that previously existed, but no longer do StoppedTracking.
			ARSystemSupport->RemoveHandleIf<UARTrackedPoint>([&Points](const FGuid& TrackingHandle) -> bool {
				return !Points.Contains(TrackingHandle);
			});

			TrackedPoints = Points;

			for (const FGuid& PointID : TrackedPoints)
			{
				UARTrackedGeometry* ARTrackedGeometry = ARSystemSupport->GetOrCreateTrackableFromHandle<UARTrackedPoint>(PointID);

				if (ARTrackedGeometry && ARTrackedGeometry->GetTrackingState() != EARTrackingState::StoppedTracking)
				{
					ARTrackedGeometry->SetTrackingState(EARTrackingState::Tracking);
					FLuminARTrackableResource* TrackableResource = static_cast<FLuminARTrackableResource*>(ARTrackedGeometry->GetNativeResource());
					TrackableResource->UpdateGeometryData(ARSystemSupport);
				}
			}
		}
	}
}

bool FLuminARPointsTracker::IsHandleTracked(const FGuid& Handle) const
{
	return TrackedPoints.Contains(Handle);
}

UARTrackedGeometry* FLuminARPointsTracker::CreateTrackableObject()
{
	return NewObject<UARTrackedPoint>();
}

IARRef* FLuminARPointsTracker::CreateNativeResource(const FGuid& Handle, UARTrackedGeometry* TrackableObject)
{
	return new FLuminARTrackedPointResource(Handle, TrackableObject);
}

void FLuminARTrackedPointResource::UpdateGeometryData(FLuminARImplementation* InARSystemSupport)
{
	FLuminARTrackableResource::UpdateGeometryData(InARSystemSupport);

	UARTrackedPoint* Point = CastChecked<UARTrackedPoint>(TrackedGeometry);

	if (!InARSystemSupport || TrackedGeometry->GetTrackingState() == EARTrackingState::StoppedTracking)
	{
		return;
	}

	FVector Position;
	FRotator Orientation;
	bool bPointFoundInEnvironment;
	const bool bResult = UMagicLeapARPinFunctionLibrary::GetARPinPositionAndOrientation_TrackingSpace(GetNativeHandle(), Position, Orientation, bPointFoundInEnvironment);

	if (!bResult)
	{
		// Something bad happend! don't update transform.
		// We can choose to mark this point's tracking state as "StoppedTracking" based on bPointFoundInEnvironment but that will happen automatically on next tick.
		return;
	}
	const FTransform LocalToTrackingTransform(Orientation, Position);

	const uint32 FrameNum = InARSystemSupport->GetFrameNum();
	const int64 TimeStamp = InARSystemSupport->GetCameraTimestamp();

	Point->UpdateTrackedGeometry(InARSystemSupport->GetARSystem(), FrameNum, static_cast<double>(TimeStamp), LocalToTrackingTransform, InARSystemSupport->GetARSystem()->GetAlignmentTransform());
	Point->SetDebugName(FName(*GetNativeHandle().ToString()));
}
