// Copyright Epic Games, Inc. All Rights Reserved.

#include "HoloLensARSystem.h"
#include "HoloLensModule.h"
#include "HoloLensCameraImageTexture.h"

#include "ARPin.h"
#include "ARTrackable.h"
#include "ARTraceResult.h"
#include "MixedRealityInterop.h"
#include "WindowsMixedRealityInteropUtility.h"

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

	/** This gets filled in during the mesh allocation */
	FVector Location;
	FQuat Rotation;
	FVector Scale;

	// These will use MoveTemp to avoid another copy
	// The interop fills these in directly
	TArray<FVector> Vertices;
	TArray<MRMESH_INDEX_TYPE> Indices;
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

}

FHoloLensARSystem::~FHoloLensARSystem()
{

}

void FHoloLensARSystem::SetTrackingSystem(TSharedPtr<FXRTrackingSystemBase, ESPMode::ThreadSafe> InTrackingSystem)
{
	TrackingSystem = InTrackingSystem;
}

void FHoloLensARSystem::SetInterop(WindowsMixedReality::MixedRealityInterop* InWMRInterop)
{
	WMRInterop = InWMRInterop;

	HandlerId = WMRInterop->SubscribeConnectionEvent([this](WindowsMixedReality::MixedRealityInterop::ConnectionEvent evt) 
	{
		if (evt == WindowsMixedReality::MixedRealityInterop::ConnectionEvent::DisconnectedFromPeer)
		{
			OnStopARSession();

#if WITH_EDITOR
			if (GEditor && GEditor->IsVRPreviewActive())
			{
				GEditor->RequestEndPlayMap();
			}
#endif

			UE_LOG(LogHoloLensAR, Warning, TEXT("HoloLens AR session disconnected from peer"));
		}
	});
}

void FHoloLensARSystem::Shutdown()
{
	WMRInterop->UnsubscribeConnectionEvent(HandlerId);
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
	Collector.AddReferencedObjects(TrackedGeometries);
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

#if PLATFORM_HOLOLENS
	// Start the camera image grabber
	SetupCameraImageSupport();
#endif

	if (SessionConfig->bGenerateMeshDataFromTrackedGeometry)
	{
		// Start spatial mesh mapping
		SetupMeshObserver();
	}

	// Start QR code tracking
	SetupQRCodeTracking();

	SessionStatus.Status = EARSessionStatus::Running;

#if WITH_EDITOR
	UEditorEngine* EditorEngine = CastChecked<UEditorEngine>(GEngine);
	FSceneViewport* PIEViewport = (FSceneViewport*)EditorEngine->GetPIEViewport();
	if (!PIEViewport->IsStereoRenderingAllowed())
	{
		// Running the AR session on a non-stereo window will break spatial anchors.
		SessionStatus.Status = EARSessionStatus::NotSupported;
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
void FHoloLensARSystem::SetupCameraImageSupport()
{
	// Remoting does not support CameraCapture currently.
	//if (WMRInterop->IsRemoting()) //TEMP Disabling passthrough camera, it has d3d corruption bugs.
	{
		return;
	}

	// Start the camera capture device
	CameraImageCapture& CameraCapture = CameraImageCapture::Get();
#if UE_BUILD_DEBUG
	CameraCapture.SetOnLog(&OnLog);
#endif
	const FARVideoFormat Format = SessionConfig->GetDesiredVideoFormat();
	CameraCapture.StartCameraCapture(&OnCameraImageReceived_Raw, Format.Width, Format.Height, Format.FPS);
}
#endif

void FHoloLensARSystem::OnPauseARSession()
{
	if (SessionConfig->bGenerateMeshDataFromTrackedGeometry)
	{
		// Stop spatial mesh mapping. Existing meshes will remain
		WMRInterop->StopSpatialMapping();
	}
}

void FHoloLensARSystem::OnStopARSession()
{
#if SUPPORTS_WINDOWS_MIXED_REALITY_AR
	FWorldDelegates::OnWorldTickStart.RemoveAll(this);

	// If remoting stay connected.
	if (WMRInterop && !WMRInterop->IsRemoting())
	{
		WMRInterop->DisconnectFromDevice();
	}

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
	TrackedGeometries.GenerateValueArray(Geometries);
	return Geometries;
}

TArray<UARPin*> FHoloLensARSystem::OnGetAllPins() const
{
	TArray<UARPin*> ConvPins;
	for (UWMRARPin* Pin : Pins)
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

UARLightEstimate* FHoloLensARSystem::OnGetCurrentLightEstimate() const
{
	return nullptr;
}

UWMRARPin* FHoloLensARSystem::FindPinByComponent(const USceneComponent* Component)
{
	for (UWMRARPin* Pin : Pins)
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
		if (UWMRARPin* FindResult = FindPinByComponent(ComponentToPin))
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
			} while (AnchorIdToPinMap.Contains(WMRAnchorId));

			bool bSuccess = WMRCreateAnchor(*WMRAnchorId, PinToTrackingTransform.GetLocation(), PinToTrackingTransform.GetRotation());
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
			AnchorIdToPinMap.Add(WMRAnchorId, NewPin);
			NewPin->SetAnchorId(WMRAnchorId);
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

		const FString& AnchorId = WMRARPin->GetAnchorId();
		if (!AnchorId.IsEmpty())
		{
			AnchorIdToPinMap.Remove(AnchorId);
			WMRRemoveAnchor(*AnchorId);
			WMRARPin->SetAnchorId(FString());
		}
	}
}

void FHoloLensARSystem::UpdateWMRAnchors()
{
	if (SessionStatus.Status != EARSessionStatus::Running) { return; }
	
	for (UWMRARPin* Pin : Pins)
	{
		const FString& AnchorId = Pin->GetAnchorId();
		if (!AnchorId.IsEmpty())
		{
			FTransform Transform;
			if (WMRGetAnchorTransform(*AnchorId, Transform))
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

UARTextureCameraImage* FHoloLensARSystem::OnGetCameraImage()
{
	return CameraImage;
}

UARTextureCameraDepth* FHoloLensARSystem::OnGetCameraDepth()
{
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

#if SUPPORTS_WINDOWS_MIXED_REALITY_AR
void FHoloLensARSystem::OnCameraImageReceived_Raw(ID3D11Texture2D* CameraFrame)
{
	FHoloLensARSystem* HoloLensARThis = FHoloLensModuleAR::GetHoloLensARSystem().Get();
	// To keep this from being deleted while we are processing it
	CameraFrame->AddRef();

	auto CameraImageTask = FSimpleDelegateGraphTask::FDelegate::CreateThreadSafeSP(HoloLensARThis, &FHoloLensARSystem::OnCameraImageReceived_GameThread, CameraFrame);
	FSimpleDelegateGraphTask::CreateAndDispatchWhenReady(CameraImageTask, GET_STATID(STAT_FHoloLensARSystem_OnCameraImageReceived), nullptr, ENamedThreads::GameThread);
}

void FHoloLensARSystem::OnCameraImageReceived_GameThread(ID3D11Texture2D* CameraFrame)
{
	if (CameraFrame == nullptr)
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
	CameraImage->Init(CameraFrame);

	// The raw handler added a ref to keep this in memory through the async task, so we need to release our ref
	CameraFrame->Release();
}
#endif

void FHoloLensARSystem::OnLog(const wchar_t* LogMsg)
{
	UE_LOG(LogHoloLensAR, Log, TEXT("%s"), LogMsg);
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

void FHoloLensARSystem::SetupMeshObserver()
{
	check(WMRInterop != nullptr);

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

	WMRInterop->StartSpatialMapping(TriangleDensity, VolumeSize, &StartMeshUpdates_Raw, &AllocateMeshBuffers_Raw, &RemovedMesh_Raw, &EndMeshUpdates_Raw);
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

	// If this is zero, then the mesh wasn't updated so we'll just update the timestamp related to it
	// Otherwise the mesh itself was added/changed and requires an update
	if (InMeshUpdate->NumVertices > 0)
	{
		MeshUpdate->Vertices.AddUninitialized(InMeshUpdate->NumVertices);
		InMeshUpdate->Vertices = MeshUpdate->Vertices.GetData();
		MeshUpdate->Indices.AddUninitialized(InMeshUpdate->NumIndices);
		InMeshUpdate->Indices = MeshUpdate->Indices.GetData();

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
	UARTrackedGeometry* NewUpdatedGeometry = nullptr;
	UARTrackedGeometry** FoundGeometry = TrackedGeometries.Find(CurrentMesh->Id);
	if (FoundGeometry == nullptr)
	{
		// We haven't seen this one before so add it to our set
		NewUpdatedGeometry = NewObject<UARTrackedGeometry>();
		TrackedGeometries.Add(CurrentMesh->Id, NewUpdatedGeometry);
		bIsAdd = true;
	}
	else
	{
		NewUpdatedGeometry = *FoundGeometry;
	}
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
		NewUpdatedGeometry->SetObjectClassification(EARObjectClassification::World);
		if (NewUpdatedGeometry->GetUnderlyingMesh() == nullptr)
		{
			// Attach this component to our single origin actor
			AAROriginActor* OriginActor = AAROriginActor::GetOriginActor();

			// During shutdown we can get a mesh update after the OriginActor has been destroyed, just return in that case.
			if (OriginActor == nullptr)
			{
				return;
			}

			UMRMeshComponent* MRMesh = NewObject<UMRMeshComponent>(OriginActor);

			// Set the occlusion and wireframe defaults
			MRMesh->SetEnableMeshOcclusion(SessionConfig->bUseMeshDataForOcclusion);
			MRMesh->SetUseWireframe(SessionConfig->bRenderMeshDataInWireframe);
			MRMesh->SetNeverCreateCollisionMesh(!SessionConfig->bGenerateCollisionForMeshData);
			MRMesh->SetEnableNavMesh(SessionConfig->bGenerateNavMeshForMeshData);

			// Set parent and register
			MRMesh->SetupAttachment(OriginActor->GetRootComponent());
			MRMesh->RegisterComponent();

			// Connect the tracked geo to the MRMesh
			NewUpdatedGeometry->SetUnderlyingMesh(MRMesh);
		}
		UMRMeshComponent* MRMesh = NewUpdatedGeometry->GetUnderlyingMesh();
		check(MRMesh != nullptr);
		// MRMesh takes ownership of the data in the arrays at this point
		MRMesh->UpdateMesh(CurrentMesh->Location, CurrentMesh->Rotation, CurrentMesh->Scale, CurrentMesh->Vertices, CurrentMesh->Indices);
	}
	// Trigger the proper notification delegate
	if (bIsAdd)
	{
		TriggerOnTrackableAddedDelegates(NewUpdatedGeometry);
	}
	else
	{
		TriggerOnTrackableUpdatedDelegates(NewUpdatedGeometry);
	}
}

void FHoloLensARSystem::RemovedMesh_GameThread(FMeshUpdate* RemovedMesh)
{
	UARTrackedGeometry** TrackedGeometry = TrackedGeometries.Find(RemovedMesh->Id);
	if (TrackedGeometry != nullptr)
	{
		(*TrackedGeometry)->SetTrackingState(EARTrackingState::NotTracking);

		// Detach the mesh component from our scene if it's valid
		UMRMeshComponent* MRMesh = (*TrackedGeometry)->GetUnderlyingMesh();
		if (MRMesh != nullptr)
		{
			MRMesh->UnregisterComponent();
			(*TrackedGeometry)->SetUnderlyingMesh(nullptr);
		}

		TrackedGeometries.Remove(RemovedMesh->Id);
		TriggerOnTrackableRemovedDelegates(*TrackedGeometry);
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

void FHoloLensARSystem::SetupQRCodeTracking()
{
	check(WMRInterop != nullptr);

	WMRInterop->StartQRCodeTracking(&QRCodeAdded_Raw, &QRCodeUpdated_Raw, &QRCodeRemoved_Raw);
	UE_LOG(LogHoloLensAR, Verbose, TEXT("FHoloLensARSystem::SetupQRCodeTracking() called"));
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
		UARTrackedQRCode* NewQRCode = NewObject<UARTrackedQRCode>();
		TrackedGeometries.Add(InCode->Id, NewQRCode);

		NewQRCode->UpdateTrackedGeometry(TrackingSystem->GetARCompositionComponent().ToSharedRef(),
			GFrameCounter,
			FPlatformTime::Seconds(),
			InCode->Transform,
			TrackingSystem->GetARCompositionComponent()->GetAlignmentTransform(),
			InCode->Size,
			InCode->QRCode,
			InCode->Version);

		TriggerOnTrackableAddedDelegates(NewQRCode);

		//		DebugDumpQRData(InCode);
	}
	delete InCode;
}

void FHoloLensARSystem::QRCodeUpdated_GameThread(FQRCodeData* InCode)
{
	UE_LOG(LogHoloLensAR, Log, TEXT("FHoloLensARSystem::QRCodeUpdated() called"));

	if (InCode != nullptr)
	{
		UARTrackedGeometry** FoundGeometry = TrackedGeometries.Find(InCode->Id);
		if (FoundGeometry != nullptr)
		{
			UARTrackedQRCode* UpdatedQRCode = Cast<UARTrackedQRCode>(*FoundGeometry);

			UpdatedQRCode->UpdateTrackedGeometry(TrackingSystem->GetARCompositionComponent().ToSharedRef(),
				GFrameCounter,
				FPlatformTime::Seconds(),
				InCode->Transform,
				TrackingSystem->GetARCompositionComponent()->GetAlignmentTransform(),
				InCode->Size,
				InCode->QRCode,
				InCode->Version);

			TriggerOnTrackableUpdatedDelegates(UpdatedQRCode);
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
		UARTrackedGeometry** FoundGeometry = TrackedGeometries.Find(InCode->Id);
		if (FoundGeometry != nullptr)
		{
			(*FoundGeometry)->SetTrackingState(EARTrackingState::NotTracking);

			TrackedGeometries.Remove(InCode->Id);
			TriggerOnTrackableRemovedDelegates(*FoundGeometry);
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
