// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Containers/Union.h"
#include "IMagicLeapPlugin.h"
#include "IMagicLeapPlanesModule.h"
#include "MagicLeapPlanesTypes.h"
#include "IMagicLeapTrackerEntity.h"
#include "Lumin/CAPIShims/LuminAPIPlanes.h"

DECLARE_LOG_CATEGORY_EXTERN(LogMagicLeapPlanes, Verbose, All);

class FMagicLeapPlanesModule : public IMagicLeapPlanesModule, public IMagicLeapTrackerEntity
{
public:
	FMagicLeapPlanesModule();

	void StartupModule() override;
	void ShutdownModule() override;
	bool Tick(float DeltaTime);

	/** IMagicLeapTrackerEntity interface */
	virtual void DestroyEntityTracker() override;

	virtual bool CreateTracker() override;
	virtual bool DestroyTracker() override;
	virtual bool IsTrackerValid() const override;
	virtual FGuid AddQuery(EMagicLeapPlaneQueryType QueryType) override;
	virtual bool RemoveQuery(FGuid Handle) override;
	virtual bool QueryBeginAsync(const FMagicLeapPlanesQuery& QueryParams, const FMagicLeapPlanesResultStaticDelegate& InResultDelegate) override;
	virtual bool QueryBeginAsync(const FMagicLeapPlanesQuery& QueryParams, const FMagicLeapPlanesResultDelegateMulti& InResultDelegate) override;
	virtual bool PersistentQueryBeginAsync(const FMagicLeapPlanesQuery& QueryParams, const FGuid& QueryHandle, const FMagicLeapPersistentPlanesResultStaticDelegate& ResultDelegate) override;
	virtual bool PersistentQueryBeginAsync(const FMagicLeapPlanesQuery& QueryParams, const FGuid& QueryHandle, const FMagicLeapPersistentPlanesResultDelegateMulti& ResultDelegate) override;
private:
#if WITH_MLSDK

	using DelegateType = TUnion<
		FMagicLeapPlanesResultDelegateMulti,
		FMagicLeapPlanesResultStaticDelegate,
		FMagicLeapPersistentPlanesResultStaticDelegate,
		FMagicLeapPersistentPlanesResultDelegateMulti
	>;
	
	struct FPlanesRequestMetaData
	{
	public:
		FPlanesRequestMetaData(EMagicLeapPlaneQueryType InQueryType) :
			QueryType(InQueryType),
			ResultHandle(ML_INVALID_HANDLE),
			SimilarityThreshold(1.0f),
			bInProgress(false),
			bRemoveRequested(false),
			bIsPersistent(false),
			bTrackingSpace(false)
		{		
		}
		
		/** Dispatches the appropriate result delegate based on the query type
		 *	Returns true if the dispatch was successful, false otherwise
		 */
		bool Dispatch(bool bSuccess,
			const TArray<FMagicLeapPlaneResult>& AddedPlanes,
			const TArray<FGuid>& RemovedPlaneIDs,
			const TArray<FMagicLeapPlaneBoundaries>& Polygons,
			const TArray<FGuid>& RemovedPolygonIDs);
		
		EMagicLeapPlaneQueryType QueryType;
		MLHandle ResultHandle;
		FGuid QueryHandle;
		DelegateType ResultDelegate;
		TArray<MLPlane> ResultMLPlanes;

		float SimilarityThreshold;

		bool bInProgress;
		bool bRemoveRequested;
		bool bIsPersistent;
		bool bTrackingSpace;

		// The mapped storage of outer plane IDs and their respective planes.
		// Only the transformation matrix and inner plane ID need to be stored for each plane
		TMap<FGuid, TArray< TTuple<FMatrix, FGuid>>> PlaneStorage;
		
	};

	MLHandle SubmitPlanesQuery(const FMagicLeapPlanesQuery& QueryParams);

	/** Retrieves the corresponding planes metadata with checks */
	FPlanesRequestMetaData& GetQuery(const FGuid& Handle);

	/** Verifies that the request handle exists */
	bool ContainsQuery(const FGuid& Handle) const;
	
	TSparseArray<FPlanesRequestMetaData> Requests;
	TArray<FGuid> PendingRequests;
	MLHandle Tracker;
#endif //WITH_MLSDK
	FTickerDelegate TickDelegate;
	FDelegateHandle TickDelegateHandle;

	/** The last serial number we assigned from this module */
	uint64 LastAssignedSerialNumber;
	
};

inline FMagicLeapPlanesModule& GetMagicLeapPlanesModule()
{
	return FModuleManager::Get().GetModuleChecked<FMagicLeapPlanesModule>("MagicLeapPlanes");
}
