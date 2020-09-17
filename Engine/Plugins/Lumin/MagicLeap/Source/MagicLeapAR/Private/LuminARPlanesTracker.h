// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ILuminARTracker.h"
#include "MagicLeapPlanesTypes.h"
#include "LuminARTrackableResource.h"

class FLuminARImplementation;

struct FLuminPlanesAndBoundaries
{
public:
	FMagicLeapPlaneResult Plane;
	TArray<FVector> PolygonVerticesLocalSpace;
};

class FLuminARPlanesTracker : public ILuminARTracker
{
public:
	FLuminARPlanesTracker(FLuminARImplementation& InARSystemSupport);
	virtual ~FLuminARPlanesTracker();

	virtual void CreateEntityTracker() override;
	virtual void DestroyEntityTracker() override;
	virtual void OnStartGameFrame() override;
	virtual bool IsHandleTracked(const FGuid& Handle) const override;
	virtual UARTrackedGeometry* CreateTrackableObject() override;
	virtual UClass* GetARComponentClass(const UARSessionConfig& SessionConfig) override;
	virtual IARRef* CreateNativeResource(const FGuid& Handle, UARTrackedGeometry* TrackableObject) override;

	const FGuid* GetParentHandle(const FGuid& Handle) const;
	const FLuminPlanesAndBoundaries* GetPlaneResult(const FGuid& Handle) const;

private:
	void StartPlaneQuery();
	void ProcessPlaneQuery(const bool bSuccess, 
		const FGuid& Handle,
		const EMagicLeapPlaneQueryType QueryType,
		const TArray<FMagicLeapPlaneResult>& NewPlanes, 
		const TArray<FGuid>& RemovedPlanes,
		const TArray<FMagicLeapPlaneBoundaries>& NewPolygons, 
		const TArray<FGuid>& RemovedPolygons);

	EMagicLeapPlaneQueryFlags GetMostSignificantPlaneFlag(const TArray<EMagicLeapPlaneQueryFlags>& PlaneFlags) const;

	// Maps a ML plane's ID to the authoritative InnerID
	TMap<FGuid, FGuid> PlaneParentMap;

	// Maps InnerID's to the plane and boundary data
	TMap<FGuid, FLuminPlanesAndBoundaries> PlaneResultsMap;

	bool bDiscardZeroExtentPlanes = false;

	bool bPlanesQueryPending;

	FGuid PlanesQueryHandle;
	FMagicLeapPersistentPlanesResultStaticDelegate ResultDelegate;
};

class FLuminARTrackedPlaneResource : public FLuminARTrackableResource
{
public:
	FLuminARTrackedPlaneResource(const FGuid& InTrackableHandle, UARTrackedGeometry* InTrackedGeometry, const FLuminARPlanesTracker& InTracker)
		: FLuminARTrackableResource(InTrackableHandle, InTrackedGeometry)
		, Tracker(InTracker)
	{
	}

	void UpdateGeometryData(FLuminARImplementation* InARSystemSupport) override;

private:
	const FLuminARPlanesTracker& Tracker;
};
