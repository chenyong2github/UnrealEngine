// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "MeshTrackerComponent.h"
#include "MagicLeapHMD.h"
#include "AppFramework.h"
#include "MagicLeapMath.h"
#include "AppEventHandler.h"
#include "MagicLeapHMDFunctionLibrary.h"

#include "Components/BoxComponent.h"
#include "Engine/Engine.h"
#include "HeadMountedDisplayFunctionLibrary.h"
#if WITH_EDITOR
#include "Editor.h"
#endif
#include "MRMeshComponent.h"
#include "Lumin/CAPIShims/LuminAPIMeshing.h"

//#define DEBUG_MESH_BLOCK_ADD_REMOVE
//#define DEBUG_MESH_REQUEST_AND_RESPONSE

#if WITH_MLSDK
// TODO: Don't rely on the size being same.
static_assert(sizeof(FGuid) == sizeof(MLCoordinateFrameUID), "Size of FGuid should be same as MLCoordinateFrameUID. TODO: Don't rely on the size being same.");

// Map an Unreal meshing LOD to the corresponding ML meshing LOD
FORCEINLINE MLMeshingLOD UnrealToML_MeshLOD(EMagicLeapMeshLOD UnrealMeshLOD)
{
	switch (UnrealMeshLOD)
	{
		case EMagicLeapMeshLOD::Minimum:
			return MLMeshingLOD_Minimum;
		case EMagicLeapMeshLOD::Medium:
			return MLMeshingLOD_Medium;
		case EMagicLeapMeshLOD::Maximum:
			return MLMeshingLOD_Maximum;
	}
	check(false);
	return MLMeshingLOD_Minimum;
}

EMagicLeapMeshState MLToUEMeshState(MLMeshingMeshState MLMeshState)
{
	switch (MLMeshState)
	{
		case MLMeshingMeshState_New:
			return EMagicLeapMeshState::New;
		case MLMeshingMeshState_Updated:
			return EMagicLeapMeshState::Updated;
		case MLMeshingMeshState_Deleted:
			return EMagicLeapMeshState::Deleted;
		case MLMeshingMeshState_Unchanged:
			return EMagicLeapMeshState::Unchanged;
	}
	check(false);
	return EMagicLeapMeshState::Unchanged;
}

void MLToUnrealBlockInfo(const MLMeshingBlockInfo& MLBlockInfo, const FTransform& TrackingToWorld, float WorldToMetersScale, FMagicLeapMeshBlockInfo& UEBlockInfo)
{
	FMemory::Memcpy(&UEBlockInfo.BlockID, &MLBlockInfo.id, sizeof(MLCoordinateFrameUID));

	FTransform BlockTransform = FTransform(MagicLeap::ToFQuat(MLBlockInfo.extents.rotation), MagicLeap::ToFVector(MLBlockInfo.extents.center, WorldToMetersScale), FVector(1.0f, 1.0f, 1.0f));
	if (!BlockTransform.GetRotation().IsNormalized())
	{
		FQuat rotation = BlockTransform.GetRotation();
		rotation.Normalize();
		BlockTransform.SetRotation(rotation);
	}

	BlockTransform = BlockTransform * TrackingToWorld;
	UEBlockInfo.BlockPosition = BlockTransform.GetLocation();
	UEBlockInfo.BlockOrientation = BlockTransform.Rotator();

	// Splat the OBB to an AABB
	FMatrix AbsWorldMatrix(BlockTransform.ToMatrixNoScale());
	AbsWorldMatrix.SetAxis(3, FVector::ZeroVector);
	for (int32 R = 0; R < 3; ++R)
	{
		for (int32 C = 0; C < 3; ++C)
		{
			AbsWorldMatrix.M[R][C] = FMath::Abs(AbsWorldMatrix.M[R][C]);
		}
	}

	// The extents returned are 'full' extents - width, height, and depth. UE4 boxes are 'half' extents
	UEBlockInfo.BlockDimensions = AbsWorldMatrix.
		TransformVector(MagicLeap::ToFVectorExtents(MLBlockInfo.extents.extents, WorldToMetersScale * 0.5f));

	UEBlockInfo.Timestamp = FTimespan::FromMicroseconds(MLBlockInfo.timestamp / 1000.0);
	UEBlockInfo.BlockState = MLToUEMeshState(MLBlockInfo.state);
}

void UnrealToMLBlockRequest(const FMagicLeapMeshBlockRequest& UEBlockRequest, MLMeshingBlockRequest& MLBlockRequest)
{
	FMemory::Memcpy(&MLBlockRequest.id, &UEBlockRequest.BlockID, sizeof(MLCoordinateFrameUID));
	MLBlockRequest.level = UnrealToML_MeshLOD(UEBlockRequest.LevelOfDetail);
}
#endif //WITH_MLSDK

class FMagicLeapMeshTrackerImpl : public MagicLeap::IAppEventHandler
{
public:
	FMagicLeapMeshTrackerImpl()
	{
	};

#if WITH_MLSDK
	MLMeshingSettings CreateSettings(const UMeshTrackerComponent& MeshTrackerComponent)
	{
		MLMeshingSettings Settings;

		MLMeshingInitSettings(&Settings);

		float WorldToMetersScale = 100.0f;
		if (IMagicLeapPlugin::Get().IsMagicLeapHMDValid())
		{
			const FAppFramework& AppFramework = static_cast<FMagicLeapHMD *>
				(GEngine->XRSystem->GetHMDDevice())->GetAppFrameworkConst();
			WorldToMetersScale = AppFramework.GetWorldToMetersScale();
		}

		if (MeshTrackerComponent.MeshType == EMagicLeapMeshType::PointCloud)
		{
			Settings.flags |= MLMeshingFlags_PointCloud;
		}
		if (MeshTrackerComponent.RequestNormals)
		{
			Settings.flags |= MLMeshingFlags_ComputeNormals;
		}
		if (MeshTrackerComponent.RequestVertexConfidence)
		{
			Settings.flags |= MLMeshingFlags_ComputeConfidence;
		}
		if (MeshTrackerComponent.Planarize)
		{
			Settings.flags |= MLMeshingFlags_Planarize;
		}
		if (MeshTrackerComponent.RemoveOverlappingTriangles)
		{
			Settings.flags |= MLMeshingFlags_RemoveMeshSkirt;
		}

		Settings.fill_hole_length = MeshTrackerComponent.PerimeterOfGapsToFill / WorldToMetersScale;
		Settings.disconnected_component_area = MeshTrackerComponent.
			DisconnectedSectionArea / (WorldToMetersScale * WorldToMetersScale);

		return Settings;
	};

#endif //WITH_MLSDK

	void OnAppPause() override
	{
	}

	void OnAppResume() override
	{
	}

	void OnClear()
	{
		MeshBrickIndex = 0;
		GuidToBrickId.Empty();

		for (auto& PendingMeshBrick : PendingMeshBricksByBrickId)
		{
			PendingMeshBrick.Value->Recycle(PendingMeshBrick.Value);
		}
		PendingMeshBricksByBrickId.Empty();
	}

public:
#if WITH_MLSDK
	// Handle to ML mesh tracker
	MLHandle MeshTracker = ML_INVALID_HANDLE;

	// Handle to ML mesh info request
	MLHandle CurrentMeshInfoRequest = ML_INVALID_HANDLE;

	// Handle to ML mesh request
	MLHandle CurrentMeshRequest = ML_INVALID_HANDLE;

	// Current ML meshing settings
	MLMeshingSettings CurrentMeshSettings;

	// List of ML mesh block IDs and states
	TArray<MLMeshingBlockRequest> MeshBlockRequests;
#endif //WITH_MLSDK

	struct FMLTrackingInfoImpl : FMagicLeapTrackingMeshInfo
	{
		// Maps the FMagicLeapTrackingMeshInfo block GUIDs to their entry in the BlockData array
		TMap<FGuid, FMagicLeapMeshBlockInfo> BlockInfoByGuid;
	};

	FMLTrackingInfoImpl LatestMeshInfo;
	TArray<FMagicLeapMeshBlockRequest> UEMeshBlockRequests;
	TScriptInterface<IMagicLeapMeshBlockSelectorInterface> BlockSelector;

	// Keep a copy of the mesh data here. MRMeshComponent will use it from the game and render thread.
	struct FMLCachedMeshData
	{
		typedef TSharedPtr<FMLCachedMeshData, ESPMode::ThreadSafe> SharedPtr;
		
		FMagicLeapMeshTrackerImpl* Owner = nullptr;
		
		FGuid BlockID;
		uint64 BrickID;
		TArray<FVector> OffsetVertices;
		TArray<FVector> WorldVertices;
		TArray<uint32> Triangles;
		TArray<FVector> Normals;
		TArray<FVector2D> UV0;
		TArray<FColor> VertexColors;
		TArray<FPackedNormal> Tangents;
		TArray<float> Confidence;

		FMagicLeapMeshBlockInfo BlockInfo;

		void Recycle(SharedPtr& MeshData)
		{
			check(Owner);

			BlockID.Invalidate();
			BrickID = INT64_MAX;
			OffsetVertices.Reset();
			WorldVertices.Reset();
			Triangles.Reset();
			Normals.Reset();
			UV0.Reset();
			VertexColors.Reset();
			Tangents.Reset();
			Confidence.Reset();

			Owner->FreeMeshDataCache(MeshData);
			Owner = nullptr;
		}

		void Init(FMagicLeapMeshTrackerImpl* InOwner)
		{
			check(!Owner);
			Owner = InOwner;
		}
	};

	// When load-balancing is enabled (BricksPerFrame > 0) this map contains mesh data
	// pending submission to MR Mesh
	TMap<uint64, FMLCachedMeshData::SharedPtr> PendingMeshBricksByBrickId;

	FDelegateHandle OnClearDelegateHandle;

	// This receipt will be kept in the FSendBrickDataArgs to ensure the cached data outlives MRMeshComponent use of it.
	class FMeshTrackerComponentBrickDataReceipt : public IMRMesh::FBrickDataReceipt
	{
	public:
		FMeshTrackerComponentBrickDataReceipt(FMLCachedMeshData::SharedPtr& MeshData) :
			CachedMeshData(MeshData)
		{
		}
		~FMeshTrackerComponentBrickDataReceipt() override
		{
			CachedMeshData->Recycle(CachedMeshData);
		}
	private:
		FMLCachedMeshData::SharedPtr CachedMeshData;
	};
	
	FMLCachedMeshData::SharedPtr AcquireMeshDataCache()
	{
		if (FreeCachedMeshDatas.Num() > 0)
		{
			FScopeLock ScopeLock(&FreeCachedMeshDatasMutex);
			FMLCachedMeshData::SharedPtr CachedMeshData(FreeCachedMeshDatas.Pop(false));
			CachedMeshData->Init(this);
			return CachedMeshData;
		}
		else
		{
			FMLCachedMeshData::SharedPtr CachedMeshData(new FMLCachedMeshData());
			CachedMeshData->Init(this);
			CachedMeshDatas.Add(CachedMeshData);
			return CachedMeshData;
		}
	}

	void FreeMeshDataCache(FMLCachedMeshData::SharedPtr& DataCache)
	{
		FScopeLock ScopeLock(&FreeCachedMeshDatasMutex);
		FreeCachedMeshDatas.Add(DataCache);
	}

	FVector BoundsCenter;
	FQuat BoundsRotation;

	bool Create(const UMeshTrackerComponent& MeshTrackerComponent)
	{
#if WITH_MLSDK
		if (!MLHandleIsValid(MeshTracker))
		{
			// Create the tracker on demand.
			//UE_LOG(LogMagicLeap, Log, TEXT("Creating Mesh MeshTracker"));
			
			CurrentMeshSettings = CreateSettings(MeshTrackerComponent);

			MLResult Result = MLMeshingCreateClient(&MeshTracker, &CurrentMeshSettings);

			if (Result != MLResult_Ok)
			{
				UE_LOG(LogMagicLeap, Error, TEXT("MLMeshingCreateClient failed: %s."), 
					UTF8_TO_TCHAR(MLGetResultString(Result)));
				return false;
			}

			GuidToBrickId.Empty();
			MeshBrickIndex = 0;
		}
#endif //WITH_MLSDK
		return true;
	}

	bool Update(const UMeshTrackerComponent& MeshTrackerComponent)
	{
#if WITH_MLSDK
		MLMeshingSettings MeshSettings = CreateSettings(MeshTrackerComponent);

		if (0 != memcmp(&CurrentMeshSettings, &MeshSettings, sizeof(MeshSettings)))
		{
			auto Result = MLMeshingUpdateSettings(MeshTracker, &MeshSettings);

			if (MLResult_Ok == Result)
			{
				// For some parameter changes we will want to clear already-generated data
				if (MeshTrackerComponent.MRMesh != nullptr)
				{
					if ((MLMeshingFlags_PointCloud & MeshSettings.flags) !=
						(MLMeshingFlags_PointCloud & CurrentMeshSettings.flags))
					{
						UE_LOG(LogMagicLeap, Log,
							TEXT("MLMeshingSettings change caused a clear"));
						MeshTrackerComponent.MRMesh->Clear(); // Will call our OnClear
					}
				}

				CurrentMeshSettings = MeshSettings;
				return true;
			}

			UE_LOG(LogMagicLeap, Error,
				TEXT("MLMeshingUpdateSettings failed: %s"), UTF8_TO_TCHAR(MLGetResultString(Result)));
		}
#endif //WITH_MLSDK
		return false;
	}

	void Destroy()
	{
#if WITH_MLSDK
		if (MLHandleIsValid(MeshTracker))
		{
			MLResult Result = MLMeshingDestroyClient(&MeshTracker);
			if (Result != MLResult_Ok)
			{
				UE_LOG(LogMagicLeap, Error, 
					TEXT("MLMeshingDestroyClient failed: %s."), UTF8_TO_TCHAR(MLGetResultString(Result)));
			}
			MeshTracker = ML_INVALID_HANDLE;
		}
#endif //WITH_MLSDK
	}

	uint64 *GetMrMeshIdFromMeshGuid(const FGuid& meshGuid, bool addIfNotFound)
	{
		auto MeshBrickId = GuidToBrickId.Find(meshGuid);
		if (MeshBrickId == nullptr && addIfNotFound)
		{
			GuidToBrickId.Add(meshGuid, MeshBrickIndex++);
			MeshBrickId = GuidToBrickId.Find(meshGuid);
		}
		return MeshBrickId;
	}

private:
	// Maps system 128-bit mesh GUIDs to 64-bit brick indices
	TMap<FGuid, uint64> GuidToBrickId;

	// Next 64-bit brick index
	uint64 MeshBrickIndex = 0;

	// A free list to recycle the CachedMeshData instances.  
	TArray<FMLCachedMeshData::SharedPtr> CachedMeshDatas;
	TArray<FMLCachedMeshData::SharedPtr> FreeCachedMeshDatas;
	FCriticalSection FreeCachedMeshDatasMutex; //The free list may be pushed/popped from multiple threads.
};

UMeshTrackerComponent::UMeshTrackerComponent()
	: VertexColorFromConfidenceZero(FLinearColor::Red)
	, VertexColorFromConfidenceOne(FLinearColor::Blue)
	, Impl(new FMagicLeapMeshTrackerImpl())
{

	// Make sure this component ticks
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;

	bAutoActivate = true;

	BoundingVolume = CreateDefaultSubobject<UBoxComponent>(TEXT("BoundingVolume"));
	BoundingVolume->AttachToComponent(this, FAttachmentTransformRules::KeepRelativeTransform);
	BoundingVolume->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BoundingVolume->SetCanEverAffectNavigation(false);
	BoundingVolume->CanCharacterStepUpOn = ECanBeCharacterBase::ECB_No;
	BoundingVolume->SetCollisionObjectType(ECollisionChannel::ECC_WorldDynamic);
	BoundingVolume->SetGenerateOverlapEvents(false);
	// Recommended default box extents for meshing - 10m (5m radius)
	BoundingVolume->SetBoxExtent(FVector(500.0f, 500.0f, 500.0f), false);

	BlockVertexColors.Add(FColor::Blue);
	BlockVertexColors.Add(FColor::Red);
	BlockVertexColors.Add(FColor::Green);
	BlockVertexColors.Add(FColor::Yellow);
	BlockVertexColors.Add(FColor::Cyan);
	BlockVertexColors.Add(FColor::Magenta);

#if WITH_EDITOR
	if (GIsEditor)
	{
		FEditorDelegates::PrePIEEnded.AddUObject(this, &UMeshTrackerComponent::PrePIEEnded);
	}
#endif
}

UMeshTrackerComponent::~UMeshTrackerComponent()
{
	delete Impl;
}

void UMeshTrackerComponent::ConnectMRMesh(UMRMeshComponent* InMRMeshPtr)
{
	if (!InMRMeshPtr)
	{
		UE_LOG(LogMagicLeap, Warning,
			TEXT("MRMesh given is not valid. Ignoring this connect."));
		return;
	}
	else if (MRMesh)
	{
		UE_LOG(LogMagicLeap, Warning, 
			TEXT("MeshTrackerComponent already has a MRMesh connected.  Ignoring this connect."));
		return;
	}
	else if (InMRMeshPtr->IsConnected())
	{
		UE_LOG(LogMagicLeap, Warning, 
			TEXT("MRMesh is already connected to a UMeshTrackerComponent. Ignoring this connect."));
		return;
	}
	else
	{
		InMRMeshPtr->SetConnected(true);
		MRMesh = InMRMeshPtr;
		Impl->OnClearDelegateHandle = MRMesh->OnClear().AddRaw(Impl, &FMagicLeapMeshTrackerImpl::OnClear);
	}
}

void UMeshTrackerComponent::DisconnectMRMesh(class UMRMeshComponent* InMRMeshPtr)
{
	if (!MRMesh)
	{
		UE_LOG(LogMagicLeap, Warning, 
			TEXT("MeshTrackerComponent MRMesh is already disconnected. Ignoring this disconnect."));
		return;
	}
	else if (InMRMeshPtr != MRMesh)
	{
		UE_LOG(LogMagicLeap, Warning, 
			TEXT("MeshTrackerComponent MRMesh given is not the MRMesh connected. "
				 "Ignoring this disconnect."));
		return;
	}
	else
	{
		check(MRMesh->IsConnected());
		MRMesh->SetConnected(false);

		if (Impl->OnClearDelegateHandle.IsValid())
		{
			MRMesh->OnClear().Remove(Impl->OnClearDelegateHandle);
			Impl->OnClearDelegateHandle.Reset();
		}
	}
	MRMesh = nullptr;
}

#if WITH_EDITOR
void UMeshTrackerComponent::PostEditChangeProperty(FPropertyChangedEvent& e)
{
#if WITH_MLSDK
	if (MLHandleIsValid(Impl->MeshTracker) && e.Property != nullptr)
	{
		if (Impl->Update(*this))
		{
			UE_LOG(LogMagicLeap, Log, 
				TEXT("PostEditChangeProperty is changing MLMeshingSettings"));
		}
	}
#endif //WITH_MLSDK

	Super::PostEditChangeProperty(e);
}
#endif

void UMeshTrackerComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

#if WITH_MLSDK
	if (!MRMesh)
	{
		return;
	}

	if (!IMagicLeapPlugin::Get().IsMagicLeapHMDValid())
	{
		return;
	}

	const FAppFramework& AppFramework = 
		static_cast<FMagicLeapHMD *>(GEngine->XRSystem->GetHMDDevice())->GetAppFrameworkConst();

	if (!Impl->Create(*this))
	{
		return;
	}

	// Dont use the bool() operator from TScriptInterface class since it only checks for the InterfacePointer. 
	// Since the InterfacePointer is null for it's blueprint implementors, bool() operator gives us the wrong result for checking if interface is valid.
	if (Impl->BlockSelector.GetObject() == nullptr)
	{
		UE_LOG(LogMagicLeap, Warning, TEXT("No block selector is connected, using default implementation."));
		Impl->BlockSelector = this;
	}

	// Update the bounding box.
	const FTransform WorldToTracking = UHeadMountedDisplayFunctionLibrary::GetTrackingToWorldTransform(this).Inverse();
	Impl->BoundsCenter = WorldToTracking.TransformPosition(BoundingVolume->GetComponentLocation());
	Impl->BoundsRotation = WorldToTracking.TransformRotation(BoundingVolume->GetComponentQuat());

	// Potentially update for changed component parameters
	if (Impl->Update(*this))
	{
		UE_LOG(LogMagicLeap, Log, TEXT("MLMeshingSettings changed on the fly"));
	}

	TSet<EMagicLeapHeadTrackingMapEvent> MapEvents;
	bool bHasMapEvents = UMagicLeapHMDFunctionLibrary::GetHeadTrackingMapEvents(MapEvents);
	if (bHasMapEvents)
	{
		for (EMagicLeapHeadTrackingMapEvent MapEvent : MapEvents)
		{
			if (MapEvent == EMagicLeapHeadTrackingMapEvent::NewSession)
			{
				// Clear existing meshes if a new headpose session has started.
				MRMesh->Clear();
			}
		}
	}

	const float WorldToMetersScale = AppFramework.GetWorldToMetersScale();

	// Make sure MR Mesh is at 0,0,0 (verts received from ML meshing are in tracking space)
	MRMesh->SendRelativeTransform(FTransform::Identity);

	if (ScanWorld)
	{
		if (GetMeshResult())
		{
			RequestMeshInfo();
			if (GetMeshInfoResult())
			{
				RequestMesh();
			}
		}

		if (BricksPerFrame > 0)
		{
			// Load-balancing of MR Mesh brick creation. It is recommended that applications use the GetNumQueuedBlockUpdates
			// function and experimentation to fine-tune this number for their application.
			auto PendingIter = Impl->PendingMeshBricksByBrickId.CreateConstIterator();
			int32 numBricksProcessed;
			for (numBricksProcessed = 0; numBricksProcessed < BricksPerFrame && PendingIter; ++numBricksProcessed)
			{
				auto CachedMeshData = PendingIter.Value();
				++PendingIter; // Assuming this needs to be done before removing the entry it points to

#ifdef DEBUG_MESH_BLOCK_ADD_REMOVE
				if (CachedMeshData->WorldVertices.Num() > 0)
				{
					UE_LOG(LogMagicLeap,
						Log,
						TEXT("UMeshTrackerComponent: ADDING/UPDATING brick %s"),
						*(CachedMeshData->BlockID.ToString()));
				}
				else
				{
					UE_LOG(LogMagicLeap,
						Log,
						TEXT("UMeshTrackerComponent: REMOVING brick %s"),
						*(CachedMeshData->BlockID.ToString()));
				}
#endif //DEBUG_MESH_BLOCK_ADD_REMOVE
				// Broadcast that a mesh was updated
				if (OnMeshTrackerUpdated.IsBound())
				{
					// Hack because blueprints don't support uint32.
					TArray<int32> Triangles(reinterpret_cast<const int32*>(CachedMeshData->
						Triangles.GetData()), CachedMeshData->Triangles.Num());
					OnMeshTrackerUpdated.Broadcast(CachedMeshData->BlockID,
						CachedMeshData->OffsetVertices, Triangles, CachedMeshData->Normals,
						CachedMeshData->Confidence);
				}

				// Remove it from the pending list
				Impl->PendingMeshBricksByBrickId.Remove(CachedMeshData->BrickID);

				if (MeshType != EMagicLeapMeshType::PointCloud)
				{
					// Create/update brick
					static_cast<IMRMesh*>(MRMesh)->SendBrickData(IMRMesh::FSendBrickDataArgs
						{
							TSharedPtr<IMRMesh::FBrickDataReceipt, ESPMode::ThreadSafe>
							(new FMagicLeapMeshTrackerImpl::FMeshTrackerComponentBrickDataReceipt(CachedMeshData)),
							CachedMeshData->BrickID,
							CachedMeshData->WorldVertices,
							CachedMeshData->UV0,
							CachedMeshData->Tangents,
							CachedMeshData->VertexColors,
							CachedMeshData->Triangles,
							FBox(CachedMeshData->BlockInfo.BlockPosition - CachedMeshData->BlockInfo.BlockDimensions,
								 CachedMeshData->BlockInfo.BlockPosition + CachedMeshData->BlockInfo.BlockDimensions)
						});
				}
				else
				{
					// Discard
					CachedMeshData->Recycle(CachedMeshData);
				}
			}
		}
		else
		{
			// Clear pending queries as it will not be drained
			Impl->PendingMeshBricksByBrickId.Reset();
		}
	}
	else
	{
		// Clear pending queries as it will not be drained
		Impl->PendingMeshBricksByBrickId.Reset();
	}
#endif //WITH_MLSDK

}

void UMeshTrackerComponent::SelectMeshBlocks_Implementation(const FMagicLeapTrackingMeshInfo& NewMeshInfo, TArray<FMagicLeapMeshBlockRequest>& RequestedMesh)
{
	for (const FMagicLeapMeshBlockInfo& BlockInfo : NewMeshInfo.BlockData)
	{
		FMagicLeapMeshBlockRequest BlockRequest;
		BlockRequest.BlockID = BlockInfo.BlockID;
		BlockRequest.LevelOfDetail = LevelOfDetail;
		RequestedMesh.Add(BlockRequest);
	}
}

void UMeshTrackerComponent::ConnectBlockSelector(TScriptInterface<IMagicLeapMeshBlockSelectorInterface> Selector)
{
	if (Impl != nullptr)
	{
		// Dont use the bool() operator from TScriptInterface class since it only checks for the InterfacePointer. 
		// Since the InterfacePointer is null for it's blueprint implementors, bool() operator gives us the wrong result for checking if interface is valid.
		if (Selector.GetObject() != nullptr)
		{
			// If called via C++, Selector might have been created manually and not implement IMagicLeapMeshBlockSelectorInterface.
			if (Selector.GetObject()->GetClass()->ImplementsInterface(UMagicLeapMeshBlockSelectorInterface::StaticClass()))
			{
				Impl->BlockSelector = Selector;
			}
			else
			{
				UE_LOG(LogMagicLeap, Warning, TEXT("Selector %s does not implement IMagicLeapMeshBlockSelectorInterface. Using default block selector from MeshTrackerComponent."), *(Selector.GetObject()->GetFName().ToString()));
				Impl->BlockSelector = this;	
			}
		}
		else
		{
			UE_LOG(LogMagicLeap, Warning, TEXT("Invalid selector passed to UMeshTrackerComponent::ConnectBlockSelector(). Using default block selector from MeshTrackerComponent."));
			Impl->BlockSelector = this;	
		}
	}
}

void UMeshTrackerComponent::DisconnectBlockSelector()
{
	if (Impl != nullptr)
	{
		Impl->BlockSelector = this;
	}
}

int32 UMeshTrackerComponent::GetNumQueuedBlockUpdates()
{
	return Impl->PendingMeshBricksByBrickId.Num();
}

void UMeshTrackerComponent::BeginDestroy()
{
	if (MRMesh != nullptr)
	{
		DisconnectMRMesh(MRMesh);
	}
	Super::BeginDestroy();
}

void UMeshTrackerComponent::FinishDestroy()
{
#if WITH_EDITOR
	if (GIsEditor)
	{
		FEditorDelegates::PrePIEEnded.RemoveAll(this);
	}
#endif
	Impl->Destroy();
	Super::FinishDestroy();
}

void UMeshTrackerComponent::RequestMeshInfo()
{
#if WITH_MLSDK
	const FAppFramework& AppFramework = static_cast<FMagicLeapHMD *>(GEngine->XRSystem->GetHMDDevice())->GetAppFrameworkConst();
	const float WorldToMetersScale = AppFramework.GetWorldToMetersScale();

	// Request mesh info as frequently as possible.
	// Actual request for mesh will be submitted based on the latest available info at the time of triggering the mesh request.
	if (Impl->CurrentMeshInfoRequest == ML_INVALID_HANDLE)
	{
		MLMeshingExtents Extents = {};
		Extents.center = MagicLeap::ToMLVector(Impl->BoundsCenter, WorldToMetersScale);
		Extents.rotation = MagicLeap::ToMLQuat(Impl->BoundsRotation);
		// The C-API extents are 'full' extents - width, height, and depth. UE4 boxes are 'half' extents.
		Extents.extents = MagicLeap::ToMLVectorExtents(BoundingVolume->GetScaledBoxExtent() * 2, WorldToMetersScale);

		auto Result = MLMeshingRequestMeshInfo(Impl->MeshTracker, &Extents, &Impl->CurrentMeshInfoRequest);
		if (MLResult_Ok != Result)
		{
			UE_LOG(LogMagicLeap, Error, TEXT("MLMeshingRequestMeshInfo failed: %s."), UTF8_TO_TCHAR(MLGetResultString(Result)));
			Impl->CurrentMeshInfoRequest = ML_INVALID_HANDLE;
		}
	}
#endif // WITH_MLSDK
}

bool UMeshTrackerComponent::GetMeshInfoResult()
{
#if WITH_MLSDK
	const FAppFramework& AppFramework = static_cast<FMagicLeapHMD *>(GEngine->XRSystem->GetHMDDevice())->GetAppFrameworkConst();
	const float WorldToMetersScale = AppFramework.GetWorldToMetersScale();

	// Check for updated mesh info and cache the result.
	// The cached result will be used by app to choose which blocks it wants to actually request the mesh for.
	if (Impl->CurrentMeshInfoRequest != ML_INVALID_HANDLE)
	{
		MLMeshingMeshInfo MeshInfo = {};

		auto Result = MLMeshingGetMeshInfoResult(Impl->MeshTracker, Impl->CurrentMeshInfoRequest, &MeshInfo);
		if (MLResult_Ok != Result)
		{
			// Just silently wait for pending result
			if (MLResult_Pending != Result)
			{
				UE_LOG(LogMagicLeap, Error,
					TEXT("MLMeshingGetMeshInfoResult failed: %s."), UTF8_TO_TCHAR(MLGetResultString(Result)));
				Impl->CurrentMeshInfoRequest = ML_INVALID_HANDLE;
				return true;
			}
			return false;
		}
		else
		{
			// Clear our stored block requests
			Impl->LatestMeshInfo.BlockInfoByGuid.Empty(MeshInfo.data_count);
			Impl->LatestMeshInfo.BlockData.Empty(MeshInfo.data_count);
			Impl->LatestMeshInfo.BlockData.AddUninitialized(MeshInfo.data_count);
			Impl->LatestMeshInfo.Timestamp = FTimespan::FromMicroseconds(MeshInfo.timestamp / 1000.0);

			uint32_t MeshIndex = 0;
			for (uint32_t DataIndex = 0; DataIndex < MeshInfo.data_count; ++DataIndex)
			{
				const auto &MeshInfoData = MeshInfo.data[DataIndex];

				switch (MeshInfoData.state)
				{
					case MLMeshingMeshState_Unchanged:
					case MLMeshingMeshState_New:
					case MLMeshingMeshState_Updated:
					{
						auto BlockID = FGuid(MeshInfoData.id.data[0],
											 MeshInfoData.id.data[0] >> 32,
											 MeshInfoData.id.data[1],
											 MeshInfoData.id.data[1] >> 32);

						if (MeshInfoData.state == MLMeshingMeshState_Unchanged)
						{
							// Make sure we have actually received this brick before considering
							// it unchanged
							auto MeshBrickId = Impl->GetMrMeshIdFromMeshGuid(BlockID, false);
							if (MeshBrickId != nullptr)
							{
								break;
							}

#ifdef DEBUG_MESH_REQUEST_AND_RESPONSE
							UE_LOG(LogMagicLeap,
								Log,
								TEXT("UMeshTrackerComponent: Received 'unchanged' event for unseen block %s"),
								*(BlockID.ToString()));
#endif
						}

						// Convert and add the block info, and map it by GUID
						auto& BlockInfo = Impl->LatestMeshInfo.BlockData[MeshIndex++];
						MLToUnrealBlockInfo(MeshInfoData,
							UHeadMountedDisplayFunctionLibrary::GetTrackingToWorldTransform(this),
							WorldToMetersScale,
							BlockInfo);

						// It's new to us
						BlockInfo.BlockState = EMagicLeapMeshState::New;

						Impl->LatestMeshInfo.BlockInfoByGuid.Add(BlockID, BlockInfo);
						break;
					}
					case MLMeshingMeshState_Deleted:
					{
						const auto BlockID = FGuid(MeshInfoData.id.data[0],
												   MeshInfoData.id.data[0] >> 32,
												   MeshInfoData.id.data[1],
												   MeshInfoData.id.data[1] >> 32);

						// Only process delete for blocks for which we actually received data
						auto MeshBrickId = Impl->GetMrMeshIdFromMeshGuid(BlockID, false);
						if (MeshBrickId != nullptr)
						{
#ifdef DEBUG_MESH_BLOCK_ADD_REMOVE
							UE_LOG(LogMagicLeap,
								Log,
								TEXT("UMeshTrackerComponent: REMOVING brick %s"),
								*(BlockID.ToString()));
#endif //DEBUG_MESH_BLOCK_ADD_REMOVE
							// Broadcast the mesh was removed
							if (OnMeshTrackerUpdated.IsBound())
							{
								OnMeshTrackerUpdated.Broadcast(BlockID,
									TArray<FVector>(), TArray<int32>(), TArray<FVector>(), TArray<float>());
							}

							// Remove any pending block creations/updates
							Impl->PendingMeshBricksByBrickId.Remove(*MeshBrickId);

							if (MeshType != EMagicLeapMeshType::PointCloud)
							{
								// Remove brick
								const static TArray<FVector> EmptyVertices;
								const static TArray<FVector2D> EmptyUVs;
								const static TArray<FPackedNormal> EmptyTangents;
								const static TArray<FColor> EmptyVertexColors;
								const static TArray<uint32> EmptyTriangles;
								static_cast<IMRMesh*>(MRMesh)->SendBrickData(IMRMesh::FSendBrickDataArgs
								{
									nullptr,
									*MeshBrickId,
									EmptyVertices,
									EmptyUVs,
									EmptyTangents,
									EmptyVertexColors,
									EmptyTriangles
								});
							}
						}
						else
						{
#ifdef DEBUG_MESH_REQUEST_AND_RESPONSE
							UE_LOG(LogMagicLeap,
								Log,
								TEXT("UMeshTrackerComponent: Received 'deleted' event for unseen block %s"),
								*(BlockID.ToString()));
#endif
						}
						break;
					}
					default:
						break;
				}
			}

			// We probably discarded some, so reduce count to the actual size
			Impl->LatestMeshInfo.BlockData.SetNum(MeshIndex, true);

			// Free up the ML meshing resources
			MLMeshingFreeResource(Impl->MeshTracker, &Impl->CurrentMeshInfoRequest);
			Impl->CurrentMeshInfoRequest = ML_INVALID_HANDLE;

			return true;
		}
	}
#endif

	// if it reaches here, something has gone wrong
	return false;
}

void UMeshTrackerComponent::RequestMesh()
{
#if WITH_MLSDK
	// Request block meshes for current mesh info and block list
	if (Impl->CurrentMeshRequest == ML_INVALID_HANDLE)
	{
		Impl->UEMeshBlockRequests.Empty(Impl->LatestMeshInfo.BlockData.Num());

		// Allow applications to choose which of the available blocks to mesh.
		IMagicLeapMeshBlockSelectorInterface::Execute_SelectMeshBlocks(Impl->BlockSelector.GetObject(), Impl->LatestMeshInfo, Impl->UEMeshBlockRequests);

		if (Impl->UEMeshBlockRequests.Num() > 0)
		{
			// Convert selected block requests to ML format
			Impl->MeshBlockRequests.Empty(Impl->UEMeshBlockRequests.Num());
			Impl->MeshBlockRequests.AddUninitialized(Impl->UEMeshBlockRequests.Num());
			for (int32 i = 0; i < Impl->UEMeshBlockRequests.Num(); ++i)
			{
				UnrealToMLBlockRequest(Impl->UEMeshBlockRequests[i], Impl->MeshBlockRequests[i]);
			}

			// Submit query
			MLMeshingMeshRequest MeshRequest = {};
			MeshRequest.request_count = static_cast<int>(Impl->MeshBlockRequests.Num());
			MeshRequest.data = Impl->MeshBlockRequests.GetData();
			auto Result = MLMeshingRequestMesh(Impl->MeshTracker, &MeshRequest, &Impl->CurrentMeshRequest);
			if (MLResult_Ok != Result)
			{
				UE_LOG(LogMagicLeap, Error, TEXT("MLMeshingRequestMesh failed: %s."), UTF8_TO_TCHAR(MLGetResultString(Result)));
				Impl->CurrentMeshInfoRequest = ML_INVALID_HANDLE;
			}
		}
	}
#endif
}

bool UMeshTrackerComponent::GetMeshResult()
{
#if WITH_MLSDK
	const FAppFramework& AppFramework = static_cast<FMagicLeapHMD *>(GEngine->XRSystem->GetHMDDevice())->GetAppFrameworkConst();
	const float WorldToMetersScale = AppFramework.GetWorldToMetersScale();

	// Get mesh result
	if (Impl->CurrentMeshRequest != ML_INVALID_HANDLE)
	{
		MLMeshingMesh Mesh = {};

		auto Result = MLMeshingGetMeshResult(Impl->MeshTracker, Impl->CurrentMeshRequest, &Mesh);

		if (MLResult_Ok != Result)
		{
			// Just silently wait for pending result
			if (MLResult_Pending != Result)
			{
				UE_LOG(LogMagicLeap, Error, TEXT("MLMeshingGetMeshResult failed: %s."), UTF8_TO_TCHAR(MLGetResultString(Result)));
				// Mesh request failed, lets queue another one.
				Impl->CurrentMeshRequest = ML_INVALID_HANDLE;
				return true;
			}
			// Mesh request pending...
			return false;
		}
		else
		{
			// Translate mesh block data
			const FTransform TrackingToWorld = UHeadMountedDisplayFunctionLibrary::GetTrackingToWorldTransform(nullptr);
			for (uint32_t MeshIndex = 0; MeshIndex < Mesh.data_count; ++ MeshIndex)
			{
				const auto &MeshData = Mesh.data[MeshIndex];

				auto BlockID = FGuid(MeshData.id.data[0],
					MeshData.id.data[0] >> 32, MeshData.id.data[1], MeshData.id.data[1] >> 32);

				// Get the block info
				auto pendingBlockInfo = Impl->LatestMeshInfo.BlockInfoByGuid.Find(BlockID);

				// Simulator can return unrequested meshes, so we cannot currently checkf
				// https://jira.magicleap.com/browse/REM-3259
				//checkf(pendingBlockInfo != nullptr, TEXT("Unable to find block info for pending mesh"));
				if (pendingBlockInfo == nullptr)
				{
#ifdef DEBUG_MESH_REQUEST_AND_RESPONSE
					UE_LOG(LogMagicLeap,
						Log,
						TEXT("UMeshTrackerComponent: Received unrequested mesh with ID %s"),
						*(BlockID.ToString()));
#endif
					continue;
				}

				// Remove them as we receive them so we can tell if we did not receive some requested blocks
				Impl->LatestMeshInfo.BlockInfoByGuid.Remove(BlockID);

				// Acquire mesh data cache and mark its ML block ID and UE brick ID
				FMagicLeapMeshTrackerImpl::FMLCachedMeshData::SharedPtr CurrentMeshDataCache = Impl->AcquireMeshDataCache();
				CurrentMeshDataCache->BlockID = BlockID;
				CurrentMeshDataCache->BrickID = *Impl->GetMrMeshIdFromMeshGuid(CurrentMeshDataCache->BlockID, true);

				// Copy over the block info
				CurrentMeshDataCache->BlockInfo = *pendingBlockInfo;

				// Pull vertices
				CurrentMeshDataCache->OffsetVertices.Reserve(MeshData.vertex_count);
				CurrentMeshDataCache->WorldVertices.Reserve(MeshData.vertex_count);
				for (uint32_t v = 0; v < MeshData.vertex_count; ++ v)
				{
					const FVector WorldVertex = TrackingToWorld.TransformPosition(MagicLeap::ToFVector(MeshData.vertex[v], WorldToMetersScale));
					CurrentMeshDataCache->OffsetVertices.Add(WorldVertex);
					CurrentMeshDataCache->WorldVertices.Add(MagicLeap::ToFVector(MeshData.vertex[v], WorldToMetersScale));
				}

				// Pull indices
				CurrentMeshDataCache->Triangles.Reserve(MeshData.index_count);
				for (uint16_t i = 0; i < MeshData.index_count; ++ i)
				{
					CurrentMeshDataCache->Triangles.Add(static_cast<uint32>(MeshData.index[i]));
				}

				// Pull normals
				CurrentMeshDataCache->Normals.Reserve(MeshData.vertex_count);
				if (nullptr != MeshData.normal)
				{
					for (uint32_t n = 0; n < MeshData.vertex_count; ++ n)
					{
						CurrentMeshDataCache->Normals.Add(MagicLeap::ToFVectorNoScale(MeshData.normal[n]));
					}
				}
				// If no normals were provided we need to pack fake ones for Vulkan
				else
				{
					for (uint32_t n = 0; n < MeshData.vertex_count; ++ n)
					{
						FVector FakeNormal = CurrentMeshDataCache->OffsetVertices[n];
						FakeNormal.Normalize();
						CurrentMeshDataCache->Normals.Add(FakeNormal);
					}
				}

				// Calculate and pack tangents
				CurrentMeshDataCache->Tangents.Reserve(MeshData.vertex_count * 2);
				for (uint32_t t = 0; t < MeshData.vertex_count; ++ t)
				{
					const FVector& Norm = CurrentMeshDataCache->Normals[t];

					// Calculate tangent
					auto Perp = Norm.X < Norm.Z ? 
						FVector(1.0f, 0.0f, 0.0f) : FVector(0.0f, 1.0f, 0.0f);
					auto Tang = FVector::CrossProduct(Norm, Perp);

					CurrentMeshDataCache->Tangents.Add(Tang);
					CurrentMeshDataCache->Tangents.Add(Norm);
				}

				// Pull confidence
				if (nullptr != MeshData.confidence)
				{
					CurrentMeshDataCache->Confidence.Append(MeshData.confidence, MeshData.vertex_count);
				}

				// Apply chosen vertex color mode
				switch (VertexColorMode)
				{
					case EMagicLeapMeshVertexColorMode::Confidence:
					{
						if (nullptr != MeshData.confidence)
						{
							CurrentMeshDataCache->VertexColors.Reserve(MeshData.vertex_count);
							for (uint32 v = 0; v < MeshData.vertex_count; ++ v)
							{
								const FLinearColor VertexColor = FMath::Lerp(VertexColorFromConfidenceZero, 
									VertexColorFromConfidenceOne, CurrentMeshDataCache->Confidence[v]);
								CurrentMeshDataCache->VertexColors.Add(VertexColor.ToFColor(false));
							}
						}
						else
						{
							/* TODO: Replace with log once: SDKUNREAL-870
							UE_LOG(LogMagicLeap, Warning, TEXT("MeshTracker vertex color mode is Confidence "
								"but no confidence values available. Using white for all blocks."));
							*/
						}
						break;
					}
					case EMagicLeapMeshVertexColorMode::Block:
					{
						if (BlockVertexColors.Num() > 0)
						{
							const FColor& VertexColor = BlockVertexColors[CurrentMeshDataCache->BrickID % BlockVertexColors.Num()];

							CurrentMeshDataCache->VertexColors.Reserve(MeshData.vertex_count);
							for (uint32 v = 0; v < MeshData.vertex_count; ++ v)
							{
								CurrentMeshDataCache->VertexColors.Add(VertexColor);
							}
						}
						else
						{
							UE_LOG(LogMagicLeap, Warning, TEXT("MeshTracker vertex color mode is Block but "
								"no BlockVertexColors set. Using white for all blocks."));
						}
						break;
					}
					case EMagicLeapMeshVertexColorMode::LOD:
					{
						if (BlockVertexColors.Num() >= MLMeshingLOD_Maximum)
						{
							const FColor& VertexColor = BlockVertexColors[MeshData.level];

							CurrentMeshDataCache->VertexColors.Reserve(MeshData.vertex_count);
							for (uint32 v = 0; v < MeshData.vertex_count; ++ v)
							{
								CurrentMeshDataCache->VertexColors.Add(VertexColor);
							}
						}
						else
						{
							UE_LOG(LogMagicLeap, Warning, TEXT("MeshTracker vertex color mode is LOD but "
								"BlockVertexColors are less then the number of LODs. Using white for all blocks."));
						}
						break;
					}
					case EMagicLeapMeshVertexColorMode::None:
					{
						break;
					}
					default:
						check(false);
						break;
				}

				// To work in all rendering paths we always set a vertex color
				if (CurrentMeshDataCache->VertexColors.Num() == 0)
				{
					for (uint32 v = 0; v < MeshData.vertex_count; ++ v)
					{
						CurrentMeshDataCache->VertexColors.Add(FColor::White);
					}
				}

				// Write UVs
				CurrentMeshDataCache->UV0.Reserve(MeshData.vertex_count);
				for (uint32 v = 0; v < MeshData.vertex_count; ++ v)
				{
					const float FakeCoord = static_cast<float>(v) / static_cast<float>(MeshData.vertex_count);
					CurrentMeshDataCache->UV0.Add(FVector2D(FakeCoord, FakeCoord));
				}

				if (BricksPerFrame > 0)
				{
					Impl->PendingMeshBricksByBrickId.Add(CurrentMeshDataCache->BrickID, CurrentMeshDataCache);
				}
				else
				{
#ifdef DEBUG_MESH_BLOCK_ADD_REMOVE
					UE_LOG(LogMagicLeap,
						Log,
						TEXT("UMeshTrackerComponent: ADDING/UPDATING brick %s"),
						*(BlockID.ToString()));
#endif //DEBUG_MESH_BLOCK_ADD_REMOVE
					// Broadcast that a mesh was updated
					if (OnMeshTrackerUpdated.IsBound())
					{
						// Hack because blueprints don't support uint32.
						TArray<int32> Triangles(reinterpret_cast<const int32*>(CurrentMeshDataCache->
							Triangles.GetData()), CurrentMeshDataCache->Triangles.Num());
						OnMeshTrackerUpdated.Broadcast(CurrentMeshDataCache->BlockID, CurrentMeshDataCache->OffsetVertices,
							Triangles, CurrentMeshDataCache->Normals, CurrentMeshDataCache->Confidence);
					}

					if (MeshType != EMagicLeapMeshType::PointCloud)
					{
						// Create/update brick
						static_cast<IMRMesh*>(MRMesh)->SendBrickData(IMRMesh::FSendBrickDataArgs
							{
								TSharedPtr<IMRMesh::FBrickDataReceipt, ESPMode::ThreadSafe>
									(new FMagicLeapMeshTrackerImpl::FMeshTrackerComponentBrickDataReceipt(CurrentMeshDataCache)),
									CurrentMeshDataCache->BrickID,
									CurrentMeshDataCache->WorldVertices,
									CurrentMeshDataCache->UV0,
									CurrentMeshDataCache->Tangents,
									CurrentMeshDataCache->VertexColors,
									CurrentMeshDataCache->Triangles,
									FBox(CurrentMeshDataCache->BlockInfo.BlockPosition - CurrentMeshDataCache->BlockInfo.BlockDimensions,
										 CurrentMeshDataCache->BlockInfo.BlockPosition + CurrentMeshDataCache->BlockInfo.BlockDimensions)
							});
					}
					else
					{
						// Discard
						CurrentMeshDataCache->Recycle(CurrentMeshDataCache);
					}
				}
			}

#ifdef DEBUG_MESH_REQUEST_AND_RESPONSE
			if (Impl->LatestMeshInfo.BlockInfoByGuid.Num() != 0)
			{
				UE_LOG(LogMagicLeap,
					Log,
					TEXT("UMeshTrackerComponent: Failed to receive meshes for %d requests:"),
					Impl->LatestMeshInfo.BlockInfoByGuid.Num());
				for (const auto& BlockInfo : Impl->LatestMeshInfo.BlockInfoByGuid)
				{
					UE_LOG(LogMagicLeap,
						Log,
						TEXT("UMeshTrackerComponent: No data received for %s"),
						*BlockInfo.Key.ToString());
				}
			}
#endif

			// All meshes pulled and/or updated; free the ML resource
			MLMeshingFreeResource(Impl->MeshTracker, &Impl->CurrentMeshRequest);
			Impl->CurrentMeshRequest = ML_INVALID_HANDLE;

			return true;
		}
	}
#endif

	return true;
}

#if WITH_EDITOR
void UMeshTrackerComponent::PrePIEEnded(bool bWasSimulatingInEditor)
{
	Impl->Destroy();
}
#endif
