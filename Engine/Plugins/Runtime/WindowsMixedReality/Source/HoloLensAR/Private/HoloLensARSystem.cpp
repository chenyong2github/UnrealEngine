// Copyright Epic Games, Inc. All Rights Reserved.

#include "HoloLensARSystem.h"
#include "HoloLensModule.h"
#include "HoloLensCameraImageTexture.h"

#include "ARLifeCycleComponent.h"
#include "ARPin.h"
#include "ARTrackable.h"
#include "ARTraceResult.h"
#include "MixedRealityInterop.h"
#include "WindowsMixedRealityInteropUtility.h"

#include "HoloLensARFunctionLibrary.h"
#include "HeadMountedDisplayFunctionLibrary.h"

#include "Misc/ConfigCacheIni.h"
#include "Async/Async.h"

#include "MRMeshComponent.h"
#include "AROriginActor.h"
#if WITH_EDITOR
#include "Editor.h"
#include "Editor/EditorEngine.h"
#include "Slate/SceneViewport.h"
#endif

DECLARE_CYCLE_STAT(TEXT("OnCameraImageReceived"), STAT_FHoloLensARSystem_OnCameraImageReceived, STATGROUP_HOLOLENS);
DECLARE_CYCLE_STAT(TEXT("Process Mesh Updates"), STAT_FHoloLensARSystem_ProcessMeshUpdates, STATGROUP_HOLOLENS);
DECLARE_CYCLE_STAT(TEXT("QR Code Added"), STAT_FHoloLensARSystem_QRCodeAdded_GameThread, STATGROUP_HOLOLENS);
DECLARE_CYCLE_STAT(TEXT("QR Code Updated"), STAT_FHoloLensARSystem_QRCodeUpdated_GameThread, STATGROUP_HOLOLENS);
DECLARE_CYCLE_STAT(TEXT("QR Code Removed"), STAT_FHoloLensARSystem_QRCodeRemoved_GameThread, STATGROUP_HOLOLENS);

static inline FGuid GUIDToFGuid(GUID InGuid)
{
	check(sizeof(FGuid) == sizeof(GUID));

	FGuid OutGuid;
	FMemory::Memcpy(&OutGuid, &InGuid, sizeof(FGuid));
	return OutGuid;
}

/** The UE4 version of the mesh update from the interop */
struct FMeshUpdate
{
	FGuid Id;
	EARObjectClassification Type;

	/** This gets filled in during the mesh allocation */
	FVector Location;
	FQuat Rotation;
	FVector Scale;

	// These will use MoveTemp to avoid another copy
	// The interop fills these in directly
	TArray<FVector> Vertices;
	TArray<MRMESH_INDEX_TYPE> Indices;
	TArray<FVector> Normals;

	bool bIsRightHandMesh;
};

/** A set of updates to be processed at once */
struct FMeshUpdateSet
{
	FMeshUpdateSet()
	{
	}
	~FMeshUpdateSet()
	{
		GuidToMeshUpdateList.Empty();
	}

	TMap<FGuid, FMeshUpdate*> GuidToMeshUpdateList;
};

/** The UE4 version of the QR code update from the interop */
struct FQRCodeData
{
	FGuid Id;

	FTransform Transform;
	FVector2D Size;
	FString QRCode;
	int32 Version;

	FQRCodeData(QRCodeData* InData)
		: Id(GUIDToFGuid(InData->Id))
		, Size(InData->SizeInMeters * 100.f, InData->SizeInMeters * 100.f)
		, QRCode(InData->Data)
		, Version(InData->Version)
	{
		FVector Translation(InData->Translation[0], InData->Translation[1], InData->Translation[2]);
		FQuat Orientation(InData->Rotation[0], InData->Rotation[1], InData->Rotation[2], InData->Rotation[3]);
		Orientation.Normalize();
		Transform = FTransform(Orientation, Translation);
	}
};

FHoloLensARSystem::FHoloLensARSystem()
	: CameraImage(nullptr)
	, SessionConfig(nullptr)
{
	SpawnARActorDelegateHandle = UARLifeCycleComponent::OnSpawnARActorDelegate.AddRaw(this, &FHoloLensARSystem::OnSpawnARActor);
	HandMeshes.Init(FMeshUpdate(), 2);

	IModularFeatures::Get().RegisterModularFeature(IHandTracker::GetModularFeatureName(), static_cast<IHandTracker*>(this));
}

FHoloLensARSystem::~FHoloLensARSystem()
{
	UARLifeCycleComponent::OnSpawnARActorDelegate.Remove(SpawnARActorDelegateHandle);
	IModularFeatures::Get().UnregisterModularFeature(IHandTracker::GetModularFeatureName(), static_cast<IHandTracker*>(this));
}

void FHoloLensARSystem::SetTrackingSystem(TSharedPtr<FXRTrackingSystemBase, ESPMode::ThreadSafe> InTrackingSystem)
{
	TrackingSystem = InTrackingSystem;
}

void FHoloLensARSystem::SetInterop(WindowsMixedReality::MixedRealityInterop* InWMRInterop)
{
	WMRInterop = InWMRInterop;

	if (!WMRInterop)
	{
		return;
	}
	
	HandlerId = WMRInterop->SubscribeConnectionEvent([this](WindowsMixedReality::MixedRealityInterop::ConnectionEvent evt) 
	{
		if (evt == WindowsMixedReality::MixedRealityInterop::ConnectionEvent::DisconnectedFromPeer)
		{
			OnPauseARSession();

#if WITH_EDITOR
			//Removed in case it is intentional to keep executing on accidental disconnect
			//if (GEditor && GEditor->IsVRPreviewActive())
			//{
			//	GEditor->RequestEndPlayMap();
			//}
#endif

			UE_LOG(LogHoloLensAR, Warning, TEXT("HoloLens AR session disconnected from peer"));
		}
		else if (evt == WindowsMixedReality::MixedRealityInterop::ConnectionEvent::Connected)
		{
			OnResumeARSession();
		}
	});
}

void FHoloLensARSystem::Shutdown()
{
	OnStopARSession();
	if (WMRInterop)
	{
		WMRInterop->UnsubscribeConnectionEvent(HandlerId);
	}
	HandlerId = 0;
	FWorldDelegates::OnWorldTickStart.RemoveAll(this);
	WMRInterop = nullptr;
	TrackingSystem.Reset();
	CameraImage = nullptr;
	SessionConfig = nullptr;
}

void FHoloLensARSystem::OnWorldTickStart(UWorld* World, ELevelTick TickType, float DeltaTime)
{
	UpdateWMRAnchors();
}

void FHoloLensARSystem::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(CameraImage);
	Collector.AddReferencedObject(SessionConfig);
	Collector.AddReferencedObjects(AnchorIdToPinMap);
	Collector.AddReferencedObjects(Pins);
	// Iterate all geometries
	for (auto GeoIt = TrackedGeometryGroups.CreateIterator(); GeoIt; ++GeoIt)
	{
		FTrackedGeometryGroup& TrackedGeometryGroup = GeoIt.Value();

		Collector.AddReferencedObject(TrackedGeometryGroup.TrackedGeometry);
		Collector.AddReferencedObject(TrackedGeometryGroup.ARActor);
		Collector.AddReferencedObject(TrackedGeometryGroup.ARComponent);
	}
}

void FHoloLensARSystem::OnARSystemInitialized()
{
	UE_LOG(LogHoloLensAR, Log, TEXT("HoloLens AR system has been initialized"));
}

bool FHoloLensARSystem::IsARAvailable() const
{
	return true;
}

EARTrackingQuality FHoloLensARSystem::OnGetTrackingQuality() const
{
	return TrackingQuality;
}

EARTrackingQualityReason FHoloLensARSystem::OnGetTrackingQualityReason() const
{
#if SUPPORTS_WINDOWS_MIXED_REALITY_AR
	if (WMRInterop == nullptr
		|| !WMRInterop->IsInitialized())
	{
		return EARTrackingQualityReason::Initializing;
	}
#endif

	switch (TrackingQuality)
	{
	case EARTrackingQuality::NotTracking:
	case EARTrackingQuality::OrientationOnly:
		return EARTrackingQualityReason::Relocalizing;

	case EARTrackingQuality::OrientationAndPosition:
		return EARTrackingQualityReason::None;
	}

	return EARTrackingQualityReason::None;
}
void OnTrackingChanged_Raw(WindowsMixedReality::HMDSpatialLocatability InTrackingState)
{
	UE_LOG(LogHoloLensAR, Log, TEXT("OnTrackingChanged(%d)"), (int32)InTrackingState);
	FHoloLensARSystem* HoloLensARThis = FHoloLensModuleAR::GetHoloLensARSystem().Get();
	if (!HoloLensARThis)
	{
		return;
	}
	EARTrackingQuality TrackingQuality = EARTrackingQuality::NotTracking;
	switch (InTrackingState)
	{
		case WindowsMixedReality::HMDSpatialLocatability::PositionalTrackingActive:
		{
			TrackingQuality = EARTrackingQuality::OrientationAndPosition;
			break;
		}

		case WindowsMixedReality::HMDSpatialLocatability::OrientationOnly:
		case WindowsMixedReality::HMDSpatialLocatability::PositionalTrackingActivating:
		{
			TrackingQuality = EARTrackingQuality::OrientationOnly;
			break;
		}

		case WindowsMixedReality::HMDSpatialLocatability::PositionalTrackingInhibited:
		case WindowsMixedReality::HMDSpatialLocatability::Unavailable:
		default:
		{
			TrackingQuality = EARTrackingQuality::NotTracking;
			break;
		}
	}
	HoloLensARThis->OnTrackingChanged(TrackingQuality);
}

void FHoloLensARSystem::OnStartARSession(UARSessionConfig* InSessionConfig)
{
	if (InSessionConfig == nullptr)
	{
		UE_LOG(LogHoloLensAR, Error, TEXT("ARSessionConfig object was null"));
		return;
	}
	SessionConfig = InSessionConfig;

#if SUPPORTS_WINDOWS_MIXED_REALITY_AR

	if (WMRInterop)
	{
		if (!WMRInterop->IsRemoting())
		{
#if PLATFORM_HOLOLENS
			WMRInterop->ConnectToLocalHoloLens();
#else
			WMRInterop->ConnectToLocalWMRHeadset();
#endif
		}

		WMRInterop->SetTrackingChangedCallback(&OnTrackingChanged_Raw);
		// Simulate a tracking state change so we update our value
		OnTrackingChanged_Raw(WMRInterop->GetTrackingState());

		WMRInterop->SetLogCallback(&OnLog);
	}

	// If spatial mapping was requested before the session started, start it now.
#if WITH_EDITOR
	if (bShouldStartSpatialMapping && SessionConfig->bGenerateMeshDataFromTrackedGeometry)
#else
	if (bShouldStartSpatialMapping && !WMRInterop->IsRemoting() && SessionConfig->bGenerateMeshDataFromTrackedGeometry)
#endif
	{
		// Start spatial mesh mapping
		SetupMeshObserver();
	}

	// If QR was requested before the session started, start it now.
	if (bShouldStartQRDetection)
	{
		UHoloLensARFunctionLibrary::StartQRCodeCapture();
	}

	// If the camera was requested before the session started, start it now.
	if (bShouldStartPVCamera)
	{
		UHoloLensARFunctionLibrary::StartCameraCapture();
	}

	SessionStatus.Status = EARSessionStatus::Running;

#if WITH_EDITOR
	UEditorEngine* EditorEngine = Cast<UEditorEngine>(GEngine);
	if (EditorEngine)
	{
		FSceneViewport* PIEViewport = (FSceneViewport*)EditorEngine->GetPIEViewport();
		if (PIEViewport != nullptr && !PIEViewport->IsStereoRenderingAllowed())
		{
			// Running the AR session on a non-stereo window will break spatial anchors.
			SessionStatus.Status = EARSessionStatus::NotSupported;
		}
	}
#endif

	FWorldDelegates::OnWorldTickStart.AddRaw(this, &FHoloLensARSystem::OnWorldTickStart);

	UE_LOG(LogHoloLensAR, Log, TEXT("HoloLens AR session started"));
#else
	SessionStatus.Status = EARSessionStatus::NotSupported;
	UE_LOG(LogHoloLensAR, Log, TEXT("HoloLens AR requires a higher sdk to run"));
#endif
}

#if  SUPPORTS_WINDOWS_MIXED_REALITY_AR
bool FHoloLensARSystem::SetupCameraImageSupport()
{
	// Remoting does not support CameraCapture currently.
	if (WMRInterop->IsRemoting())
	{
		UE_LOG(LogHoloLensAR, Warning, TEXT("HoloLens remoting does not support the device forward facing camera.  No images will be collected."));
		return false;
	}

	// Start the camera capture device
	CameraImageCapture& CameraCapture = CameraImageCapture::Get();
#if UE_BUILD_DEBUG
	CameraCapture.SetOnLog(&OnLog);
#endif
	if (SessionConfig)
	{
		const FARVideoFormat Format = SessionConfig->GetDesiredVideoFormat();
		return CameraCapture.StartCameraCapture(&OnCameraImageReceived_Raw, Format.Width, Format.Height, Format.FPS);
	}
	else
	{
		UE_LOG(LogHoloLensAR, Warning, TEXT("Session Config must be specified before using SetupCameraImageSupport."));
	}

	return false;
}
#endif

void FHoloLensARSystem::OnPauseARSession()
{
	if (bShouldStartSpatialMapping && SessionConfig != nullptr && SessionConfig->bGenerateMeshDataFromTrackedGeometry)
	{
		// Stop spatial mesh mapping. Existing meshes will remain
		WMRInterop->StopSpatialMapping();
	}
}

void FHoloLensARSystem::OnResumeARSession()
{
	if (bShouldStartSpatialMapping && !WITH_EDITOR && SessionConfig != nullptr && SessionConfig->bGenerateMeshDataFromTrackedGeometry)
	{
		SetupMeshObserver();
	}
}

void FHoloLensARSystem::OnStopARSession()
{
#if SUPPORTS_WINDOWS_MIXED_REALITY_AR
	FWorldDelegates::OnWorldTickStart.RemoveAll(this);

	ClearTrackedGeometries();

#if !PLATFORM_HOLOLENS
	// wmr does not support CameraCapture currently.
	if (!WMRInterop->IsRemoting())
	{
		CameraImageCapture::Release();
	}
#endif

	check(WMRInterop != nullptr);

	if (SessionConfig && SessionConfig->bGenerateMeshDataFromTrackedGeometry)
	{
		WMRInterop->StopSpatialMapping();
	}

	WMRInterop->SetTrackingChangedCallback(nullptr);
	TrackingQuality = EARTrackingQuality::NotTracking;
	WMRInterop->StopQRCodeTracking();
#endif

	SessionConfig = nullptr;

	SessionStatus.Status = EARSessionStatus::NotStarted;

	UE_LOG(LogHoloLensAR, Log, TEXT("HoloLens AR session stopped (sort of, not really)"));
}

FARSessionStatus FHoloLensARSystem::OnGetARSessionStatus() const
{
	return SessionStatus;
}

void FHoloLensARSystem::OnSetAlignmentTransform(const FTransform& InAlignmentTransform)
{
	const FTransform& NewAlignmentTransform = InAlignmentTransform;

	TArray<UARTrackedGeometry*> AllTrackedGeometries = OnGetAllTrackedGeometries();
	for (UARTrackedGeometry* TrackedGeometry : AllTrackedGeometries)
	{
		TrackedGeometry->UpdateAlignmentTransform(NewAlignmentTransform);
	}

	TArray<UARPin*> AllARPins = OnGetAllPins();
	for (UARPin* SomePin : AllARPins)
	{
		SomePin->UpdateAlignmentTransform(NewAlignmentTransform);
	}
}

TArray<FARTraceResult> FHoloLensARSystem::OnLineTraceTrackedObjects(const FVector2D ScreenCoord, EARLineTraceChannels TraceChannels)
{
	return TArray<FARTraceResult>();
}

TArray<FARTraceResult> FHoloLensARSystem::OnLineTraceTrackedObjects(const FVector Start, const FVector End, EARLineTraceChannels TraceChannels)
{
	return TArray<FARTraceResult>();
}

TArray<UARTrackedGeometry*> FHoloLensARSystem::OnGetAllTrackedGeometries() const
{
	TArray<UARTrackedGeometry*> Geometries;
	//TrackedGeometries.GenerateValueArray(Geometries);
	// Gather all geometries
	for (auto GeoIt = TrackedGeometryGroups.CreateConstIterator(); GeoIt; ++GeoIt)
	{
		Geometries.Add(GeoIt.Value().TrackedGeometry);
	}
	return Geometries;
}

TArray<UARPin*> FHoloLensARSystem::OnGetAllPins() const
{
	TArray<UARPin*> ConvPins;
	for (UARPin* Pin : Pins)
	{
		ConvPins.Add(Pin);
	}
	return ConvPins;
}

bool FHoloLensARSystem::OnIsTrackingTypeSupported(EARSessionType SessionType) const
{
	switch (SessionType)
	{
		case EARSessionType::World:
			return true;

		case EARSessionType::Orientation:
		case EARSessionType::Face:
		case EARSessionType::Image:
		case EARSessionType::ObjectScanning:
		default:
			break;
	}
	return false;
}

bool FHoloLensARSystem::OnToggleARCapture(const bool bOnOff, const EARCaptureType CaptureType)
{
	switch (CaptureType)
	{
		case EARCaptureType::Camera:
			if (bOnOff)
			{
				bShouldStartPVCamera = true;
				if (SessionConfig != nullptr)
				{
					// We need a valid SessionConfig for camera setup.
					return UHoloLensARFunctionLibrary::StartCameraCapture();
				}
				return true;
			}
			else
			{
				bShouldStartPVCamera = false;
				return UHoloLensARFunctionLibrary::StopCameraCapture();
			}
			break;
		case EARCaptureType::QRCode:
			if (bOnOff)
			{
				bShouldStartQRDetection = true;
				if (SessionConfig != nullptr)
				{
					// When spawning QRCode objects, we need a valid SessionConfig for the QRCode class.
					return UHoloLensARFunctionLibrary::StartQRCodeCapture();
				}
				return true;
			}
			else
			{
				bShouldStartQRDetection = false;
				return UHoloLensARFunctionLibrary::StopQRCodeCapture();
			}
			break;
		case EARCaptureType::SpatialMapping:
			if (bOnOff)
			{
				bShouldStartSpatialMapping = true;
				if (SessionConfig != nullptr && SessionConfig->bGenerateMeshDataFromTrackedGeometry)
				{
					// If we already have a session config, start spatial mapping now.
					// Otherwise ToggleARCapture was called before the ARSession started, 
					// spatial mapping will start when the ARSession starts.
					return SetupMeshObserver();
				}
				return true;
			}
			else
			{
				bShouldStartSpatialMapping = false;
				return WMRInterop->StopSpatialMapping();
			}
			break;
		case EARCaptureType::HandMesh:
			if (bOnOff)
			{
				if (!WMRInterop)
				{
					return false;
				}

				if (WMRInterop->IsRemoting())
				{
					// Hand mesh is not supported over remoting
					return true;
				}

				bShowHandMeshes = true;
				return WMRInterop->StartHandMesh(&StartMeshUpdates_Raw, &AllocateMeshBuffers_Raw, &EndMeshUpdates_Raw);
			}
			else
			{
				if (!WMRInterop)
				{
					return false;
				}

				if (WMRInterop->IsRemoting())
				{
					// Hand mesh is not supported over remoting
					return true;
				}

				bShowHandMeshes = false;
				WMRInterop->StopHandMesh();
				return true;
			}
			break;
	}

	// If we got here, this is an unsupported ARCapture type - return true to avoid a retry.
	return true;
}

void FHoloLensARSystem::OnSetEnabledXRCamera(bool bOnOff)
{
	UHoloLensARFunctionLibrary::SetEnabledMixedRealityCamera(bOnOff);
}

FIntPoint FHoloLensARSystem::OnResizeXRCamera(const FIntPoint& InSize)
{
	return UHoloLensARFunctionLibrary::ResizeMixedRealityCamera(InSize);
}


UARLightEstimate* FHoloLensARSystem::OnGetCurrentLightEstimate() const
{
	return nullptr;
}

UARPin* FHoloLensARSystem::FindARPinByComponent(const USceneComponent* Component) const
{
	for (UARPin* Pin : Pins)
	{
		if (Pin->GetPinnedComponent() == Component)
		{
			return Pin;
		}
	}

	return nullptr;
}

UARPin* FHoloLensARSystem::OnPinComponent(USceneComponent* ComponentToPin, const FTransform& PinToWorldTransform, UARTrackedGeometry* TrackedGeometry, const FName DebugName)
{
	if (ensureMsgf(ComponentToPin != nullptr, TEXT("Cannot pin component.")))
	{
		if (UARPin* FindResult = FindARPinByComponent(ComponentToPin))
		{
			UE_LOG(LogHoloLensAR, Warning, TEXT("Component %s is already pinned. Unpinning it first."), *ComponentToPin->GetReadableName());
			OnRemovePin(FindResult);
		}
		TSharedPtr<FARSupportInterface, ESPMode::ThreadSafe> ARSupportInterface = TrackingSystem->GetARCompositionComponent();

		// PinToWorld * AlignedTrackingToWorld(-1) * TrackingToAlignedTracking(-1) = PinToWorld * WorldToAlignedTracking * AlignedTrackingToTracking
		// The Worlds and AlignedTracking cancel out, and we get PinToTracking
		// But we must translate this logic into Unreal's transform API
		const FTransform& TrackingToAlignedTracking = ARSupportInterface->GetAlignmentTransform();
		const FTransform PinToTrackingTransform = PinToWorldTransform.GetRelativeTransform(TrackingSystem->GetTrackingToWorldTransform()).GetRelativeTransform(TrackingToAlignedTracking);

		FString WMRAnchorId;

		// If the user did not provide a TrackedGeometry, create an WMRAnchor for this pin.
		if (TrackedGeometry == nullptr)
		{
			do 
			{
				RuntimeWMRAnchorCount += 1;
				WMRAnchorId = FString::Format(TEXT("_RuntimeAnchor_{0}_{1}"), { DebugName.ToString(), RuntimeWMRAnchorCount });
			} while (AnchorIdToPinMap.Contains(FName(*WMRAnchorId)));

			bool bSuccess = WMRCreateAnchor(*WMRAnchorId.ToLower(), PinToTrackingTransform.GetLocation(), PinToTrackingTransform.GetRotation());
			if (!bSuccess)
			{
				UE_LOG(LogHoloLensAR, Warning, TEXT("OnPinComponent: Creation of anchor %s for component %s failed!  No anchor or pin created."), *WMRAnchorId, *ComponentToPin->GetReadableName());
				return nullptr;
			}
		}

		UWMRARPin* NewPin = NewObject<UWMRARPin>();
		NewPin->InitARPin(ARSupportInterface.ToSharedRef(), ComponentToPin, PinToTrackingTransform, TrackedGeometry, DebugName);
		if (!WMRAnchorId.IsEmpty())
		{
			FName AnchorID = FName(*WMRAnchorId);
			AnchorIdToPinMap.Add(AnchorID, NewPin);
			NewPin->SetAnchorId(AnchorID);
		}
		Pins.Add(NewPin);

		return NewPin;
	}
	else
	{
		return nullptr;
	}
}

void FHoloLensARSystem::OnRemovePin(UARPin* PinToRemove)
{
	if (PinToRemove == nullptr)
	{
		return;
	}

	UWMRARPin* WMRARPin = Cast<UWMRARPin>(PinToRemove);

	if (WMRARPin)
	{
		Pins.RemoveSingleSwap(WMRARPin);

		const FName& AnchorId = WMRARPin->GetAnchorIdName();
		if (AnchorId.IsValid())
		{
			AnchorIdToPinMap.Remove(AnchorId);
			WMRRemoveAnchor(*AnchorId.ToString().ToLower());
			WMRARPin->SetAnchorId(FName());
		}
	}
}

void FHoloLensARSystem::UpdateWMRAnchors()
{
	if (SessionStatus.Status != EARSessionStatus::Running) { return; }
	
	for (UARPin* Pin : Pins)
	{
		UWMRARPin* WMRPin = Cast<UWMRARPin>(Pin);
		const FName& AnchorId = WMRPin->GetAnchorIdName();
		if (AnchorId.IsValid())
		{
			FTransform Transform;
			if (WMRGetAnchorTransform(*AnchorId.ToString().ToLower(), Transform))
			{
				Pin->OnTransformUpdated(Transform);
				Pin->OnTrackingStateChanged(EARTrackingState::Tracking);
			}
			else
			{
				Pin->OnTrackingStateChanged(EARTrackingState::NotTracking);
			}
		}
	}
}

UARTexture* FHoloLensARSystem::OnGetARTexture(EARTextureType TextureType) const
{
	if (TextureType == EARTextureType::CameraImage)
	{
		return CameraImage;
	}
	return nullptr;
}

bool FHoloLensARSystem::OnAddManualEnvironmentCaptureProbe(FVector Location, FVector Extent)
{
	return false;
}

TSharedPtr<FARGetCandidateObjectAsyncTask, ESPMode::ThreadSafe> FHoloLensARSystem::OnGetCandidateObject(FVector Location, FVector Extent) const
{
	return TSharedPtr<FARGetCandidateObjectAsyncTask, ESPMode::ThreadSafe>();
}

TSharedPtr<FARSaveWorldAsyncTask, ESPMode::ThreadSafe> FHoloLensARSystem::OnSaveWorld() const
{
	return TSharedPtr<FARSaveWorldAsyncTask, ESPMode::ThreadSafe>();
}

EARWorldMappingState FHoloLensARSystem::OnGetWorldMappingStatus() const
{
	return EARWorldMappingState::NotAvailable;
}

TArray<FARVideoFormat> FHoloLensARSystem::OnGetSupportedVideoFormats(EARSessionType SessionType) const
{
	return TArray<FARVideoFormat>();
}

TArray<FVector> FHoloLensARSystem::OnGetPointCloud() const
{
	return TArray<FVector>();
}

bool FHoloLensARSystem::OnAddRuntimeCandidateImage(UARSessionConfig* InSessionConfig, UTexture2D* CandidateTexture, FString FriendlyName, float PhysicalWidth)
{
	return false;
}

bool FHoloLensARSystem::IsLocalPinSaveSupported() const
{
	return true;
}

bool FHoloLensARSystem::ArePinsReadyToLoad()
{
#if WITH_WINDOWS_MIXED_REALITY
	return WMRInterop && WMRInterop->IsSpatialAnchorStoreLoaded();
#else
	return false;
#endif
}

void FHoloLensARSystem::LoadARPins(TMap<FName, UARPin*>& LoadedPins)
{
	TArray<FName> AnchorIds;
	bool Success = WMRLoadAnchors([&AnchorIds](const wchar_t* SaveId, const wchar_t* AnchorId) { AnchorIds.Add(AnchorId); });

	TSharedPtr<FARSupportInterface, ESPMode::ThreadSafe> ARSupportInterface = TrackingSystem->GetARCompositionComponent();
	for (FName& AnchorId : AnchorIds)
	{
		UWMRARPin* NewPin = NewObject<UWMRARPin>();
		NewPin->InitARPin(ARSupportInterface.ToSharedRef(), nullptr, FTransform::Identity, nullptr, AnchorId);

		AnchorIdToPinMap.Add(AnchorId, NewPin);
		NewPin->SetAnchorId(AnchorId);
		NewPin->SetIsInAnchorStore(true); // Note this is deprecated functionality.

		Pins.Add(NewPin);
		LoadedPins.Add(AnchorId, NewPin);
	}
	UpdateWMRAnchors();
}

bool FHoloLensARSystem::SaveARPin(FName InName, UARPin* InPin)
{
	if(InPin != nullptr)
	{
		UWMRARPin* WMRPin = CastChecked<UWMRARPin>(InPin);
		
		// Force save identifier to lowercase because FName case is not guaranteed to be the same across multiple UE4 sessions.
		const FString SaveId = InName.ToString().ToLower();
		const FString AnchorId = WMRPin->GetAnchorIdName().ToString().ToLower();
		bool Saved = WMRSaveAnchor(*SaveId, *AnchorId);
		if (!Saved)
		{
			UE_LOG(LogHoloLensAR, Warning, TEXT("SaveARPin with SaveId %s and AnchorId %s failed!  Perhaps the SaveId is already used or the AnchorId is invalid?"), *SaveId, *AnchorId);
		}
		WMRPin->SetIsInAnchorStore(Saved); // Note this is deprecated functionality.
		return Saved;
	}

	UE_LOG(LogHoloLensAR, Log, TEXT("SaveARPin: InName %s not found!"), *InName.ToString());
	return false;
}

void FHoloLensARSystem::RemoveSavedARPin(FName InName)
{
	UARPin** ARPin = AnchorIdToPinMap.Find(InName);
	if (ARPin)
	{
		UWMRARPin* WMRPin = Cast<UWMRARPin>(*ARPin);
		check(WMRPin);

		WMRPin->SetIsInAnchorStore(false); // Note this is deprecated functionality.
		const FString& AnchorId = WMRPin->GetAnchorId();
		// Force save identifier to lowercase because FName case is not guaranteed to be the same across multiple UE4 sessions.
		FString SaveId = AnchorId.ToLower();
		WMRRemoveSavedAnchor(*SaveId);
	}
}

void FHoloLensARSystem::RemoveAllSavedARPins()
{
	for (UARPin* Pin : Pins)
	{
		UWMRARPin* WMRPin = Cast<UWMRARPin>(Pin);
		if (WMRPin)
		{
			WMRPin->SetIsInAnchorStore(false);
		}
	}
	WMRClearSavedAnchors();
}




#if SUPPORTS_WINDOWS_MIXED_REALITY_AR
void FHoloLensARSystem::OnCameraImageReceived_Raw(void* handle, DirectX::XMFLOAT4X4 camToTracking)
{
	FHoloLensARSystem* HoloLensARThis = FHoloLensModuleAR::GetHoloLensARSystem().Get();

	DirectX::XMVECTOR p,r,s;
	DirectX::XMMATRIX m = DirectX::XMLoadFloat4x4(&camToTracking);
	DirectX::XMMatrixDecompose(&s, &r, &p, m);

	DirectX::XMFLOAT3 pos, scale;
	DirectX::XMFLOAT4 rot;

	DirectX::XMStoreFloat3(&pos, p);
	DirectX::XMStoreFloat3(&scale, s);
	DirectX::XMStoreFloat4(&rot, r);

	FQuat rotUE = WindowsMixedReality::WMRUtility::FromMixedRealityQuaternion(rot);
	FVector posUE = WindowsMixedReality::WMRUtility::FromMixedRealityVector(pos) * 100.0f;
	FVector scaleUE = WindowsMixedReality::WMRUtility::FromMixedRealityScale(scale);

	FTransform transform = FTransform(rotUE, posUE, scaleUE);

	auto CameraImageTask = FSimpleDelegateGraphTask::FDelegate::CreateThreadSafeSP(HoloLensARThis, &FHoloLensARSystem::OnCameraImageReceived_GameThread, handle, transform);
	FSimpleDelegateGraphTask::CreateAndDispatchWhenReady(CameraImageTask, GET_STATID(STAT_FHoloLensARSystem_OnCameraImageReceived), nullptr, ENamedThreads::GameThread);
}

void FHoloLensARSystem::OnCameraImageReceived_GameThread(void* handle, FTransform camToTracking)
{
	if ((HANDLE)handle == INVALID_HANDLE_VALUE)
	{
		UE_LOG(LogHoloLensAR, Log, TEXT("OnCameraImageReceived_GameThread() passed a null texture!"));
		return;
	}
	// We leave our point null until there's an image to wrap around, so create on demand
	if (CameraImage == nullptr)
	{
		CameraImage = NewObject<UHoloLensCameraImageTexture>();
	}
	// This will start the async update process
	CameraImage->Init(handle);
	
	const FTransform TrackingToWorldTransform = TrackingSystem->GetTrackingToWorldTransform();
	FScopeLock sl(&PVCamToWorldLock);
	PVCameraToWorldMatrix = camToTracking * TrackingToWorldTransform;
}
#endif

FTransform FHoloLensARSystem::GetPVCameraToWorldTransform()
{
	FScopeLock sl(&PVCamToWorldLock);
	return PVCameraToWorldMatrix;
}

bool FHoloLensARSystem::GetPVCameraIntrinsics(FVector2D& OutFocalLength, int& OutWidth, int& OutHeight, FVector2D& OutPrincipalPoint, FVector& OutRadialDistortion, FVector2D& OutTangentialDistortion)
{
	CameraImageCapture& CameraCapture = CameraImageCapture::Get();
	
	DirectX::XMFLOAT2 FocalLength, PrincipalPoint, TangentialDistortion;
	DirectX::XMFLOAT3 RadialDistortion;
	if (!CameraCapture.GetCameraIntrinsics(FocalLength, OutWidth, OutHeight, PrincipalPoint, RadialDistortion, TangentialDistortion))
	{
		return false;
	}

	// Convert to FVector - 2d Vectors preserve windows coordinate system (x is left/right, y is up/down)
	OutFocalLength = FVector2D(FocalLength.x, FocalLength.y);
	OutPrincipalPoint = FVector2D(PrincipalPoint.x, PrincipalPoint.y);
	OutRadialDistortion = WindowsMixedReality::WMRUtility::FromMixedRealityVector(RadialDistortion);
	OutTangentialDistortion = FVector2D(TangentialDistortion.x, TangentialDistortion.y);

	return true;
}

bool FHoloLensARSystem::OnGetCameraIntrinsics(FARCameraIntrinsics& OutCameraIntrinsics) const
{
	CameraImageCapture& CameraCapture = CameraImageCapture::Get();

	DirectX::XMFLOAT2 FocalLength, PrincipalPoint, TangentialDistortion;
	DirectX::XMFLOAT3 RadialDistortion;
	int Width, Height;
	if (!CameraCapture.GetCameraIntrinsics(FocalLength, Width, Height, PrincipalPoint, RadialDistortion, TangentialDistortion))
	{
		return false;
	}

	// Convert to FVector - 2d Vectors preserve windows coordinate system (x is left/right, y is up/down)
	OutCameraIntrinsics.FocalLength = FVector2D(FocalLength.x, FocalLength.y);
	OutCameraIntrinsics.PrincipalPoint = FVector2D(PrincipalPoint.x, PrincipalPoint.y);
	OutCameraIntrinsics.ImageResolution = FIntPoint(Width, Height);

	return true;
}

FVector FHoloLensARSystem::GetWorldSpaceRayFromCameraPoint(FVector2D PixelCoordinate)
{
	CameraImageCapture& CameraCapture = CameraImageCapture::Get();
	
	DirectX::XMFLOAT2 CameraPoint = DirectX::XMFLOAT2(PixelCoordinate.X, PixelCoordinate.Y);
	DirectX::XMFLOAT2 UnprojectedPointAtUnitDepth = CameraCapture.UnprojectPVCamPointAtUnitDepth(CameraPoint);
	
	FVector Ray = WindowsMixedReality::WMRUtility::FromMixedRealityVector(
		DirectX::XMFLOAT3(
			UnprojectedPointAtUnitDepth.x,
			UnprojectedPointAtUnitDepth.y,
			-1.0f // Unprojection happened at 1 meter
		)
	) * 100.0f;

	Ray.Normalize();

	FScopeLock sl(&PVCamToWorldLock);
	return PVCameraToWorldMatrix.TransformVector(Ray);
}

bool FHoloLensARSystem::StartCameraCapture()
{
	return SetupCameraImageSupport();
}

bool FHoloLensARSystem::StopCameraCapture()
{
	CameraImageCapture& CameraCapture = CameraImageCapture::Get();
	return CameraCapture.StopCameraCapture();
}

void FHoloLensARSystem::OnLog(const wchar_t* LogMsg)
{
	FHoloLensARSystem* HoloLensARThis = FHoloLensModuleAR::GetHoloLensARSystem().Get();
	if (HoloLensARThis)
	{
	UE_LOG(LogHoloLensAR, Log, TEXT("%s"), LogMsg);
	}
}

#if SUPPORTS_WINDOWS_MIXED_REALITY_AR
void FHoloLensARSystem::StartMeshUpdates_Raw()
{
	FHoloLensARSystem* HoloLensARThis = FHoloLensModuleAR::GetHoloLensARSystem().Get();
	HoloLensARThis->StartMeshUpdates();
}

void FHoloLensARSystem::AllocateMeshBuffers_Raw(MeshUpdate* InMeshUpdate)
{
	FHoloLensARSystem* HoloLensARThis = FHoloLensModuleAR::GetHoloLensARSystem().Get();
	HoloLensARThis->AllocateMeshBuffers(InMeshUpdate);
}

void FHoloLensARSystem::RemovedMesh_Raw(MeshUpdate* InMeshRemoved)
{
	FMeshUpdate* MeshUpdate = new FMeshUpdate();
	MeshUpdate->Id = GUIDToFGuid(InMeshRemoved->Id);

	FHoloLensARSystem* HoloLensARThis = FHoloLensModuleAR::GetHoloLensARSystem().Get();
	auto GTTask = FSimpleDelegateGraphTask::FDelegate::CreateThreadSafeSP(HoloLensARThis, &FHoloLensARSystem::RemovedMesh_GameThread, MeshUpdate);
	FSimpleDelegateGraphTask::CreateAndDispatchWhenReady(GTTask, GET_STATID(STAT_FHoloLensARSystem_ProcessMeshUpdates), nullptr, ENamedThreads::GameThread);
}

void FHoloLensARSystem::EndMeshUpdates_Raw()
{
	FHoloLensARSystem* HoloLensARThis = FHoloLensModuleAR::GetHoloLensARSystem().Get();
	HoloLensARThis->EndMeshUpdates();
}

bool FHoloLensARSystem::SetupMeshObserver()
{
	if (WMRInterop == nullptr)
	{
		return false;
	}

	// Start the mesh observer. If the user says no to spatial mapping, then no updates will occur

	// Get the settings for triangle density and mapping volume size
	FString iniFile = GEngineIni;
#if WITH_EDITOR
	// If remoting, the default GEngineIni file will be Engine.ini whch does not have any HoloLens information.  Find HoloLensEngine.ini instead.
	iniFile = FPaths::Combine(FPaths::ProjectConfigDir(), FString("HoloLens"), FString("HoloLensEngine.ini"));
#endif

	float TriangleDensity = 500.f;
	GConfig->GetFloat(TEXT("/Script/HoloLensPlatformEditor.HoloLensTargetSettings"), TEXT("MaxTrianglesPerCubicMeter"), TriangleDensity, *iniFile);
	float VolumeSize = 1.f;
	GConfig->GetFloat(TEXT("/Script/HoloLensPlatformEditor.HoloLensTargetSettings"), TEXT("SpatialMeshingVolumeSize"), VolumeSize, *iniFile);

	return WMRInterop->StartSpatialMapping(TriangleDensity, VolumeSize, &StartMeshUpdates_Raw, &AllocateMeshBuffers_Raw, &RemovedMesh_Raw, &EndMeshUpdates_Raw);
}

void FHoloLensARSystem::StartMeshUpdates()
{
	CurrentUpdateSync.Lock();
	CurrentUpdate = new FMeshUpdateSet();
}

void FHoloLensARSystem::AllocateMeshBuffers(MeshUpdate* InMeshUpdate)
{
	// Allocate our memory for the mesh update
	FMeshUpdate* MeshUpdate = new FMeshUpdate();

	MeshUpdate->Id = GUIDToFGuid(InMeshUpdate->Id);
	switch (InMeshUpdate->Type)
	{
	case MeshUpdate::World:
		MeshUpdate->Type = EARObjectClassification::World;
		break;
	case MeshUpdate::Hand:
		MeshUpdate->Type = EARObjectClassification::HandMesh;
		break;
	}

	// If this is zero, then the mesh wasn't updated so we'll just update the timestamp related to it
	// Otherwise the mesh itself was added/changed and requires an update
	if (InMeshUpdate->NumVertices > 0)
	{
		MeshUpdate->Vertices.AddUninitialized(InMeshUpdate->NumVertices);
		InMeshUpdate->Vertices = MeshUpdate->Vertices.GetData();
		MeshUpdate->Indices.AddUninitialized(InMeshUpdate->NumIndices);
		InMeshUpdate->Indices = MeshUpdate->Indices.GetData();

		if (InMeshUpdate->NumNormals > 0)
		{
			MeshUpdate->Normals.AddUninitialized(InMeshUpdate->NumNormals);
			InMeshUpdate->Normals = MeshUpdate->Normals.GetData();
		}

		MeshUpdate->bIsRightHandMesh = InMeshUpdate->IsRightHandMesh;

		const FTransform TrackingToWorldTransform = TrackingSystem->GetTrackingToWorldTransform();
		
		// The transform information is only updated when the vertices are updated so it needs to be captured here
		FVector Translation(InMeshUpdate->Translation[0], InMeshUpdate->Translation[1], InMeshUpdate->Translation[2]);
		Translation = TrackingToWorldTransform.TransformPosition(Translation);
		MeshUpdate->Location = Translation;
		FVector Scale(InMeshUpdate->Scale[0], InMeshUpdate->Scale[1], InMeshUpdate->Scale[2]);
		MeshUpdate->Scale = Scale;
		FQuat Rotation(InMeshUpdate->Rotation[0], InMeshUpdate->Rotation[1], InMeshUpdate->Rotation[2], InMeshUpdate->Rotation[3]);
		Rotation = TrackingToWorldTransform.TransformRotation(Rotation);
		MeshUpdate->Rotation = Rotation;
	}

	// Add this to our managed set
	CurrentUpdate->GuidToMeshUpdateList.Add(MeshUpdate->Id, MeshUpdate);

	UE_LOG(LogHoloLensAR, Verbose,
		TEXT("FHoloLensARSystem::AllocateMeshBuffers() added mesh id (%s), vert count (%d), index count (%d)"),
		*MeshUpdate->Id.ToString(), MeshUpdate->Vertices.Num(), MeshUpdate->Indices.Num());
}

void FHoloLensARSystem::EndMeshUpdates()
{
	bool bNeedsThreadQueueing = true;
	// Lock the list to process, append our new work, and then release our work set
	{
		FScopeLock sl(&MeshUpdateListSync);
		MeshUpdateList.Add(CurrentUpdate);
		bNeedsThreadQueueing = MeshUpdateList.Num() == 1;
	}
	CurrentUpdate = nullptr;

	// Since the game thread worker works through the queue we only need queue if there is only 1 item
	if (bNeedsThreadQueueing)
	{
		// Queue a game thread processing update
		auto MeshProcessTask = FSimpleDelegateGraphTask::FDelegate::CreateThreadSafeSP(this, &FHoloLensARSystem::ProcessMeshUpdates_GameThread);
		FSimpleDelegateGraphTask::CreateAndDispatchWhenReady(MeshProcessTask, GET_STATID(STAT_FHoloLensARSystem_ProcessMeshUpdates), nullptr, ENamedThreads::GameThread);
	}

	CurrentUpdateSync.Unlock();
}

void FHoloLensARSystem::ProcessMeshUpdates_GameThread()
{
	FMeshUpdateSet* UpdateToProcess = nullptr;
	bool bIsDone = false;
	while (!bIsDone)
	{
		// Lock our game thread queue to pull the next set of updates
		{
			FScopeLock sl(&MeshUpdateListSync);
			if (MeshUpdateList.Num() > 0)
			{
				UpdateToProcess = MeshUpdateList[0];
				MeshUpdateList.RemoveAt(0);
			}
			else
			{
				bIsDone = true;
			}
		}
		// It's possible that a previous call handled the updates since we loop
		if (UpdateToProcess != nullptr)
		{
			// Iterate through the list of updates processing them
			for (TMap<FGuid, FMeshUpdate*>::TConstIterator Iter(UpdateToProcess->GuidToMeshUpdateList); Iter; ++Iter)
			{
				FMeshUpdate* CurrentMeshUpdate = Iter.Value();
				AddOrUpdateMesh(CurrentMeshUpdate);
				delete CurrentMeshUpdate;
			}

			// This update is done, so delete it
			delete UpdateToProcess;
			UpdateToProcess = nullptr;
		}
	}
}

void FHoloLensARSystem::AddOrUpdateMesh(FMeshUpdate* CurrentMesh)
{
	bool bIsAdd = false;

	bool bUseXRVisualizationForThisMesh = CurrentMesh->Type == EARObjectClassification::HandMesh && !bUseLegacyHandMeshVisualization;

	FTrackedGeometryGroup* FoundTrackedGeometryGroup = TrackedGeometryGroups.Find(CurrentMesh->Id);
	//UARTrackedGeometry** FoundGeometry = TrackedGeometries.Find(CurrentMesh->Id);
	if (FoundTrackedGeometryGroup == nullptr)
	{
		// We haven't seen this one before so add it to our set
		//JB AR - Which class to use for this one?  Should the base class just work?
		FTrackedGeometryGroup TrackedGeometryGroup(NewObject<UARTrackedGeometry>());
		TrackedGeometryGroups.Add(CurrentMesh->Id, TrackedGeometryGroup);

		FoundTrackedGeometryGroup = TrackedGeometryGroups.Find(CurrentMesh->Id);
		check(FoundTrackedGeometryGroup);
		
		bIsAdd = true;
	}

	UARTrackedGeometry* NewUpdatedGeometry = FoundTrackedGeometryGroup->TrackedGeometry;
	UARComponent* NewUpdatedARComponent = FoundTrackedGeometryGroup->ARComponent;

	check(NewUpdatedGeometry != nullptr);
	// We will only get a new transform when there are also vert updates
	if (CurrentMesh->Vertices.Num() > 0)
	{
		// Update the tracking data
		NewUpdatedGeometry->UpdateTrackedGeometry(TrackingSystem->GetARCompositionComponent().ToSharedRef(),
			GFrameCounter,
			FPlatformTime::Seconds(),
			FTransform(CurrentMesh->Rotation, CurrentMesh->Location, CurrentMesh->Scale),
			TrackingSystem->GetARCompositionComponent()->GetAlignmentTransform());
		// Mark this as a world mesh that isn't recognized as a particular scene type, since it is loose triangles
		NewUpdatedGeometry->SetObjectClassification(CurrentMesh->Type);
		
		if (bUseXRVisualizationForThisMesh)
		{
			// The current mesh will be deleted when this function completes, so move relevant data to our cached hand mesh array.
			int HandIndex = CurrentMesh->bIsRightHandMesh ? 1 : 0;
			HandMeshes[HandIndex].Vertices = MoveTemp(CurrentMesh->Vertices);
			HandMeshes[HandIndex].Indices = MoveTemp(CurrentMesh->Indices);
			HandMeshes[HandIndex].Normals = MoveTemp(CurrentMesh->Normals);
			HandMeshes[HandIndex].Location = MoveTemp(CurrentMesh->Location);
			HandMeshes[HandIndex].Rotation = MoveTemp(CurrentMesh->Rotation);
			HandMeshes[HandIndex].Scale = MoveTemp(CurrentMesh->Scale);
			return;
		}

		// Update MRMesh if it's available
		if (auto MRMesh = NewUpdatedGeometry->GetUnderlyingMesh())
		{
			// MRMesh takes ownership of the data in the arrays at this point
			MRMesh->UpdateMesh(CurrentMesh->Location, CurrentMesh->Rotation, CurrentMesh->Scale, CurrentMesh->Vertices, CurrentMesh->Indices);
		}
	}
	else
	{
		// Update the tracking data
		NewUpdatedGeometry->UpdateTrackedGeometryNoMove(TrackingSystem->GetARCompositionComponent().ToSharedRef(),
			GFrameCounter,
			FPlatformTime::Seconds()
		);
	}

	// Trigger the proper notification delegate
	if (bIsAdd)
	{
		// RequestSpawn should happen after UpdateTrackedGeometry so the TrackableAdded event has the correct object classification.
		if (SessionConfig != nullptr)
		{
			AARActor::RequestSpawnARActor(CurrentMesh->Id, SessionConfig->GetMeshComponentClass());
		}
	}
	else
	{
		if (NewUpdatedARComponent)
		{
			NewUpdatedARComponent->Update(NewUpdatedGeometry);
			TriggerOnTrackableUpdatedDelegates(NewUpdatedGeometry);
		}
	}
}

void FHoloLensARSystem::RemovedMesh_GameThread(FMeshUpdate* RemovedMesh)
{
	FTrackedGeometryGroup* FoundTrackedGeometryGroup = TrackedGeometryGroups.Find(RemovedMesh->Id);
	if (FoundTrackedGeometryGroup != nullptr)
	{
		UARTrackedGeometry* TrackedGeometry = FoundTrackedGeometryGroup->TrackedGeometry;
		UARComponent* ARComponent = FoundTrackedGeometryGroup->ARComponent;
		AARActor* ARActor = FoundTrackedGeometryGroup->ARActor;

		check(TrackedGeometry != nullptr);

		//send the notification before we delete anything
		if (ARComponent)
		{
			ARComponent->Remove(TrackedGeometry);
			AARActor::RequestDestroyARActor(ARActor);
		}
		TrackedGeometry->SetTrackingState(EARTrackingState::NotTracking);

		// Detach the mesh component from our scene if it's valid
		UMRMeshComponent* MRMesh = TrackedGeometry->GetUnderlyingMesh();
		if (MRMesh != nullptr)
		{
			MRMesh->UnregisterComponent();
			TrackedGeometry->SetUnderlyingMesh(nullptr);
		}

		TrackedGeometryGroups.Remove(RemovedMesh->Id);
		TriggerOnTrackableRemovedDelegates(TrackedGeometry);
	}
	delete RemovedMesh;
}

void FHoloLensARSystem::QRCodeAdded_Raw(QRCodeData* InCode)
{
	FQRCodeData* QRCode = new FQRCodeData(InCode);
	FHoloLensARSystem* HoloLensARThis = FHoloLensModuleAR::GetHoloLensARSystem().Get();
	auto CameraImageTask = FSimpleDelegateGraphTask::FDelegate::CreateThreadSafeSP(HoloLensARThis, &FHoloLensARSystem::QRCodeAdded_GameThread, QRCode);
	FSimpleDelegateGraphTask::CreateAndDispatchWhenReady(CameraImageTask, GET_STATID(STAT_FHoloLensARSystem_QRCodeAdded_GameThread), nullptr, ENamedThreads::GameThread);
}

void FHoloLensARSystem::QRCodeUpdated_Raw(QRCodeData* InCode)
{
	FQRCodeData* QRCode = new FQRCodeData(InCode);
	FHoloLensARSystem* HoloLensARThis = FHoloLensModuleAR::GetHoloLensARSystem().Get();
	auto CameraImageTask = FSimpleDelegateGraphTask::FDelegate::CreateThreadSafeSP(HoloLensARThis, &FHoloLensARSystem::QRCodeUpdated_GameThread, QRCode);
	FSimpleDelegateGraphTask::CreateAndDispatchWhenReady(CameraImageTask, GET_STATID(STAT_FHoloLensARSystem_QRCodeUpdated_GameThread), nullptr, ENamedThreads::GameThread);
}

void FHoloLensARSystem::QRCodeRemoved_Raw(QRCodeData* InCode)
{
	FQRCodeData* QRCode = new FQRCodeData(InCode);
	FHoloLensARSystem* HoloLensARThis = FHoloLensModuleAR::GetHoloLensARSystem().Get();
	auto CameraImageTask = FSimpleDelegateGraphTask::FDelegate::CreateThreadSafeSP(HoloLensARThis, &FHoloLensARSystem::QRCodeRemoved_GameThread, QRCode);
	FSimpleDelegateGraphTask::CreateAndDispatchWhenReady(CameraImageTask, GET_STATID(STAT_FHoloLensARSystem_QRCodeRemoved_GameThread), nullptr, ENamedThreads::GameThread);
}

bool FHoloLensARSystem::SetupQRCodeTracking()
{
	if (WMRInterop == nullptr)
	{
		return false;
	}

	UE_LOG(LogHoloLensAR, Verbose, TEXT("FHoloLensARSystem::SetupQRCodeTracking() called"));
	return WMRInterop->StartQRCodeTracking(&QRCodeAdded_Raw, &QRCodeUpdated_Raw, &QRCodeRemoved_Raw);
}

bool FHoloLensARSystem::StopQRCodeTracking()
{
	if (WMRInterop == nullptr)
	{
		return false;
	}

	UE_LOG(LogHoloLensAR, Verbose, TEXT("FHoloLensARSystem::StopQRCodeTracking() called"));
	return WMRInterop->StopQRCodeTracking();
}

static void DebugDumpQRData(QRCodeData* InCode)
{
	FGuid Guid = GUIDToFGuid(InCode->Id);
	UE_LOG(LogHoloLensAR, Log, TEXT("  Id = %s"), *(Guid.ToString(EGuidFormats::DigitsWithHyphensInBraces)));
	UE_LOG(LogHoloLensAR, Log, TEXT("  Version = %d"), InCode->Version);
	UE_LOG(LogHoloLensAR, Log, TEXT("  SizeM = %0.3f"), InCode->SizeInMeters);
	UE_LOG(LogHoloLensAR, Log, TEXT("  Timestamp = %0.6f"), InCode->LastSeenTimestamp);
	UE_LOG(LogHoloLensAR, Log, TEXT("  DataSize = %d"), InCode->DataSize);
	if ((InCode->DataSize > 0) && (InCode->Data != nullptr))
	{
		UE_LOG(LogHoloLensAR, Log, TEXT("  Data = %s"), InCode->Data);
	}

	FVector Translation(InCode->Translation[0], InCode->Translation[1], InCode->Translation[2]);
	FQuat Orientation(InCode->Rotation[0], InCode->Rotation[1], InCode->Rotation[2], InCode->Rotation[3]);
	Orientation.Normalize();
	UE_LOG(LogHoloLensAR, Log, TEXT("  Location = %s"), *Translation.ToString());
	UE_LOG(LogHoloLensAR, Log, TEXT("  Orientation = %s"), *Orientation.ToString());
}

void FHoloLensARSystem::QRCodeAdded_GameThread(FQRCodeData* InCode)
{
	UE_LOG(LogHoloLensAR, Verbose, TEXT("FHoloLensARSystem::QRCodeAdded() called"));

	if (InCode != nullptr)
	{
		// We haven't seen this one before so add it to our set
		FTrackedGeometryGroup TrackedGeometryGroup(NewObject<UARTrackedQRCode>());
		TrackedGeometryGroups.Add(InCode->Id, TrackedGeometryGroup);
		
		UARTrackedQRCode* NewQRCode = Cast<UARTrackedQRCode>(TrackedGeometryGroup.TrackedGeometry);
		NewQRCode->UniqueId = InCode->Id;

		NewQRCode->UpdateTrackedGeometry(TrackingSystem->GetARCompositionComponent().ToSharedRef(),
			GFrameCounter,
			FPlatformTime::Seconds(),
			InCode->Transform,
			TrackingSystem->GetARCompositionComponent()->GetAlignmentTransform(),
			InCode->Size,
			InCode->QRCode,
			InCode->Version);

		//		DebugDumpQRData(InCode);
		
		if (SessionConfig != nullptr)
		{
			AARActor::RequestSpawnARActor(InCode->Id, SessionConfig->GetQRCodeComponentClass());
		}
	}
	delete InCode;
}

void FHoloLensARSystem::QRCodeUpdated_GameThread(FQRCodeData* InCode)
{
	UE_LOG(LogHoloLensAR, Log, TEXT("FHoloLensARSystem::QRCodeUpdated() called"));

	if (InCode != nullptr)
	{
		FTrackedGeometryGroup* TrackedGeometryGroup = TrackedGeometryGroups.Find(InCode->Id);
		if (TrackedGeometryGroup != nullptr)
		{
			UARTrackedGeometry* FoundGeometry = TrackedGeometryGroup->TrackedGeometry;
			UARTrackedQRCode* UpdatedQRCode = Cast<UARTrackedQRCode>(FoundGeometry);
			check(UpdatedQRCode != nullptr);

			UpdatedQRCode->UpdateTrackedGeometry(TrackingSystem->GetARCompositionComponent().ToSharedRef(),
				GFrameCounter,
				FPlatformTime::Seconds(),
				InCode->Transform,
				TrackingSystem->GetARCompositionComponent()->GetAlignmentTransform(),
				InCode->Size,
				InCode->QRCode,
				InCode->Version);

			if (TrackedGeometryGroup->ARComponent != nullptr)
			{
				TrackedGeometryGroup->ARComponent->Update(UpdatedQRCode);
				TriggerOnTrackableUpdatedDelegates(UpdatedQRCode);
			}
		}
		//		DebugDumpQRData(InCode);
	}
	delete InCode;
}

void FHoloLensARSystem::QRCodeRemoved_GameThread(FQRCodeData* InCode)
{
	UE_LOG(LogHoloLensAR, Log, TEXT("FHoloLensARSystem::QRCodeRemoved() called"));

	if (InCode != nullptr)
	{
		FTrackedGeometryGroup* TrackedGeometryGroup = TrackedGeometryGroups.Find(InCode->Id);
		if (TrackedGeometryGroup != nullptr)
		{
			if (TrackedGeometryGroup->ARComponent)
			{
				check(TrackedGeometryGroup->ARActor);

				TrackedGeometryGroup->ARComponent->Remove(TrackedGeometryGroup->TrackedGeometry);
				AARActor::RequestDestroyARActor(TrackedGeometryGroup->ARActor);
			}

			check(TrackedGeometryGroup->TrackedGeometry);
			TrackedGeometryGroup->TrackedGeometry->SetTrackingState(EARTrackingState::NotTracking);

			TrackedGeometryGroups.Remove(InCode->Id);
			TriggerOnTrackableRemovedDelegates(TrackedGeometryGroup->TrackedGeometry);
		}
		//		DebugDumpQRData(InCode);
	}
	delete InCode;
}

#endif

void FHoloLensARSystem::OnTrackingChanged(EARTrackingQuality InTrackingQuality)
{
	TrackingQuality = InTrackingQuality;
}

void FHoloLensARSystem::SetEnabledMixedRealityCamera(bool enabled)
{
	if (WMRInterop)
	{
		WMRInterop->SetEnabledMixedRealityCamera(enabled);
	}
}

void FHoloLensARSystem::ResizeMixedRealityCamera(/*inout*/ FIntPoint& size)
{
	if (WMRInterop)
	{
		SIZE newSize = { size.X, size.Y };
		if (WMRInterop->ResizeMixedRealityCamera(newSize))
		{
			size = FIntPoint(newSize.cx, newSize.cy);
		}
	}
}

void FHoloLensARSystem::ClearTrackedGeometries()
{
	TArray<UARPin*> TempPins;
	for (UARPin* Pin : Pins)
	{
		TempPins.Add(Pin);
	}

	for (UARPin* PinToRemove : TempPins)
	{
		OnRemovePin(PinToRemove);
	}

	for (auto GeoIt = TrackedGeometryGroups.CreateIterator(); GeoIt; ++GeoIt)
	{
		FTrackedGeometryGroup& TrackedGeometryGroup = GeoIt.Value();
		if (TrackedGeometryGroup.ARActor)
		{
			AARActor::RequestDestroyARActor(TrackedGeometryGroup.ARActor);
		}
		// Remove the occlusion mesh if present
		UARTrackedGeometry* TrackedGeometryBeingRemoved = TrackedGeometryGroup.TrackedGeometry;
		check(TrackedGeometryBeingRemoved);
		UMRMeshComponent* MRMesh = TrackedGeometryBeingRemoved->GetUnderlyingMesh();
		if (MRMesh != nullptr)
		{
			MRMesh->DestroyComponent();
			TrackedGeometryBeingRemoved->SetUnderlyingMesh(nullptr);
		}
	}
	TrackedGeometryGroups.Empty();
}

FName FHoloLensARSystem::GetHandTrackerDeviceTypeName() const
{
	return FName("WMRHandMeshTracking");
}

bool FHoloLensARSystem::IsHandTrackingStateValid() const
{
	if (!bShowHandMeshes)
	{
		return false;
	}

	for (int i = 0; i < 2; i++)
	{
		FXRMotionControllerData data;
		UHeadMountedDisplayFunctionLibrary::GetMotionControllerData(nullptr, (EControllerHand)i, data);
		if (data.bValid)
		{
			return true;
		}
	}

	return false;
}

bool FHoloLensARSystem::HasHandMeshData() const
{
	if (!bShowHandMeshes)
	{
		return false;
	}

	return HandMeshes[0].Vertices.Num() > 0
		|| HandMeshes[1].Vertices.Num() > 0;

	return false;
}

bool FHoloLensARSystem::GetHandMeshData(EControllerHand Hand, TArray<FVector>& OutVertices, TArray<FVector>& OutNormals, TArray<int32>& OutIndices, FTransform& OutHandMeshTransform) const
{
	FMeshUpdate HandState = HandMeshes[(int)Hand];

	OutIndices.Reset(HandState.Indices.Num());
	OutVertices.Reset(HandState.Vertices.Num());
	OutNormals.Reset(HandState.Normals.Num());

	OutHandMeshTransform = FTransform(HandState.Rotation, HandState.Location, HandState.Scale);

	OutIndices.AddUninitialized(HandState.Indices.Num());
	OutVertices.AddUninitialized(HandState.Vertices.Num());
	OutNormals.AddUninitialized(HandState.Normals.Num());

	OutVertices = CopyTemp(HandState.Vertices);
	OutNormals = CopyTemp(HandState.Normals);

	auto DestIndices = OutIndices.GetData();
	for (size_t i = 0; i < HandState.Indices.Num(); i++)
	{
		DestIndices[i] = HandState.Indices[i];
	}

	return true;
}

/** Used to run Exec commands */
static bool HoloLensARTestingExec(UWorld* InWorld, const TCHAR* Cmd, FOutputDevice& Ar)
{
	bool bHandled = false;

	if (FParse::Command(&Cmd, TEXT("HOLOLENSAR")))
	{
		if (FParse::Command(&Cmd, TEXT("MRMESH")))
		{
			AAROriginActor* OriginActor = AAROriginActor::GetOriginActor();
			UMRMeshComponent* NewComp = NewObject<UMRMeshComponent>(OriginActor);
			NewComp->RegisterComponent();
			// Send a fake update to it
			FTransform Transform = FTransform::Identity;
			static TArray<FVector> Verts;
			static TArray<MRMESH_INDEX_TYPE> Indices;

			Verts.Reset(4);
			Indices.Reset(6);

			Verts.Add(FVector(100.f, -100.f, 0.f));
			Verts.Add(FVector(100.f, 100.f, 0.f));
			Verts.Add(FVector(-100.f, -100.f, 0.f));
			Verts.Add(FVector(-100.f, 100.f, 0.f));
			Indices.Add(0);
			Indices.Add(1);
			Indices.Add(2);
			Indices.Add(2);
			Indices.Add(3);
			Indices.Add(0);
			NewComp->UpdateMesh(Transform.GetLocation(), Transform.GetRotation(), Transform.GetScale3D(), Verts, Indices);

			return true;
		}
	}

	return false;
}


FStaticSelfRegisteringExec HoloLensARTestingExecRegistration(HoloLensARTestingExec);

