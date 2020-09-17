// Copyright Epic Games, Inc. All Rights Reserved.

#include "LuminARTrackingSystem.h"
#include "Engine/Engine.h"
#include "RHIDefinitions.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/WorldSettings.h"
#include "ARSessionConfig.h"
#include "ARLifeCycleComponent.h"

#include "IMagicLeapPlugin.h"
#include "HeadMountedDisplayFunctionLibrary.h"
#include "MagicLeapHMDFunctionLibrary.h"
#include "MagicLeapARPinFunctionLibrary.h"

#include "IMagicLeapImageTrackerModule.h"

#include "LuminARPlanesTracker.h"
#include "LuminARPointsTracker.h"
#include "LuminARLightTracker.h"
#include "LuminARImageTracker.h"

FLuminARImplementation::FLuminARImplementation()
	: bIsLuminARSessionRunning(false)
	, FrameNumber(0)
	, LatestCameraTimestamp(0)
	, LatestCameraTrackingState(ELuminARTrackingState::StoppedTracking)
	, bStartSessionRequested(false)
	, LastTrackedGeometry_DebugId(0)
	, CurrentSessionStatus(EARSessionStatus::NotStarted)
	, LightEstimateTracker(nullptr)
{
	Trackers.Add(new FLuminARPlanesTracker(*this));
	Trackers.Add(new FLuminARPointsTracker(*this));
	ImageTracker = new FLuminARImageTracker(*this);
	Trackers.Add(ImageTracker);

	SpawnARActorDelegateHandle = UARLifeCycleComponent::OnSpawnARActorDelegate.AddRaw(this, &FLuminARImplementation::OnSpawnARActor);
}

FLuminARImplementation::~FLuminARImplementation()
{
	UARLifeCycleComponent::OnSpawnARActorDelegate.Remove(SpawnARActorDelegateHandle);

	if (bIsLuminARSessionRunning)
	{
		OnStopARSession();
	}

	if (LightEstimateTracker)
	{
		LightEstimateTracker = nullptr;
	}

	if (ImageTracker)
	{
		ImageTracker = nullptr;
	}

	for (ILuminARTracker* Tracker : Trackers)
	{
		delete Tracker;
	}
}

bool FLuminARImplementation::IsTrackableTypeSupported(UClass* ClassType) const
{
	return (ClassType == UARTrackedGeometry::StaticClass()) ||
			(ClassType == UARTrackedPoint::StaticClass()) ||
			(ClassType == UARPlaneGeometry::StaticClass()) ||
			(ClassType == UARTrackedImage::StaticClass());
}

/////////////////////////////////////////////////////////////////////////////////
// Begin FLuminARImplementation IHeadMountedDisplay Virtual Interface   //
////////////////////////////////////////////////////////////////////////////////


void* FLuminARImplementation::GetARSessionRawPointer()
{
	return nullptr;
}

void* FLuminARImplementation::GetGameThreadARFrameRawPointer()
{
	return nullptr;
}

ELuminARTrackingState FLuminARImplementation::GetTrackingState() const
{
	if (!bIsLuminARSessionRunning)
	{
		return ELuminARTrackingState::StoppedTracking;
	}
	return LatestCameraTrackingState;
}

bool FLuminARImplementation::GetStartSessionRequestFinished() const
{
	return !bStartSessionRequested;
}

void FLuminARImplementation::ARLineTrace(const FVector2D& ScreenPosition, ELuminARLineTraceChannel TraceChannels, TArray<FARTraceResult>& OutHitResults)
{
	OutHitResults.Empty();
	if (!bIsLuminARSessionRunning)
	{
		return;
	}

	// Only testing straight forward from a little below the headset... Lumin isn't a handheld, but it's nice to have this do something.
	IXRTrackingSystem* XRTrackingSystem = GetARSystem()->GetXRTrackingSystem();
	TArray<int32> Devices;
	XRTrackingSystem->EnumerateTrackedDevices(Devices, EXRTrackedDeviceType::HeadMountedDisplay);
	check(Devices.Num() == 1);
	if (Devices.Num() > 0)
	{
		const int32 HMDDeviceID = Devices[0];
		FQuat HMDQuat;
		FVector HMDPosition;
		const bool Success = XRTrackingSystem->GetCurrentPose(HMDDeviceID, HMDQuat, HMDPosition);
		const FTransform TrackingToWorldTransform = XRTrackingSystem->GetTrackingToWorldTransform();
		if (Success)
		{
			const FVector HMDWorldPosition = TrackingToWorldTransform.TransformPosition(HMDPosition);
			const FQuat HMDWorldQuat = TrackingToWorldTransform.TransformRotation(HMDQuat);
			const FVector Start = HMDWorldPosition + FVector(0.0f, 0.0f, -10.0f);
			const FVector Direction = HMDWorldQuat.Vector();
			const FVector End = Start + (Direction * 10000.0f);

			ARLineTrace(Start, End, TraceChannels, OutHitResults);
		}
	}
}

uint32 FLuminARImplementation::GetFrameNum() const
{
	return FrameNumber;
}

int64 FLuminARImplementation::GetCameraTimestamp() const
{
	return LatestCameraTimestamp;
}

void FLuminARImplementation::OnARSystemInitialized()
{
}

bool FLuminARImplementation::OnStartARGameFrame(FWorldContext& WorldContext)
{
	const float WorldToMeterScale = IMagicLeapPlugin::Get().GetWorldToMetersScale();

	TFunction<void()> Func;
	while (RunOnGameThreadQueue.Dequeue(Func))
	{
		Func();
	}

	// Start session with requested config
	if (!bIsLuminARSessionRunning && bStartSessionRequested)
	{
		bStartSessionRequested = false;

		const UARSessionConfig& RequestedConfig = ARSystem->AccessSessionConfig();

		if (!OnIsTrackingTypeSupported(RequestedConfig.GetSessionType()))
		{
			UE_LOG(LogLuminAR, Warning, TEXT("Start AR failed: Unsupported AR tracking type %d for LuminAR"), static_cast<int32>(RequestedConfig.GetSessionType()));
			CurrentSessionStatus = EARSessionStatus::UnsupportedConfiguration;
			return false;
		}

		for (ILuminARTracker* Tracker : Trackers)
		{
			Tracker->CreateEntityTracker();
		}

		// TODO : when trackeers that need privs are introduced, delay setting this flag until privs are granted.
		bIsLuminARSessionRunning = true;
		CurrentSessionStatus = EARSessionStatus::Running;

		ARSystem->OnARSessionStarted.Broadcast();
	}

	if (bIsLuminARSessionRunning)
	{
		FMagicLeapHeadTrackingState TrackingState;
		bool bResult = UMagicLeapHMDFunctionLibrary::GetHeadTrackingState(TrackingState);
		if (!bResult || TrackingState.Error != EMagicLeapHeadTrackingError::None)
		{
			LatestCameraTrackingState = ELuminARTrackingState::NotTracking;
			return true;
		}

		LatestCameraTimestamp = FPlatformTime::Seconds();
		LatestCameraTrackingState = ELuminARTrackingState::Tracking;
		FrameNumber++;

		for (auto& Tracker : Trackers)
		{
			Tracker->OnStartGameFrame();
		}
	
		// Update Anchors
		for (auto HandleToAnchorMapPair : HandleToAnchorMap)
		{
			const FGuid AnchorHandle = HandleToAnchorMapPair.Key;
			UARPin* const AnchorPin = HandleToAnchorMapPair.Value;
			LuminArAnchor* LuminArAnchor = reinterpret_cast<struct LuminArAnchor*>(AnchorPin->GetNativeResource());
			check(LuminArAnchor);
			const FGuid ParentTrackableHandle = LuminArAnchor->ParentTrackable;
			if (MagicLeap::FGuidIsValidHandle(ParentTrackableHandle))
			{
				UARTrackedGeometry* const ParentTrackable = GetOrCreateTrackableFromHandle<UARTrackedGeometry>(ParentTrackableHandle);
				const EARTrackingState AnchorTrackingState = ParentTrackable->GetTrackingState();
				if (AnchorPin->GetTrackingState() != EARTrackingState::StoppedTracking)
				{
					AnchorPin->OnTrackingStateChanged(AnchorTrackingState);
				}

				if (AnchorPin->GetTrackingState() == EARTrackingState::Tracking)
				{
					AnchorPin->OnTransformUpdated(ParentTrackable->GetLocalToTrackingTransform());
				}
			}
		}
	}

	return true;
}

bool FLuminARImplementation::IsARAvailable() const
{
	return true;
}

EARTrackingQualityReason FLuminARImplementation::OnGetTrackingQualityReason() const
{
	return EARTrackingQualityReason::None;
}

EARTrackingQuality FLuminARImplementation::OnGetTrackingQuality() const
{
	FMagicLeapHeadTrackingState TrackingState;
	const bool bResult = UMagicLeapHMDFunctionLibrary::GetHeadTrackingState(TrackingState);
	if (bResult)
	{
		return (TrackingState.Mode == EMagicLeapHeadTrackingMode::PositionAndOrientation) ? EARTrackingQuality::OrientationAndPosition : EARTrackingQuality::NotTracking;
	}

	return EARTrackingQuality::NotTracking;
}

void FLuminARImplementation::OnStartARSession(UARSessionConfig* SessionConfig)
{
	UE_LOG(LogLuminAR, Log, TEXT("Start LuminAR session requested"));

	if (bIsLuminARSessionRunning)
	{
		if (SessionConfig == &ARSystem->AccessSessionConfig())
		{
			UE_LOG(LogLuminAR, Warning, TEXT("LuminAR session is already running with the requested LuminAR config. Request aborted."));
			bStartSessionRequested = false;
			return;
		}

		OnPauseARSession();
	}

	if (bStartSessionRequested)
	{
		UE_LOG(LogLuminAR, Warning, TEXT("LuminAR session is already starting. This will overriding the previous session config with the new one."))
	}

	bStartSessionRequested = true;

	// TODO : check if this code is needed.
	// Try recreating the LuminARSession to fix the fatal error.
	// if (CurrentSessionStatus == EARSessionStatus::FatalError)
	// {
	// 	UE_LOG(LogLuminAR, Warning, TEXT("Reset LuminAR session due to fatal error detected."));
	// 	ResetLuminARSession();
	// }

	if (SessionConfig->GetLightEstimationMode() == EARLightEstimationMode::AmbientLightEstimate)
	{
		// Keep a pointer to the tracker, in addition to adding it to the Trackers list, for direct access when getting light estimates
		LightEstimateTracker = new FLuminARLightTracker(*this);
		Trackers.Add(LightEstimateTracker);
	}
}

void FLuminARImplementation::OnPauseARSession()
{
	UE_LOG(LogLuminAR, Log, TEXT("Pausing LuminAR session."));
	if (!bIsLuminARSessionRunning)
	{
		if(bStartSessionRequested)
		{
			bStartSessionRequested = false;
		}
		else
		{
			UE_LOG(LogLuminAR, Log, TEXT("Could not stop LuminAR tracking session because there is no running tracking session!"));
		}
		return;
	}

	for (UARPin* Anchor : AllAnchors)
	{
		Anchor->OnTrackingStateChanged(EARTrackingState::NotTracking);
	}

	bIsLuminARSessionRunning = false;
	CurrentSessionStatus = EARSessionStatus::NotStarted;
}

void FLuminARImplementation::OnStopARSession()
{
	OnPauseARSession();
	ClearTrackedGeometries();
}


FARSessionStatus FLuminARImplementation::OnGetARSessionStatus() const
{
	return CurrentSessionStatus;
}

void FLuminARImplementation::OnSetAlignmentTransform(const FTransform& InAlignmentTransform)
{
	const FTransform& NewAlignmentTransform = InAlignmentTransform;

	TArray<UARTrackedGeometry*> AllTrackedGeometries = OnGetAllTrackedGeometries();
	for (UARTrackedGeometry* TrackedGeometry : AllTrackedGeometries)
	{
		TrackedGeometry->UpdateAlignmentTransform(NewAlignmentTransform);
	}

	TArray<UARPin*> AllARPins = OnGetAllPins();
	for (UARPin* SomePin : AllARPins)
	{
		SomePin->UpdateAlignmentTransform(NewAlignmentTransform);
	}
}

static ELuminARLineTraceChannel ConvertToLuminTraceChannels(EARLineTraceChannels TraceChannels)
{
	ELuminARLineTraceChannel LuminARTraceChannels = ELuminARLineTraceChannel::None;
	if (!!(TraceChannels & EARLineTraceChannels::FeaturePoint))
	{
		LuminARTraceChannels = LuminARTraceChannels | ELuminARLineTraceChannel::FeaturePoint;
	}
	if (!!(TraceChannels & EARLineTraceChannels::GroundPlane))
	{
		LuminARTraceChannels = LuminARTraceChannels | ELuminARLineTraceChannel::InfinitePlane;
	}
	if (!!(TraceChannels & EARLineTraceChannels::PlaneUsingBoundaryPolygon))
	{
		LuminARTraceChannels = LuminARTraceChannels | ELuminARLineTraceChannel::PlaneUsingBoundaryPolygon;
	}
	if (!!(TraceChannels & EARLineTraceChannels::PlaneUsingExtent))
	{
		LuminARTraceChannels = LuminARTraceChannels | ELuminARLineTraceChannel::PlaneUsingExtent;
	}
	return LuminARTraceChannels;
}

TArray<FARTraceResult> FLuminARImplementation::OnLineTraceTrackedObjects(const FVector2D ScreenCoord, EARLineTraceChannels TraceChannels)
{
	TArray<FARTraceResult> OutHitResults;
	ARLineTrace(ScreenCoord, ConvertToLuminTraceChannels(TraceChannels), OutHitResults);
	return OutHitResults;
}

TArray<FARTraceResult> FLuminARImplementation::OnLineTraceTrackedObjects(const FVector Start, const FVector End, EARLineTraceChannels TraceChannels)
{
	TArray<FARTraceResult> OutHitResults;
	ARLineTrace(Start, End, ConvertToLuminTraceChannels(TraceChannels), OutHitResults);
	return OutHitResults;
}

TArray<UARTrackedGeometry*> FLuminARImplementation::OnGetAllTrackedGeometries() const
{
	TArray<UARTrackedGeometry*> AllTrackedGeometry;
	GetAllTrackables<UARTrackedGeometry>(AllTrackedGeometry);
	return AllTrackedGeometry;
}

TArray<UARPin*> FLuminARImplementation::OnGetAllPins() const
{
	return AllAnchors;
}

bool FLuminARImplementation::OnIsTrackingTypeSupported(EARSessionType SessionType) const
{
	return (SessionType == EARSessionType::World) ? true : false;
}

UARLightEstimate* FLuminARImplementation::OnGetCurrentLightEstimate() const
{
	return LightEstimateTracker ? LightEstimateTracker->LightEstimate : nullptr;
}

UARPin* FLuminARImplementation::FindARPinByComponent(const USceneComponent* Component) const
{
	for (UARPin* Pin : AllAnchors)
	{
		if (Pin->GetPinnedComponent() == Component)
		{
			return Pin;
		}
	}

	return nullptr;
}

UARPin* FLuminARImplementation::OnPinComponent(USceneComponent* ComponentToPin, const FTransform& PinToWorldTransform, UARTrackedGeometry* TrackedGeometry /*= nullptr*/, const FName DebugName /*= NAME_None*/)
{
	UARPin* NewARPin = nullptr;

	if (TrackedGeometry == nullptr)
	{
		UE_LOG(LogLuminAR, Error, TEXT("Can pin a component only to a valid tracked geometry."));
	}
	else if (bIsLuminARSessionRunning)
	{
		const FTransform& TrackingToAlignedTracking = ARSystem->GetAlignmentTransform();
		const FTransform PinToTrackingTransform = PinToWorldTransform.GetRelativeTransform(ARSystem->GetXRTrackingSystem()->GetTrackingToWorldTransform()).GetRelativeTransform(TrackingToAlignedTracking);

		ensure(TrackedGeometry->GetNativeResource() != nullptr);
		const FGuid& ParentHandle = static_cast<FLuminARTrackableResource*>(TrackedGeometry->GetNativeResource())->GetNativeHandle();
		ensure(MagicLeap::FGuidIsValidHandle(ParentHandle));
		TSharedPtr<LuminArAnchor> NewLuminArAnchor = MakeShared<LuminArAnchor>(ParentHandle);

		NewARPin = NewObject<UARPin>();
		NewARPin->InitARPin(GetARSystem(), ComponentToPin, PinToTrackingTransform, TrackedGeometry, DebugName);
		NewARPin->SetNativeResource(reinterpret_cast<void*>(NewLuminArAnchor.Get()));

		HandleToLuminAnchorMap.Add(NewLuminArAnchor->Handle, NewLuminArAnchor);
		AllAnchors.Add(NewARPin);
		HandleToAnchorMap.Add(NewLuminArAnchor->Handle, NewARPin);
	}

	return NewARPin;
}

void FLuminARImplementation::OnRemovePin(UARPin* PinToRemove)
{
	if (bIsLuminARSessionRunning && AllAnchors.Contains(PinToRemove))
	{
		LuminArAnchor* NativeResource = reinterpret_cast<LuminArAnchor*>(PinToRemove->GetNativeResource());
		check(NativeResource);
		NativeResource->Detach();
		PinToRemove->SetNativeResource(nullptr);

		PinToRemove->OnTrackingStateChanged(EARTrackingState::StoppedTracking);

		HandleToAnchorMap.Remove(NativeResource->Handle);
		HandleToLuminAnchorMap.Remove(NativeResource->Handle);
		AllAnchors.Remove(PinToRemove);
	}
}

EARWorldMappingState FLuminARImplementation::OnGetWorldMappingStatus() const
{
	if (bIsLuminARSessionRunning && UMagicLeapARPinFunctionLibrary::IsTrackerValid())
	{
		int32 Count = 0;
		const EMagicLeapPassableWorldError PassableWorldResult = UMagicLeapARPinFunctionLibrary::GetNumAvailableARPins(Count);
		switch (PassableWorldResult)
		{
			case EMagicLeapPassableWorldError::None:
				// TODO : when can we consider the state to be "mapped"? return StillMappingRelocalizable for now.
				return (Count > 0) ? EARWorldMappingState::StillMappingRelocalizable : EARWorldMappingState::StillMappingNotRelocalizable;
			case EMagicLeapPassableWorldError::LowMapQuality:
			case EMagicLeapPassableWorldError::UnableToLocalize:
				return EARWorldMappingState::StillMappingNotRelocalizable;
		}
	}

	return EARWorldMappingState::NotAvailable;
}

TArray<FVector> FLuminARImplementation::OnGetPointCloud() const
{
	//TODO NEEDED
	return TArray<FVector>();
}

void FLuminARImplementation::SetARSystem(TSharedPtr<FARSupportInterface , ESPMode::ThreadSafe> InARSystem)
{
	ARSystem = InARSystem;
}

void FLuminARImplementation::AddReferencedObjects(FReferenceCollector& Collector)
{
	for (auto GeoIt = TrackedGeometryGroups.CreateIterator(); GeoIt; ++GeoIt)
	{
		FTrackedGeometryGroup& TrackedGeometryGroup = GeoIt.Value();

		Collector.AddReferencedObject(TrackedGeometryGroup.TrackedGeometry);
		Collector.AddReferencedObject(TrackedGeometryGroup.ARActor);
		Collector.AddReferencedObject(TrackedGeometryGroup.ARComponent);
	}

	if (LightEstimateTracker != nullptr)
	{
		Collector.AddReferencedObject(LightEstimateTracker->LightEstimate);
	}

	Collector.AddReferencedObjects(AllAnchors);
}


void FLuminARImplementation::ClearTrackedGeometries()
{
	for (UARPin* Anchor : AllAnchors)
	{
		Anchor->OnTrackingStateChanged(EARTrackingState::StoppedTracking);
	}

	for (ILuminARTracker* Tracker : Trackers)
	{
		Tracker->DestroyEntityTracker();
	}

	// Delete the LightEstimateTracker on session end.  It will be recreated, if necessary, on session start.
	Trackers.Remove(LightEstimateTracker);
	delete LightEstimateTracker;
	LightEstimateTracker = nullptr;

	CurrentSessionStatus = EARSessionStatus::NotStarted;

	TrackedGeometryGroups.Empty();
}

void FLuminARImplementation::OnSpawnARActor(AARActor* NewARActor, UARComponent* NewARComponent, FGuid NativeID)
{
	FTrackedGeometryGroup* TrackedGeometryGroup = TrackedGeometryGroups.Find(NativeID);
	if (TrackedGeometryGroup != nullptr)
	{
		//this should still be null
		check(TrackedGeometryGroup->ARActor == nullptr);
		check(TrackedGeometryGroup->ARComponent == nullptr);

		check(NewARActor);
		check(NewARComponent);

		TrackedGeometryGroup->ARActor = NewARActor;
		TrackedGeometryGroup->ARComponent = NewARComponent;

		//NOW, we can make the callbacks
		TrackedGeometryGroup->ARComponent->Update(TrackedGeometryGroup->TrackedGeometry);
		TriggerOnTrackableAddedDelegates(TrackedGeometryGroup->TrackedGeometry);
	}
	else
	{
		UE_LOG(LogLuminAR, Warning, TEXT("AR NativeID not found.  Make sure to set this on the ARComponent!"));
	}
}


TSharedRef<FARSupportInterface , ESPMode::ThreadSafe> FLuminARImplementation::GetARSystem()
{
	return ARSystem.ToSharedRef();
}

void FLuminARImplementation::DumpTrackableHandleMap(const FGuid& SessionHandle) const
{
	UE_LOG(LogLuminAR, Log, TEXT("ULuminARUObjectManager::DumpTrackableHandleMap"));
	for (const auto& KeyValuePair : TrackedGeometryGroups)
	{
		const FTrackedGeometryGroup& TrackedGeometryGroup = KeyValuePair.Value;
		UARTrackedGeometry* TrackedGeometry = TrackedGeometryGroup.TrackedGeometry;
		UE_LOG(LogLuminAR, Log, TEXT("  Trackable Handle %s"), *KeyValuePair.Key.ToString());
		if (TrackedGeometry != nullptr)
		{
			const FLuminARTrackableResource* NativeResource = static_cast<FLuminARTrackableResource*>(TrackedGeometry->GetNativeResource());
			UE_LOG(LogLuminAR, Log, TEXT("  TrackedGeometry - NativeResource:%s, type: %s, tracking state: %d"),
				*NativeResource->GetNativeHandle().ToString(), *TrackedGeometry->GetClass()->GetFName().ToString(), (int)TrackedGeometry->GetTrackingState());
		}
		else
		{
			UE_LOG(LogLuminAR, Log, TEXT("  TrackedGeometry - InValid or Pending Kill."))
		}
	}
}

void FLuminARImplementation::ARLineTrace(const FVector& Start, const FVector& End, ELuminARLineTraceChannel TraceChannels, TArray<FARTraceResult>& OutHitResults)
{
	if (!bIsLuminARSessionRunning)
	{
		return;
	}
	OutHitResults.Empty();

	// Only testing vs planes now, but not the ground plane.
	ELuminARLineTraceChannel AllPlaneTraceChannels = /*ELuminARLineTraceChannel::InfinitePlane |*/ ELuminARLineTraceChannel::PlaneUsingExtent | ELuminARLineTraceChannel::PlaneUsingBoundaryPolygon;
	if (!(TraceChannels & AllPlaneTraceChannels))
		return;

	TArray<UARPlaneGeometry*> Planes;
	GetAllTrackables(Planes);

	for (UARPlaneGeometry* PPlane : Planes)
	{
		check(PPlane);
		UARPlaneGeometry& Plane = *PPlane;

		const FTransform LocalToWorld = Plane.GetLocalToWorldTransform();
		const FVector PlaneOrigin = LocalToWorld.GetLocation();
		const FVector PlaneNormal = LocalToWorld.TransformVectorNoScale(FVector(0, 0, 1));
		const FVector Dir = End - Start;
		// check if Dir is parallel to plane, no intersection
		if (!FMath::IsNearlyZero(Dir | PlaneNormal, KINDA_SMALL_NUMBER))
		{
			// if T < 0 or > 1 we are outside the line segment, no intersection
			float T = (((PlaneOrigin - Start) | PlaneNormal) / ((End - Start) | PlaneNormal));
			if (T >= 0.0f || T <= 1.0f)
			{
				const FVector Intersection = Start + (Dir * T);

				EARLineTraceChannels FoundInTraceChannel = EARLineTraceChannels::None;

				if (!!(TraceChannels & (ELuminARLineTraceChannel::PlaneUsingExtent | ELuminARLineTraceChannel::PlaneUsingBoundaryPolygon)))
				{
					const FTransform WorldToLocal = LocalToWorld.Inverse();
					const FVector LocalIntersection = WorldToLocal.TransformPosition(Intersection);

					// Note: doing boundary check first for consistency with ARCore

					if (FoundInTraceChannel == EARLineTraceChannels::None
						&& !!(TraceChannels & ELuminARLineTraceChannel::PlaneUsingBoundaryPolygon))
					{
						// Note: could optimize this by computing an aligned boundary bounding rectangle during plane update and testing that first.

						// Did we hit inside the boundary?
						const TArray<FVector> Boundary = Plane.GetBoundaryPolygonInLocalSpace();
						if (Boundary.Num() > 3) // has to be at least a triangle to have an inside
						{
							// This is the 'ray casting algorithm' for detecting if a point is inside a polygon.

							// Draw a line from the point to the outside. Test for intersect with all edges.  If an odd number of edges are intersected the point is inside the polygon.
							// The bounds are in plane local space, such that the plane is the x-y plane (z is always zero).
							// We will offset that to put the point we are testing at 0,0 and our ray will be the +y axis (meaning the endpoint is 0,infinity and certainly outside the polygon).

							// This could get the wrong answer if the test line goes exactly through a boundary vertex because that would register as two intersections.
							// We are ignoring this rare failure cases.

							const FVector2D Origin(LocalIntersection.X, LocalIntersection.Y);
							const int Num = Boundary.Num();
							FVector2D A(Boundary[Num - 1].X - Origin.X, Boundary[Num - 1].Y - Origin.Y);
							int32 Crossings = 0;
							for (int i = 0; i < Num; ++i)
							{
								const FVector2D B(Boundary[i].X - Origin.X, Boundary[i].Y - Origin.Y);

								// Check if there is any Y intercept in the line segment.
								if (FMath::Sign(A.X) != FMath::Sign(B.X))
								{
									// Check if the Y intercept is positive.
									const float Slope = (B.Y - A.Y) / (B.X - A.X);
									const float YIntercept = A.Y - (Slope * A.X);
									if (YIntercept > 0.0f)
									{
										Crossings += 1;
									}
								}

								A = B;
							}
							if ((Crossings & 0x01) == 0x01)
							{
								FoundInTraceChannel = EARLineTraceChannels::PlaneUsingBoundaryPolygon;
							}
						}
					}

					if (FoundInTraceChannel == EARLineTraceChannels::None
						&& !!(TraceChannels & ELuminARLineTraceChannel::PlaneUsingExtent))
					{
						// Did we hit inside the plane extents?
						if (FMath::Abs(LocalIntersection.X) <= Plane.GetExtent().X
							&& FMath::Abs(LocalIntersection.Y) <= Plane.GetExtent().Y)
						{
							FoundInTraceChannel = EARLineTraceChannels::PlaneUsingExtent;
						}
					}
				}

				//// This 'infinite plane' 'ground plane' stuff seems... weird.
				//if (FoundInTraceChannel == EARLineTraceChannels::None
				//	&&!!(TraceChannels & ELuminARLineTraceChannel::InfinitePlane))
				//{
				//	FoundInTraceChannel = EARLineTraceChannels::GroundPlane;
				//}

				// write the result
				if (FoundInTraceChannel != EARLineTraceChannels::None)
				{
					const float Distance = Dir.Size() * T;

					FTransform HitTransform = LocalToWorld;
					HitTransform.SetLocation(Intersection);

					FARTraceResult UEHitResult(GetARSystem(), Distance, FoundInTraceChannel, HitTransform, &Plane);
					UEHitResult.SetLocalToWorldTransform(HitTransform);
					OutHitResults.Add(UEHitResult);
				}
			}
		}
	}

	// Sort closest to furthest
	OutHitResults.Sort(FARTraceResult::FARTraceResultComparer());
}

bool FLuminARImplementation::OnAddRuntimeCandidateImage(UARSessionConfig* SessionConfig, UTexture2D* CandidateTexture, FString FriendlyName, float PhysicalWidth)
{
	float PhysicalHeight = PhysicalWidth / CandidateTexture->GetSizeX() * CandidateTexture->GetSizeY();
	UARCandidateImage* NewCandidateImage = UARCandidateImage::CreateNewARCandidateImage(CandidateTexture, FriendlyName, PhysicalWidth, PhysicalHeight, EARCandidateImageOrientation::Landscape);
	SessionConfig->AddCandidateImage(NewCandidateImage);

	if (ImageTracker != nullptr)
	{
		ImageTracker->AddCandidateImageForTracking(NewCandidateImage);
	}

	return true;
}

ULuminARCandidateImage* FLuminARImplementation::AddLuminRuntimeCandidateImage(UARSessionConfig* SessionConfig, UTexture2D* CandidateTexture, FString FriendlyName, float PhysicalWidth, bool bUseUnreliablePose, bool bImageIsStationary, EMagicLeapImageTargetOrientation InAxisOrientation)
{
	float PhysicalHeight = PhysicalWidth / CandidateTexture->GetSizeX() * CandidateTexture->GetSizeY();
	ULuminARCandidateImage* NewCandidateImage = ULuminARCandidateImage::CreateNewLuminARCandidateImage(CandidateTexture, FriendlyName, PhysicalWidth, PhysicalHeight, EARCandidateImageOrientation::Landscape, bUseUnreliablePose, bImageIsStationary, InAxisOrientation);
	SessionConfig->AddCandidateImage(NewCandidateImage);

	if (ImageTracker != nullptr)
	{
		ImageTracker->AddCandidateImageForTracking(NewCandidateImage);
	}

	return NewCandidateImage;
}
