// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "WindowsMixedRealityAvailability.h"
#include "IXRTrackingSystem.h"
#include "ARSystemSupportBase.h"
#include "ARPin.h"
#include "ARActor.h"
#include <functional>

#include "IHandTracker.h"

#pragma warning(disable:4668)  
#include <DirectXMath.h>
#pragma warning(default:4668)

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
	void SetAnchorId(const FName& InAnchorId) { AnchorId = InAnchorId; }
	void SetIsInAnchorStore(bool b) { IsInAnchorStore = b; } // Note this is deprecated functionality.

	UFUNCTION(BlueprintPure, Category = "HoloLensAR|ARPin", meta = (Keywords = "hololensar wmr pin ar all", DeprecatedFunction, DeprecationMessage = "Please use ARPin references instead of UWMRARPin AnchorIds for cross platform compatibility."))
	const FString GetAnchorId() const { return AnchorId.ToString(); }

	const FName& GetAnchorIdName() const { return AnchorId;  }

	UFUNCTION(BlueprintPure, Category = "HoloLensAR|ARPin", meta = (Keywords = "hololensar wmr pin ar all", DeprecatedFunction, DeprecationMessage = "Please update to the new cross platform Pin Local Store api defined in ARBlueprintLibrary.  In that retaining information about which pins have been saved under what names is a project responsbility."))
	const bool GetIsInAnchorStore() const { return IsInAnchorStore; }

private:
	FName AnchorId;
	bool IsInAnchorStore = false; // Note this is deprecated functionality.
};


class FHoloLensARSystem :
	public FARSystemSupportBase,
	public FGCObject,
	public TSharedFromThis<FHoloLensARSystem, ESPMode::ThreadSafe>,
	public IHandTracker
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
	virtual void OnResumeARSession();
	virtual void OnStopARSession() override;
	virtual FARSessionStatus OnGetARSessionStatus() const override;
	virtual void OnSetAlignmentTransform(const FTransform& InAlignmentTransform) override;
	virtual TArray<FARTraceResult> OnLineTraceTrackedObjects(const FVector2D ScreenCoord, EARLineTraceChannels TraceChannels) override;
	virtual TArray<FARTraceResult> OnLineTraceTrackedObjects(const FVector Start, const FVector End, EARLineTraceChannels TraceChannels) override;
	virtual TArray<UARTrackedGeometry*> OnGetAllTrackedGeometries() const override;
	virtual TArray<UARPin*> OnGetAllPins() const override;
	virtual bool OnIsTrackingTypeSupported(EARSessionType SessionType) const override;
	virtual bool OnToggleARCapture(const bool bOnOff, const EARCaptureType CaptureType) override;
	virtual void OnSetEnabledXRCamera(bool bOnOff) override;
	virtual FIntPoint OnResizeXRCamera(const FIntPoint& InSize) override;
	virtual UARLightEstimate* OnGetCurrentLightEstimate() const override;
	virtual UARPin* FindARPinByComponent(const USceneComponent* Component) const override;
	virtual UARPin* OnPinComponent(USceneComponent* ComponentToPin, const FTransform& PinToWorldTransform, UARTrackedGeometry* TrackedGeometry = nullptr, const FName DebugName = NAME_None) override;
	virtual void OnRemovePin(UARPin* PinToRemove) override;
	virtual UARTexture* OnGetARTexture(EARTextureType TextureType) const override;
	virtual bool OnAddManualEnvironmentCaptureProbe(FVector Location, FVector Extent) override;
	virtual TSharedPtr<FARGetCandidateObjectAsyncTask, ESPMode::ThreadSafe> OnGetCandidateObject(FVector Location, FVector Extent) const override;
	virtual TSharedPtr<FARSaveWorldAsyncTask, ESPMode::ThreadSafe> OnSaveWorld() const override;
	virtual EARWorldMappingState OnGetWorldMappingStatus() const override;
	virtual TArray<FARVideoFormat> OnGetSupportedVideoFormats(EARSessionType SessionType) const override;
	virtual TArray<FVector> OnGetPointCloud() const override;
	virtual bool OnAddRuntimeCandidateImage(UARSessionConfig* SessionConfig, UTexture2D* CandidateTexture, FString FriendlyName, float PhysicalWidth) override;
	virtual bool OnGetCameraIntrinsics(FARCameraIntrinsics& OutCameraIntrinsics) const;

	// @todo JoeG - Figure out why we have these and if we really need them
	virtual void* GetARSessionRawPointer() override { return nullptr; }
	virtual void* GetGameThreadARFrameRawPointer() override { return nullptr; }

	/** Pin Interface */
	virtual bool IsLocalPinSaveSupported() const override;
	virtual bool ArePinsReadyToLoad() override;
	virtual void LoadARPins(TMap<FName, UARPin*>& LoadedPins) override;
	virtual bool SaveARPin(FName InName, UARPin* InPin) override;
	virtual void RemoveSavedARPin(FName InName) override;
	virtual void RemoveAllSavedARPins() override;

	//~IARSystemSupport

	// Tracking notification callback
public:
	void OnTrackingChanged(EARTrackingQuality InTrackingQuality);
private:
	//~ Tracking notification callback

	// Third camera
public:
	void SetEnabledMixedRealityCamera(bool enabled);
	void ResizeMixedRealityCamera(/*inout*/ FIntPoint& size);
	FTransform GetPVCameraToWorldTransform();
	bool GetPVCameraIntrinsics(FVector2D& focalLength, int& width, int& height, FVector2D& principalPoint, FVector& radialDistortion, FVector2D& tangentialDistortion);
	FVector GetWorldSpaceRayFromCameraPoint(FVector2D pixelCoordinate);
	bool StartCameraCapture();
	bool StopCameraCapture();

	// Use the legacy MRMesh support for rendering the hand tracker.  Otherwise, use XRVisualization.
	void SetUseLegacyHandMeshVisualization(bool bInUseLegacyHandMeshVisualization)
	{
		bUseLegacyHandMeshVisualization = bInUseLegacyHandMeshVisualization;
	}

#if SUPPORTS_WINDOWS_MIXED_REALITY_AR
	/** Starts the camera with the desired settings */
	bool SetupCameraImageSupport();
	/** Starts the interop layer mesh observer that will notify us of mesh changes */
	bool SetupMeshObserver();

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
	FCriticalSection HandMeshLock;
	TArray<FMeshUpdate> HandMeshes;
	bool bShouldStartSpatialMapping = false;
	bool bShouldStartQRDetection = false;
	bool bShouldStartPVCamera = false;
	//~ Mesh observer callback support

private:
	bool bUseLegacyHandMeshVisualization = false;
	bool bShowHandMeshes = false;

public:
	/** Starts the interop layer QR code observer that will notify us of QR codes tracked by the system */
	bool SetupQRCodeTracking();
	bool StopQRCodeTracking();

private:
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
	static void OnCameraImageReceived_Raw(void* handle, DirectX::XMFLOAT4X4 camToTracking);
	/**
	 * Callback for the camera image that runs on the game thread so we can do UObject stuff
	 */
	void OnCameraImageReceived_GameThread(void* handle, FTransform camToTracking);
#endif
	/** Callback so the interop layer can log messages that show up in the UE4 log */
	static void OnLog(const wchar_t* LogMsg);

	// WMR Anchor Implementation
public:
	UWMRARPin* WMRCreateNamedARPinAroundAnchor(FName Name, FString AnchorId);

	// Deprecated WMRAnchorStore support functions.
	virtual UWMRARPin* WMRCreateNamedARPin(FName Name, const FTransform& WorldTransform);
	TArray<UWMRARPin*> WMRLoadWMRAnchorStoreARPins();
	bool WMRSaveARPinToAnchorStore(UARPin* InPin);
	void WMRRemoveARPinFromAnchorStore(UARPin* InPin);

private:
	// These functions operate in WMR Tracking Space but UE4 units (so they deal with worldscale).	
	void UpdateWMRAnchors();
	bool WMRIsSpatialAnchorStoreLoaded() const;
	bool WMRCreateAnchor(const wchar_t* AnchorId, FVector InPosition, FQuat InRotationQuat);
	void WMRRemoveAnchor(const wchar_t* AnchorId);
	bool WMRDoesAnchorExist(const wchar_t* AnchorId) const;
	bool WMRGetAnchorTransform(const wchar_t* AnchorId, FTransform& Transform) const;
	bool WMRSaveAnchor(const wchar_t* saveId, const wchar_t* anchorId);
	void WMRRemoveSavedAnchor(const wchar_t* anchorId);
	bool WMRLoadAnchors(std::function<void(const wchar_t* saveId, const wchar_t* anchorId)> anchorIdWritingFunctionPointer);
	void WMRClearSavedAnchors();

	void OnSpawnARActor(AARActor* NewARActor, UARComponent* NewARComponent, FGuid NativeID);

	/** Removes all tracked geometries, marking them as not tracked and sending the delegate event */
	void ClearTrackedGeometries();

	int32 RuntimeWMRAnchorCount = 0;

	/** The HMD this AR system is associated with */
	TSharedPtr<class FXRTrackingSystemBase, ESPMode::ThreadSafe> TrackingSystem;

	/** WMRInterop is a pointer to our c++ wrapper around the WMR winrt APIs.  That object is owned by the WindowsMixedRealityHMD.*/
	WindowsMixedReality::MixedRealityInterop* WMRInterop;

	FARSessionStatus SessionStatus;
	uint32 HandlerId = 0;

	/** The current tracking quality for the system */
	EARTrackingQuality TrackingQuality = EARTrackingQuality::NotTracking;

	//
	// PROPERTIES REPORTED TO FGCObject
	// ...
	UHoloLensCameraImageTexture* CameraImage;
	UARSessionConfig* SessionConfig;
	TMap< FName, UARPin* > AnchorIdToPinMap;
	TArray<UARPin*> Pins;
	//TMap<FGuid, UARTrackedGeometry*> TrackedGeometries;
	TMap<FGuid, FTrackedGeometryGroup> TrackedGeometryGroups;
	// ...
	// PROPERTIES REPORTED TO FGCObject
	//

	FCriticalSection PVCamToWorldLock;
	FTransform PVCameraToWorldMatrix;

	//for networked callbacks
	FDelegateHandle SpawnARActorDelegateHandle;

	// Inherited via IHandTracker
	virtual FName GetHandTrackerDeviceTypeName() const override;
	virtual bool IsHandTrackingStateValid() const override;
	// We are using IHandTracker here for hand meshes, keypoint states are found in WindowsMixedRealityHandTracking
	virtual bool GetKeypointState(EControllerHand Hand, EHandKeypoint Keypoint, FTransform& OutTransform, float& OutRadius) const override
	{
		return false;
	}
	virtual bool GetAllKeypointStates(EControllerHand Hand, TArray<FVector>& OutPositions, TArray<FQuat>& OutRotations, TArray<float>& OutRadii) const override
	{
		return false;
	}
	virtual bool HasHandMeshData() const override;
	virtual bool GetHandMeshData(EControllerHand Hand, TArray<FVector>& OutVertices, TArray<FVector>& OutNormals, TArray<int32>& OutIndices, FTransform& OutHandMeshTransform) const override;
};
