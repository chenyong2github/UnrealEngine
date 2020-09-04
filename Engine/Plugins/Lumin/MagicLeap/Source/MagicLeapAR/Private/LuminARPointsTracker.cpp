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

UClass* FLuminARPointsTracker::GetARComponentClass(const UARSessionConfig& SessionConfig)
{
	return SessionConfig.GetPointComponentClass();
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

	const uint32 FrameNum = InARSystemSupport->GetFrameNum();
	const int64 TimeStamp = InARSystemSupport->GetCameraTimestamp();
	FTransform LocalToTrackingTransform;

	if (!bResult)
	{
		// Something bad happend!
		// We can choose to mark this point's tracking state as "StoppedTracking" based on bPointFoundInEnvironment but that will happen automatically on next tick.

		// In rare situations (usually at the end of a level transition), blueprint may call functions that require a valid ARSystem even though the above function
		// may have returned false.  So we still need to update the transform, but we just update it to what it currently is.
		LocalToTrackingTransform = Point->GetLocalToTrackingTransform();
	}
	else
	{
		LocalToTrackingTransform = FTransform(Orientation, Position);
	}

	Point->UpdateTrackedGeometry(InARSystemSupport->GetARSystem(), FrameNum, static_cast<double>(TimeStamp), LocalToTrackingTransform, InARSystemSupport->GetARSystem()->GetAlignmentTransform());
	Point->SetDebugName(FName(*GetNativeHandle().ToString()));
}
