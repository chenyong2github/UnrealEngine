// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "XRTrackingSystemBase.h"
#include "HeadMountedDisplay.h"
#include "IHeadMountedDisplay.h"
#include "Slate/SceneViewport.h"
#include "SceneView.h"
#include "ARSystemSupportBase.h"
#include "ARLightEstimate.h"
#include "Engine/Engine.h" // for FWorldContext
#include "Containers/Queue.h"
#include "Containers/Map.h"
#include "ARActor.h"

#include "LuminARTypes.h"
#include "LuminARTypesPrivate.h"
#include "LuminARSessionConfig.h"
#include "MagicLeapHandle.h"
#include "ILuminARTracker.h"
#include "LuminARTrackableResource.h"

DEFINE_LOG_CATEGORY_STATIC(LogLuminAR, Log, All);

class FLuminARImplementation : public FARSystemSupportBase, public FGCObject, public TSharedFromThis<IARSystemSupport, ESPMode::ThreadSafe>
{
public:
	FLuminARImplementation();
	~FLuminARImplementation();

	// IXRTrackingSystem

	void* GetARSessionRawPointer() override;
	void* GetGameThreadARFrameRawPointer() override;

	ELuminARTrackingState GetTrackingState() const;
	bool GetStartSessionRequestFinished() const;
	void SetARSystem(TSharedPtr<FARSupportInterface , ESPMode::ThreadSafe> InARSystem);
	void ARLineTrace(const FVector2D& ScreenPosition, ELuminARLineTraceChannel TraceChannels, TArray<FARTraceResult>& OutHitResults);
	uint32 GetFrameNum() const;
	int64 GetCameraTimestamp() const;

	TSharedRef<FARSupportInterface , ESPMode::ThreadSafe> GetARSystem();
	bool IsTrackableTypeSupported(UClass* ClassType) const;

	// ULuminARUObjectManager functions
	template< class T > void RemoveHandleIf(TUniqueFunction<bool(const FGuid&)> HandleOperator);
	template< class T > T* GetOrCreateTrackableFromHandle(const FGuid& TrackableHandle);
	void DumpTrackableHandleMap(const FGuid& SessionHandle) const;
	// ~ULuminARUObjectManager functions

	ULuminARCandidateImage* AddLuminRuntimeCandidateImage(UARSessionConfig* SessionConfig, UTexture2D* CandidateTexture, FString FriendlyName, float PhysicalWidth, bool bUseUnreliablePose, bool bImageIsStationary, EMagicLeapImageTargetOrientation InAxisOrientation);

protected:
	// IARSystemSupport
	virtual void OnARSystemInitialized() override;
	virtual bool OnStartARGameFrame(FWorldContext& WorldContext) override;

	/** Returns true/false based on whether AR features are available */
	virtual bool IsARAvailable() const override;

	virtual EARTrackingQuality OnGetTrackingQuality() const override;
	virtual EARTrackingQualityReason OnGetTrackingQualityReason() const override;
	virtual void OnStartARSession(UARSessionConfig* SessionConfig) override;
	virtual void OnPauseARSession() override;
	virtual void OnStopARSession() override;
	virtual FARSessionStatus OnGetARSessionStatus() const override;
	virtual void OnSetAlignmentTransform(const FTransform& InAlignmentTransform) override;
	virtual TArray<FARTraceResult> OnLineTraceTrackedObjects(const FVector2D ScreenCoord, EARLineTraceChannels TraceChannels) override;
	virtual TArray<FARTraceResult> OnLineTraceTrackedObjects(const FVector Start, const FVector End, EARLineTraceChannels TraceChannels) override;
	virtual TArray<UARTrackedGeometry*> OnGetAllTrackedGeometries() const override;
	virtual TArray<UARPin*> OnGetAllPins() const override;
	virtual bool OnIsTrackingTypeSupported(EARSessionType SessionType) const override;
	virtual UARLightEstimate* OnGetCurrentLightEstimate() const override;

	virtual UARPin* FindARPinByComponent(const USceneComponent* Component) const override;
	virtual UARPin* OnPinComponent(USceneComponent* ComponentToPin, const FTransform& PinToWorldTransform, UARTrackedGeometry* TrackedGeometry = nullptr, const FName DebugName = NAME_None) override;
	virtual void OnRemovePin(UARPin* PinToRemove) override;
	virtual bool OnAddManualEnvironmentCaptureProbe(FVector Location, FVector Extent) { return false; }
	virtual TSharedPtr<FARGetCandidateObjectAsyncTask, ESPMode::ThreadSafe> OnGetCandidateObject(FVector Location, FVector Extent) const { return TSharedPtr<FARGetCandidateObjectAsyncTask, ESPMode::ThreadSafe>(); }
	virtual TSharedPtr<FARSaveWorldAsyncTask, ESPMode::ThreadSafe> OnSaveWorld() const { return TSharedPtr<FARSaveWorldAsyncTask, ESPMode::ThreadSafe>(); }
	virtual EARWorldMappingState OnGetWorldMappingStatus() const;
	virtual TArray<FARVideoFormat> OnGetSupportedVideoFormats(EARSessionType SessionType) const override { return TArray<FARVideoFormat>(); }
	virtual TArray<FVector> OnGetPointCloud() const;
	virtual bool OnAddRuntimeCandidateImage(UARSessionConfig* SessionConfig, UTexture2D* CandidateTexture, FString FriendlyName, float PhysicalWidth);
	//~IARSystemSupport

private:
	//~ FGCObject
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	//~ FGCObject

	void ClearTrackedGeometries();
	void OnSpawnARActor(AARActor* NewARActor, UARComponent* NewARComponent, FGuid NativeID);

	template< class T >
	void GetAllTrackables(TArray<T*>& OutLuminARTrackableList) const;

	void ARLineTrace(const FVector& Start, const FVector& End, ELuminARLineTraceChannel TraceChannels, TArray<FARTraceResult>& OutHitResults);

	// ULuminARUObjectManager properties
	UPROPERTY()
	TArray<UARPin*> AllAnchors;

	TMap<FGuid, TSharedPtr<LuminArAnchor>> HandleToLuminAnchorMap;
	TMap<FGuid, UARPin*> HandleToAnchorMap;
	TMap<FGuid, FTrackedGeometryGroup> TrackedGeometryGroups;
	// ~ULuminARUObjectManager functions

private:
	bool bIsLuminARSessionRunning;
	uint32 FrameNumber;
	int64 LatestCameraTimestamp;
	ELuminARTrackingState LatestCameraTrackingState;
	bool bStartSessionRequested; // User called StartSession
	int32 LastTrackedGeometry_DebugId;

	EARSessionStatus CurrentSessionStatus;

	TArray<ILuminARTracker*> Trackers;
	class FLuminARLightTracker* LightEstimateTracker;
	class FLuminARImageTracker* ImageTracker;
	TSharedPtr<FARSupportInterface , ESPMode::ThreadSafe> ARSystem;
	TQueue<TFunction<void()>> RunOnGameThreadQueue;

	//for networked callbacks
	FDelegateHandle SpawnARActorDelegateHandle;
};

DEFINE_LOG_CATEGORY_STATIC(LogLuminARImplementation, Log, All);

// Template function definition

/*	HandleOperator - A function that will be applied to every stored handle.
*		If the function returns true, the handle and its associated value will be removed from AR tracking.
*		If the function returns false, nothing happens.
*		The function is only applied to matching UARTrackedGeometry types
*/
template< class T >
void FLuminARImplementation::RemoveHandleIf(TUniqueFunction<bool(const FGuid&)> HandleOperator)
{
	static_assert(TIsDerivedFrom<T, UARTrackedGeometry>::IsDerived, "T must be derived from UARTrackedGeometry");

	for (TMap<FGuid, FTrackedGeometryGroup>::TIterator Iterator = TrackedGeometryGroups.CreateIterator(); Iterator; ++Iterator)
	{
		FTrackedGeometryGroup& TrackedGeoemtryGroup = Iterator->Value;
		auto* DerivedPointer = Cast<T>(TrackedGeoemtryGroup.TrackedGeometry);
		if (DerivedPointer && HandleOperator(Iterator->Key))
		{
			DerivedPointer->SetTrackingState(EARTrackingState::StoppedTracking);
			Iterator.RemoveCurrent();
		}
	}
}

template< class T >
T* FLuminARImplementation::GetOrCreateTrackableFromHandle(const FGuid& TrackableHandle)
{
	static_assert(TIsDerivedFrom<T, UARTrackedGeometry>::IsDerived, "T must be derived from UARTrackedGeometry");

	if (!TrackedGeometryGroups.Contains(TrackableHandle)
		|| !(TrackedGeometryGroups[TrackableHandle].TrackedGeometry != nullptr)
		|| TrackedGeometryGroups[TrackableHandle].TrackedGeometry->GetTrackingState() == EARTrackingState::StoppedTracking)
	{
		// Add the trackable to the cache.
		UARTrackedGeometry* NewTrackableObject = nullptr;
		IARRef* NativeResource = nullptr;
		const UARSessionConfig& SessionConfig = ARSystem->GetSessionConfig();
		UClass* ARComponentClass = nullptr;


		for (ILuminARTracker* Tracker : Trackers)
		{
			if (Tracker->IsHandleTracked(TrackableHandle))
			{
				NewTrackableObject = Tracker->CreateTrackableObject();
				ARComponentClass = Tracker->GetARComponentClass(SessionConfig);
				NewTrackableObject->UniqueId = TrackableHandle;
				NativeResource = Tracker->CreateNativeResource(TrackableHandle, NewTrackableObject);
				break;
			}
		}

		if ((NewTrackableObject == nullptr) || (ARComponentClass == nullptr))
		{
			//checkf(false, TEXT("ULuminARUObjectManager failed to get a valid trackable %p. Unknow LuminAR Trackable Type."), TrackableHandle);
			return nullptr;
		}

		NewTrackableObject->InitializeNativeResource(NativeResource);
		NativeResource = nullptr;

		FLuminARTrackableResource* TrackableResource = static_cast<FLuminARTrackableResource*>(NewTrackableObject->GetNativeResource());

		FString NewAnchorDebugName = FString::Printf(TEXT("OBJ-%02d"), LastTrackedGeometry_DebugId++);

		FTrackedGeometryGroup TrackedGeometryGroup(NewTrackableObject);
		TrackedGeometryGroups.Add(TrackableHandle, TrackedGeometryGroup);
		NewTrackableObject->UniqueId = TrackableHandle;
		NewTrackableObject->SetDebugName(FName(*NewAnchorDebugName));

		// no, we always do this in ProcessPlaneQuery
		// Update the tracked geometry data using the native resource
		//TrackableResource->UpdateGeometryData();
		ensure(TrackableResource->GetTrackingState() != EARTrackingState::StoppedTracking);
		
		AARActor::RequestSpawnARActor(TrackableHandle, ARComponentClass);
	}

	//T* Result = Cast<T>(TrackableHandleMap[TrackableHandle].Get());
	T* Result = Cast<T>(TrackedGeometryGroups[TrackableHandle].TrackedGeometry);
	//checkf(Result, TEXT("ULuminARUObjectManager failed to get a valid trackable %p from the map."), TrackableHandle);
	return Result;
}

template< class T >
void FLuminARImplementation::GetAllTrackables(TArray<T*>& OutLuminARTrackableList) const
{
	OutLuminARTrackableList.Empty();

	static_assert(TIsDerivedFrom<T, UARTrackedGeometry>::IsDerived, "T must be derived from UARTrackedGeometry");

	//for (auto TrackableHandleMapPair : TrackableHandleMap)
	for (auto GeoIt = TrackedGeometryGroups.CreateConstIterator(); GeoIt; ++GeoIt)
	{
		const FTrackedGeometryGroup& TrackedGeometryGroup = GeoIt.Value();

		UARTrackedGeometry* TrackedGeometry = TrackedGeometryGroup.TrackedGeometry;
		if ((TrackedGeometry != nullptr) &&
			(TrackedGeometry->GetTrackingState() != EARTrackingState::StoppedTracking)
			)
		{
			T* Trackable = Cast<T>(TrackedGeometry);
			if (Trackable)
			{
				OutLuminARTrackableList.Add(Trackable);
			}
		}	
	}
}
