// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "XRTrackingSystemBase.h"
#include "ARSystem.h"
#include "AppleARKitHitTestResult.h"
#include "AppleARKitTextures.h"
#include "Kismet/BlueprintPlatformLibrary.h"
#include "AppleARKitFaceSupport.h"
#include "AppleARKitPoseTrackingLiveLink.h"

// ARKit
#if SUPPORTS_ARKIT_1_0
	#import <ARKit/ARKit.h>
	#include "AppleARKitSessionDelegate.h"
#endif


DECLARE_STATS_GROUP(TEXT("ARKit"), STATGROUP_ARKIT, STATCAT_Advanced);

//
//  FAppleARKitSystem
//

struct FAppleARKitFrame;
struct FAppleARKitAnchorData;

class FAppleARKitSystem : public IARSystemSupport, public FXRTrackingSystemBase, public FGCObject, public TSharedFromThis<FAppleARKitSystem, ESPMode::ThreadSafe>
{
	friend class FAppleARKitXRCamera;
	
	
public:
	FAppleARKitSystem();
	~FAppleARKitSystem();
	
	//~ IXRTrackingSystem
	FName GetSystemName() const override;
	bool GetCurrentPose(int32 DeviceId, FQuat& OutOrientation, FVector& OutPosition) override;
	FString GetVersionString() const override;
	bool EnumerateTrackedDevices(TArray<int32>& OutDevices, EXRTrackedDeviceType Type) override;
	void ResetOrientationAndPosition(float Yaw) override;
	bool IsHeadTrackingAllowed() const override;
	TSharedPtr<class IXRCamera, ESPMode::ThreadSafe> GetXRCamera(int32 DeviceId) override;
	float GetWorldToMetersScale() const override;
	void OnBeginRendering_GameThread() override;
	bool OnStartGameFrame(FWorldContext& WorldContext) override;
	//~ IXRTrackingSystem

	void* GetARSessionRawPointer() override;
	void* GetGameThreadARFrameRawPointer() override;
	
	// @todo arkit : this is for the blueprint library only; try to get rid of this method
	bool GetCurrentFrame(FAppleARKitFrame& OutCurrentFrame) const;

	/** So the module can shut down the ar services cleanly */
	void Shutdown();

private:
	//~ FGCObject
	virtual void AddReferencedObjects( FReferenceCollector& Collector ) override;
	//~ FGCObject
protected:
	//~IARSystemSupport
	/** Returns true/false based on whether AR features are available */
	virtual bool IsARAvailable() const override;
	virtual void OnARSystemInitialized() override;
	virtual EARTrackingQuality OnGetTrackingQuality() const override;
	virtual EARTrackingQualityReason OnGetTrackingQualityReason() const override;
	virtual void OnStartARSession(UARSessionConfig* SessionConfig) override;
	virtual void OnPauseARSession() override;
	virtual void OnStopARSession() override;
	virtual FARSessionStatus OnGetARSessionStatus() const override;
	virtual void OnSetAlignmentTransform(const FTransform& InAlignmentTransform) override;
	virtual TArray<FARTraceResult> OnLineTraceTrackedObjects( const FVector2D ScreenCoord, EARLineTraceChannels TraceChannels ) override;
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
	virtual bool OnAddRuntimeCandidateImage(UARSessionConfig* SessionConfig, UTexture2D* CandidateTexture, FString FriendlyName, float PhysicalWidth) override { return true; };
	
	virtual bool OnIsSessionTrackingFeatureSupported(EARSessionType SessionType, EARSessionTrackingFeature SessionTrackingFeature) const override;
	virtual TArray<FARPose2D> OnGetTracked2DPose() const override;
	virtual UARTextureCameraImage* OnGetPersonSegmentationImage() const override;
	virtual UARTextureCameraImage* OnGetPersonSegmentationDepthImage() const override;
	//~IARSystemSupport

private:
	bool Run(UARSessionConfig* SessionConfig);
	bool IsRunning() const;
	bool Pause();
	void OrientationChanged(const int32 NewOrientation);
	void UpdatePoses();
	void UpdateFrame();
	void CalcTrackingToWorldRotation();
#if SUPPORTS_ARKIT_1_0
	/** Asynchronously writes a JPEG to disk */
	void WriteCameraImageToDisk(CVPixelBufferRef PixelBuffer);
#endif
	class FAppleARKitXRCamera* GetARKitXRCamera();
	
public:
	// Session delegate callbacks
	void SessionDidUpdateFrame_DelegateThread( TSharedPtr< FAppleARKitFrame, ESPMode::ThreadSafe > Frame );
	void SessionDidFailWithError_DelegateThread( const FString& Error );
#if SUPPORTS_ARKIT_1_0
	void SessionDidAddAnchors_DelegateThread( NSArray<ARAnchor*>* anchors );
	void SessionDidUpdateAnchors_DelegateThread( NSArray<ARAnchor*>* anchors );
	void SessionDidRemoveAnchors_DelegateThread( NSArray<ARAnchor*>* anchors );
private:
	void SessionDidAddAnchors_Internal( TSharedRef<FAppleARKitAnchorData> AnchorData );
	void SessionDidUpdateAnchors_Internal( TSharedRef<FAppleARKitAnchorData> AnchorData );
	void SessionDidRemoveAnchors_Internal( FGuid AnchorGuid );
#endif
	void SessionDidUpdateFrame_Internal( TSharedRef< FAppleARKitFrame, ESPMode::ThreadSafe > Frame );
	/** Removes all tracked geometries, marking them as not tracked and sending the delegate event */
	void ClearTrackedGeometries();
	
public:
	/**
	 * Searches the last processed frame for anchors corresponding to a point in the captured image.
	 *
	 * A 2D point in the captured image's coordinate space can refer to any point along a line segment
	 * in the 3D coordinate space. Hit-testing is the process of finding anchors of a frame located along this line segment.
	 *
	 * NOTE: The hit test locations are reported in ARKit space. For hit test results
	 * in game world coordinates, you're after UAppleARKitCameraComponent::HitTestAtScreenPosition
	 *
	 * @param ScreenPosition The viewport pixel coordinate of the trace origin.
	 */
	UFUNCTION( BlueprintCallable, Category="AppleARKit|Session" )
	bool HitTestAtScreenPosition( const FVector2D ScreenPosition, EAppleARKitHitTestResultType Types, TArray< FAppleARKitHitTestResult >& OutResults );
	
	
private:
	
	bool bIsRunning = false;
	
	void SetDeviceOrientationAndDerivedTracking(EDeviceScreenOrientation InOrientation);

	/** Creates or clears the face ar support object if face ar has been requested */
	void CheckForFaceARSupport(UARSessionConfig* InSessionConfig);

	/** Creates or clears the pose tracking ar support object if face ar has been requested */
	void CheckForPoseTrackingARLiveLink(UARSessionConfig* InSessionConfig);
	
	/** Updates the ARKit perf counters */
	void UpdateARKitPerfStats();

	/** Inits the textures and sets the texture on the overlay */
	void SetupCameraTextures();

	/** The orientation of the device; see EDeviceScreenOrientation */
	EDeviceScreenOrientation DeviceOrientation;
	
	/** A rotation from ARKit TrackingSpace to Unreal Space. It is re-derived based on other parameters; users should not set it directly. */
	FRotator DerivedTrackingToUnrealRotation;

#if SUPPORTS_ARKIT_1_0

	// ARKit Session
	ARSession* Session = nullptr;
	
	// ARKit Session Delegate
	FAppleARKitSessionDelegate* Delegate = nullptr;

	/** Cache of images that we've converted previously to prevent repeated conversion */
	TMap< FString, CGImage* > ConvertedCandidateImages;

#endif

	//
	// PROPERTIES REPORTED TO FGCObject
	// ...
	TMap< FGuid, UARTrackedGeometry* > TrackedGeometries;
	TArray<UARPin*> Pins;
	UARLightEstimate* LightEstimate;
	UAppleARKitTextureCameraImage* CameraImage;
	UAppleARKitTextureCameraDepth* CameraDepth;
	TMap< FString, UARCandidateImage* > CandidateImages;
	TMap< FString, UARCandidateObject* > CandidateObjects;
	UAppleARKitTextureCameraImage* PersonSegmentationImage = nullptr;
	UAppleARKitTextureCameraImage* PersonSegmentationDepthImage = nullptr;
	// ...
	// PROPERTIES REPORTED TO FGCObject
	//
	
	// An int counter that provides a human-readable debug number for Tracked Geometries.
	uint32 LastTrackedGeometry_DebugId;

	//'threadsafe' sharedptrs merely guaranteee atomicity when adding/removing refs.  You can still have a race
	//with destruction and copying sharedptrs.
	FCriticalSection FrameLock;

	// Last frame grabbed & processed by via the ARKit session delegate
	TSharedPtr< FAppleARKitFrame, ESPMode::ThreadSafe > GameThreadFrame;
	TSharedPtr< FAppleARKitFrame, ESPMode::ThreadSafe > RenderThreadFrame;
	TSharedPtr< FAppleARKitFrame, ESPMode::ThreadSafe > LastReceivedFrame;

	// The object that is handling face support if present
	IAppleARKitFaceSupport* FaceARSupport;

	// The object that is handling pose tracking livelink if present
	IAppleARKitPoseTrackingLiveLink* PoseTrackingARLiveLink;

	/** The time code provider to use when tagging time stamps */
	UTimecodeProvider* TimecodeProvider = nullptr;
};


namespace AppleARKitSupport
{
	APPLEARKIT_API TSharedPtr<class FAppleARKitSystem, ESPMode::ThreadSafe> CreateAppleARKitSystem();
}

