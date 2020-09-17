// Copyright Epic Games, Inc. All Rights Reserved.

#include "MagicLeapPlanesModule.h"
#include "MagicLeapMath.h"
#include "HeadMountedDisplayFunctionLibrary.h"
#include "Engine/Engine.h"
#include "Components/BoxComponent.h"
#include "MagicLeapHandle.h"
#include "Stats/Stats.h"

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

FMagicLeapPlanesModule::FMagicLeapPlanesModule():
	LastAssignedSerialNumber(0)
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
	QUICK_SCOPE_CYCLE_COUNTER(STAT_FMagicLeapPlanesModule_Tick);

#if WITH_MLSDK
	if (!(IMagicLeapPlugin::Get().IsMagicLeapHMDValid()))
	{
		return false;
	}
	else if (!MLHandleIsValid(Tracker))
	{
		return true;
	}


	for (int32 iRequest = PendingRequests.Num()-1; iRequest > -1; --iRequest)
	{
		FPlanesRequestMetaData& PendingRequest = GetQuery(PendingRequests[iRequest]);
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
				
			const bool bDispatchSuccess = PendingRequest.Dispatch(false, TArray<FMagicLeapPlaneResult>(), TArray<FGuid>(), TArray<FMagicLeapPlaneBoundaries>(), TArray<FGuid>());

			UE_CLOG(!bDispatchSuccess,LogMagicLeapPlanes, Error, TEXT("Plane result dispatch failed."));
				
			PendingRequests.RemoveAt(iRequest);
			PendingRequest.bInProgress = false;
				
			if(PendingRequest.bRemoveRequested)
			{
				RemoveQuery(PendingRequest.QueryHandle);
			}
		
		}
		break;
		case MLResult_Ok:
		{
			const IMagicLeapPlugin& MLPlugin = IMagicLeapPlugin::Get();
			float WorldToMetersScale = MLPlugin.GetWorldToMetersScale();

			const FTransform TrackingToWorld = PendingRequest.bTrackingSpace ? FTransform::Identity : UHeadMountedDisplayFunctionLibrary::GetTrackingToWorldTransform(nullptr);

			TArray<FMagicLeapPlaneResult> Planes;
			Planes.Reserve(OutNumResults); // Reserve for the worse case scenario
				
			TArray<FGuid> RemovedPlanes;
			TMap<FGuid, TArray< TTuple<FMatrix, FGuid>>> ScratchPlaneStorage;
			if (PendingRequest.QueryType == EMagicLeapPlaneQueryType::Delta)
			{
				
				// Reserve for the worse cast scenario
				RemovedPlanes.Reserve(OutNumResults);
				ScratchPlaneStorage.Reserve(OutNumResults);
				
			}
				
				
			for (uint32 i = 0; i < OutNumResults; ++i)
			{
				FMagicLeapPlaneResult ResultPlane;
				// Perception uses all coordinates in RUB so for them X axis is right and corresponds to the width of the plane.
				// Unreal uses FRU, so the Y-axis is towards the right which makes the Y component of the vector the width.
				ResultPlane.PlaneDimensions = FVector2D(PendingRequest.ResultMLPlanes[i].height * WorldToMetersScale, PendingRequest.ResultMLPlanes[i].width * WorldToMetersScale);

				FTransform PlaneTransform_TrackingSpace = FTransform(MagicLeap::ToFQuat(PendingRequest.ResultMLPlanes[i].rotation), MagicLeap::ToFVector(PendingRequest.ResultMLPlanes[i].position, WorldToMetersScale), FVector(1.0f, 1.0f, 1.0f));
				if (PlaneTransform_TrackingSpace.ContainsNaN())
				{
					UE_LOG(LogMagicLeapPlanes, Error, TEXT("Plane result %d transform contains NaN."), i);
					continue;
				}
				if (!PlaneTransform_TrackingSpace.GetRotation().IsNormalized())
				{
					FQuat rotation = PlaneTransform_TrackingSpace.GetRotation();
					rotation.Normalize();
					PlaneTransform_TrackingSpace.SetRotation(rotation);
				}

				// Cache origin tracking space orientation, since we'll use it later on to calculate ContentOrientation
				const FQuat PlaneOrientation_TrackingSpace = PlaneTransform_TrackingSpace.GetRotation();

				// The plane orientation from the C-API has its Back axis (Z) pointing in the direction of the plane's normal.
				// When converted to Unreal's FRU system, its Forward axis (X) ends up pointing in the opposite direction of the plane's normal (RUB to FRU, Back -> Forward)
				// So, rotate by 180 degrees about the Z (Up) axis to make X (Forward) line up with the plane's normal.
				PlaneTransform_TrackingSpace.ConcatenateRotation(FQuat(FVector(0, 0, 1), PI));

				FTransform PlaneTransform_WorldSpace = PlaneTransform_TrackingSpace * TrackingToWorld;
				ResultPlane.PlanePosition = PlaneTransform_WorldSpace.GetLocation();
				ResultPlane.PlaneOrientation = PlaneTransform_WorldSpace.Rotator();

				// The plane orientation from the C-API has its Back axis (Z) pointing in the direction of the plane's normal.
				// When converted to Unreal's FRU system, its Forward axis (X) ends up pointing in the opposite direction of the plane's normal (RUB to FRU, Back -> Forward)
				// Rotate -90 degrees around Y (Right) to makes Z (Up) line up with the plane's normal.
				PlaneTransform_TrackingSpace.SetRotation(PlaneOrientation_TrackingSpace * FQuat(FVector(0, 1, 0), -PI / 2));
				PlaneTransform_WorldSpace = PlaneTransform_TrackingSpace * TrackingToWorld;
				ResultPlane.ContentOrientation = PlaneTransform_WorldSpace.Rotator();

				ResultPlane.ID = MagicLeap::MLHandleToFGuid(PendingRequest.ResultMLPlanes[i].id);
				MLToUnrealPlanesQueryFlags(PendingRequest.ResultMLPlanes[i].flags, ResultPlane.PlaneFlags);

				// Do some extra processing depending on the query type
				if(PendingRequest.QueryType == EMagicLeapPlaneQueryType::Bulk)
				{
					
					Planes.Add(ResultPlane);
					
				}
				else
				{

					const FMatrix PlaneMatrix = PlaneTransform_WorldSpace.ToMatrixWithScale();

					bool bPlaneFound = false;

					// The current plane only needs to be compared to other planes with the same outer plane ID
					auto& PlaneInfoArray = PendingRequest.PlaneStorage.FindOrAdd(ResultPlane.ID);
					for (int32 t = 0; t < PlaneInfoArray.Num(); ++t) {

						//Calculate the Frobenius norm of the matrix
						float Error = 0.0f;

						// Avoiding the FMatrix operators allows a single iteration over all matrix elements.
						for (int32 j = 0; j < 4; ++j) {
							for (int32 k = 0; k < 4; ++k) {
								
								const float Difference = PlaneMatrix.M[j][k] - PlaneInfoArray[t].Key.M[j][k];
								Error += Difference * Difference;

							}

						}
						
						// Note: The matrix norm is only being compared, and the monotonic sqrt(Error) isn't needed.					
						// Remove the plane from the list of removed planes
						if (Error < PendingRequest.SimilarityThreshold)
						{
							
							bPlaneFound = true;
							ResultPlane.InnerID = PlaneInfoArray[t].Value;
							PlaneInfoArray.RemoveAtSwap(t, 1, false);

							break;
							
						}
					}

					// Create a new plane if nothing was found in storage
					if (!bPlaneFound)
					{
						
						ResultPlane.InnerID = FGuid::NewGuid();
						Planes.Add(ResultPlane);
						
					}

					// All planes get placed into the temporary query storage
					ScratchPlaneStorage.FindOrAdd(ResultPlane.ID).Add(MakeTuple(PlaneMatrix, ResultPlane.InnerID));
					
				}
			}

			// Query types that send `Removed Plane IDs` need post-processing
			if (PendingRequest.QueryType == EMagicLeapPlaneQueryType::Delta)
			{

				// Everything leftover in `PlaneStorage` was not matched and can be removed
				for(const auto& PlaneInfoArrayPair : PendingRequest.PlaneStorage)
				{
					for(auto& InfoTuple : PlaneInfoArrayPair.Value)
					{
						
						RemovedPlanes.Add(InfoTuple.Value);
						
					}
				}

				// The removed planes are processed and the storage can now hold the processed planes
				PendingRequest.PlaneStorage = ScratchPlaneStorage;
				
			}
				
			TArray<FMagicLeapPlaneBoundaries> ResultBoundariesList;
			TArray<FGuid> RemovedBoundaries;
				
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
				
			const bool bDispatchSuccess = PendingRequest.Dispatch(true, Planes, RemovedPlanes, ResultBoundariesList, RemovedBoundaries);

			UE_CLOG(!bDispatchSuccess,LogMagicLeapPlanes, Error, TEXT("Plane result dispatch failed."));
							
			PendingRequests.RemoveAt(iRequest);
			PendingRequest.bInProgress = false;
				
			if (PendingRequest.bRemoveRequested)
			{
				RemoveQuery(PendingRequest.QueryHandle);
			}
				
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

	Requests.Empty();
	PendingRequests.Empty();
	
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

FGuid FMagicLeapPlanesModule::AddQuery(EMagicLeapPlaneQueryType QueryType)
{
#if WITH_MLSDK
	const int32 Index = Requests.Add(FPlanesRequestMetaData(QueryType));
	const FGuid QueryHandle = FGuid(Index, ++LastAssignedSerialNumber, 0, 0);
	Requests[Index].QueryHandle = QueryHandle;
	
	return QueryHandle;
#else
	return FGuid();
#endif //WITH_MLSDK	
}

bool FMagicLeapPlanesModule::RemoveQuery(FGuid QueryHandle){	
#if WITH_MLSDK
	if(ContainsQuery(QueryHandle))
	{
		FPlanesRequestMetaData& RequestMetaData = GetQuery(QueryHandle);
		if(RequestMetaData.bInProgress)
		{
			// Defer the removal until the plane query is processed.
			RequestMetaData.bRemoveRequested = true;
			
		}
		else
		{
			
			Requests.RemoveAt(QueryHandle.A);
			
		}
		
		return true;	
	}
#endif //WITH_MLSDK
	
	return false;
	
}

bool FMagicLeapPlanesModule::QueryBeginAsync(
	const FMagicLeapPlanesQuery& QueryParams, 
	const FMagicLeapPlanesResultStaticDelegate& InResultDelegate)
{
#if WITH_MLSDK
	FGuid QueryHandle = AddQuery(EMagicLeapPlaneQueryType::Bulk);
	MLHandle Handle = SubmitPlanesQuery(QueryParams);
	if (MLHandleIsValid(Handle))
	{
		FPlanesRequestMetaData& RequestMetaData = GetQuery(QueryHandle);
		RequestMetaData.ResultHandle = Handle;
		RequestMetaData.ResultMLPlanes.SetNum(QueryParams.MaxResults);
		RequestMetaData.ResultDelegate.SetSubtype<FMagicLeapPlanesResultStaticDelegate>(InResultDelegate);
		RequestMetaData.bTrackingSpace = QueryParams.bResultTrackingSpace;
		RequestMetaData.SimilarityThreshold = QueryParams.SimilarityThreshold;
		RequestMetaData.bRemoveRequested = true;
		RequestMetaData.bIsPersistent = false;
		
		PendingRequests.Add(QueryHandle);
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
	FGuid QueryHandle = AddQuery(EMagicLeapPlaneQueryType::Bulk);
	MLHandle Handle = SubmitPlanesQuery(QueryParams);
	if (MLHandleIsValid(Handle))
	{
		FPlanesRequestMetaData& RequestMetaData = GetQuery(QueryHandle);
		RequestMetaData.ResultHandle = Handle;
		RequestMetaData.ResultMLPlanes.SetNum(QueryParams.MaxResults);
		RequestMetaData.ResultDelegate.SetSubtype<FMagicLeapPlanesResultDelegateMulti>(InResultDelegate);
		RequestMetaData.bTrackingSpace = QueryParams.bResultTrackingSpace;
		RequestMetaData.SimilarityThreshold = QueryParams.SimilarityThreshold;
		RequestMetaData.bRemoveRequested = true;
		RequestMetaData.bIsPersistent = false;
		
		PendingRequests.Add(QueryHandle);
		return true;
	}

#endif // WITH_MLSDK
	return false;
}

bool FMagicLeapPlanesModule::PersistentQueryBeginAsync(
	const FMagicLeapPlanesQuery& QueryParams, 
	const FGuid& QueryHandle, 
	const FMagicLeapPersistentPlanesResultStaticDelegate& InResultDelegate)
{
#if WITH_MLSDK

	if (ContainsQuery(QueryHandle))
	{
		
		MLHandle Handle = SubmitPlanesQuery(QueryParams);
		if (MLHandleIsValid(Handle))
		{
			FPlanesRequestMetaData& RequestMetaData = GetQuery(QueryHandle);
			RequestMetaData.ResultHandle = Handle;
			RequestMetaData.ResultMLPlanes.SetNum(QueryParams.MaxResults);
			RequestMetaData.ResultDelegate.SetSubtype<FMagicLeapPersistentPlanesResultStaticDelegate>(InResultDelegate);
			RequestMetaData.bTrackingSpace = QueryParams.bResultTrackingSpace;
			RequestMetaData.SimilarityThreshold = QueryParams.SimilarityThreshold;
			RequestMetaData.bRemoveRequested = false;
			RequestMetaData.bIsPersistent = true;
			
			// Persistent queries do not enqueue another request if one is already in progress
			if(!RequestMetaData.bInProgress)
			{
				
				RequestMetaData.bInProgress = true;
				PendingRequests.Add(QueryHandle);
				
			}		
			
			return true;
		}
	}
#endif // WITH_MLSDK
	return false;
}

bool FMagicLeapPlanesModule::PersistentQueryBeginAsync(
	const FMagicLeapPlanesQuery& QueryParams,
	const FGuid& QueryHandle,
	const FMagicLeapPersistentPlanesResultDelegateMulti& InResultDelegate)
{
#if WITH_MLSDK
	
	if (ContainsQuery(QueryHandle))
	{
		MLHandle Handle = SubmitPlanesQuery(QueryParams);
		if (MLHandleIsValid(Handle))
		{
			FPlanesRequestMetaData& RequestMetaData = GetQuery(QueryHandle);
			RequestMetaData.ResultHandle = Handle;
			RequestMetaData.ResultMLPlanes.SetNum(QueryParams.MaxResults);
			RequestMetaData.ResultDelegate.SetSubtype<FMagicLeapPersistentPlanesResultDelegateMulti>(InResultDelegate);
			RequestMetaData.bTrackingSpace = QueryParams.bResultTrackingSpace;
			RequestMetaData.SimilarityThreshold = QueryParams.SimilarityThreshold;
			RequestMetaData.bRemoveRequested = false;
			RequestMetaData.bIsPersistent = true;

			// Persistent queries do not enqueue another request if one is already in progress
			if (!RequestMetaData.bInProgress)
			{
				
				RequestMetaData.bInProgress = true;
				PendingRequests.Add(QueryHandle);
				
			}
			
			return true;
		}
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

	MLPlanesQuery Query;
	Query.max_results = static_cast<uint32>(QueryParams.MaxResults);
	Query.flags = UnrealToMLPlanesQueryFlags(QueryParams.Flags);
	Query.min_hole_length = QueryParams.MinHoleLength / WorldToMetersScale;
	Query.min_plane_area = QueryParams.MinPlaneArea / (WorldToMetersScale * WorldToMetersScale);

	if (QueryParams.bSearchVolumeTrackingSpace)
	{
		Query.bounds_center = MagicLeap::ToMLVector(QueryParams.SearchVolumePosition, WorldToMetersScale);
		Query.bounds_rotation = MagicLeap::ToMLQuat(QueryParams.SearchVolumeOrientation);
	}
	else
	{
		const FTransform WorldToTracking = UHeadMountedDisplayFunctionLibrary::GetTrackingToWorldTransform(nullptr).Inverse();
		Query.bounds_center = MagicLeap::ToMLVector(WorldToTracking.TransformPosition(QueryParams.SearchVolumePosition), WorldToMetersScale);
		Query.bounds_rotation = MagicLeap::ToMLQuat(WorldToTracking.TransformRotation(QueryParams.SearchVolumeOrientation));
	}

	// The C-API extents are 'full' extents - width, height, and depth. UE4 boxes are 'half' extents.
	Query.bounds_extents = MagicLeap::ToMLVectorExtents(QueryParams.SearchVolumeExtents * 2, WorldToMetersScale);

	MLHandle Handle = ML_INVALID_HANDLE;
	MLResult Result = MLPlanesQueryBegin(Tracker, &Query, &Handle);
	UE_CLOG(Result != MLResult_Ok, LogMagicLeapPlanes, Error, TEXT("MLPlanesQueryBegin failed with error '%s'."), UTF8_TO_TCHAR(MLGetResultString(Result)));

	return Handle;
}

FMagicLeapPlanesModule::FPlanesRequestMetaData& FMagicLeapPlanesModule::GetQuery(const FGuid& Handle)
{
	
	FPlanesRequestMetaData& MetaData = Requests[Handle.A];
	check(MetaData.QueryHandle == Handle);
	
	return MetaData;
	
}

bool FMagicLeapPlanesModule::ContainsQuery(const FGuid& Handle) const
{
	
	return Requests.IsValidIndex(Handle.A) && Requests[Handle.A].QueryHandle == Handle;
	
}

bool FMagicLeapPlanesModule::FPlanesRequestMetaData::Dispatch(
	bool bSuccess, 
	const TArray<FMagicLeapPlaneResult>& AddedPlanes,
	const TArray<FGuid>& RemovedPlaneIDs,
	const TArray<FMagicLeapPlaneBoundaries>& AddedPolygons,
	const TArray<FGuid>& RemovedPolygonIDs)
{

	if (!bIsPersistent)
	{
		if (ResultDelegate.HasSubtype<FMagicLeapPlanesResultStaticDelegate>())
		{
			
			auto& Delegate = ResultDelegate.GetSubtype<FMagicLeapPlanesResultStaticDelegate>();
			return Delegate.ExecuteIfBound(bSuccess, AddedPlanes, AddedPolygons);
			
		}
		else if(ResultDelegate.HasSubtype<FMagicLeapPlanesResultDelegateMulti>())
		{
			
			auto& Delegate = ResultDelegate.GetSubtype<FMagicLeapPlanesResultDelegateMulti>();
			Delegate.Broadcast(bSuccess, AddedPlanes, AddedPolygons);
			return true;
			
		}
	}
	else
	{
		if (ResultDelegate.HasSubtype<FMagicLeapPersistentPlanesResultStaticDelegate>())
		{
			
			auto& Delegate = ResultDelegate.GetSubtype<FMagicLeapPersistentPlanesResultStaticDelegate>();
			return Delegate.ExecuteIfBound(bSuccess, QueryHandle, QueryType, AddedPlanes, RemovedPlaneIDs, AddedPolygons, RemovedPolygonIDs);
			
		}
		else if(ResultDelegate.HasSubtype<FMagicLeapPersistentPlanesResultDelegateMulti>())
		{
			
			auto& Delegate = ResultDelegate.GetSubtype<FMagicLeapPersistentPlanesResultDelegateMulti>();
			Delegate.Broadcast(bSuccess, QueryHandle, QueryType, AddedPlanes, RemovedPlaneIDs, AddedPolygons, RemovedPolygonIDs);
			return true;
			
		}
	}
	
	return false;
}
#endif // WITH_MLSDK

IMPLEMENT_MODULE(FMagicLeapPlanesModule, MagicLeapPlanes);
