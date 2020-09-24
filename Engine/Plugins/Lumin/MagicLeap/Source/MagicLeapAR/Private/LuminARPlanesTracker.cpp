// Copyright Epic Games, Inc. All Rights Reserved.

#include "LuminARPlanesTracker.h"
#include "HeadMountedDisplayFunctionLibrary.h"
#include "MagicLeapHMDFunctionLibrary.h"
#include "IMagicLeapPlanesModule.h"
#include "LuminARTrackingSystem.h"
#include "LuminAROrigin.h"
#include "GameFramework/PlayerController.h"
#include "IMagicLeapPlugin.h"

FLuminARPlanesTracker::FLuminARPlanesTracker(FLuminARImplementation& InARSystemSupport)
: ILuminARTracker(InARSystemSupport)
, bPlanesQueryPending(false)
{
	ResultDelegate.BindRaw(this, &FLuminARPlanesTracker::ProcessPlaneQuery);
}

FLuminARPlanesTracker::~FLuminARPlanesTracker()
{
}

void FLuminARPlanesTracker::CreateEntityTracker()
{
	IMagicLeapPlanesModule::Get().CreateTracker();
	PlanesQueryHandle = IMagicLeapPlanesModule::Get().AddQuery(EMagicLeapPlaneQueryType::Delta);
}

void FLuminARPlanesTracker::DestroyEntityTracker()
{
	// This will clear any pending call to FLuminARPlanesTracker::ProcessPlaneQuery() so we don't need to worry about the result delegate coming back to a deleted object.
	IMagicLeapPlanesModule::Get().RemoveQuery(PlanesQueryHandle);
	IMagicLeapPlanesModule::Get().DestroyTracker();
	PlaneResultsMap.Empty();
	PlaneParentMap.Empty();
	bPlanesQueryPending = false;
}

void FLuminARPlanesTracker::OnStartGameFrame()
{
	if (IMagicLeapPlanesModule::Get().IsTrackerValid())
	{
		StartPlaneQuery();
	}
}

bool FLuminARPlanesTracker::IsHandleTracked(const FGuid& Handle) const
{
	return PlaneResultsMap.Contains(Handle);
}

UARTrackedGeometry* FLuminARPlanesTracker::CreateTrackableObject()
{
	return NewObject<UARPlaneGeometry>();
}

UClass* FLuminARPlanesTracker::GetARComponentClass(const UARSessionConfig& SessionConfig)
{
	return SessionConfig.GetPlaneComponentClass();
}

IARRef* FLuminARPlanesTracker::CreateNativeResource(const FGuid& Handle, UARTrackedGeometry* TrackableObject)
{
	return new FLuminARTrackedPlaneResource(Handle, TrackableObject, *this);
}

const FGuid* FLuminARPlanesTracker::GetParentHandle(const FGuid& Handle) const
{
	return PlaneParentMap.Find(Handle);
}

const FLuminPlanesAndBoundaries* FLuminARPlanesTracker::GetPlaneResult(const FGuid& Handle) const
{
	return PlaneResultsMap.Find(Handle);
}

void FLuminARPlanesTracker::StartPlaneQuery()
{
	//if we haven't queried yet, start one!
	if (!bPlanesQueryPending)
	{
		if (IMagicLeapPlugin::Get().IsMagicLeapHMDValid())
		{
			FMagicLeapPlanesQuery Query;

			// Apply Lumin specific AR session config, if available.  Otherwise use default values.
			const UARSessionConfig& ARSessionConfig = ARSystemSupport->GetARSystem()->AccessSessionConfig();
			const ULuminARSessionConfig* LuminARSessionConfig = Cast<ULuminARSessionConfig>(&ARSessionConfig);
			if (LuminARSessionConfig != nullptr)
			{
				Query = LuminARSessionConfig->PlanesQuery;
				if (LuminARSessionConfig->bArbitraryOrientationPlaneDetection)	{ Query.Flags.Add(EMagicLeapPlaneQueryFlags::Arbitrary); }
				bDiscardZeroExtentPlanes = LuminARSessionConfig->bDiscardZeroExtentPlanes;
			}
			else
			{
				UE_LOG(LogLuminAR, Log, TEXT("LuminArSessionConfig not found, using defaults for lumin specific settings."));
				Query.MaxResults = 200;
				Query.MinHoleLength = 50.0f;
				Query.MinPlaneArea = 400.0f;
				Query.SearchVolumeExtents = FVector(10000.0f, 10000.0f, 10000.0f);
				Query.SimilarityThreshold = 1.0f;
				Query.bSearchVolumeTrackingSpace = true;
				bDiscardZeroExtentPlanes = false;
			}

			Query.bResultTrackingSpace = true;
			Query.Flags.Add(EMagicLeapPlaneQueryFlags::Polygons);
			if (ARSessionConfig.ShouldDoHorizontalPlaneDetection())
			{
				Query.Flags.Add(EMagicLeapPlaneQueryFlags::Horizontal);
				// Result query flags will contains semantic flags only if requested
				Query.Flags.Add(EMagicLeapPlaneQueryFlags::Floor);
				Query.Flags.Add(EMagicLeapPlaneQueryFlags::Ceiling);
			}

			if (ARSessionConfig.ShouldDoVerticalPlaneDetection())
			{
				Query.Flags.Add(EMagicLeapPlaneQueryFlags::Vertical);
				// Result query flags will contains semantic flags only if requested
				Query.Flags.Add(EMagicLeapPlaneQueryFlags::Wall);
			}

			bPlanesQueryPending = IMagicLeapPlanesModule::Get().PersistentQueryBeginAsync(Query, PlanesQueryHandle, ResultDelegate);
			if (!bPlanesQueryPending)
			{
				UE_LOG(LogLuminAR, Error, TEXT("LuminARFrame could not request planes."));
			}
		}
	}
}

void FLuminARPlanesTracker::ProcessPlaneQuery(const bool bSuccess, 
	const FGuid& Handle, 
	const EMagicLeapPlaneQueryType QueryType,
	const TArray<FMagicLeapPlaneResult>& NewPlanes, 
	const TArray<FGuid>& RemovedPlanes, 
	const TArray<FMagicLeapPlaneBoundaries>& NewPolygons, 
	const TArray<FGuid>& RemovedPolygons)
{
	if (bSuccess)
	{
		// The whole reason we are hanging onto the UWorld of the AROrigin here
		//	is to obtain the transform of the root component of the first 
		//	player's pawn within the same world. //
		UWorld*const AROriginWorld = ALuminAROrigin::FindWorld();
		verify(AROriginWorld);
		// We need to check the ARSessionConfig to see if we are configured to
		//	generate geometry based on tracked data.
		//	@session-config-mesh-properties-support //
		const UARSessionConfig& ARSessionConfig =
						 ARSystemSupport->GetARSystem()->AccessSessionConfig();
		// If we're configured to do so, then before processing all the planes
		//	we need to obtain a global actor located at the origin to 
		//	accumulate MRMeshComponents to hang geometry onto. //
		ALuminAROrigin*const OriginActor =
			ARSessionConfig.bGenerateMeshDataFromTrackedGeometry ?
				ALuminAROrigin::GetOriginActor(ARSessionConfig, AROriginWorld) :
				nullptr;

		for (const FGuid& RemovedPlaneID : RemovedPlanes)
		{
			// Only remove from the parent map if the unique ID represents an outer plane
			FLuminPlanesAndBoundaries* const planesAndBoundaries = 
										  PlaneResultsMap.Find(RemovedPlaneID);
			const auto& Plane = planesAndBoundaries->Plane;
			if (!Plane.PlaneFlags.Contains(EMagicLeapPlaneQueryFlags::PreferInner))
			{
				PlaneParentMap.Remove(Plane.ID);
			}
			if (OriginActor)
			{
				OriginActor->DestroyPlane(RemovedPlaneID);
			}
			PlaneResultsMap.Remove(RemovedPlaneID);
		}

		PlaneResultsMap.Reserve(PlaneResultsMap.Num() + NewPlanes.Num());

		for (const FMagicLeapPlaneResult& ResultUEPlane : NewPlanes)
		{
			if (ResultUEPlane.PlaneDimensions.X == 0.0f || ResultUEPlane.PlaneDimensions.Y == 0.0f)
			{
				if (bDiscardZeroExtentPlanes)
				{
					PlaneResultsMap.Remove(ResultUEPlane.InnerID);
					continue;
				}
			}

			FLuminPlanesAndBoundaries& CachedResult = PlaneResultsMap.Add(ResultUEPlane.InnerID);

			// Set the plane mapping if this plane is an outer plane
			if (!ResultUEPlane.PlaneFlags.Contains(EMagicLeapPlaneQueryFlags::PreferInner))
			{
				PlaneParentMap.Add(ResultUEPlane.ID) = ResultUEPlane.InnerID;
			}

			// TODO: @njain We are only saving 1 boundary per ID :(
			CachedResult.Plane = ResultUEPlane;
			const float HalfWidth = CachedResult.Plane.PlaneDimensions.Y * 0.5f;
			const float HalfHeight = CachedResult.Plane.PlaneDimensions.X * 0.5f;
			CachedResult.PolygonVerticesLocalSpace = TArray<FVector>( 
				{ FVector(HalfHeight, HalfWidth, 0)
				, FVector(-HalfHeight, HalfWidth, 0)
				, FVector(-HalfHeight, -HalfWidth, 0)
				, FVector(HalfHeight, -HalfWidth, 0) } );
			// The LuminAROrigin actor is now responsible for creation &
			//	destruction of planes using UMRMeshComponent.
			//	@session-config-mesh-properties-support //
			if (ARSessionConfig.bGenerateMeshDataFromTrackedGeometry && OriginActor)
			{
				const FTransform LocalToTracking = 
								   FTransform(ResultUEPlane.ContentOrientation,
											  ResultUEPlane.PlanePosition);
				OriginActor->CreatePlane(ResultUEPlane.InnerID, 
										 CachedResult.PolygonVerticesLocalSpace,
										 LocalToTracking,
										 GetMostSignificantPlaneFlag(ResultUEPlane.PlaneFlags));
			}
		}

		// Setup for boundaries, build a map of Handles to Boundaries
		for (const FMagicLeapPlaneBoundaries& AllBoundaries : NewPolygons)
		{
			if (AllBoundaries.Boundaries.Num() > 0)
			{

				const auto ParentID = GetParentHandle(AllBoundaries.ID);
				if (!ParentID)
				{
					continue;
				}

				FLuminPlanesAndBoundaries* CachedResult = PlaneResultsMap.Find(*ParentID);
				if (!CachedResult)
				{
					continue;
				}
				const FTransform TrackingToLocalTransform = FTransform(CachedResult->Plane.ContentOrientation, CachedResult->Plane.PlanePosition).Inverse();
				// TODO: @njain We are only saving 1 boundary per ID :(
				CachedResult->PolygonVerticesLocalSpace = AllBoundaries.Boundaries[0].Polygon.Vertices;
				for (FVector& Vertex : CachedResult->PolygonVerticesLocalSpace)
				{
					Vertex = TrackingToLocalTransform.TransformPosition(Vertex);
				}
			}
		}

		// Remove planes that previously existed, but no longer do StoppedTracking.
		ARSystemSupport->RemoveHandleIf<UARPlaneGeometry>([this](const FGuid& TrackingHandle) -> bool {
			return !PlaneResultsMap.Contains(TrackingHandle);
		});

		for (const auto& PlaneResultPair : PlaneResultsMap)
		{
			UARTrackedGeometry* ARTrackedGeometry = ARSystemSupport->GetOrCreateTrackableFromHandle<UARPlaneGeometry>(PlaneResultPair.Key);

			if (ARTrackedGeometry && ARTrackedGeometry->GetTrackingState() != EARTrackingState::StoppedTracking)
			{
				ARTrackedGeometry->SetTrackingState(EARTrackingState::Tracking);
				FLuminARTrackableResource* TrackableResource = static_cast<FLuminARTrackableResource*>(ARTrackedGeometry->GetNativeResource());
				TrackableResource->UpdateGeometryData(ARSystemSupport);
			}
		}
	}

	bPlanesQueryPending = false;
}

EMagicLeapPlaneQueryFlags FLuminARPlanesTracker::GetMostSignificantPlaneFlag(const TArray<EMagicLeapPlaneQueryFlags>& PlaneFlags) const
{
	EMagicLeapPlaneQueryFlags Result = EMagicLeapPlaneQueryFlags::Arbitrary;

	int32 i = PlaneFlags.Num() - 1;
	while (i >= 0)
	{
		if (PlaneFlags[i] != EMagicLeapPlaneQueryFlags::OrientToGravity && PlaneFlags[i] != EMagicLeapPlaneQueryFlags::PreferInner)
		{
			Result = PlaneFlags[i];
			break;
		}
		--i;
	}

	return Result;
}

void FLuminARTrackedPlaneResource::UpdateGeometryData(FLuminARImplementation* InARSystemSupport)
{
	FLuminARTrackableResource::UpdateGeometryData(InARSystemSupport);

	UARPlaneGeometry* PlaneGeometry = CastChecked<UARPlaneGeometry>(TrackedGeometry);

	if (!InARSystemSupport || TrackedGeometry->GetTrackingState() == EARTrackingState::StoppedTracking)
	{
		return;
	}

	// PlaneResult is in unreal tracking space, so already scaled and axis corrected
	const FLuminPlanesAndBoundaries* Results = Tracker.GetPlaneResult(TrackableHandle);
	check(Results);
	const FMagicLeapPlaneResult* PlaneResult = &Results->Plane;

	// The ContentOrientation and PlanePosition have been transformed back to Tracking space in ProcessPlanesQuery()
	const FTransform LocalToTrackingTransform(PlaneResult->ContentOrientation, PlaneResult->PlanePosition);
	FVector Extent(PlaneResult->PlaneDimensions.X * 0.5f, PlaneResult->PlaneDimensions.Y * 0.5f, 0);  // Extent is half the width and height

	uint32 FrameNum = InARSystemSupport->GetFrameNum();
	int64 TimeStamp = InARSystemSupport->GetCameraTimestamp();

	const auto ParentID = Tracker.GetParentHandle(PlaneResult->ID);

	// Only used SubsumedBy(parent) if the current ID is not the authoritative parent plane
	UARPlaneGeometry* SubsumedByPlane = ParentID && PlaneResult->PlaneFlags.Contains(EMagicLeapPlaneQueryFlags::PreferInner) ?
		InARSystemSupport->GetOrCreateTrackableFromHandle<UARPlaneGeometry>(*ParentID) :
		nullptr;

	PlaneGeometry->UpdateTrackedGeometry(InARSystemSupport->GetARSystem(), FrameNum, static_cast<double>(TimeStamp), LocalToTrackingTransform, InARSystemSupport->GetARSystem()->GetAlignmentTransform(), FVector::ZeroVector, Extent, Results->PolygonVerticesLocalSpace, SubsumedByPlane);
	PlaneGeometry->SetDebugName(FName(TEXT("LuminARPlane")));
	// Inspect the plane flags and set the scene understanding data based upon them
	for (EMagicLeapPlaneQueryFlags Flag : PlaneResult->PlaneFlags)
	{
		switch (Flag)
		{
			case EMagicLeapPlaneQueryFlags::Vertical:
			{
				PlaneGeometry->SetOrientation(EARPlaneOrientation::Vertical);
				break;
			}
			case EMagicLeapPlaneQueryFlags::Horizontal:
			{
				PlaneGeometry->SetOrientation(EARPlaneOrientation::Horizontal);
				break;
			}
			case EMagicLeapPlaneQueryFlags::Arbitrary:
			{
				PlaneGeometry->SetOrientation(EARPlaneOrientation::Diagonal);
				break;
			}
			case EMagicLeapPlaneQueryFlags::Ceiling:
			{
				PlaneGeometry->SetObjectClassification(EARObjectClassification::Ceiling);
				break;
			}
			case EMagicLeapPlaneQueryFlags::Floor:
			{
				PlaneGeometry->SetObjectClassification(EARObjectClassification::Floor);
				break;
			}
			case EMagicLeapPlaneQueryFlags::Wall:
			{
				PlaneGeometry->SetObjectClassification(EARObjectClassification::Wall);
				break;
			}
		}
	}
}
