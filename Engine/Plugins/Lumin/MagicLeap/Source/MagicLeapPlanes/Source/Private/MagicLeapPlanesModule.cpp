// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "MagicLeapPlanesModule.h"
#include "MagicLeapMath.h"
#include "HeadMountedDisplayFunctionLibrary.h"
#include "Engine/Engine.h"
#include "Components/BoxComponent.h"
#include "MagicLeapHandle.h"

//PRAGMA_DISABLE_OPTIMIZATION
using namespace MagicLeap;

DEFINE_LOG_CATEGORY(LogMagicLeapPlanes);

#if WITH_MLSDK
namespace MagicLeap
{
	EMagicLeapPlaneQueryFlags MLToUnrealPlanesQueryFlagMap(MLPlanesQueryFlags QueryFlag)
	{
		switch (QueryFlag)
		{
		case MLPlanesQueryFlag_Vertical:
			return EMagicLeapPlaneQueryFlags::Vertical;
		case MLPlanesQueryFlag_Horizontal:
			return EMagicLeapPlaneQueryFlags::Horizontal;
		case MLPlanesQueryFlag_Arbitrary:
			return EMagicLeapPlaneQueryFlags::Arbitrary;
		case MLPlanesQueryFlag_OrientToGravity:
			return EMagicLeapPlaneQueryFlags::OrientToGravity;
		case MLPlanesQueryFlag_Inner:
			return EMagicLeapPlaneQueryFlags::PreferInner;
		case MLPlanesQueryFlag_Semantic_Ceiling:
			return EMagicLeapPlaneQueryFlags::Ceiling;
		case MLPlanesQueryFlag_Semantic_Floor:
			return EMagicLeapPlaneQueryFlags::Floor;
		case MLPlanesQueryFlag_Semantic_Wall:
			return EMagicLeapPlaneQueryFlags::Wall;
		case MLPlanesQueryFlag_Polygons:
			return EMagicLeapPlaneQueryFlags::Polygons;
		}
		return static_cast<EMagicLeapPlaneQueryFlags>(0);
	}

	void MLToUnrealPlanesQueryFlags(uint32 QueryFlags, TArray<EMagicLeapPlaneQueryFlags>& OutPlaneFlags)
	{
		static TArray<MLPlanesQueryFlags> AllMLFlags({
		  MLPlanesQueryFlag_Vertical,
		  MLPlanesQueryFlag_Horizontal,
		  MLPlanesQueryFlag_Arbitrary,
		  MLPlanesQueryFlag_OrientToGravity,
		  MLPlanesQueryFlag_Inner,
		  MLPlanesQueryFlag_Semantic_Ceiling,
		  MLPlanesQueryFlag_Semantic_Floor,
		  MLPlanesQueryFlag_Semantic_Wall,
		  MLPlanesQueryFlag_Polygons
		});

		OutPlaneFlags.Empty();

		for (MLPlanesQueryFlags flag : AllMLFlags)
		{
			if ((QueryFlags & static_cast<MLPlanesQueryFlags>(flag)) != 0)
			{
				OutPlaneFlags.Add(MLToUnrealPlanesQueryFlagMap(flag));
			}
		}
	}

	MLPlanesQueryFlags UnrealToMLPlanesQueryFlagMap(EMagicLeapPlaneQueryFlags QueryFlag)
	{
		switch (QueryFlag)
		{
		case EMagicLeapPlaneQueryFlags::Vertical:
			return MLPlanesQueryFlag_Vertical;
		case EMagicLeapPlaneQueryFlags::Horizontal:
			return MLPlanesQueryFlag_Horizontal;
		case EMagicLeapPlaneQueryFlags::Arbitrary:
			return MLPlanesQueryFlag_Arbitrary;
		case EMagicLeapPlaneQueryFlags::OrientToGravity:
			return MLPlanesQueryFlag_OrientToGravity;
		case EMagicLeapPlaneQueryFlags::PreferInner:
			return MLPlanesQueryFlag_Inner;
		case EMagicLeapPlaneQueryFlags::Ceiling:
			return MLPlanesQueryFlag_Semantic_Ceiling;
		case EMagicLeapPlaneQueryFlags::Floor:
			return MLPlanesQueryFlag_Semantic_Floor;
		case EMagicLeapPlaneQueryFlags::Wall:
			return MLPlanesQueryFlag_Semantic_Wall;
		case EMagicLeapPlaneQueryFlags::Polygons:
			return MLPlanesQueryFlag_Polygons;
		}
		return static_cast<MLPlanesQueryFlags>(0);
	}

	MLPlanesQueryFlags UnrealToMLPlanesQueryFlags(const TArray<EMagicLeapPlaneQueryFlags>& QueryFlags)
	{
		MLPlanesQueryFlags MLFlags = static_cast<MLPlanesQueryFlags>(0);

		for (EMagicLeapPlaneQueryFlags Flag : QueryFlags)
		{
			MLFlags = static_cast<MLPlanesQueryFlags>(MLFlags | UnrealToMLPlanesQueryFlagMap(Flag));
		}

		return MLFlags;
	}
}
#endif // WITH_MLSDK

FMagicLeapPlanesModule::FMagicLeapPlanesModule()
{}

void FMagicLeapPlanesModule::StartupModule()
{
	IMagicLeapPlanesModule::StartupModule();
#if WITH_MLSDK
	Tracker = ML_INVALID_HANDLE;
#endif // WITH_MLSDK
	TickDelegate = FTickerDelegate::CreateRaw(this, &FMagicLeapPlanesModule::Tick);

	IMagicLeapPlugin::Get().RegisterMagicLeapTrackerEntity(this);
}

void FMagicLeapPlanesModule::ShutdownModule()
{
	IMagicLeapPlugin::Get().UnregisterMagicLeapTrackerEntity(this);

	DestroyTracker();
	IModuleInterface::ShutdownModule();
}

void FMagicLeapPlanesModule::DestroyEntityTracker()
{
	DestroyTracker();
}

bool FMagicLeapPlanesModule::Tick(float DeltaTime)
{
#if WITH_MLSDK
	if (!(IMagicLeapPlugin::Get().IsMagicLeapHMDValid()))
	{
		return false;
	}
	else if (!MLHandleIsValid(Tracker))
	{
		return true;
	}

	const FTransform TrackingToWorld = UHeadMountedDisplayFunctionLibrary::GetTrackingToWorldTransform(nullptr);

	for (int32 iRequest = PendingRequests.Num()-1; iRequest > -1; --iRequest)
	{
		FPlanesRequestMetaData& PendingRequest = PendingRequests[iRequest];
		uint32 OutNumResults = 0;

		MLPlaneBoundariesList BoundariesList;
		MLPlaneBoundariesListInit(&BoundariesList);

		MLResult Result = MLPlanesQueryGetResultsWithBoundaries(Tracker, PendingRequest.ResultHandle, PendingRequest.ResultMLPlanes.GetData(), &OutNumResults, &BoundariesList);
		switch (Result)
		{
		case MLResult_Pending:
		{
			// Intentionally skip. We'll continue to check until it has completed.
		}
		break;
		case MLResult_UnspecifiedFailure:
		{
			PendingRequest.ResultDelegateStatic.ExecuteIfBound(false, TArray<FMagicLeapPlaneResult>(), TArray<FMagicLeapPlaneBoundaries>());
			PendingRequest.ResultDelegateDynamic.Broadcast(false, TArray<FMagicLeapPlaneResult>(), TArray<FMagicLeapPlaneBoundaries>());
			PendingRequests.RemoveAt(iRequest);
		}
		break;
		case MLResult_Ok:
		{
			const IMagicLeapPlugin& MLPlugin = IMagicLeapPlugin::Get();
			float WorldToMetersScale = MLPlugin.GetWorldToMetersScale();

			TArray<FMagicLeapPlaneResult> Planes;
			Planes.Reserve(OutNumResults);

			for (uint32 i = 0; i < OutNumResults; ++i)
			{
				FMagicLeapPlaneResult ResultPlane;
				// Perception uses all coordinates in RUB so for them X axis is right and corresponds to the width of the plane.
				// Unreal uses FRU, so the Y-axis is towards the right which makes the Y component of the vector the width.
				ResultPlane.PlaneDimensions = FVector2D(PendingRequest.ResultMLPlanes[i].height * WorldToMetersScale, PendingRequest.ResultMLPlanes[i].width * WorldToMetersScale);

				FTransform PlaneTransform = FTransform(MagicLeap::ToFQuat(PendingRequest.ResultMLPlanes[i].rotation), MagicLeap::ToFVector(PendingRequest.ResultMLPlanes[i].position, WorldToMetersScale), FVector(1.0f, 1.0f, 1.0f));
				if (PlaneTransform.ContainsNaN())
				{
					UE_LOG(LogMagicLeapPlanes, Error, TEXT("Plane result %d transform contains NaN."), i);
					continue;
				}
				if (!PlaneTransform.GetRotation().IsNormalized())
				{
					FQuat rotation = PlaneTransform.GetRotation();
					rotation.Normalize();
					PlaneTransform.SetRotation(rotation);
				}

				PlaneTransform.ConcatenateRotation(FQuat(FVector(0, 0, 1), PI));
				PlaneTransform = PlaneTransform * TrackingToWorld;
				ResultPlane.PlanePosition = PlaneTransform.GetLocation();
				ResultPlane.PlaneOrientation = PlaneTransform.Rotator();
				// The plane orientation has the forward axis (X) pointing in the direction of the plane's normal.
				// We are rotating it by 90 degrees clock-wise about the right axis (Y) to get the up vector (Z) to point in the direction of the plane's normal.
				// Since we are rotating the axis, the rotation is in the opposite direction of the object i.e. -90 degrees.
				PlaneTransform.ConcatenateRotation(FQuat(FVector(0, 1, 0), -PI/2));
				ResultPlane.ContentOrientation = PlaneTransform.Rotator();
				ResultPlane.ID = MagicLeap::MLHandleToFGuid(PendingRequest.ResultMLPlanes[i].id);
				MLToUnrealPlanesQueryFlags(PendingRequest.ResultMLPlanes[i].flags, ResultPlane.PlaneFlags);
				
				Planes.Add(ResultPlane);
			}

			TArray<FMagicLeapPlaneBoundaries> ResultBoundariesList;
			if (BoundariesList.plane_boundaries != nullptr)
			{
				ResultBoundariesList.AddDefaulted(BoundariesList.plane_boundaries_count);
				for (uint32 i = 0; i < BoundariesList.plane_boundaries_count; ++i)
				{
					const MLPlaneBoundaries& Boundaries = BoundariesList.plane_boundaries[i];

					FMagicLeapPlaneBoundaries& ResultBoundaries = ResultBoundariesList[i];
					ResultBoundaries.ID = MagicLeap::MLHandleToFGuid(Boundaries.id);
					ResultBoundaries.Boundaries.AddDefaulted(Boundaries.boundaries_count);
					for (uint32 j = 0; j < Boundaries.boundaries_count; ++j)
					{
						const MLPlaneBoundary& Boundary = Boundaries.boundaries[j];

						FMagicLeapPlaneBoundary& ResultBoundary = ResultBoundaries.Boundaries[j];
						ResultBoundary.Polygon.Vertices.AddDefaulted(Boundary.polygon->vertices_count);
						for (uint32 k = 0; k < Boundary.polygon->vertices_count; ++k)
						{
							ResultBoundary.Polygon.Vertices[k] = MagicLeap::ToFVector(Boundary.polygon->vertices[k], WorldToMetersScale);
							ResultBoundary.Polygon.Vertices[k] = TrackingToWorld.TransformPosition(ResultBoundary.Polygon.Vertices[k]);
						}

						if (Boundary.holes != nullptr && Boundary.holes_count > 0)
						{
							ResultBoundary.Holes.AddDefaulted(Boundary.holes_count);
							const MLPolygon* hole = Boundary.holes;
							for (uint32 k = 0; k < Boundary.holes_count; ++k)
							{
								ResultBoundary.Holes[k].Vertices.AddDefaulted(hole->vertices_count);
								for (uint32 m = 0; m < hole->vertices_count; ++m)
								{
									ResultBoundary.Holes[k].Vertices[m] = MagicLeap::ToFVector(hole->vertices[m], WorldToMetersScale);
									ResultBoundary.Holes[k].Vertices[m] = TrackingToWorld.TransformPosition(ResultBoundary.Holes[k].Vertices[m]);
								}
								++hole;
							}
						}
					}
				}

				Result = MLPlanesReleaseBoundariesList(Tracker, &BoundariesList);
				UE_CLOG(Result != MLResult_Ok, LogMagicLeapPlanes, Error, TEXT("MLPlanesReleaseBoundariesList failed with error %s"), UTF8_TO_TCHAR(MLGetResultString(Result)));
			}

			PendingRequest.ResultDelegateStatic.ExecuteIfBound(true, Planes, ResultBoundariesList);
			PendingRequest.ResultDelegateDynamic.Broadcast(true, Planes, ResultBoundariesList);
			PendingRequests.RemoveAt(iRequest);
		}
		break;
		default:
		{
			UE_LOG(LogMagicLeapPlanes, Warning, TEXT("MLPlanesQueryGetResults failed with error '%s'"), UTF8_TO_TCHAR(MLGetResultString(Result)));
		}
		}
	}
#endif //WITH_MLSDK

	return true;
}

bool FMagicLeapPlanesModule::CreateTracker()
{
#if WITH_MLSDK
	if (!MLHandleIsValid(Tracker))
	{
		UE_LOG(LogMagicLeapPlanes, Display, TEXT("Creating Planes Tracker"));
		MLResult Result = MLPlanesCreate(&Tracker);
		UE_CLOG(Result != MLResult_Ok, LogMagicLeapPlanes, Error, TEXT("MLPlanesCreate failed with error '%s'."), UTF8_TO_TCHAR(MLGetResultString(Result)));

		if (Result == MLResult_Ok)
		{
			TickDelegateHandle = FTicker::GetCoreTicker().AddTicker(TickDelegate);
		}

		return Result == MLResult_Ok;
	}
	else
	{
		UE_LOG(LogMagicLeapPlanes, Log, TEXT("Planes tracker already created."));
	}
#endif // WITH_MLSDK
	return false;
}

bool FMagicLeapPlanesModule::DestroyTracker()
{
#if WITH_MLSDK
	if (MLHandleIsValid(Tracker))
	{
		MLResult Result = MLPlanesDestroy(Tracker);
		UE_CLOG(Result != MLResult_Ok, LogMagicLeapPlanes, Error, TEXT("MLPlanesDestroy failed with error '%s'."), UTF8_TO_TCHAR(MLGetResultString(Result)));
		Tracker = ML_INVALID_HANDLE;
		FTicker::GetCoreTicker().RemoveTicker(TickDelegateHandle);
		return true;
	}
	else
	{
		UE_LOG(LogMagicLeapPlanes, Log, TEXT("Planes tracker already destroyed."));
	}
#endif // WITH_MLSDK
	return false;
}

bool FMagicLeapPlanesModule::IsTrackerValid() const
{
#if WITH_MLSDK
	return MLHandleIsValid(Tracker);
#else
	return false;
#endif // WITH_MLSDK
}

bool FMagicLeapPlanesModule::QueryBeginAsync(const FMagicLeapPlanesQuery& QueryParams, const FMagicLeapPlanesResultStaticDelegate& InResultDelegate)
{
#if WITH_MLSDK
	MLHandle Handle = SubmitPlanesQuery(QueryParams);
	if (MLHandleIsValid(Handle))
	{
		FPlanesRequestMetaData& RequestMetaData = PendingRequests.AddDefaulted_GetRef();
		RequestMetaData.ResultHandle = Handle;
		RequestMetaData.ResultMLPlanes.AddDefaulted(QueryParams.MaxResults);
		RequestMetaData.ResultDelegateStatic = InResultDelegate;
		return true;
	}
#endif // WITH_MLSDK
	return false;
}

bool FMagicLeapPlanesModule::QueryBeginAsync(
	const FMagicLeapPlanesQuery& QueryParams,
	const FMagicLeapPlanesResultDelegateMulti& InResultDelegate)
{
#if WITH_MLSDK
	MLHandle Handle = SubmitPlanesQuery(QueryParams);
	if (MLHandleIsValid(Handle))
	{
		FPlanesRequestMetaData& RequestMetaData = PendingRequests.AddDefaulted_GetRef();
		RequestMetaData.ResultHandle = Handle;
		RequestMetaData.ResultMLPlanes.AddDefaulted(QueryParams.MaxResults);
		RequestMetaData.ResultDelegateDynamic = InResultDelegate;
		return true;
	}
#endif // WITH_MLSDK
	return false;
}

#if WITH_MLSDK
MLHandle FMagicLeapPlanesModule::SubmitPlanesQuery(const FMagicLeapPlanesQuery& QueryParams)
{
	if (!IMagicLeapPlugin::Get().IsMagicLeapHMDValid())
	{
		return ML_INVALID_HANDLE;
	}

	const IMagicLeapPlugin& MLPlugin = IMagicLeapPlugin::Get();
	float WorldToMetersScale = MLPlugin.GetWorldToMetersScale();
	check(WorldToMetersScale != 0);

	const FTransform WorldToTracking = UHeadMountedDisplayFunctionLibrary::GetTrackingToWorldTransform(nullptr).Inverse();

	MLPlanesQuery Query;
	Query.max_results = static_cast<uint32>(QueryParams.MaxResults);
	Query.flags = UnrealToMLPlanesQueryFlags(QueryParams.Flags);
	Query.min_hole_length = QueryParams.MinHoleLength / WorldToMetersScale;
	Query.min_plane_area = QueryParams.MinPlaneArea / (WorldToMetersScale * WorldToMetersScale);
	Query.bounds_center = MagicLeap::ToMLVector(WorldToTracking.TransformPosition(QueryParams.SearchVolumePosition), WorldToMetersScale);
	Query.bounds_rotation = MagicLeap::ToMLQuat(WorldToTracking.TransformRotation(QueryParams.SearchVolumeOrientation));
	// The C-API extents are 'full' extents - width, height, and depth. UE4 boxes are 'half' extents.
	Query.bounds_extents = MagicLeap::ToMLVectorExtents(QueryParams.SearchVolumeExtents * 2, WorldToMetersScale);

	MLHandle Handle = ML_INVALID_HANDLE;
	MLResult Result = MLPlanesQueryBegin(Tracker, &Query, &Handle);
	UE_CLOG(Result != MLResult_Ok, LogMagicLeapPlanes, Error, TEXT("MLPlanesQueryBegin failed with error '%s'."), UTF8_TO_TCHAR(MLGetResultString(Result)));

	return Handle;
}
#endif // WITH_MLSDK

IMPLEMENT_MODULE(FMagicLeapPlanesModule, MagicLeapPlanes);
