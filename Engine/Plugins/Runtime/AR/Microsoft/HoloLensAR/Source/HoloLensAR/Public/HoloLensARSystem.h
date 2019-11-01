// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "WindowsMixedRealityAvailability.h"
#include "IXRTrackingSystem.h"
#include "ARSystem.h"
#include "ARPin.h"
#include <functional>

#include "HoloLensARSystem.generated.h"

class UARSessionConfig;
class UHoloLensCameraImageTexture;

DECLARE_STATS_GROUP(TEXT("HoloLens"), STATGROUP_HOLOLENS, STATCAT_Advanced);

namespace WindowsMixedReality
{
	class MixedRealityInterop;
}

// Forward declaration of our mesh update struct from the interop layer
struct MeshUpdate;
struct FMeshUpdate;
// Forward declaration of our copy of the mesh update data
struct FMeshUpdateSet;
// Forward declaration of the QR code data from the interop layer
struct QRCodeData;
struct FQRCodeData;

UCLASS()
class HOLOLENSAR_API UWMRARPin : public UARPin
{
	GENERATED_BODY()

public:
	void SetAnchorId(const FString& InAnchorId) { AnchorId = InAnchorId; }
	void SetIsInAnchorStore(bool b) { IsInAnchorStore = b; }

	UFUNCTION(BlueprintPure, Category = "HoloLensAR|ARPin", meta = (Keywords = "hololensar wmr pin ar all"))
	const FString& GetAnchorId() const { return AnchorId; }

	UFUNCTION(BlueprintPure, Category = "HoloLensAR|ARPin", meta = (Keywords = "hololensar wmr pin ar all"))
	const bool GetIsInAnchorStore() const { return IsInAnchorStore; }

private:
	FString AnchorId;
	bool IsInAnchorStore = false;
};


class FHoloLensARSystem :
	public IARSystemSupport,
	public FGCObject,
	public TSharedFromThis<FHoloLensARSystem, ESPMode::ThreadSafe>
{
public:
	FHoloLensARSystem();
	virtual ~FHoloLensARSystem();

	/** Used to associate the HMD with the AR system bits */
	void SetTrackingSystem(TSharedPtr<FXRTrackingSystemBase, ESPMode::ThreadSafe> InTrackingSystem);

	/** Give the AR System access to the Windows Mixed Reality Interop which wraps UWP and or winrt mixed reality APIs.*/
	void SetInterop(WindowsMixedReality::MixedRealityInterop* InWMRInterop);

	/** So the module can shut down the ar services cleanly */
	void Shutdown();

private:
	void OnWorldTickStart(UWorld* World, ELevelTick TickType, float DeltaTime);

	//~ FGCObject
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	//~ FGCObject

	//~IARSystemSupport
	virtual bool IsARAvailable() const override;
	virtual void OnARSystemInitialized() override;
	virtual EARTrackingQuality OnGetTrackingQuality() const override;
	virtual EARTrackingQualityReason OnGetTrackingQualityReason() const override;
	virtual void OnStartARSession(UARSessionConfig* InSessionConfig) override;
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
	virtual UARPin* OnPinComponent(USceneComponent* ComponentToPin, const FTransform& PinToWorldTransform, UARTrackedGeometry* TrackedGeometry = nullptr, const FName DebugName = NAME_None) override;
	virtual void OnRemovePin(UARPin* PinToRemove) override;
	virtual UARTextureCameraImage* OnGetCameraImage() override;
	virtual UARTextureCameraDepth* OnGetCameraDepth() override;
	virtual bool OnAddManualEnvironmentCaptureProbe(FVector Location, FVector Extent) override;
	virtual TSharedPtr<FARGetCandidateObjectAsyncTask, ESPMode::ThreadSafe> OnGetCandidateObject(FVector Location, FVector Extent) const override;
	virtual TSharedPtr<FARSaveWorldAsyncTask, ESPMode::ThreadSafe> OnSaveWorld() const override;
	virtual EARWorldMappingState OnGetWorldMappingStatus() const override;
	virtual TArray<FARVideoFormat> OnGetSupportedVideoFormats(EARSessionType SessionType) const override;
	virtual TArray<FVector> OnGetPointCloud() const override;
	virtual bool OnAddRuntimeCandidateImage(UARSessionConfig* SessionConfig, UTexture2D* CandidateTexture, FString FriendlyName, float PhysicalWidth) override;

	// @todo JoeG - Figure out why we have these and if we really need them
	virtual void* GetARSessionRawPointer() override { return nullptr; }
	virtual void* GetGameThreadARFrameRawPointer() override { return nullptr; }
	//~IARSystemSupport

	// Tracking notification callback
public:
	void OnTrackingChanged(EARTrackingQuality InTrackingQuality);
private:
	//~ Tracking notification callback

#if SUPPORTS_WINDOWS_MIXED_REALITY_AR
	/** Starts the camera with the desired settings */
	void SetupCameraImageSupport();
	/** Starts the interop layer mesh observer that will notify us of mesh changes */
	void SetupMeshObserver();

	// Mesh observer callback support
	static void StartMeshUpdates_Raw();
	static void AllocateMeshBuffers_Raw(MeshUpdate* InMeshUpdate);
	static void RemovedMesh_Raw(MeshUpdate* InMeshUpdate);
	static void EndMeshUpdates_Raw();
	void StartMeshUpdates();
	void AllocateMeshBuffers(MeshUpdate* InMeshUpdate);
	void RemovedMesh_GameThread(FMeshUpdate* RemovedMesh);
	void ProcessMeshUpdates_GameThread();
	void EndMeshUpdates();
	void AddOrUpdateMesh(FMeshUpdate* CurrentMesh);
	void ReconcileKnownMeshes(const TArray<FGuid>& KnownMeshes);
	/** Used to lock access to the update list that will be queued for the game thread */
	FCriticalSection CurrentUpdateSync;
	/** This pointer is locked until the list construction is complete, where this gets queued for game thread processing */
	FMeshUpdateSet* CurrentUpdate;
	/** Controls the access to the queue of mesh updates for the game thread to process */
	FCriticalSection MeshUpdateListSync;
	/** List of mesh updates for the game thread to process */
	TArray<FMeshUpdateSet*> MeshUpdateList;
	/** Holds the set of last known meshes so we can detect removed meshes. Only touched on the game thread */
	TSet<FGuid> LastKnownMeshes;
	//~ Mesh observer callback support

	/** Starts the interop layer QR code observer that will notify us of QR codes tracked by the system */
	void SetupQRCodeTracking();

	// QR Code observer callback support
	static void QRCodeAdded_Raw(QRCodeData* InCode);
	static void QRCodeUpdated_Raw(QRCodeData* InCode);
	static void QRCodeRemoved_Raw(QRCodeData* InCode);
	void QRCodeAdded_GameThread(FQRCodeData* InCode);
	void QRCodeUpdated_GameThread(FQRCodeData* InCode);
	void QRCodeRemoved_GameThread(FQRCodeData* InCode);
	//~ QR Code observer callback support

	/**
	 * Callback from the WinRT layer notifying us of a new camera frame.
	 * Note: runs on an arbitrary thread!
	 */
	static void OnCameraImageReceived_Raw(struct ID3D11Texture2D* CameraFrame);
	/**
	 * Callback for the camera image that runs on the game thread so we can do UObject stuff
	 */
	void OnCameraImageReceived_GameThread(struct ID3D11Texture2D* CameraFrame);
#endif
	/** Callback so the interop layer can log messages that show up in the UE4 log */
	static void OnLog(const wchar_t* LogMsg);

	// WMR Anchor Implementation
public:
	UWMRARPin* CreateNamedARPin(FName Name, const FTransform& PinToWorldTransform);
	bool PinComponentToARPin(USceneComponent* ComponentToPin, UWMRARPin* Pin);

	bool IsWMRAnchorStoreReady() const;
	TArray<UWMRARPin*> LoadWMRAnchorStoreARPins();
	bool SaveARPinToAnchorStore(UARPin* InPin);
	void RemoveARPinFromAnchorStore(UARPin* InPin);
	void RemoveAllARPinsFromAnchorStore();
private:
	UWMRARPin* FindPinByComponent(const USceneComponent* Component);
	
	// These functions operate in WMR Tracking Space but UE4 units (so they deal with worldscale).	
	void UpdateWMRAnchors();
	bool WMRIsSpatialAnchorStoreLoaded() const;
	bool WMRCreateAnchor(const wchar_t* AnchorId, FVector InPosition, FQuat InRotationQuat);
	void WMRRemoveAnchor(const wchar_t* AnchorId);
	bool WMRDoesAnchorExist(const wchar_t* AnchorId) const;
	bool WMRGetAnchorTransform(const wchar_t* AnchorId, FTransform& Transform) const;
	bool WMRSaveAnchor(const wchar_t* anchorId);
	void WMRRemoveSavedAnchor(const wchar_t* anchorId);
	bool WMRLoadAnchors(std::function<void(const wchar_t* text)> anchorIdWritingFunctionPointer);
	void WMRClearSavedAnchors();
	int32 RuntimeWMRAnchorCount = 0;

	/** The HMD this AR system is associated with */
	TSharedPtr<class FXRTrackingSystemBase, ESPMode::ThreadSafe> TrackingSystem;

	/** WMRInterop is a pointer to our c++ wrapper around the WMR winrt APIs.  That object is owned by the WindowsMixedRealityHMD.*/
	WindowsMixedReality::MixedRealityInterop* WMRInterop;

	FARSessionStatus SessionStatus;
	uint32 HandlerId = 0;

	/** The current tracking quality for the system */
	EARTrackingQuality TrackingQuality;

	//
	// PROPERTIES REPORTED TO FGCObject
	// ...
	UHoloLensCameraImageTexture* CameraImage;
	UARSessionConfig* SessionConfig;
	TMap< FString, UWMRARPin* > AnchorIdToPinMap;
	TArray<UWMRARPin*> Pins;
	TMap<FGuid, UARTrackedGeometry*> TrackedGeometries;
	// ...
	// PROPERTIES REPORTED TO FGCObject
	//
};
