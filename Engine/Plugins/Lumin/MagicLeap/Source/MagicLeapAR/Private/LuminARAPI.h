// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"

#include "LuminARTypes.h"
#include "LuminARSessionConfig.h"
#include "ARSessionConfig.h"
#include "MagicLeapPlanesTypes.h"
#include "MagicLeapHandle.h"

#include "LuminARAPI.generated.h"

DEFINE_LOG_CATEGORY_STATIC(LogLuminARAPI, Log, All);


enum class ELuminARAPIStatus : int
{
	/// The operation was successful.
	AR_SUCCESS = 0,

	/// One of the arguments was invalid, either null or not appropriate for the
	/// operation requested.
	AR_ERROR_INVALID_ARGUMENT = -1,

	/// An internal error occurred that the application should not attempt to
	/// recover from.
	AR_ERROR_FATAL = -2,
};

enum class ELuminARPlaneQueryStatus : int
{
	Unknown,
	Success,
	Fail
};

static ArTrackableType GetTrackableType(UClass* ClassType)
{
	if (ClassType == UARTrackedGeometry::StaticClass())
	{
		return ArTrackableType::LUMIN_AR_TRACKABLE_PLANE;
	}
	else if (ClassType == UARPlaneGeometry::StaticClass())
	{
		return ArTrackableType::LUMIN_AR_TRACKABLE_PLANE;
	}
	else
	{
		return ArTrackableType::LUMIN_AR_TRACKABLE_NOT_VALID;
	}
}

class FLuminARFrame;
class FLuminARSession;
class ULuminARCameraImage;

UCLASS()
class ULuminARUObjectManager : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY()
	TArray<UARPin*> AllAnchors;

	TMap<FGuid, TSharedPtr<LuminArAnchor>> HandleToLuminAnchorMap;
	TMap<FGuid, UARPin*> HandleToAnchorMap;
	TMap<FGuid, TWeakObjectPtr<UARTrackedGeometry>> TrackableHandleMap;

	template< class T > T* GetTrackableFromHandle(const FGuid& TrackableHandle, FLuminARSession* Session);

	void DumpTrackableHandleMap(const FGuid& SessionHandle);
};


class FLuminARSession : public TSharedFromThis<FLuminARSession>, public FGCObject
{

public:
	static TSharedPtr<FLuminARSession> CreateLuminARSession();

	FLuminARSession();
	~FLuminARSession();

	// Properties
	ULuminARUObjectManager* GetUObjectManager();
	float GetWorldToMeterScale();
	void SetARSystem(TSharedRef<FARSupportInterface , ESPMode::ThreadSafe> InArSystem) { ARSystem = InArSystem; }
	TSharedRef<FARSupportInterface , ESPMode::ThreadSafe> GetARSystem() { return ARSystem.ToSharedRef(); }

	ELuminARAPIStatus Resume();
	ELuminARAPIStatus Pause();
	ELuminARAPIStatus Update(float WorldToMeterScale);
	const FLuminARFrame* GetLatestFrame();
	uint32 GetFrameNum();

	// Anchor API
	ELuminARAPIStatus CreateARAnchor(const FTransform& TransfromInTrackingSpace, UARTrackedGeometry* TrackedGeometry, USceneComponent* ComponentToPin, FName InDebugName, UARPin*& OutAnchor);
	void DetachAnchor(UARPin* Anchor);

	void GetAllAnchors(TArray<UARPin*>& OutAnchors) const;
	template< class T > void GetAllTrackables(TArray<T*>& OutLuminARTrackableList);

	ELuminARAPIStatus AcquireCameraImage(ULuminARCameraImage *&OutCameraImage);
	
	void* GetLatestFrameRawPointer();

private:
	void InitTracker();
	void DestroyTracker();
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	FLuminARFrame* LatestFrame;
	ULuminARUObjectManager* UObjectManager;
	float CachedWorldToMeterScale;
	uint32 FrameNumber;

	TSharedPtr<FARSupportInterface , ESPMode::ThreadSafe> ARSystem;
};

struct FPlanesAndBoundaries
{
public:
	FMagicLeapPlaneResult Plane;
	TArray<FVector> PolygonVerticesLocalSpace;
};

class FLuminARFrame
{
	friend class FLuminARSession;

public:
	FLuminARFrame(FLuminARSession* Session);
	~FLuminARFrame();

	void Init();

	void Update(float WorldToMeterScale);

	FTransform GetCameraPose() const;
	int64 GetCameraTimestamp() const;
	ELuminARTrackingState GetCameraTrackingState() const;

	void GetUpdatedAnchors(TArray<UARPin*>& OutUpdatedAnchors) const;

	const FPlanesAndBoundaries* GetPlaneResult(const FGuid& Handle) const { return PlaneResultsMap.Find(Handle); }

	void ARLineTrace(FVector2D ScreenPosition, ELuminARLineTraceChannel RequestedTraceChannels, TArray<FARTraceResult>& OutHitResults) const;
	void ARLineTrace(FVector Start, FVector End, ELuminARLineTraceChannel RequestedTraceChannels, TArray<FARTraceResult>& OutHitResults) const;

	FMatrix GetProjectionMatrix() const;
	void TransformDisplayUvCoords(const TArray<float>& UvCoords, TArray<float>& OutUvCoords) const;

	FLuminARLightEstimate GetLightEstimate() const;

private:

	void StartPlaneQuery();
	void ProcessPlaneQuery(const bool bSuccess, const TArray<FMagicLeapPlaneResult>& Planes, const TArray<FMagicLeapPlaneBoundaries>& Polygons);


	FLuminARSession* Session;
	//FTransform LatestCameraPose;
	int64 LatestCameraTimestamp;
	ELuminARTrackingState LatestCameraTrackingState;
	ELuminARPlaneQueryStatus LatestARPlaneQueryStatus;

	TArray<UARPin*> UpdatedAnchors;
	TMap<FGuid, FPlanesAndBoundaries> PlaneResultsMap;

	int32 MaxPlaneQueryResults = 0;
	bool bDiscardZeroExtentPlanes = false;

	bool bPlanesQueryPending;

	FMagicLeapPlanesResultStaticDelegate ResultDelegate;
};

class FLuminARTrackableResource : public IARRef
{
public:
	// IARRef interface
	virtual void AddRef() override { }

	virtual void RemoveRef() override
	{
#if WITH_MLSDK
		TrackableHandle = MagicLeap::MLHandleToFGuid(ML_INVALID_HANDLE);
#endif // WITH_MLSDK
	}

public:
	FLuminARTrackableResource(const FGuid& InTrackableHandle, UARTrackedGeometry* InTrackedGeometry)
		: TrackableHandle(InTrackableHandle)
		, TrackedGeometry(InTrackedGeometry)
	{
#if WITH_MLSDK
		ensure(MagicLeap::FGuidIsValidHandle(TrackableHandle));
#endif // WITH_MLSDK
	}

	virtual ~FLuminARTrackableResource()
	{
	}

	EARTrackingState GetTrackingState();

	virtual void UpdateGeometryData(FLuminARSession* InSession);

	const FGuid& GetNativeHandle() { return TrackableHandle; }

	void ResetNativeHandle(LuminArTrackable* InTrackableHandle);

protected:
	FGuid TrackableHandle;
	UARTrackedGeometry* TrackedGeometry;
};

class FLuminARTrackedPlaneResource : public FLuminARTrackableResource
{
public:
	FLuminARTrackedPlaneResource(const FGuid& InTrackableHandle, UARTrackedGeometry* InTrackedGeometry)
		: FLuminARTrackableResource(InTrackableHandle, InTrackedGeometry)
	{
	}

	void UpdateGeometryData(FLuminARSession* InSession) override;
};

// Template function definition
template< class T >
T* ULuminARUObjectManager::GetTrackableFromHandle(const FGuid& TrackableHandle, FLuminARSession* Session)
{
	if (!TrackableHandleMap.Contains(TrackableHandle)
		|| !TrackableHandleMap[TrackableHandle].IsValid()
		|| TrackableHandleMap[TrackableHandle]->GetTrackingState() == EARTrackingState::StoppedTracking)
	{
		// Add the trackable to the cache.
		UARTrackedGeometry* NewTrackableObject = nullptr;
		ArTrackableType TrackableType = ArTrackableType::LUMIN_AR_TRACKABLE_NOT_VALID;

		check(Session);
		const FLuminARFrame* Frame = Session->GetLatestFrame();
		check(Frame);
		const FPlanesAndBoundaries* PlaneResult = Frame->GetPlaneResult(TrackableHandle);
		if (PlaneResult)
		{
			TrackableType = ArTrackableType::LUMIN_AR_TRACKABLE_PLANE;
		}

		IARRef* NativeResource = nullptr;
		if (TrackableType == ArTrackableType::LUMIN_AR_TRACKABLE_PLANE)
		{
			UARPlaneGeometry* PlaneObject = NewObject<UARPlaneGeometry>();
			NewTrackableObject = static_cast<UARTrackedGeometry*>(PlaneObject);
			NativeResource = new FLuminARTrackedPlaneResource(TrackableHandle, NewTrackableObject);
		}
		else
		{
			//checkf(false, TEXT("ULuminARUObjectManager failed to get a valid trackable %p. Unknow LuminAR Trackable Type."), TrackableHandle);
			return nullptr;
		}

		// We should have a valid trackable object now.
		checkf(NewTrackableObject, TEXT("Unknow LuminAR Trackable Type: %d"), TrackableType);

		NewTrackableObject->InitializeNativeResource(NativeResource);
		NativeResource = nullptr;

		FLuminARTrackableResource* TrackableResource = static_cast<FLuminARTrackableResource*>(NewTrackableObject->GetNativeResource());

		// no, we always do this in ProcessPlaneQuery
		// Update the tracked geometry data using the native resource
		//TrackableResource->UpdateGeometryData();
		ensure(TrackableResource->GetTrackingState() != EARTrackingState::StoppedTracking);

		TrackableHandleMap.Add(TrackableHandle, TWeakObjectPtr<UARTrackedGeometry>(NewTrackableObject));
	}

	T* Result = Cast<T>(TrackableHandleMap[TrackableHandle].Get());
	//checkf(Result, TEXT("ULuminARUObjectManager failed to get a valid trackable %p from the map."), TrackableHandle);
	return Result;
}

template< class T >
void FLuminARSession::GetAllTrackables(TArray<T*>& OutARCoreTrackableList)
{
	OutARCoreTrackableList.Empty();

	ArTrackableType TrackableType = GetTrackableType(T::StaticClass());
	if (TrackableType == ArTrackableType::LUMIN_AR_TRACKABLE_NOT_VALID)
	{
		UE_LOG(LogLuminARAPI, Error, TEXT("Invalid Trackable type: %s"), *T::StaticClass()->GetName());
		return;
	}

	for (auto TrackableHandleMapPair : GetUObjectManager()->TrackableHandleMap)
	{
		TWeakObjectPtr<UARTrackedGeometry>& Value = TrackableHandleMapPair.Value;
		if (Value.IsValid() &&
			Value->GetTrackingState() != EARTrackingState::StoppedTracking
			)
		{
			T* Trackable = Cast<T>(Value.Get());
			if (Trackable)
			{
				OutARCoreTrackableList.Add(Trackable);
			}
		}	
	}
}
