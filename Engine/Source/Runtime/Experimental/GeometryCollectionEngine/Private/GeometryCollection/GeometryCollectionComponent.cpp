// Copyright Epic Games, Inc. All Rights Reserved.

#include "GeometryCollection/GeometryCollectionComponent.h"

#include "Async/ParallelFor.h"
#include "Components/BoxComponent.h"
#include "GeometryCollection/GeometryCollectionObject.h"
#include "GeometryCollection/GeometryCollectionAlgo.h"
#include "GeometryCollection/GeometryCollectionComponentPluginPrivate.h"
#include "GeometryCollection/GeometryCollectionSceneProxy.h"
#include "GeometryCollection/GeometryCollectionSQAccelerator.h"
#include "GeometryCollection/GeometryCollectionUtility.h"
#include "GeometryCollection/GeometryCollectionClusteringUtility.h"
#include "GeometryCollection/GeometryCollectionCache.h"
#include "GeometryCollection/GeometryCollectionActor.h"
#include "GeometryCollection/GeometryCollectionDebugDrawComponent.h"
#include "Physics/Experimental/PhysScene_Chaos.h"
#include "Modules/ModuleManager.h"
#include "ChaosSolversModule.h"
#include "ChaosStats.h"
#include "PhysicsProxy/GeometryCollectionPhysicsProxy.h"
#include "PhysicsSolver.h"
#include "Physics/PhysicsFiltering.h"
#include "Chaos/ChaosPhysicalMaterial.h"
#include "AI/NavigationSystemHelpers.h"
#include "Net/UnrealNetwork.h"
#include "Net/Core/PushModel/PushModel.h"
#include "PhysicalMaterials/PhysicalMaterial.h"

#if WITH_EDITOR
#include "AssetToolsModule.h"
#include "Editor.h"
#endif

#include "PhysicsEngine/BodySetup.h"
#include "PhysicsEngine/BodyInstance.h"
#include "Chaos/ChaosGameplayEventDispatcher.h"

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
#include "Logging/MessageLog.h"
#include "Misc/UObjectToken.h"
#endif //!(UE_BUILD_SHIPPING || UE_BUILD_TEST)

#if INTEL_ISPC

#if USING_CODE_ANALYSIS
MSVC_PRAGMA(warning(push))
MSVC_PRAGMA(warning(disable : ALL_CODE_ANALYSIS_WARNINGS))
#endif    // USING_CODE_ANALYSIS

#include "GeometryCollectionComponent.ispc.generated.h"

#if USING_CODE_ANALYSIS
MSVC_PRAGMA(warning(pop))
#endif    // USING_CODE_ANALYSIS

#endif

DEFINE_LOG_CATEGORY_STATIC(UGCC_LOG, Error, All);

FString NetModeToString(ENetMode InMode)
{
	switch(InMode)
	{
	case ENetMode::NM_Client:
		return FString("Client");
	case ENetMode::NM_DedicatedServer:
		return FString("DedicatedServer");
	case ENetMode::NM_ListenServer:
		return FString("ListenServer");
	case ENetMode::NM_Standalone:
		return FString("Standalone");
	default:
		break;
	}

	return FString("INVALID NETMODE");
}

FString RoleToString(ENetRole InRole)
{
	switch(InRole)
	{
	case ROLE_None:
		return FString(TEXT("None"));
	case ROLE_SimulatedProxy:
		return FString(TEXT("SimProxy"));
	case ROLE_AutonomousProxy:
		return FString(TEXT("AutoProxy"));
	case ROLE_Authority:
		return FString(TEXT("Auth"));
	default:
		break;
	}

	return FString(TEXT("Invalid Role"));
}

int32 GetClusterLevel(const FTransformCollection* Collection, int32 TransformGroupIndex)
{
	int32 Level = 0;
	while(Collection && Collection->Parent[TransformGroupIndex] != -1)
	{
		TransformGroupIndex = Collection->Parent[TransformGroupIndex];
		Level++;
	}
	return Level;
}

#if WITH_PHYSX && !WITH_CHAOS_NEEDS_TO_BE_FIXED
FGeometryCollectionSQAccelerator GlobalGeomCollectionAccelerator;	//todo(ocohen): proper lifetime management needed

void HackRegisterGeomAccelerator(UGeometryCollectionComponent& Component)
{
#if TODO_REIMPLEMENT_SCENEQUERY_CROSSENGINE
	if (UWorld* World = Component.GetWorld())
	{
		if (FPhysScene* PhysScene = World->GetPhysicsScene())
		{
			if (FSQAcceleratorUnion* SQAccelerationUnion = PhysScene->GetSQAcceleratorUnion())
			{
				SQAccelerationUnion->AddSQAccelerator(&GlobalGeomCollectionAccelerator);
			}
		}
	}
#endif
}
#endif

bool FGeometryCollectionRepData::Identical(const FGeometryCollectionRepData* Other, uint32 PortFlags) const
{
	return Other && (Version == Other->Version);
}

bool FGeometryCollectionRepData::NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess)
{
	bOutSuccess = true;

	Ar << Version;

	int32 NumPoses = Poses.Num();
	Ar << NumPoses;

	if(Ar.IsLoading())
	{
		Poses.SetNum(NumPoses);
	}

	for(FGeometryCollectionRepPose& Pose : Poses)
	{
		SerializePackedVector<100, 30>(Pose.Position, Ar);
		SerializePackedVector<100, 30>(Pose.LinearVelocity, Ar);
		SerializePackedVector<100, 30>(Pose.AngularVelocity, Ar);
		Pose.Rotation.NetSerialize(Ar, Map, bOutSuccess);
		Ar << Pose.ParticleIndex;
	}

	return true;
}

// Size in CM used as a threshold for whether a geometry in the collection is collected and exported for
// navigation purposes. Measured as the diagonal of the leaf node bounds.
float GGeometryCollectionNavigationSizeThreshold = 20.0f;
FAutoConsoleVariableRef CVarGeometryCollectionNavigationSizeThreshold(TEXT("p.GeometryCollectionNavigationSizeThreshold"), GGeometryCollectionNavigationSizeThreshold, TEXT("Size in CM used as a threshold for whether a geometry in the collection is collected and exported for navigation purposes. Measured as the diagonal of the leaf node bounds."));

FGeomComponentCacheParameters::FGeomComponentCacheParameters()
	: CacheMode(EGeometryCollectionCacheType::None)
	, TargetCache(nullptr)
	, ReverseCacheBeginTime(0.0f)
	, SaveCollisionData(false)
	, DoGenerateCollisionData(false)
	, CollisionDataSizeMax(512)
	, DoCollisionDataSpatialHash(false)
	, CollisionDataSpatialHashRadius(50.f)
	, MaxCollisionPerCell(1)
	, SaveBreakingData(false)
	, DoGenerateBreakingData(false)
	, BreakingDataSizeMax(512)
	, DoBreakingDataSpatialHash(false)
	, BreakingDataSpatialHashRadius(50.f)
	, MaxBreakingPerCell(1)
	, SaveTrailingData(false)
	, DoGenerateTrailingData(false)
	, TrailingDataSizeMax(512)
	, TrailingMinSpeedThreshold(200.f)
	, TrailingMinVolumeThreshold(10000.f)
{
}

UGeometryCollectionComponent::UGeometryCollectionComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, ChaosSolverActor(nullptr)
	, Simulating(true)
	, InitializationState(ESimulationInitializationState::Unintialized)
	, ObjectType(EObjectStateTypeEnum::Chaos_Object_Dynamic)
	, EnableClustering(true)
	, ClusterGroupIndex(0)
	, MaxClusterLevel(100)
	, DamageThreshold({250.0})
	, ClusterConnectionType(EClusterConnectionTypeEnum::Chaos_PointImplicit)
	, CollisionGroup(0)
	, CollisionSampleFraction(1.0)
	, InitialVelocityType(EInitialVelocityTypeEnum::Chaos_Initial_Velocity_User_Defined)
	, InitialLinearVelocity(0.f, 0.f, 0.f)
	, InitialAngularVelocity(0.f, 0.f, 0.f)
	, BaseRigidBodyIndex(INDEX_NONE)
	, NumParticlesAdded(0)
	, CachePlayback(false)
	, bNotifyBreaks(false)
	, bNotifyCollisions(false)
	, bEnableReplication(false)
	, bEnableAbandonAfterLevel(false)
	, ReplicationAbandonClusterLevel(0)
	, bRenderStateDirty(true)
	, bShowBoneColors(false)
	, bEnableBoneSelection(false)
	, ViewLevel(-1)
	, NavmeshInvalidationTimeSliceIndex(0)
	, IsObjectDynamic(false)
	, IsObjectLoading(true)
	, PhysicsProxy(nullptr)
#if WITH_EDITOR && WITH_EDITORONLY_DATA
	, EditorActor(nullptr)
#endif
#if GEOMETRYCOLLECTION_EDITOR_SELECTION
	, bIsTransformSelectionModeEnabled(false)
#endif  // #if GEOMETRYCOLLECTION_EDITOR_SELECTION
{
	PrimaryComponentTick.bCanEverTick = true;
	bTickInEditor = true;
	bAutoActivate = true;

	static uint32 GlobalNavMeshInvalidationCounter = 0;
	//space these out over several frames (3 is arbitrary)
	GlobalNavMeshInvalidationCounter += 3;
	NavmeshInvalidationTimeSliceIndex = GlobalNavMeshInvalidationCounter;

	WorldBounds = FBoxSphereBounds(FBox(ForceInit));	

	// default current cache time
	CurrentCacheTime = MAX_flt;

	// Buffer for rolling cache of past N=3 transforms being equal.
	TransformsAreEqual.AddDefaulted(3);
	TransformsAreEqualIndex = 0;

	SetGenerateOverlapEvents(false);

	// By default use the destructible object channel unless the user specifies otherwise
	BodyInstance.SetObjectType(ECC_Destructible);

	EventDispatcher = ObjectInitializer.CreateDefaultSubobject<UChaosGameplayEventDispatcher>(this, TEXT("GameplayEventDispatcher"));

	DynamicCollection = nullptr;
	bHasCustomNavigableGeometry = EHasCustomNavigableGeometry::Yes;

	bWantsInitializeComponent = true;
}

Chaos::FPhysicsSolver* GetSolver(const UGeometryCollectionComponent& GeometryCollectionComponent)
{
#if INCLUDE_CHAOS
	if(GeometryCollectionComponent.ChaosSolverActor)
	{
		return GeometryCollectionComponent.ChaosSolverActor->GetSolver();
	}
	else if(UWorld* CurrentWorld = GeometryCollectionComponent.GetWorld())
	{
		if(FPhysScene* Scene = CurrentWorld->GetPhysicsScene())
		{
			return Scene->GetSolver();
		}
	}
#endif
	return nullptr;
}

void UGeometryCollectionComponent::BeginPlay()
{
	Super::BeginPlay();
#if WITH_PHYSX && !WITH_CHAOS_NEEDS_TO_BE_FIXED
	HackRegisterGeomAccelerator(*this);
#endif

	//////////////////////////////////////////////////////////////////////////
	// Commenting out these callbacks for now due to the threading model. The callbacks here
	// expect the rest collection to be mutable which is not the case when running in multiple
	// threads. Ideally we have some separate animation collection or track that we cache to
	// without affecting the data we've dispatched to the physics thread
	//////////////////////////////////////////////////////////////////////////
	// ---------- SolverCallbacks->SetResetAnimationCacheFunction([&]()
	// ---------- {
	// ---------- 	FGeometryCollectionEdit Edit = EditRestCollection();
	// ---------- 	Edit.GetRestCollection()->RecordedData.SetNum(0);
	// ---------- });
	// ---------- SolverCallbacks->SetUpdateTransformsFunction([&](const TArrayView<FTransform>&)
	// ---------- {
	// ---------- 	// todo : Move the update to the array passed here...
	// ---------- });
	// ---------- 
	// ---------- SolverCallbacks->SetUpdateRestStateFunction([&](const int32 & CurrentFrame, const TManagedArray<int32> & RigidBodyID, const TManagedArray<FGeometryCollectionBoneNode>& Hierarchy, const FSolverCallbacks::FParticlesType& Particles)
	// ---------- {
	// ---------- 	FGeometryCollectionEdit Edit = EditRestCollection();
	// ---------- 	UGeometryCollection * RestCollection = Edit.GetRestCollection();
	// ---------- 	check(RestCollection);
	// ---------- 
	// ---------- 	if (CurrentFrame >= RestCollection->RecordedData.Num())
	// ---------- 	{
	// ---------- 		RestCollection->RecordedData.SetNum(CurrentFrame + 1);
	// ---------- 		RestCollection->RecordedData[CurrentFrame].SetNum(RigidBodyID.Num());
	// ---------- 		ParallelFor(RigidBodyID.Num(), [&](int32 i)
	// ---------- 		{
	// ---------- 			if (!Hierarchy[i].Children.Num())
	// ---------- 			{
	// ---------- 				RestCollection->RecordedData[CurrentFrame][i].SetTranslation(Particles.X(RigidBodyID[i]));
	// ---------- 				RestCollection->RecordedData[CurrentFrame][i].SetRotation(Particles.R(RigidBodyID[i]));
	// ---------- 			}
	// ---------- 			else
	// ---------- 			{
	// ---------- 				RestCollection->RecordedData[CurrentFrame][i].SetTranslation(FVector::ZeroVector);
	// ---------- 				RestCollection->RecordedData[CurrentFrame][i].SetRotation(FQuat::Identity);
	// ---------- 			}
	// ---------- 		});
	// ---------- 	}
	// ---------- });
	//////////////////////////////////////////////////////////////////////////

	// default current cache time
	CurrentCacheTime = MAX_flt;
}


void UGeometryCollectionComponent::EndPlay(const EEndPlayReason::Type ReasonEnd)
{
#if WITH_EDITOR && WITH_EDITORONLY_DATA
	// Track our editor component if needed for syncing simulations back from PIE on shutdown
	EditorActor = EditorUtilities::GetEditorWorldCounterpartActor(GetTypedOuter<AActor>());
#endif

	Super::EndPlay(ReasonEnd);

	CurrentCacheTime = MAX_flt;
}

void UGeometryCollectionComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	
	FDoRepLifetimeParams Params;
	Params.bIsPushBased = true;
	Params.RepNotifyCondition = REPNOTIFY_OnChanged;
	DOREPLIFETIME_WITH_PARAMS_FAST(UGeometryCollectionComponent, RepData, Params);
}

FBoxSphereBounds UGeometryCollectionComponent::CalcBounds(const FTransform& LocalToWorldIn) const
{	
	SCOPE_CYCLE_COUNTER(STAT_GCCUpdateBounds);

	// #todo(dmp): hack to make bounds calculation work when we don't have valid physics proxy data.  This will
	// force bounds calculation.

	const FGeometryCollectionResults* Results = PhysicsProxy ? PhysicsProxy->GetConsumerResultsGT() : nullptr;

	const int32 NumTransforms = Results ? Results->GlobalTransforms.Num() : 0;

	if (!CachePlayback && WorldBounds.GetSphere().W > 1e-5 && NumTransforms > 0)
	{
		return WorldBounds;
	} 
	else if (RestCollection && RestCollection->HasVisibleGeometry())
	{			
		const FMatrix LocalToWorldWithScale = LocalToWorldIn.ToMatrixWithScale();

		FBox BoundingBox(ForceInit);

		//Hold on to reference so it doesn't get GC'ed
		auto HackGeometryCollectionPtr = RestCollection->GetGeometryCollection();

		const TManagedArray<FBox>& BoundingBoxes = GetBoundingBoxArray();
		const TManagedArray<int32>& TransformIndices = GetTransformIndexArray();
		const TManagedArray<int32>& ParentIndices = GetParentArray();
		const TManagedArray<int32>& TransformToGeometryIndex = GetTransformToGeometryIndexArray();

		const int32 NumBoxes = BoundingBoxes.Num();

		// #todo(dmp): we could do the bbox transform in parallel with a bit of reformulating		
		// #todo(dmp):  there are some cases where the calcbounds function is called before the component
		// has set the global matrices cache while in the editor.  This is a somewhat weak guard against this
		// to default to just calculating tmp global matrices.  This should be removed or modified somehow
		// such that we always cache the global matrices and this method always does the correct behavior
		if (GlobalMatrices.Num() != RestCollection->NumElements(FGeometryCollection::TransformGroup))
		{
			TArray<FMatrix> TmpGlobalMatrices;
			GeometryCollectionAlgo::GlobalMatrices(GetTransformArray(), ParentIndices, TmpGlobalMatrices);

			if (TmpGlobalMatrices.Num() == 0)
			{
				return FBoxSphereBounds(ForceInitToZero);
			}

			for (int32 BoxIdx = 0; BoxIdx < NumBoxes; ++BoxIdx)
			{
				const int32 TransformIndex = TransformIndices[BoxIdx];

				if(RestCollection->GetGeometryCollection()->IsGeometry(TransformIndex))
				{
					BoundingBox += BoundingBoxes[BoxIdx].TransformBy(TmpGlobalMatrices[TransformIndex] * LocalToWorldWithScale);
				}
			}
		}
		else
		{
#if INTEL_ISPC
			ispc::BoxCalcBounds(
				(int32 *)&TransformToGeometryIndex[0],
				(int32 *)&TransformIndices[0],
				(ispc::FMatrix *)&GlobalMatrices[0],
				(ispc::FBox *)&BoundingBoxes[0],
				(ispc::FMatrix &)LocalToWorldWithScale,
				(ispc::FBox &)BoundingBox,
				NumBoxes);
#else
			for (int32 BoxIdx = 0; BoxIdx < NumBoxes; ++BoxIdx)
			{
				const int32 TransformIndex = TransformIndices[BoxIdx];

				if(RestCollection->GetGeometryCollection()->IsGeometry(TransformIndex))
				{
					BoundingBox += BoundingBoxes[BoxIdx].TransformBy(GlobalMatrices[TransformIndex] * LocalToWorldWithScale);
				}
			}
#endif
		}

		return FBoxSphereBounds(BoundingBox);
	}
	return FBoxSphereBounds(ForceInitToZero);
}

void UGeometryCollectionComponent::CreateRenderState_Concurrent(FRegisterComponentContext* Context)
{
	Super::CreateRenderState_Concurrent(Context);
}

FPrimitiveSceneProxy* UGeometryCollectionComponent::CreateSceneProxy()
{
	if(RestCollection)
	{
		FGeometryCollectionSceneProxy* NewProxy = new FGeometryCollectionSceneProxy(this);

		if(RestCollection->HasVisibleGeometry())
		{
#if GEOMETRYCOLLECTION_EDITOR_SELECTION
			// Re-init subsections
			if(bIsTransformSelectionModeEnabled)
			{
				NewProxy->UseSubSections(true, false);  // Do not force reinit now, it'll be done in SetConstantData_RenderThread
			}
#endif  // #if GEOMETRYCOLLECTION_EDITOR_SELECTION

			FGeometryCollectionConstantData* const ConstantData = ::new FGeometryCollectionConstantData;
			InitConstantData(ConstantData);

			FGeometryCollectionDynamicData* const DynamicData = ::new FGeometryCollectionDynamicData;
			InitDynamicData(DynamicData);

			// Send constant data and first dynamic data over to the proxy on the render thread
			ENQUEUE_RENDER_COMMAND(CreateRenderState)(
				[NewProxy, ConstantData, DynamicData](FRHICommandListImmediate& RHICmdList)
			{
				if(NewProxy)
				{
					NewProxy->SetConstantData_RenderThread(ConstantData);
					NewProxy->SetDynamicData_RenderThread(DynamicData);
				}
			}
			);
		}

		return NewProxy;
	}
	return nullptr;
}

bool UGeometryCollectionComponent::ShouldCreatePhysicsState() const
{
	// Geometry collections always create physics state, not relying on the
	// underlying implementation that requires the body instance to decide
	return true;
}

bool UGeometryCollectionComponent::HasValidPhysicsState() const
{
	return PhysicsProxy != nullptr;
}

void UGeometryCollectionComponent::SetNotifyBreaks(bool bNewNotifyBreaks)
{
	if (bNotifyBreaks != bNewNotifyBreaks)
	{
		bNotifyBreaks = bNewNotifyBreaks;
		UpdateBreakEventRegistration();
	}
}

FBodyInstance* UGeometryCollectionComponent::GetBodyInstance(FName BoneName /*= NAME_None*/, bool bGetWelded /*= true*/) const
{
	return nullptr;// const_cast<FBodyInstance*>(&DummyBodyInstance);
}

void UGeometryCollectionComponent::SetNotifyRigidBodyCollision(bool bNewNotifyRigidBodyCollision)
{
	Super::SetNotifyRigidBodyCollision(bNewNotifyRigidBodyCollision);
	UpdateRBCollisionEventRegistration();
}

void UGeometryCollectionComponent::DispatchBreakEvent(const FChaosBreakEvent& Event)
{
	// native
	NotifyBreak(Event);

	// bp
	if (OnChaosBreakEvent.IsBound())
	{
		OnChaosBreakEvent.Broadcast(Event);
	}
}

bool UGeometryCollectionComponent::DoCustomNavigableGeometryExport(FNavigableGeometryExport& GeomExport) const
{
	if(!RestCollection)
	{
		// No geometry data so skip export - geometry collections don't have other geometry sources
		// so return false here to skip non-custom export for this component as well.
		return false;
	}

	TArray<FVector> OutVertexBuffer;
	TArray<int32> OutIndexBuffer;

	const FGeometryCollection* const Collection = RestCollection->GetGeometryCollection().Get();
	check(Collection);

	const float SizeThreshold = GGeometryCollectionNavigationSizeThreshold * GGeometryCollectionNavigationSizeThreshold;

	// for all geometry. inspect bounding box build int list of transform indices.
	int32 VertexCount = 0;
	int32 FaceCountEstimate = 0;
	TArray<int32> GeometryIndexBuffer;
	TArray<int32> TransformIndexBuffer;

	int32 NumGeometry = Collection->NumElements(FGeometryCollection::GeometryGroup);

	const TManagedArray<FBox>& BoundingBox = Collection->BoundingBox;
	const TManagedArray<int32>& TransformIndexArray = Collection->TransformIndex;
	const TManagedArray<int32>& VertexCountArray = Collection->VertexCount;
	const TManagedArray<int32>& FaceCountArray = Collection->FaceCount;
	const TManagedArray<int32>& VertexStartArray = Collection->VertexStart;
	const TManagedArray<FVector>& Vertex = Collection->Vertex;

	for(int32 GeometryGroupIndex = 0; GeometryGroupIndex < NumGeometry; GeometryGroupIndex++)
	{
		if(BoundingBox[GeometryGroupIndex].GetSize().SizeSquared() > SizeThreshold)
		{
			TransformIndexBuffer.Add(TransformIndexArray[GeometryGroupIndex]);
			GeometryIndexBuffer.Add(GeometryGroupIndex);
			VertexCount += VertexCountArray[GeometryGroupIndex];
			FaceCountEstimate += FaceCountArray[GeometryGroupIndex];
		}
	}

	// Get all the geometry transforms in component space (they are stored natively in parent-bone space)
	TArray<FTransform> GeomToComponent;
	GeometryCollectionAlgo::GlobalMatrices(GetTransformArray(), GetParentArray(), TransformIndexBuffer, GeomToComponent);

	OutVertexBuffer.AddUninitialized(VertexCount);

	int32 DestVertex = 0;
	//for each "subset" we care about 
	for(int32 SubsetIndex = 0; SubsetIndex < GeometryIndexBuffer.Num(); ++SubsetIndex)
	{
		//find indices into the collection data
		int32 GeometryIndex = GeometryIndexBuffer[SubsetIndex];
		int32 TransformIndex = TransformIndexBuffer[SubsetIndex];
		
		int32 SourceGeometryVertexStart = VertexStartArray[GeometryIndex];
		int32 SourceGeometryVertexCount = VertexCountArray[GeometryIndex];

		ParallelFor(SourceGeometryVertexCount, [&](int32 PointIdx)
			{
				//extract vertex from source
				int32 SourceGeometryVertexIndex = SourceGeometryVertexStart + PointIdx;
				FVector const VertexInWorldSpace = GeomToComponent[SubsetIndex].TransformPosition(Vertex[SourceGeometryVertexIndex]);

				int32 DestVertexIndex = DestVertex + PointIdx;
				OutVertexBuffer[DestVertexIndex].X = VertexInWorldSpace.X;
				OutVertexBuffer[DestVertexIndex].Y = VertexInWorldSpace.Y;
				OutVertexBuffer[DestVertexIndex].Z = VertexInWorldSpace.Z;
			});

		DestVertex += SourceGeometryVertexCount;
	}

	//gather data needed for indices
	const TManagedArray<int32>& FaceStartArray = Collection->FaceStart;
	const TManagedArray<FIntVector>& Indices = Collection->Indices;
	const TManagedArray<bool>& Visible = GetVisibleArray();
	const TManagedArray<int32>& MaterialIndex = Collection->MaterialIndex;

	//pre-allocate enough room (assuming all faces are visible)
	OutIndexBuffer.AddUninitialized(3 * FaceCountEstimate);

	//reset vertex counter so that we base the indices off the new location rather than the global vertex list
	DestVertex = 0;
	int32 DestinationIndex = 0;

	//leaving index traversal in a different loop to help cache coherency of source data
	for(int32 SubsetIndex = 0; SubsetIndex < GeometryIndexBuffer.Num(); ++SubsetIndex)
	{
		int32 GeometryIndex = GeometryIndexBuffer[SubsetIndex];

		//for each index, subtract the starting vertex for that geometry to make it 0-based.  Then add the new starting vertex index for this geometry
		int32 SourceGeometryVertexStart = VertexStartArray[GeometryIndex];
		int32 SourceGeometryVertexCount = VertexCountArray[GeometryIndex];
		int32 IndexDelta = DestVertex - SourceGeometryVertexStart;

		int32 FaceStart = FaceStartArray[GeometryIndex];
		int32 FaceCount = FaceCountArray[GeometryIndex];

		//Copy the faces
		for(int FaceIdx = FaceStart; FaceIdx < FaceStart + FaceCount; FaceIdx++)
		{
			if(Visible[FaceIdx])
			{
				OutIndexBuffer[DestinationIndex++] = Indices[FaceIdx].X + IndexDelta;
				OutIndexBuffer[DestinationIndex++] = Indices[FaceIdx].Y + IndexDelta;
				OutIndexBuffer[DestinationIndex++] = Indices[FaceIdx].Z + IndexDelta;
			}
		}

		DestVertex += SourceGeometryVertexCount;
	}

	// Invisible faces make the index buffer smaller
	OutIndexBuffer.SetNum(DestinationIndex);

	// Push as a custom mesh to navigation system
	// #CHAOSTODO This is pretty inefficient as it copies the whole buffer transforming each vert by the component to world
	// transform. Investigate a move aware custom mesh for pre-transformed verts to speed this up.
	GeomExport.ExportCustomMesh(OutVertexBuffer.GetData(), OutVertexBuffer.Num(), OutIndexBuffer.GetData(), OutIndexBuffer.Num(), GetComponentToWorld());

	return true;
}

UPhysicalMaterial* UGeometryCollectionComponent::GetPhysicalMaterial() const
{
	// Pull material from first mesh element to grab physical material. Prefer an override if one exists
	UPhysicalMaterial* PhysMatToUse = PhysicalMaterialOverride;

	if(!PhysMatToUse)
	{
		// No override, try render materials
		const int32 NumMaterials = GetNumMaterials();

		if(NumMaterials > 0)
		{
			UMaterialInterface* FirstMatInterface = GetMaterial(0);

			if(FirstMatInterface && FirstMatInterface->GetPhysicalMaterial())
			{
				PhysMatToUse = FirstMatInterface->GetPhysicalMaterial();
			}
		}
	}

	if(!PhysMatToUse)
	{
		// Still no material, fallback on default
		PhysMatToUse = GEngine->DefaultPhysMaterial;
	}

	// Should definitely have a material at this point.
	check(PhysMatToUse);
	return PhysMatToUse;
}

void UGeometryCollectionComponent::InitializeComponent()
{
	Super::InitializeComponent();

	AActor* Owner = GetOwner();

	if(!Owner)
	{
		return;
	}

	const ENetRole LocalRole = Owner->GetLocalRole();
	const ENetMode NetMode = Owner->GetNetMode();

	// If we're replicating we need some extra setup - check netmode as we don't need this for
	// standalone runtimes where we aren't going to network the component
	if(GetIsReplicated() && NetMode != NM_Standalone)
	{
		if(LocalRole == ENetRole::ROLE_Authority)
		{
			// As we're the authority we need to track velocities in the dynamic collection so we
			// can send them over to the other clients to correctly set their state. Attach this now.
			// The physics proxy will pick them up and populate them as needed
			DynamicCollection->AddAttribute<FVector>("LinearVelocity", FTransformCollection::TransformGroup);
			DynamicCollection->AddAttribute<FVector>("AngularVelocity", FTransformCollection::TransformGroup);

			// We also need to track our control of particles if that control can be shared between server and client
			if(bEnableAbandonAfterLevel)
			{
				TManagedArray<bool>& ControlFlags = DynamicCollection->AddAttribute<bool>("AuthControl", FTransformCollection::TransformGroup);
				for(bool& Flag : ControlFlags)
				{
					Flag = true;
				}
			}
		}
		else
		{
			// We're a replicated component and we're not in control.
			Chaos::FPhysicsSolver* CurrSolver = GetSolver(*this);

			if(CurrSolver)
			{
				CurrSolver->RegisterSimOneShotCallback([Prox = PhysicsProxy]()
				{
					// As we're not in control we make it so our simulated proxy cannot break clusters
					// We have to set the strain to a high value but be below the max for the data type
					// so releasing on authority demand works
					const Chaos::FReal MaxStrain = TNumericLimits<Chaos::FReal>::Max() - TNumericLimits<Chaos::FReal>::Min();

					TArray<Chaos::TPBDRigidClusteredParticleHandle<Chaos::FReal, 3>*> Particles = Prox->GetParticles();

					for(Chaos::TPBDRigidClusteredParticleHandle<Chaos::FReal, 3> * P : Particles)
					{
						if(!P)
						{
							continue;
						}

						P->SetStrain(MaxStrain);
					}
				});
			}
		}
	}
}

static void DispatchGeometryCollectionBreakEvent(const FChaosBreakEvent& Event)
{
	if (UGeometryCollectionComponent* const GC = Cast<UGeometryCollectionComponent>(Event.Component))
	{
		GC->DispatchBreakEvent(Event);
	}
}

void UGeometryCollectionComponent::DispatchChaosPhysicsCollisionBlueprintEvents(const FChaosPhysicsCollisionInfo& CollisionInfo)
{
	ReceivePhysicsCollision(CollisionInfo);
	OnChaosPhysicsCollision.Broadcast(CollisionInfo);
}

// call when first registering
void UGeometryCollectionComponent::RegisterForEvents()
{
	if (BodyInstance.bNotifyRigidBodyCollision || bNotifyBreaks || bNotifyCollisions)
	{
#if INCLUDE_CHAOS
		Chaos::FPhysicsSolver* Solver = GetWorld()->GetPhysicsScene()->GetSolver();
		if (Solver)
		{
			if (bNotifyCollisions || BodyInstance.bNotifyRigidBodyCollision)
			{
				EventDispatcher->RegisterForCollisionEvents(this, this);

				Solver->EnqueueCommandImmediate([Solver]()
					{
						Solver->SetGenerateCollisionData(true);
					});
			}

			if (bNotifyBreaks)
			{
				EventDispatcher->RegisterForBreakEvents(this, &DispatchGeometryCollectionBreakEvent);

				Solver->EnqueueCommandImmediate([Solver]()
					{
						Solver->SetGenerateBreakingData(true);
					});

			}
		}
#endif
	}
}

void UGeometryCollectionComponent::UpdateRBCollisionEventRegistration()
{
	if (bNotifyCollisions || BodyInstance.bNotifyRigidBodyCollision)
	{
		EventDispatcher->RegisterForCollisionEvents(this, this);
	}
	else
	{
		EventDispatcher->UnRegisterForCollisionEvents(this, this);
	}
}

void UGeometryCollectionComponent::UpdateBreakEventRegistration()
{
	if (bNotifyBreaks)
	{
		EventDispatcher->RegisterForBreakEvents(this, &DispatchGeometryCollectionBreakEvent);
	}
	else
	{
		EventDispatcher->UnRegisterForBreakEvents(this);
	}
}

void ActivateClusters(Chaos::FPBDRigidsEvolution::FRigidClustering& Clustering, Chaos::TPBDRigidClusteredParticleHandle<float, 3>* Cluster)
{
	if(!Cluster)
	{
		return;
	}

	if(Cluster->ClusterIds().Id)
	{
		ActivateClusters(Clustering, Cluster->ClusterIds().Id->CastToClustered());
	}

	Clustering.DeactivateClusterParticle(Cluster);
}

void UGeometryCollectionComponent::OnRep_RepData(const FGeometryCollectionRepData& OldData)
{
	if(!DynamicCollection)
	{
		return;
	}

	if(AActor* Owner = GetOwner())
	{
		const int32 NumTransforms = DynamicCollection->Transform.Num();
		const int32 NumNewPoses = RepData.Poses.Num();
		if(NumTransforms < NumNewPoses)
		{
			return;
		}

		Chaos::FPhysicsSolver* Solver = GetSolver(*this);

		for(int32 Index = 0; Index < NumNewPoses; ++Index)
		{
			const FGeometryCollectionRepPose& SourcePose = RepData.Poses[Index];
			const int32 ParticleIndex = SourcePose.ParticleIndex;

			if(ParticleIndex >= NumTransforms)
			{
				// Out of range
				continue;
			}

			Solver->RegisterSimOneShotCallback([SourcePose, Prox = PhysicsProxy]()
			{
				Chaos::TPBDRigidClusteredParticleHandle<float, 3>* Particle = Prox->GetParticles()[SourcePose.ParticleIndex];

				Chaos::FPhysicsSolver* Solver = Prox->GetSolver<Chaos::FPhysicsSolver>();
				Chaos::FPBDRigidsEvolution* Evo = Solver->GetEvolution();
				check(Evo);
				Chaos::FPBDRigidsEvolution::FRigidClustering& Clustering = Evo->GetRigidClustering();
				
				// Set X/R/V/W for next sim step from the replicated state
				Particle->SetX(SourcePose.Position);
				Particle->SetR(SourcePose.Rotation);
				Particle->SetV(SourcePose.LinearVelocity);
				Particle->SetW(SourcePose.AngularVelocity);

				if(Particle->ClusterIds().Id)
				{
					// This particle is clustered but the remote authority has it activated. Fracture the parent cluster
					ActivateClusters(Clustering, Particle->ClusterIds().Id->CastToClustered());
				}
				else if(Particle->Disabled())
				{
					// We might have disabled the particle - need to reactivate if it's active on the remote.
					Particle->SetDisabled(false);
				}

				// Make sure to wake corrected particles
				Particle->SetSleeping(false);
			});
		}
	}
}

void UGeometryCollectionComponent::UpdateRepData()
{
	if(!bEnableReplication)
	{
		return;
	}

	AActor* Owner = GetOwner();
	
	// If we have no owner or our netmode means we never require replication then early out
	if(!Owner || Owner->GetNetMode() == ENetMode::NM_Standalone)
	{
		return;
	}
	
	if(Owner && GetIsReplicated() && Owner->GetLocalRole() == ROLE_Authority)
	{
		// We're inside a replicating actor and we're the authority - update the rep data
		const int32 NumTransforms = DynamicCollection->Transform.Num();
		RepData.Poses.Reset(NumTransforms);

		TManagedArray<FVector>* LinearVelocity = DynamicCollection->FindAttributeTyped<FVector>("LinearVelocity", FTransformCollection::TransformGroup);
		TManagedArray<FVector>* AngularVelocity = DynamicCollection->FindAttributeTyped<FVector>("AngularVelocity", FTransformCollection::TransformGroup);

		for(int32 Index = 0; Index < NumTransforms; ++Index)
		{
			TManagedArray<TUniquePtr<Chaos::FGeometryParticle>>& GTParticles = PhysicsProxy->GetExternalParticles();
			Chaos::FGeometryParticle* Particle = GTParticles[Index].Get();
			if(!DynamicCollection->Active[Index] || DynamicCollection->DynamicState[Index] != static_cast<uint8>(Chaos::EObjectStateType::Dynamic))
			{
				continue;
			}

			const int32 ClusterLevel = GetClusterLevel(RestCollection->GetGeometryCollection().Get(), Index);
			const bool bLevelValid = !EnableClustering || !bEnableAbandonAfterLevel || ClusterLevel <= ReplicationAbandonClusterLevel;
			if(!bLevelValid)
			{
				const int32 ParentTransformIndex = RestCollection->GetGeometryCollection()->Parent[Index];
				TManagedArray<bool>* ControlFlags = DynamicCollection->FindAttributeTyped<bool>("AuthControl", FTransformCollection::TransformGroup);

				if(ControlFlags && (*ControlFlags)[ParentTransformIndex])
				{
					(*ControlFlags)[ParentTransformIndex] = false;
					NetAbandonCluster(ParentTransformIndex);
				}

				continue;
			}

			RepData.Poses.AddDefaulted();
			FGeometryCollectionRepPose& Pose = RepData.Poses.Last();

			// No scale transfered - shouldn't be a simulated property
			Pose.ParticleIndex = Index;
			Pose.Position = Particle->X();
			Pose.Rotation = Particle->R();
			if(LinearVelocity)
			{
				check(AngularVelocity);
				Pose.LinearVelocity = (*LinearVelocity)[Index];
				Pose.AngularVelocity = (*AngularVelocity)[Index];
			}
			else
			{
				Pose.LinearVelocity = FVector::ZeroVector;
				Pose.AngularVelocity = FVector::ZeroVector;
			}
		}

		RepData.Version++;
		MARK_PROPERTY_DIRTY_FROM_NAME(UGeometryCollectionComponent, RepData, this);
	}
}

void SetHierarchyStrain(Chaos::TPBDRigidClusteredParticleHandle<Chaos::FReal, 3>* P, TMap<Chaos::TPBDRigidParticleHandle<Chaos::FReal, 3>*, TArray<Chaos::TPBDRigidParticleHandle<Chaos::FReal, 3>*>>& Map, float Strain)
{
	TArray<Chaos::TPBDRigidParticleHandle<Chaos::FReal, 3>*>* Children = Map.Find(P);

	if(Children)
	{
		for(Chaos::TPBDRigidParticleHandle<Chaos::FReal, 3> * ChildP : (*Children))
		{
			SetHierarchyStrain(ChildP->CastToClustered(), Map, Strain);
		}
	}

	if(P)
	{
		P->SetStrain(Strain);
	}
}

void UGeometryCollectionComponent::NetAbandonCluster_Implementation(int32 TransformIndex)
{
	// Called on clients when the server abandons a particle. TransformIndex is the index of the parent
	// of that particle, should only get called once per cluster but survives multiple calls
	
	if(GetOwnerRole() == ENetRole::ROLE_Authority)
	{
		// Owner called abandon - takes no action
		return;
	}

	if(!EnableClustering)
	{
		// No clustering information to update
		return;
	}

	if(TransformIndex >= 0 && TransformIndex < DynamicCollection->NumElements(FTransformCollection::TransformGroup))
	{
		int32 ClusterLevel = GetClusterLevel(RestCollection->GetGeometryCollection().Get(), TransformIndex);
		float Strain = DamageThreshold.IsValidIndex(ClusterLevel) ? DamageThreshold[ClusterLevel] : DamageThreshold.Num() > 0 ? DamageThreshold[0] : 0.0f;

		if(Strain >= 0)
		{
			Chaos::FPhysicsSolver* Solver = GetSolver(*this);

			Solver->RegisterSimOneShotCallback([Prox = PhysicsProxy, Strain, TransformIndex, Solver]()
			{
				Chaos::TPBDRigidClustering<Chaos::FPBDRigidsEvolution, Chaos::FPBDCollisionConstraints>& Clustering = Solver->GetEvolution()->GetRigidClustering();
				Chaos::FPBDRigidClusteredParticleHandle* Parent = Prox->GetParticles()[TransformIndex];

				if(!Parent->Disabled())
				{
					SetHierarchyStrain(Parent, Clustering.GetChildrenMap(), Strain);

					// We know the server must have fractured this cluster, so repeat here
					Clustering.DeactivateClusterParticle(Parent);
				}
			});
		}
	}
}

void UGeometryCollectionComponent::InitConstantData(FGeometryCollectionConstantData* ConstantData) const
{
	// Constant data should all be moved to the DDC as time permits.

	check(ConstantData);
	check(RestCollection);
	const FGeometryCollection* Collection = RestCollection->GetGeometryCollection().Get();
	check(Collection);

	const int32 NumPoints = Collection->NumElements(FGeometryCollection::VerticesGroup);
	const TManagedArray<FVector>& Vertex = Collection->Vertex;
	const TManagedArray<int32>& BoneMap = Collection->BoneMap;
	const TManagedArray<FVector>& TangentU = Collection->TangentU;
	const TManagedArray<FVector>& TangentV = Collection->TangentV;
	const TManagedArray<FVector>& Normal = Collection->Normal;
	const TManagedArray<FVector2D>& UV = Collection->UV;
	const TManagedArray<FLinearColor>& Color = Collection->Color;
	const TManagedArray<FLinearColor>& BoneColors = Collection->BoneColor;

	ConstantData->Vertices = TArray<FVector>(Vertex.GetData(), Vertex.Num());
	ConstantData->BoneMap = TArray<int32>(BoneMap.GetData(), BoneMap.Num());
	ConstantData->TangentU = TArray<FVector>(TangentU.GetData(), TangentU.Num());
	ConstantData->TangentV = TArray<FVector>(TangentV.GetData(), TangentV.Num());
	ConstantData->Normals = TArray<FVector>(Normal.GetData(), Normal.Num());
	ConstantData->UVs = TArray<FVector2D>(UV.GetData(), UV.Num());
	ConstantData->Colors = TArray<FLinearColor>(Color.GetData(), Color.Num());

	ConstantData->BoneColors.AddUninitialized(NumPoints);
	
	ParallelFor(NumPoints, [&](const int32 InPointIndex)
	{
		const int32 BoneIndex = ConstantData->BoneMap[InPointIndex];
		ConstantData->BoneColors[InPointIndex] = BoneColors[BoneIndex];
	});

	int32 NumIndices = 0;
	const TManagedArray<FIntVector>& Indices = Collection->Indices;
	const TManagedArray<int32>& MaterialID = Collection->MaterialID;
	
	const TManagedArray<bool>& Visible = GetVisibleArray();  // Use copy on write attribute. The rest collection visible array can be overriden for the convenience of debug drawing the collision volumes
	const TManagedArray<int32>& MaterialIndex = Collection->MaterialIndex;

	const int32 NumFaceGroupEntries = Collection->NumElements(FGeometryCollection::FacesGroup);

	for (int FaceIndex = 0; FaceIndex < NumFaceGroupEntries; ++FaceIndex)
	{
		NumIndices += static_cast<int>(Visible[FaceIndex]);
	}

	ConstantData->Indices.AddUninitialized(NumIndices);
	for (int IndexIdx = 0, cdx = 0; IndexIdx < NumFaceGroupEntries; ++IndexIdx)
	{
		if (Visible[ MaterialIndex[IndexIdx] ])
		{
			ConstantData->Indices[cdx++] = Indices[ MaterialIndex[IndexIdx] ];
		}
	}

	// We need to correct the section index start point & number of triangles since only the visible ones have been copied across in the code above
	const int32 NumMaterialSections = Collection->NumElements(FGeometryCollection::MaterialGroup);
	ConstantData->Sections.AddUninitialized(NumMaterialSections);
	const TManagedArray<FGeometryCollectionSection>& Sections = Collection->Sections;
	for (int SectionIndex = 0; SectionIndex < NumMaterialSections; ++SectionIndex)
	{
		FGeometryCollectionSection Section = Sections[SectionIndex]; // deliberate copy

		for (int32 TriangleIndex = 0; TriangleIndex < Sections[SectionIndex].FirstIndex / 3; TriangleIndex++)
		{
			if(!Visible[MaterialIndex[TriangleIndex]])
			{
				Section.FirstIndex -= 3;
			}
		}

		for (int32 TriangleIndex = 0; TriangleIndex < Sections[SectionIndex].NumTriangles; TriangleIndex++)
		{
			if(!Visible[MaterialIndex[Sections[SectionIndex].FirstIndex / 3 + TriangleIndex]])
			{
				Section.NumTriangles--;
			}
		}

		ConstantData->Sections[SectionIndex] = MoveTemp(Section);
	}
	ConstantData->NumTransforms = Collection->NumElements(FGeometryCollection::TransformGroup);
	ConstantData->LocalBounds = LocalBounds;

	// store the index buffer and render sections for the base unfractured mesh
	const TManagedArray<int32>& TransformToGeometryIndex = Collection->TransformToGeometryIndex;
	const TManagedArray<int32>&	FaceStart = Collection->FaceStart;
	const TManagedArray<int32>&	FaceCount = Collection->FaceCount;
	
	const int32 NumFaces = Collection->NumElements(FGeometryCollection::FacesGroup);
	TArray<FIntVector> BaseMeshIndices;
	TArray<int32> BaseMeshOriginalFaceIndices;	

	BaseMeshIndices.Reserve(NumFaces);
	BaseMeshOriginalFaceIndices.Reserve(NumFaces);

	// add all visible external faces to the original geometry index array
	// #note:  This is a stopgap because the original geometry array is broken
	for (int FaceIndex = 0; FaceIndex < NumFaces; ++FaceIndex)
	{
		// only add visible external faces.  MaterialID that is even is an external material
		if (Visible[FaceIndex] && MaterialID[FaceIndex] % 2 == 0)
		{
			BaseMeshIndices.Add(Indices[FaceIndex]);
			BaseMeshOriginalFaceIndices.Add(FaceIndex);
		}				
	}

	// We should always have external faces of a geometry collection
	ensure(BaseMeshIndices.Num() > 0);

	// #todo(dmp): we should eventually get this working where we use geometry nodes
	// that signify original unfractured geometry.  For now, this system is broken.
	/*
	for (int i = 0; i < Collection->NumElements(FGeometryCollection::TransformGroup); ++i)
	{
		const FGeometryCollectionBoneNode &CurrBone = BoneHierarchy[i];

		// root node could be parent geo
		if (CurrBone.Parent == INDEX_NONE)
		{
			int32 GeometryIndex = TransformToGeometryIndex[i];

			// found geometry associated with base mesh root node
			if (GeometryIndex != INDEX_NONE)
			{				
				int32 CurrFaceStart = FaceStart[GeometryIndex];
				int32 CurrFaceCount = FaceCount[GeometryIndex];
			
				// add all the faces to the original geometry face array
				for (int face = CurrFaceStart; face < CurrFaceStart + CurrFaceCount; ++face)
				{
					BaseMeshIndices.Add(Indices[face]);
					BaseMeshOriginalFaceIndices.Add(face);
				}

				// build an array of mesh sections
				ConstantData->HasOriginalMesh = true;				
			}
			else
			{
				// all the direct decedents of the root node with no geometry are original geometry
				for (int32 CurrChild : CurrBone.Children)
				{					
					int32 GeometryIndex = TransformToGeometryIndex[CurrChild];
					if (GeometryIndex != INDEX_NONE)
					{
						// original geo static mesh					
						int32 CurrFaceStart = FaceStart[GeometryIndex];
						int32 CurrFaceCount = FaceCount[GeometryIndex];

						// add all the faces to the original geometry face array
						for (int face = CurrFaceStart; face < CurrFaceStart + CurrFaceCount; ++face)
						{
							BaseMeshIndices.Add(Indices[face]);
							BaseMeshOriginalFaceIndices.Add(face);
						}

						ConstantData->HasOriginalMesh = true;
					}					
				}
			}
		}
	}
	*/


	ConstantData->OriginalMeshSections = Collection->BuildMeshSections(BaseMeshIndices, BaseMeshOriginalFaceIndices, ConstantData->OriginalMeshIndices);

	TArray<FMatrix> RestMatrices;
	GeometryCollectionAlgo::GlobalMatrices(RestCollection->GetGeometryCollection()->Transform, RestCollection->GetGeometryCollection()->Parent, RestMatrices);

	ConstantData->RestTransforms = MoveTemp(RestMatrices); 
}

void UGeometryCollectionComponent::InitDynamicData(FGeometryCollectionDynamicData * DynamicData)
{
	SCOPE_CYCLE_COUNTER(STAT_GCInitDynamicData);

	check(DynamicData);
	DynamicData->IsDynamic = this->GetIsObjectDynamic() || bShowBoneColors || bEnableBoneSelection;
	DynamicData->IsLoading = this->GetIsObjectLoading();

	if (CachePlayback && CacheParameters.TargetCache && CacheParameters.TargetCache->GetData())
	{		
		const TManagedArray<int32> &Parents = GetParentArray();
		const TManagedArray<TSet<int32>> &Children = GetChildrenArray();
		const TManagedArray<FTransform> &Transform = RestCollection->GetGeometryCollection()->Transform;
		const TManagedArray<FTransform> &MassToLocal = RestCollection->GetGeometryCollection()->GetAttribute<FTransform>("MassToLocal", FGeometryCollection::TransformGroup);

		// #todo(dmp): find a better place to calculate and store this
		float CacheDt = CacheParameters.TargetCache->GetData()->GetDt();

		// if we want the state before the first cached frame, then the collection doesn't need to be dynamic since it'll render the pre-fractured geometry
		DynamicData->IsDynamic = DesiredCacheTime > CacheDt;

		// if we are already on the current cached frame, return
		if (FMath::IsNearlyEqual(CurrentCacheTime, DesiredCacheTime) && GlobalMatrices.Num() != 0)
		{
			DynamicData->PrevTransforms = GlobalMatrices;
			DynamicData->Transforms = GlobalMatrices;

			// maintaining the cache time means we should consider the transforms equal for dynamic data sending purposes
			TransformsAreEqual[(TransformsAreEqualIndex++) % TransformsAreEqual.Num()] = true;

			return;
		}

		// if the input simulation time to playback is the first frame, reset simulation time
		if (DesiredCacheTime <= CacheDt ||  FMath::IsNearlyEqual(CurrentCacheTime, FLT_MAX))
		{
			CurrentCacheTime = DesiredCacheTime;
		    
			GeometryCollectionAlgo::GlobalMatrices(GetTransformArray(), GetParentArray(), GlobalMatrices);
			DynamicData->PrevTransforms = GlobalMatrices;
			DynamicData->Transforms = GlobalMatrices;			

			EventsPlayed.Empty();
			EventsPlayed.AddDefaulted(CacheParameters.TargetCache->GetData()->Records.Num());

			// reset should send new transforms to the RT
			TransformsAreEqual[(TransformsAreEqualIndex++) % TransformsAreEqual.Num()] = false;
		}
		else if (GlobalMatrices.Num() == 0)
		{
			// bad case here.  Sequencer starts at non zero position  We cannot correctly reconstruct the current frame, so give a warning
			GeometryCollectionAlgo::GlobalMatrices(GetTransformArray(), GetParentArray(), GlobalMatrices);
			DynamicData->PrevTransforms = GlobalMatrices;
			DynamicData->Transforms = GlobalMatrices;

			EventsPlayed.Empty();
			EventsPlayed.AddDefaulted(CacheParameters.TargetCache->GetData()->Records.Num());

			// degenerate case causes and reset should send new transforms to the RT
			TransformsAreEqual[(TransformsAreEqualIndex++) % TransformsAreEqual.Num()] = false;

			UE_LOG(UGCC_LOG, Warning, TEXT("Cache out of sync - must rewind sequencer to start frame"));
		}
		else if (DesiredCacheTime >= CurrentCacheTime)
		{	
			int NumSteps = floor((DesiredCacheTime - CurrentCacheTime) / CacheDt);
			float LastDt = FMath::Fmod(DesiredCacheTime - CurrentCacheTime, CacheDt);
			NumSteps += LastDt > SMALL_NUMBER ? 1 : 0;
			
			FTransform ActorToWorld = GetComponentTransform();

			bool HasAnyActiveTransforms = false;

			// Jump ahead in increments of CacheDt evaluating the cache until we reach our desired time
			for (int st = 0; st < NumSteps; ++st)
			{
				float TimeIncrement = st == NumSteps - 1 ? LastDt : CacheDt;
				CurrentCacheTime += TimeIncrement;
			
				DynamicData->PrevTransforms = GlobalMatrices;

				const FRecordedFrame* FirstFrame = nullptr;
				const FRecordedFrame* SecondFrame = nullptr;
				CacheParameters.TargetCache->GetData()->GetFramesForTime(CurrentCacheTime, FirstFrame, SecondFrame);

				if (FirstFrame && !SecondFrame)
				{

					const TArray<FTransform> &xforms = FirstFrame->Transforms;
					const TArray<int32> &TransformIndices = FirstFrame->TransformIndices;

					const int32 NumActives = FirstFrame->TransformIndices.Num();
					
					if (NumActives > 0)
					{
						HasAnyActiveTransforms = true;
					}

					for (int i = 0; i < NumActives; ++i)
					{
						const int32 InternalIndexTmp = TransformIndices[i];
						
						if (InternalIndexTmp >= GlobalMatrices.Num())
						{
							UE_LOG(UGCC_LOG, Error, 
								TEXT("%s: TargetCache (%s) is out of sync with GeometryCollection.  Regenerate the cache."), 
								*RestCollection->GetName(), *CacheParameters.TargetCache->GetName());
							DynamicData->PrevTransforms = GlobalMatrices;
							DynamicData->Transforms = GlobalMatrices;
							return;
						}
												
						// calculate global matrix for current
						FTransform ParticleToWorld = xforms[i];						

						FTransform CurrGlobalTransform = MassToLocal[InternalIndexTmp].GetRelativeTransformReverse(ParticleToWorld).GetRelativeTransform(ActorToWorld);						
						CurrGlobalTransform.NormalizeRotation();
						GlobalMatrices[InternalIndexTmp] = CurrGlobalTransform.ToMatrixWithScale();
						
						// Traverse from active parent node down to all children and set global transforms
						GeometryCollectionAlgo::GlobalMatricesFromRoot(InternalIndexTmp, Transform, Children, GlobalMatrices);
					}
				}
				else if (FirstFrame && SecondFrame && CurrentCacheTime > FirstFrame->Timestamp)
				{
					const float Alpha = (CurrentCacheTime - FirstFrame->Timestamp) / (SecondFrame->Timestamp - FirstFrame->Timestamp);
					check(0 <= Alpha && Alpha <= 1.0f);

					const int32 NumActives = SecondFrame->TransformIndices.Num();

					if (NumActives > 0)
					{
						HasAnyActiveTransforms = true;
					}

					for (int Index = 0; Index < NumActives; ++Index)
					{
						const int32 InternalIndexTmp = SecondFrame->TransformIndices[Index];
						
						// check if transform index is valid
						if (InternalIndexTmp >= GlobalMatrices.Num())
						{
							UE_LOG(UGCC_LOG, Error, 
								TEXT("%s: TargetCache (%s) is out of sync with GeometryCollection.  Regenerate the cache."), 
								*RestCollection->GetName(), *CacheParameters.TargetCache->GetName());
							DynamicData->PrevTransforms = GlobalMatrices;
							DynamicData->Transforms = GlobalMatrices;
							return;
						}

						const int32 PreviousIndexSlot = Index < SecondFrame->PreviousTransformIndices.Num() ? SecondFrame->PreviousTransformIndices[Index] : INDEX_NONE;

						if (PreviousIndexSlot != INDEX_NONE)
						{
							FTransform ParticleToWorld;
							ParticleToWorld.Blend(FirstFrame->Transforms[PreviousIndexSlot], SecondFrame->Transforms[Index], Alpha);
							
							FTransform CurrGlobalTransform = MassToLocal[InternalIndexTmp].GetRelativeTransformReverse(ParticleToWorld).GetRelativeTransform(ActorToWorld);							
							CurrGlobalTransform.NormalizeRotation();
							GlobalMatrices[InternalIndexTmp] = CurrGlobalTransform.ToMatrixWithScale();

							// Traverse from active parent node down to all children and set global transforms
							GeometryCollectionAlgo::GlobalMatricesFromRoot(InternalIndexTmp, Transform, Children, GlobalMatrices);
						}
						else
						{
							FTransform ParticleToWorld = SecondFrame->Transforms[Index];
							
							FTransform CurrGlobalTransform = MassToLocal[InternalIndexTmp].GetRelativeTransformReverse(ParticleToWorld).GetRelativeTransform(ActorToWorld);							
							CurrGlobalTransform.NormalizeRotation();
							GlobalMatrices[InternalIndexTmp] = CurrGlobalTransform.ToMatrixWithScale();

							// Traverse from active parent node down to all children and set global transforms
							GeometryCollectionAlgo::GlobalMatricesFromRoot(InternalIndexTmp, Transform, Children, GlobalMatrices);
						}
					}
				}

				DynamicData->Transforms = GlobalMatrices;

				/**********************************************************************************************************************************************************************************/
				// Capture all events for the given time
			
				if(false)
				{
					// clear events on the solver
					Chaos::FPhysicsSolver *Solver = GetSolver(*this);
					
					if (Solver)
					{
#if TODO_REPLACE_SOLVER_LOCK
						Chaos::FSolverWriteLock ScopedWriteLock(Solver);
#endif

#if TODO_REIMPLEMENT_EVENTS_DATA_ARRAYS
						//////////////////////////////////////////////////////////////////////////
						// Temporary workaround for writing on multiple threads.
						// The following is called wide from the render thread to populate
						// Niagara data from a cache without invoking a solver directly
						//
						// The above write lock guarantees we can safely write to these buffers
						//
						// TO BE REFACTORED
						// #TODO BG
						//////////////////////////////////////////////////////////////////////////
						Chaos::FPhysicsSolver::FAllCollisionData *CollisionDataToWriteTo = const_cast<Chaos::FPhysicsSolver::FAllCollisionData*>(Solver->GetAllCollisions_FromSequencerCache_NEEDSLOCK());
						Chaos::FPhysicsSolver::FAllBreakingData *BreakingDataToWriteTo = const_cast<Chaos::FPhysicsSolver::FAllBreakingData*>(Solver->GetAllBreakings_FromSequencerCache_NEEDSLOCK());
						Chaos::FPhysicsSolver::FAllTrailingData *TrailingDataToWriteTo = const_cast<Chaos::FPhysicsSolver::FAllTrailingData*>(Solver->GetAllTrailings_FromSequencerCache_NEEDSLOCK());

						if (!FMath::IsNearlyEqual(CollisionDataToWriteTo->TimeCreated, DesiredCacheTime))
						{
							CollisionDataToWriteTo->AllCollisionsArray.Empty();
							CollisionDataToWriteTo->TimeCreated = DesiredCacheTime;
						}

						int32 Index = CacheParameters.TargetCache->GetData()->FindLastKeyBefore(CurrentCacheTime);
						const FRecordedFrame *RecordedFrame = &CacheParameters.TargetCache->GetData()->Records[Index];				

						if (RecordedFrame && PhysicsProxy && !EventsPlayed[Index])
						{
							EventsPlayed[Index] = true;

							// Collisions
							if (RecordedFrame->Collisions.Num() > 0)
							{							
								for (int32 Idx = 0; Idx < RecordedFrame->Collisions.Num(); ++Idx)
								{
									// Check if the particle is still kinematic
									int32 NewIdx = CollisionDataToWriteTo->AllCollisionsArray.Add(Chaos::FCollidingData());
									Chaos::FCollidingData& AllCollisionsDataArrayItem = CollisionDataToWriteTo->AllCollisionsArray[NewIdx];

									AllCollisionsDataArrayItem.Location = RecordedFrame->Collisions[Idx].Location;
									AllCollisionsDataArrayItem.AccumulatedImpulse = RecordedFrame->Collisions[Idx].AccumulatedImpulse;
									AllCollisionsDataArrayItem.Normal = RecordedFrame->Collisions[Idx].Normal;
									AllCollisionsDataArrayItem.Velocity1 = RecordedFrame->Collisions[Idx].Velocity1;
									AllCollisionsDataArrayItem.Velocity2 = RecordedFrame->Collisions[Idx].Velocity2;
									AllCollisionsDataArrayItem.AngularVelocity1 = RecordedFrame->Collisions[Idx].AngularVelocity1;
									AllCollisionsDataArrayItem.AngularVelocity2 = RecordedFrame->Collisions[Idx].AngularVelocity2;
									AllCollisionsDataArrayItem.Mass1 = RecordedFrame->Collisions[Idx].Mass1;
									AllCollisionsDataArrayItem.Mass2 = RecordedFrame->Collisions[Idx].Mass2;
#if TODO_CONVERT_GEOMETRY_COLLECTION_PARTICLE_INDICES_TO_PARTICLE_POINTERS
									AllCollisionsDataArrayItem.ParticleIndex = RecordedFrame->Collisions[Idx].ParticleIndex;
#endif
									AllCollisionsDataArrayItem.LevelsetIndex = RecordedFrame->Collisions[Idx].LevelsetIndex;
									AllCollisionsDataArrayItem.ParticleIndexMesh = RecordedFrame->Collisions[Idx].ParticleIndexMesh;
									AllCollisionsDataArrayItem.LevelsetIndexMesh = RecordedFrame->Collisions[Idx].LevelsetIndexMesh;
								}
							}

							// Breaking
							if (RecordedFrame->Breakings.Num() > 0)
							{
								for (int32 Idx = 0; Idx < RecordedFrame->Breakings.Num(); ++Idx)
								{
									// Check if the particle is still kinematic							
									int32 NewIdx = BreakingDataToWriteTo->AllBreakingsArray.Add(Chaos::FBreakingData());
									Chaos::FBreakingData& AllBreakingsDataArrayItem = BreakingDataToWriteTo->AllBreakingsArray[NewIdx];

									AllBreakingsDataArrayItem.Location = RecordedFrame->Breakings[Idx].Location;
									AllBreakingsDataArrayItem.Velocity = RecordedFrame->Breakings[Idx].Velocity;
									AllBreakingsDataArrayItem.AngularVelocity = RecordedFrame->Breakings[Idx].AngularVelocity;
									AllBreakingsDataArrayItem.Mass = RecordedFrame->Breakings[Idx].Mass;
#if TODO_CONVERT_GEOMETRY_COLLECTION_PARTICLE_INDICES_TO_PARTICLE_POINTERS
									AllBreakingsDataArrayItem.ParticleIndex = RecordedFrame->Breakings[Idx].ParticleIndex;
#endif
									AllBreakingsDataArrayItem.ParticleIndexMesh = RecordedFrame->Breakings[Idx].ParticleIndexMesh;
								}
							}

							// Trailing
							if (RecordedFrame->Trailings.Num() > 0)
							{
								for (FSolverTrailingData Trailing : RecordedFrame->Trailings)
								{
									// Check if the particle is still kinematic
									int32 NewIdx = TrailingDataToWriteTo->AllTrailingsArray.Add(Chaos::FTrailingData());
									Chaos::FTrailingData& AllTrailingsDataArrayItem = TrailingDataToWriteTo->AllTrailingsArray[NewIdx];

									AllTrailingsDataArrayItem.Location = Trailing.Location;
									AllTrailingsDataArrayItem.Velocity = Trailing.Velocity;
									AllTrailingsDataArrayItem.AngularVelocity = Trailing.AngularVelocity;
									AllTrailingsDataArrayItem.Mass = Trailing.Mass;
#if TODO_CONVERT_GEOMETRY_COLLECTION_PARTICLE_INDICES_TO_PARTICLE_POINTERS
									AllTrailingsDataArrayItem.ParticleIndex = Trailing.ParticleIndex;
#endif
									AllTrailingsDataArrayItem.ParticleIndexMesh = Trailing.ParticleIndexMesh;
								}
							}
						}
#endif
					}
				}								
			}

			// check if transforms at start of this tick are the same as what is calculated from the cache
			TransformsAreEqual[(TransformsAreEqualIndex++) % TransformsAreEqual.Num()] = !HasAnyActiveTransforms;
		}
		else
		{			
			// time is before current cache time so maintain the matrices we have since we can't rewind
			DynamicData->PrevTransforms = GlobalMatrices;
			DynamicData->Transforms = GlobalMatrices;

			// reset event means we don't want to consider transforms as being equal between prev and current frame
			TransformsAreEqual[(TransformsAreEqualIndex++) % TransformsAreEqual.Num()] = true;
		}
	}
	else if (DynamicData->IsDynamic)
	{
		// If we have no transforms stored in the dynamic data, then assign both prev and current to the same global matrices
		if (GlobalMatrices.Num() == 0)
		{
			// Copy global matrices over to DynamicData
			CalculateGlobalMatrices();		
			DynamicData->PrevTransforms = GlobalMatrices;		
			DynamicData->Transforms = GlobalMatrices;

			// reset event means we don't want to consider transforms as being equal between prev and current frame
			TransformsAreEqual[(TransformsAreEqualIndex++) % TransformsAreEqual.Num()] = false;
		}
		else
		{
			// Copy existing global matrices into prev transforms
			DynamicData->PrevTransforms = GlobalMatrices;

			// Copy global matrices over to DynamicData
			CalculateGlobalMatrices();

			// if the number of matrices has changed between frames, then sync previous to current
			if (GlobalMatrices.Num() != DynamicData->PrevTransforms.Num())
			{
				DynamicData->PrevTransforms = GlobalMatrices;
			}

			DynamicData->Transforms = GlobalMatrices;

			// check if previous transforms are the same as current
			TransformsAreEqual[(TransformsAreEqualIndex++) % TransformsAreEqual.Num()] = IsEqual(DynamicData->PrevTransforms, DynamicData->Transforms);
		}
	}
}

void UGeometryCollectionComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction)
{
	//UE_LOG(UGCC_LOG, Log, TEXT("GeometryCollectionComponent[%p]::TickComponent()"), this);
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

#if WITH_CHAOS
	//if (bRenderStateDirty && DynamicCollection)	//todo: always send for now
	if(RestCollection)
	{
		// In editor mode we have no DynamicCollection so this test is necessary
		if(DynamicCollection) //, TEXT("No dynamic collection available for component %s during tick."), *GetName()))
		{
			if(RestCollection->HasVisibleGeometry() || DynamicCollection->IsDirty())
			{
				MarkRenderTransformDirty();
				MarkRenderDynamicDataDirty();
				bRenderStateDirty = false;
				//DynamicCollection->MakeClean(); clean?

				const UWorld* MyWorld = GetWorld();
				if (MyWorld && MyWorld->IsGameWorld())
				{
					//cycle every 0xff frames
					//@todo - Need way of seeing if the collection is actually changing
					if (bNavigationRelevant && bRegistered && (((GFrameCounter + NavmeshInvalidationTimeSliceIndex) & 0xff) == 0))
					{
						UpdateNavigationData();
					}
				}
			}
		}
	}
#endif

}

void UGeometryCollectionComponent::OnRegister()
{
#if WITH_CHAOS
	//UE_LOG(UGCC_LOG, Log, TEXT("GeometryCollectionComponent[%p]::OnRegister()[%p]"), this,RestCollection );
	ResetDynamicCollection();

#if WITH_EDITOR
	FScopedColorEdit ColorEdit(this);
	ColorEdit.ResetBoneSelection();
	ColorEdit.ResetHighlightedBones();
#endif

#endif // WITH_CHAOS

	SetIsReplicated(bEnableReplication);

	Super::OnRegister();
}

void UGeometryCollectionComponent::ResetDynamicCollection()
{
	bool bCreateDynamicCollection = true;
#if WITH_EDITOR
	bCreateDynamicCollection = false;
	if (UWorld* World = GetWorld())
	{
		if(World->IsGameWorld())
		{
			bCreateDynamicCollection = true;
		}
	}
#endif
	//UE_LOG(UGCC_LOG, Log, TEXT("GeometryCollectionComponent[%p]::ResetDynamicCollection()"), static_cast<const void*>(this));
	if (bCreateDynamicCollection && RestCollection)
	{
		DynamicCollection = MakeUnique<FGeometryDynamicCollection>();
		for (const auto DynamicArray : CopyOnWriteAttributeList)
		{
			*DynamicArray = nullptr;
		}

		GetTransformArrayCopyOnWrite();
		GetParentArrayCopyOnWrite();
		GetChildrenArrayCopyOnWrite();
		GetSimulationTypeArrayCopyOnWrite();
		GetStatusFlagsArrayCopyOnWrite();
		SetRenderStateDirty();
	}

	if (RestCollection)
	{
		CalculateGlobalMatrices();
		CalculateLocalBounds();
	}
}

void UGeometryCollectionComponent::OnCreatePhysicsState()
{
/*#if WITH_PHYSX
	DummyBodySetup = NewObject<UBodySetup>(this, UBodySetup::StaticClass());
	DummyBodySetup->AggGeom.BoxElems.Add(FKBoxElem(1.0f));
	DummyBodyInstance.InitBody(DummyBodySetup, GetComponentToWorld(), this, nullptr);
	DummyBodyInstance.bNotifyRigidBodyCollision = BodyInstance.bNotifyRigidBodyCollision;
#endif
*/
	// Skip the chain - don't care about body instance setup
	UActorComponent::OnCreatePhysicsState();
	if (!Simulating) IsObjectLoading = false; // just mark as loaded if we are simulating.

/*#if WITH_PHYSX
	DummyBodyInstance.SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	DummyBodyInstance.SetResponseToAllChannels(ECR_Block);
#endif
*/

#if WITH_CHAOS
	// Static mesh uses an init framework that goes through FBodyInstance.  We
	// do the same thing, but through the geometry collection proxy and lambdas
	// defined below.  FBodyInstance doesn't work for geometry collections 
	// because FBodyInstance manages a single particle, where we have many.
	if (!PhysicsProxy)
	{
#if WITH_EDITOR && WITH_EDITORONLY_DATA
		EditorActor = nullptr;

		if (RestCollection)
		{
			//hack: find a better place for this
			UGeometryCollection* RestCollectionMutable = const_cast<UGeometryCollection*>(RestCollection);
			RestCollectionMutable->EnsureDataIsCooked();
		}
#endif
		const bool bValidWorld = GetWorld() && GetWorld()->IsGameWorld();
		const bool bValidCollection = DynamicCollection && DynamicCollection->Transform.Num() > 0;
		if (bValidWorld && bValidCollection)
		{
			FPhysxUserData::Set<UPrimitiveComponent>(&PhysicsUserData, this);

			FSimulationParameters SimulationParameters;
			{
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
				SimulationParameters.Name = GetPathName();
#endif
				if (RestCollection)
				{
					RestCollection->GetSharedSimulationParams(SimulationParameters.Shared);
					SimulationParameters.RestCollection = RestCollection->GetGeometryCollection().Get();
				}
				SimulationParameters.Simulating = Simulating;
				SimulationParameters.EnableClustering = EnableClustering;
				SimulationParameters.ClusterGroupIndex = EnableClustering ? ClusterGroupIndex : 0;
				SimulationParameters.MaxClusterLevel = MaxClusterLevel;
				SimulationParameters.DamageThreshold = DamageThreshold;
				SimulationParameters.ClusterConnectionMethod = (Chaos::FClusterCreationParameters::EConnectionMethod)ClusterConnectionType;
				SimulationParameters.CollisionGroup = CollisionGroup;
				SimulationParameters.CollisionSampleFraction = CollisionSampleFraction;
				SimulationParameters.InitialVelocityType = InitialVelocityType;
				SimulationParameters.InitialLinearVelocity = InitialLinearVelocity;
				SimulationParameters.InitialAngularVelocity = InitialAngularVelocity;
				SimulationParameters.bClearCache = true;
				SimulationParameters.ObjectType = ObjectType;
				SimulationParameters.CacheType = CacheParameters.CacheMode;
				SimulationParameters.ReverseCacheBeginTime = CacheParameters.ReverseCacheBeginTime;
				SimulationParameters.CollisionData.SaveCollisionData = CacheParameters.SaveCollisionData;
				SimulationParameters.CollisionData.DoGenerateCollisionData = CacheParameters.DoGenerateCollisionData;
				SimulationParameters.CollisionData.CollisionDataSizeMax = CacheParameters.CollisionDataSizeMax;
				SimulationParameters.CollisionData.DoCollisionDataSpatialHash = CacheParameters.DoCollisionDataSpatialHash;
				SimulationParameters.CollisionData.CollisionDataSpatialHashRadius = CacheParameters.CollisionDataSpatialHashRadius;
				SimulationParameters.CollisionData.MaxCollisionPerCell = CacheParameters.MaxCollisionPerCell;
				SimulationParameters.BreakingData.SaveBreakingData = CacheParameters.SaveBreakingData;
				SimulationParameters.BreakingData.DoGenerateBreakingData = CacheParameters.DoGenerateBreakingData;
				SimulationParameters.BreakingData.BreakingDataSizeMax = CacheParameters.BreakingDataSizeMax;
				SimulationParameters.BreakingData.DoBreakingDataSpatialHash = CacheParameters.DoBreakingDataSpatialHash;
				SimulationParameters.BreakingData.BreakingDataSpatialHashRadius = CacheParameters.BreakingDataSpatialHashRadius;
				SimulationParameters.BreakingData.MaxBreakingPerCell = CacheParameters.MaxBreakingPerCell;
				SimulationParameters.TrailingData.SaveTrailingData = CacheParameters.SaveTrailingData;
				SimulationParameters.TrailingData.DoGenerateTrailingData = CacheParameters.DoGenerateTrailingData;
				SimulationParameters.TrailingData.TrailingDataSizeMax = CacheParameters.TrailingDataSizeMax;
				SimulationParameters.TrailingData.TrailingMinSpeedThreshold = CacheParameters.TrailingMinSpeedThreshold;
				SimulationParameters.TrailingData.TrailingMinVolumeThreshold = CacheParameters.TrailingMinVolumeThreshold;
				SimulationParameters.RemoveOnFractureEnabled = SimulationParameters.Shared.RemoveOnFractureIndices.Num() > 0;
				SimulationParameters.WorldTransform = GetComponentToWorld();
				SimulationParameters.UserData = static_cast<void*>(&PhysicsUserData);

				UPhysicalMaterial* EnginePhysicalMaterial = GetPhysicalMaterial();
				if(ensure(EnginePhysicalMaterial))
				{
					SimulationParameters.PhysicalMaterialHandle = EnginePhysicalMaterial->GetPhysicsMaterial();
				}
			}


			//
			// Called from FGeometryCollectionPhysicsProxy::Initialize()
			//
			auto InitFunc = [this](FSimulationParameters& InParams)
			{
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
				InParams.Name = GetPathName();
#endif
				GetInitializationCommands(InParams.InitializationCommands);
				UGeometryCollectionCache* Cache = CacheParameters.TargetCache;
				if(Cache && CacheParameters.CacheMode != EGeometryCollectionCacheType::None)
				{
					bool bCacheValid = false;

					switch(CacheParameters.CacheMode)
					{
					case EGeometryCollectionCacheType::Record:
					case EGeometryCollectionCacheType::RecordAndPlay:
						bCacheValid = Cache->CompatibleWithForRecord(RestCollection);
						break;
					case EGeometryCollectionCacheType::Play:
						bCacheValid = Cache->CompatibleWithForPlayback(RestCollection);
						break;
					default:
						check(false);
						break;
					}

					if(bCacheValid)
					{
						InParams.RecordedTrack = (InParams.IsCachePlaying() && CacheParameters.TargetCache) ? CacheParameters.TargetCache->GetData() : nullptr;
					}
					else
					{
						// We attempted to utilize a cache that was not compatible with this component. Report and do not use
						UE_LOG(LogChaos, Error, TEXT("Geometry collection '%s' attempted to use cache '%s' but it is not compatible."), *GetName(), *(CacheParameters.TargetCache->GetName()));
						InParams.RecordedTrack = nullptr;
						InParams.CacheType = EGeometryCollectionCacheType::None;
						CacheParameters.CacheMode = EGeometryCollectionCacheType::None;

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
						FMessageLog("PIE").Error()
							->AddToken(FTextToken::Create(NSLOCTEXT("GeomCollectionComponent", "BadCache_01", "Geometry collection")))
							->AddToken(FUObjectToken::Create(this))
							->AddToken(FTextToken::Create(NSLOCTEXT("GeomCollectionComponent", "BadCache_02", "attempted to use cache")))
							->AddToken(FUObjectToken::Create(CacheParameters.TargetCache))
							->AddToken(FTextToken::Create(NSLOCTEXT("GeomCollectionComponent", "BadCache_03", "but it is not compatible")));
#endif // !(UE_BUILD_SHIPPING || UE_BUILD_TEST)

					}
				}

			};

			auto CacheSyncFunc = [this](const FGeometryCollectionResults& Results)
			{
#if GEOMETRYCOLLECTION_DEBUG_DRAW
				const bool bHasNumParticlesChanged = (NumParticlesAdded != Results.NumParticlesAdded);  // Needs to be evaluated before NumParticlesAdded gets updated
#endif  // #if GEOMETRYCOLLECTION_DEBUG_DRAW
				//RigidBodyIds.Init(Results.RigidBodyIds);
				BaseRigidBodyIndex = Results.BaseIndex;
				NumParticlesAdded = Results.NumParticlesAdded;
				DisabledFlags = Results.DisabledStates;
			
				if (!IsObjectDynamic && Results.IsObjectDynamic)
				{
					IsObjectDynamic = Results.IsObjectDynamic;

					NotifyGeometryCollectionPhysicsStateChange.Broadcast(this);
					SwitchRenderModels(GetOwner());
				}

				if (IsObjectLoading && !Results.IsObjectLoading)
				{
					IsObjectLoading = Results.IsObjectLoading;

					NotifyGeometryCollectionPhysicsLoadingStateChange.Broadcast(this);
				}


				WorldBounds = Results.WorldBounds;

				// Update replication data for clients if necessary
				UpdateRepData();

#if GEOMETRYCOLLECTION_DEBUG_DRAW
				// Notify debug draw componentUGeometryCollectionDebugDrawComponent of particle changes
				if (bHasNumParticlesChanged)
				{
					if (const AGeometryCollectionActor* const Owner = Cast<AGeometryCollectionActor>(GetOwner()))
					{
						if (UGeometryCollectionDebugDrawComponent* const GeometryCollectionDebugDrawComponent = Owner->GetGeometryCollectionDebugDrawComponent())
						{
							GeometryCollectionDebugDrawComponent->OnClusterChanged();
						}
					}
				}
#endif  // #if GEOMETRYCOLLECTION_DEBUG_DRAW
			};

			auto FinalSyncFunc = [this](const FRecordedTransformTrack& InTrack)
			{
#if WITH_EDITOR && WITH_EDITORONLY_DATA
				if (CacheParameters.CacheMode == EGeometryCollectionCacheType::Record && InTrack.Records.Num() > 0)
				{
					Modify();
					if (!CacheParameters.TargetCache)
					{
						CacheParameters.TargetCache = UGeometryCollectionCache::CreateCacheForCollection(RestCollection);
					}

					if (CacheParameters.TargetCache)
					{
						// Queue this up to be dirtied after PIE ends
						FPhysScene_Chaos* Scene = GetInnerChaosScene();

						CacheParameters.TargetCache->PreEditChange(nullptr);
						CacheParameters.TargetCache->Modify();
						CacheParameters.TargetCache->SetFromRawTrack(InTrack);
						CacheParameters.TargetCache->PostEditChange();

						Scene->AddPieModifiedObject(CacheParameters.TargetCache);

						if (EditorActor)
						{
							UGeometryCollectionComponent* EditorComponent = Cast<UGeometryCollectionComponent>(EditorUtilities::FindMatchingComponentInstance(this, EditorActor));

							if (EditorComponent)
							{
								EditorComponent->PreEditChange(FindFProperty<FProperty>(EditorComponent->GetClass(), GET_MEMBER_NAME_CHECKED(UGeometryCollectionComponent, CacheParameters)));
								EditorComponent->Modify();

								EditorComponent->CacheParameters.TargetCache = CacheParameters.TargetCache;

								EditorComponent->PostEditChange();

								Scene->AddPieModifiedObject(EditorComponent);
								Scene->AddPieModifiedObject(EditorActor);
							}

							EditorActor = nullptr;
						}
					}
				}
#endif
			};

			// If the Component is set to Dynamic, we look to the RestCollection for initial dynamic state override per transform.
			TManagedArray<int32>& DynamicState = DynamicCollection->DynamicState;

			if (ObjectType != EObjectStateTypeEnum::Chaos_Object_UserDefined)
			{
				if (RestCollection && (ObjectType == EObjectStateTypeEnum::Chaos_Object_Dynamic))
				{
					TManagedArray<int32>& InitialDynamicState = RestCollection->GetGeometryCollection()->InitialDynamicState;
					for (int i = 0; i < DynamicState.Num(); i++)
					{
						DynamicState[i] = (InitialDynamicState[i] == static_cast<int32>(Chaos::EObjectStateType::Uninitialized)) ? static_cast<int32>(ObjectType) : InitialDynamicState[i];
					}
				}
				else
				{
					for (int i = 0; i < DynamicState.Num(); i++)
					{
						DynamicState[i] = static_cast<int32>(ObjectType);
					}
				}
			}

			TManagedArray<bool> & Active = DynamicCollection->Active;
			{
				for (int i = 0; i < Active.Num(); i++)
				{
					Active[i] = Simulating;
				}
			}
			TManagedArray<int32> & CollisionGroupArray = DynamicCollection->CollisionGroup;
			{
				for (int i = 0; i < CollisionGroupArray.Num(); i++)
				{
					CollisionGroupArray[i] = CollisionGroup;
				}
			}
			// end temporary 

			// Set up initial filter data for our particles
			// #BGTODO We need a dummy body setup for now to allow the body instance to generate filter information. Change body instance to operate independently.
			DummyBodySetup = NewObject<UBodySetup>(this, UBodySetup::StaticClass());
			BodyInstance.BodySetup = DummyBodySetup;

			FBodyCollisionFilterData FilterData;
			FMaskFilter FilterMask = BodyInstance.GetMaskFilter();
			BodyInstance.BuildBodyFilterData(FilterData);

			InitialSimFilter = FilterData.SimFilter;
			InitialQueryFilter = FilterData.QuerySimpleFilter;

			// Enable for complex and simple (no dual representation currently like other meshes)
			InitialQueryFilter.Word3 |= (EPDF_SimpleCollision | EPDF_ComplexCollision);
			InitialSimFilter.Word3 |= (EPDF_SimpleCollision | EPDF_ComplexCollision);

 			PhysicsProxy = new FGeometryCollectionPhysicsProxy(this, *DynamicCollection, SimulationParameters, InitialQueryFilter, InitialSimFilter, InitFunc, CacheSyncFunc, FinalSyncFunc);
			FPhysScene_Chaos* Scene = GetInnerChaosScene();
			Scene->AddObject(this, PhysicsProxy);

			RegisterForEvents();
		}
	}

#if WITH_PHYSX && !WITH_CHAOS_NEEDS_TO_BE_FIXED
	if (PhysicsProxy)
	{
		GlobalGeomCollectionAccelerator.AddComponent(this);
	}
#endif
#endif // WITH_CHAOS
}

void UGeometryCollectionComponent::OnDestroyPhysicsState()
{
	UActorComponent::OnDestroyPhysicsState();

#if WITH_CHAOS
#if WITH_PHYSX && !WITH_CHAOS_NEEDS_TO_BE_FIXED
	GlobalGeomCollectionAccelerator.RemoveComponent(this);
#endif

#if WITH_PHYSX
	if(DummyBodyInstance.IsValidBodyInstance())
	{
		DummyBodyInstance.TermBody();
	}
#endif

	if(PhysicsProxy)
	{
		FPhysScene_Chaos* Scene = GetInnerChaosScene();
		Scene->RemoveObject(PhysicsProxy);
		InitializationState = ESimulationInitializationState::Unintialized;

		// Discard the pointer (cleanup happens through the scene or dedicated thread)
		PhysicsProxy = nullptr;
	}
#endif
}

void UGeometryCollectionComponent::SendRenderDynamicData_Concurrent()
{
	//UE_LOG(UGCC_LOG, Log, TEXT("GeometryCollectionComponent[%p]::SendRenderDynamicData_Concurrent()"), this);
	Super::SendRenderDynamicData_Concurrent();

	// Only update the dynamic data if the dynamic collection is dirty
	if (SceneProxy && ((DynamicCollection && DynamicCollection->IsDirty()) || CachePlayback)) 
	{
		FGeometryCollectionDynamicData * DynamicData = ::new FGeometryCollectionDynamicData;
		InitDynamicData(DynamicData);

		//
		// Only send dynamic data if the transform arrys on the past N frames are different
		//
		bool PastFramesTransformsAreEqual = true;
		for (int i = 0; i < TransformsAreEqual.Num(); ++i)
		{
			PastFramesTransformsAreEqual = PastFramesTransformsAreEqual && TransformsAreEqual[i];
		}

		if (PastFramesTransformsAreEqual)
		{
			delete DynamicData;
		}
		else
		{
			// Enqueue command to send to render thread
			FGeometryCollectionSceneProxy* GeometryCollectionSceneProxy = static_cast<FGeometryCollectionSceneProxy*>(SceneProxy);
			ENQUEUE_RENDER_COMMAND(SendRenderDynamicData)(
				[GeometryCollectionSceneProxy, DynamicData](FRHICommandListImmediate& RHICmdList)
				{
					if (GeometryCollectionSceneProxy)
					{
						GeometryCollectionSceneProxy->SetDynamicData_RenderThread(DynamicData);
					}
				}
			);
		}

		// mark collection clean now that we have rendered
		if (DynamicCollection)
		{
			DynamicCollection->MakeClean();
		}			
	}
}

void UGeometryCollectionComponent::SetRestCollection(const UGeometryCollection* RestCollectionIn)
{
	//UE_LOG(UGCC_LOG, Log, TEXT("GeometryCollectionComponent[%p]::SetRestCollection()"), this);
	if (RestCollectionIn)
	{
		RestCollection = RestCollectionIn;

		CalculateGlobalMatrices();
		CalculateLocalBounds();

		//ResetDynamicCollection();
	}
}

FGeometryCollectionEdit::FGeometryCollectionEdit(UGeometryCollectionComponent* InComponent, GeometryCollection::EEditUpdate InEditUpdate)
	: Component(InComponent)
	, EditUpdate(InEditUpdate)
{
	bHadPhysicsState = Component->HasValidPhysicsState();
	if (EnumHasAnyFlags(EditUpdate, GeometryCollection::EEditUpdate::Physics) && bHadPhysicsState)
	{
		Component->DestroyPhysicsState();
	}
}

FGeometryCollectionEdit::~FGeometryCollectionEdit()
{
#if WITH_EDITOR
	if (!!EditUpdate)
	{
		if (EnumHasAnyFlags(EditUpdate, GeometryCollection::EEditUpdate::Dynamic))
		{
			Component->ResetDynamicCollection();
		}

		if (EnumHasAnyFlags(EditUpdate, GeometryCollection::EEditUpdate::Rest) && GetRestCollection())
		{
			GetRestCollection()->Modify();
		}

		if (EnumHasAnyFlags(EditUpdate, GeometryCollection::EEditUpdate::Physics) && bHadPhysicsState)
		{
			Component->RecreatePhysicsState();
		}
	}
#endif
}

UGeometryCollection* FGeometryCollectionEdit::GetRestCollection()
{
	if (Component)
	{
		return const_cast<UGeometryCollection*>(Component->RestCollection);	//const cast is ok here since we are explicitly in edit mode. Should all this editor code be in an editor module?
	}
	return nullptr;
}

#if WITH_EDITOR
TArray<FLinearColor> FScopedColorEdit::RandomColors;

FScopedColorEdit::FScopedColorEdit(UGeometryCollectionComponent* InComponent, bool bForceUpdate) : bUpdated(bForceUpdate), Component(InComponent)
{
	if (RandomColors.Num() == 0)
	{
		FMath::RandInit(2019);
		for (int i = 0; i < 100; i++)
		{
			const FColor Color(FMath::Rand() % 100 + 5, FMath::Rand() % 100 + 5, FMath::Rand() % 100 + 5, 255);
			RandomColors.Push(FLinearColor(Color));
		}
	}
}

FScopedColorEdit::~FScopedColorEdit()
{
	if (bUpdated)
	{
		UpdateBoneColors();
	}
}
void FScopedColorEdit::SetShowBoneColors(bool ShowBoneColorsIn)
{
	if (Component->bShowBoneColors != ShowBoneColorsIn)
	{
		bUpdated = true;
		Component->bShowBoneColors = ShowBoneColorsIn;
	}
}

bool FScopedColorEdit::GetShowBoneColors() const
{
	return Component->bShowBoneColors;
}

void FScopedColorEdit::SetEnableBoneSelection(bool ShowSelectedBonesIn)
{
	if (Component->bEnableBoneSelection != ShowSelectedBonesIn)
	{
		bUpdated = true;
		Component->bEnableBoneSelection = ShowSelectedBonesIn;
	}
}

bool FScopedColorEdit::GetEnableBoneSelection() const
{
	return Component->bEnableBoneSelection;
}

bool FScopedColorEdit::IsBoneSelected(int BoneIndex) const
{
	return Component->SelectedBones.Contains(BoneIndex);
}

void FScopedColorEdit::SetSelectedBones(const TArray<int32>& SelectedBonesIn)
{
	bUpdated = true;
	Component->SelectedBones = SelectedBonesIn;
}

void FScopedColorEdit::AppendSelectedBones(const TArray<int32>& SelectedBonesIn)
{
	bUpdated = true;
	Component->SelectedBones.Append(SelectedBonesIn);
}

void FScopedColorEdit::ToggleSelectedBones(const TArray<int32>& SelectedBonesIn)
{
	bUpdated = true;
	for (int32 BoneIndex : SelectedBonesIn)
	{
		if (Component->SelectedBones.Contains(BoneIndex))
		{
			Component->SelectedBones.Remove(BoneIndex);
		}
		else
		{
			Component->SelectedBones.Add(BoneIndex);
		}
	}
}

void FScopedColorEdit::AddSelectedBone(int32 BoneIndex)
{
	if (!Component->SelectedBones.Contains(BoneIndex))
	{
		bUpdated = true;
		Component->SelectedBones.Push(BoneIndex);
	}
}

void FScopedColorEdit::ClearSelectedBone(int32 BoneIndex)
{
	if (Component->SelectedBones.Contains(BoneIndex))
	{
		bUpdated = true;
		Component->SelectedBones.Remove(BoneIndex);
	}
}

const TArray<int32>& FScopedColorEdit::GetSelectedBones() const
{
	return Component->GetSelectedBones();
}

void FScopedColorEdit::ResetBoneSelection()
{
	if (Component->SelectedBones.Num() > 0)
	{
		bUpdated = true;
	}

	Component->SelectedBones.Empty();
}

void FScopedColorEdit::SelectBones(GeometryCollection::ESelectionMode SelectionMode)
{
	check(Component);

	const UGeometryCollection* GeometryCollection = Component->GetRestCollection();
	if (GeometryCollection)
	{
		TSharedPtr<FGeometryCollection, ESPMode::ThreadSafe> GeometryCollectionPtr = GeometryCollection->GetGeometryCollection();

		switch (SelectionMode)
		{
		case GeometryCollection::ESelectionMode::None:
			ResetBoneSelection();
			break;

		case GeometryCollection::ESelectionMode::AllGeometry:
		{
			TArray<int32> Roots;
			FGeometryCollectionClusteringUtility::GetRootBones(GeometryCollectionPtr.Get(), Roots);
			ResetBoneSelection();
			for (int32 RootElement : Roots)
			{
				TArray<int32> LeafBones;
				FGeometryCollectionClusteringUtility::GetLeafBones(GeometryCollectionPtr.Get(), RootElement, true, LeafBones);
				AppendSelectedBones(LeafBones);
			}
		}
		break;

		case GeometryCollection::ESelectionMode::InverseGeometry:
		{
			TArray<int32> Roots;
			FGeometryCollectionClusteringUtility::GetRootBones(GeometryCollectionPtr.Get(), Roots);
			TArray<int32> NewSelection;
			for (int32 RootElement : Roots)
			{
				TArray<int32> LeafBones;
				FGeometryCollectionClusteringUtility::GetLeafBones(GeometryCollectionPtr.Get(), RootElement, true, LeafBones);

				for (int32 Element : LeafBones)
				{
					if (!IsBoneSelected(Element))
					{
						NewSelection.Push(Element);
					}
				}
			}
			ResetBoneSelection();
			AppendSelectedBones(NewSelection);
		}
		break;


		case GeometryCollection::ESelectionMode::Neighbors:
		{
			if (ensureMsgf(GeometryCollectionPtr->HasAttribute("Proximity", FGeometryCollection::GeometryGroup),
				TEXT("Must build breaking group for neighbor based selection")))
			{

				const TManagedArray<int32>& TransformIndex = GeometryCollectionPtr->TransformIndex;
				const TManagedArray<int32>& TransformToGeometryIndex = GeometryCollectionPtr->TransformToGeometryIndex;
				const TManagedArray<TSet<int32>>& Proximity = GeometryCollectionPtr->GetAttribute<TSet<int32>>("Proximity", FGeometryCollection::GeometryGroup);

				const TArray<int32> SelectedBones = GetSelectedBones();

				TArray<int32> NewSelection;
				for (int32 Bone : SelectedBones)
				{
					NewSelection.AddUnique(Bone);
					const TSet<int32> &Neighbors = Proximity[TransformToGeometryIndex[Bone]];
					for (int32 NeighborGeometryIndex : Neighbors)
					{
						NewSelection.AddUnique(TransformIndex[NeighborGeometryIndex]);
					}
				}

				ResetBoneSelection();
				AppendSelectedBones(NewSelection);
			}
		}
		break;

		case GeometryCollection::ESelectionMode::Siblings:
		{
			const TManagedArray<int32>& Parents = GeometryCollectionPtr->Parent;
			const TManagedArray<TSet<int32>>& Children = GeometryCollectionPtr->Children;

			const TArray<int32> SelectedBones = GetSelectedBones();

			TArray<int32> NewSelection;
			for (int32 Bone : SelectedBones)
			{
				int32 ParentBone = Parents[Bone];
				if (ParentBone != FGeometryCollection::Invalid)
				{
					for (int32 Child : Children[ParentBone])
					{
						NewSelection.AddUnique(Child);
					}
				}

			}

			ResetBoneSelection();
			AppendSelectedBones(NewSelection);
		}
		break;

		case GeometryCollection::ESelectionMode::AllInCluster:
		{
			const TManagedArray<int32>& Parents = GeometryCollectionPtr->Parent;

			const TArray<int32> SelectedBones = GetSelectedBones();

			TArray<int32> NewSelection;
			for (int32 Bone : SelectedBones)
			{
				int32 ParentBone = Parents[Bone];
				TArray<int32> LeafBones;
				FGeometryCollectionClusteringUtility::GetLeafBones(GeometryCollectionPtr.Get(), ParentBone, true, LeafBones);

				for (int32 Element : LeafBones)
				{
					NewSelection.AddUnique(Element);
				}

			}

			ResetBoneSelection();
			AppendSelectedBones(NewSelection);
		}
		break;

		default: 
			check(false); // unexpected selection mode
		break;
		}

		const TArray<int32>& SelectedBones = GetSelectedBones();
		SetHighlightedBones(SelectedBones);
	}
}

bool FScopedColorEdit::IsBoneHighlighted(int BoneIndex) const
{
	return Component->HighlightedBones.Contains(BoneIndex);
}

void FScopedColorEdit::SetHighlightedBones(const TArray<int32>& HighlightedBonesIn)
{
	if (Component->HighlightedBones != HighlightedBonesIn)
	{
		bUpdated = true;
		Component->HighlightedBones = HighlightedBonesIn;
	}
}

void FScopedColorEdit::AddHighlightedBone(int32 BoneIndex)
{
	Component->HighlightedBones.Push(BoneIndex);
}

const TArray<int32>& FScopedColorEdit::GetHighlightedBones() const
{
	return Component->GetHighlightedBones();
}

void FScopedColorEdit::ResetHighlightedBones()
{
	if (Component->HighlightedBones.Num() > 0)
	{
		bUpdated = true;
		Component->HighlightedBones.Empty();

	}
}

void FScopedColorEdit::SetLevelViewMode(int ViewLevelIn)
{
	if (Component->ViewLevel != ViewLevelIn)
	{
		bUpdated = true;
		Component->ViewLevel = ViewLevelIn;
	}
}

int FScopedColorEdit::GetViewLevel()
{
	return Component->ViewLevel;
}

void FScopedColorEdit::UpdateBoneColors()
{
	// @todo FractureTools - For large fractures updating colors this way is extremely slow because the render state (and thus all buffers) must be recreated.
	// It would be better to push the update to the proxy via a render command and update the existing buffer directly
	FGeometryCollectionEdit GeometryCollectionEdit = Component->EditRestCollection(GeometryCollection::EEditUpdate::None);
	UGeometryCollection* GeometryCollection = GeometryCollectionEdit.GetRestCollection();
	if(GeometryCollection)
	{
		FGeometryCollection* Collection = GeometryCollection->GetGeometryCollection().Get();

		FLinearColor BlankColor(FColor(80, 80, 80, 50));

		const TManagedArray<int>& Parents = Collection->Parent;
		bool HasLevelAttribute = Collection->HasAttribute("Level", FTransformCollection::TransformGroup);
		TManagedArray<int>* Levels = nullptr;
		if (HasLevelAttribute)
		{
			Levels = &Collection->GetAttribute<int32>("Level", FTransformCollection::TransformGroup);
		}
		TManagedArray<FLinearColor>& BoneColors = Collection->BoneColor;

		for (int32 BoneIndex = 0, NumBones = Parents.Num() ; BoneIndex < NumBones; ++BoneIndex)
		{
			FLinearColor BoneColor = FLinearColor(FColor::Black);

			if (Component->ViewLevel == -1)
			{
				BoneColor = RandomColors[BoneIndex % RandomColors.Num()];
			}
			else
			{
				if (HasLevelAttribute && (*Levels)[BoneIndex] >= Component->ViewLevel)
				{
					// go up until we find parent at the required ViewLevel
					int32 Bone = BoneIndex;
					while (Bone != -1 && (*Levels)[Bone] > Component->ViewLevel)
					{
						Bone = Parents[Bone];
					}

					int32 ColorIndex = Bone + 1; // parent can be -1 for root, range [-1..n]
					BoneColor = RandomColors[ColorIndex % RandomColors.Num()];

					BoneColor.LinearRGBToHSV();
					BoneColor.B *= .5;
					BoneColor.HSVToLinearRGB();
				}
				else
				{
					BoneColor = BlankColor;
				}
			}

			// store the bone selected toggle in alpha so we can use it in the shader
			BoneColor.A = IsBoneHighlighted(BoneIndex) ? 1 : 0;

			BoneColors[BoneIndex] = BoneColor;
		}

		Component->MarkRenderStateDirty();
		Component->MarkRenderDynamicDataDirty();
	}
}
#endif

void UGeometryCollectionComponent::ApplyKinematicField(float Radius, FVector Position)
{
	FName TargetName = GetGeometryCollectionPhysicsTypeName(EGeometryCollectionPhysicsTypeEnum::Chaos_DynamicState);
	DispatchFieldCommand({ TargetName,new FRadialIntMask(Radius, Position, (int32)Chaos::EObjectStateType::Dynamic,
		(int32)Chaos::EObjectStateType::Kinematic, ESetMaskConditionType::Field_Set_IFF_NOT_Interior) });
}

void UGeometryCollectionComponent::ApplyPhysicsField(bool Enabled, EGeometryCollectionPhysicsTypeEnum Target, UFieldSystemMetaData* MetaData, UFieldNodeBase* Field)
{
	if (Enabled && Field)
	{
		FFieldSystemCommand Command = FFieldObjectCommands::CreateFieldCommand(GetGeometryCollectionPhysicsTypeName(Target), Field, MetaData);
		DispatchFieldCommand(Command);
	}
}

void UGeometryCollectionComponent::DispatchFieldCommand(const FFieldSystemCommand& InCommand)
{
	if (PhysicsProxy)
	{
		FChaosSolversModule* ChaosModule = FChaosSolversModule::GetModule();
		checkSlow(ChaosModule);

		auto Solver = PhysicsProxy->GetSolver<Chaos::FPBDRigidsSolver>();
		Solver->EnqueueCommandImmediate([Solver, PhysicsProxy = this->PhysicsProxy, NewCommand = InCommand]()
		{
			// Pass through nullptr here as geom component commands can never affect other solvers
			PhysicsProxy->BufferCommand(Solver, NewCommand);
		});
	}
}

void UGeometryCollectionComponent::GetInitializationCommands(TArray<FFieldSystemCommand>& CombinedCommmands)
{
	CombinedCommmands.Reset();
	for (const AFieldSystemActor* FieldSystemActor : InitializationFields)
	{
		if (FieldSystemActor != nullptr)
		{
			if (FieldSystemActor->GetFieldSystemComponent())
			{
				const int32 NumCommands = FieldSystemActor->GetFieldSystemComponent()->ConstructionCommands.GetNumCommands();
				if (NumCommands > 0)
				{
					for (int32 CommandIndex = 0; CommandIndex < NumCommands; ++CommandIndex)
					{
						CombinedCommmands.Add(FieldSystemActor->GetFieldSystemComponent()->ConstructionCommands.BuildFieldCommand(CommandIndex));
					}
				}
				// Legacy path : only there for old levels. New ones will have the commands directly saved onto the component
				else if (FieldSystemActor->GetFieldSystemComponent()->GetFieldSystem())
				{
					for (const FFieldSystemCommand& Command : FieldSystemActor->GetFieldSystemComponent()->GetFieldSystem()->Commands)
					{
						FFieldSystemCommand NewCommand = { Command.TargetAttribute, Command.RootNode->NewCopy() };
						for (auto& Elem : Command.MetaData)
						{
							NewCommand.MetaData.Add(Elem.Key, TUniquePtr<FFieldSystemMetaData>(Elem.Value->NewCopy()));
						}
						CombinedCommmands.Add(NewCommand);
					}
				}
			}
		}
	}
}

FPhysScene_Chaos* UGeometryCollectionComponent::GetInnerChaosScene() const
{
	if (ChaosSolverActor)
	{
		return ChaosSolverActor->GetPhysicsScene().Get();
	}
	else
	{
#if INCLUDE_CHAOS
		if (ensure(GetOwner()) && ensure(GetOwner()->GetWorld()))
		{
			return GetOwner()->GetWorld()->GetPhysicsScene();
		}
		check(GWorld);
		return GWorld->GetPhysicsScene();
#else
		return nullptr;
#endif
	}
}

AChaosSolverActor* UGeometryCollectionComponent::GetPhysicsSolverActor() const
{
	if (ChaosSolverActor)
	{
		return ChaosSolverActor;
	}
	else
	{
		FPhysScene_Chaos const* const Scene = GetInnerChaosScene();
		return Scene ? Cast<AChaosSolverActor>(Scene->GetSolverActor()) : nullptr;
	}

	return nullptr;
}

void UGeometryCollectionComponent::CalculateLocalBounds()
{
	LocalBounds.Init();
	const TManagedArray<FBox>& BoundingBoxes = GetBoundingBoxArray();
	const TManagedArray<int32>& TransformIndices = GetTransformIndexArray();

	const int32 NumBoxes = BoundingBoxes.Num();

	for (int32 BoxIdx = 0; BoxIdx < NumBoxes; ++BoxIdx)
	{
		const int32 TransformIndex = TransformIndices[BoxIdx];

		if (GetRestCollection()->GetGeometryCollection()->IsGeometry(TransformIndex))
		{
			LocalBounds += BoundingBoxes[BoxIdx];
		}
	}
}

void UGeometryCollectionComponent::CalculateGlobalMatrices()
{
	SCOPE_CYCLE_COUNTER(STAT_GCCUGlobalMatrices);

	const FGeometryCollectionResults* Results = PhysicsProxy ? PhysicsProxy->GetConsumerResultsGT() : nullptr;

	const int32 NumTransforms = Results ? Results->GlobalTransforms.Num() : 0;
	if(NumTransforms > 0)
	{
		// Just calc from results
		GlobalMatrices.Empty();
		GlobalMatrices.Append(Results->GlobalTransforms);		
	}
	else
	{
		// Have to fully rebuild
		GeometryCollectionAlgo::GlobalMatrices(GetTransformArray(), GetParentArray(), GlobalMatrices);
	}

#if WITH_EDITOR
	if (GlobalMatrices.Num() > 0)
	{
		if (RestCollection->GetGeometryCollection()->HasAttribute("ExplodedVector", FGeometryCollection::TransformGroup))
		{
			const TManagedArray<FVector>& ExplodedVectors = RestCollection->GetGeometryCollection()->GetAttribute<FVector>("ExplodedVector", FGeometryCollection::TransformGroup);

			check(GlobalMatrices.Num() == ExplodedVectors.Num());

			for (int32 tt = 0, nt = GlobalMatrices.Num(); tt < nt; ++tt)
			{
				GlobalMatrices[tt] = GlobalMatrices[tt].ConcatTranslation(ExplodedVectors[tt]);
			}
		}
	}
#endif
}

// #todo(dmp): for backwards compatibility with existing maps, we need to have a default of 3 materials.  Otherwise
// some existing test scenes will crash
int32 UGeometryCollectionComponent::GetNumMaterials() const
{
	return !RestCollection || RestCollection->Materials.Num() == 0 ? 3 : RestCollection->Materials.Num();
}

UMaterialInterface* UGeometryCollectionComponent::GetMaterial(int32 MaterialIndex) const
{
	// If we have a base materials array, use that
	if (OverrideMaterials.IsValidIndex(MaterialIndex) && OverrideMaterials[MaterialIndex])
	{
		return OverrideMaterials[MaterialIndex];
	}
	// Otherwise get from geom collection
	else
	{
		return RestCollection && RestCollection->Materials.IsValidIndex(MaterialIndex) ? RestCollection->Materials[MaterialIndex] : nullptr;
	}
}

// #temp HACK for demo, When fracture happens (physics state changes to dynamic) then switch the visible render meshes in a blueprint/actor from static meshes to geometry collections
void UGeometryCollectionComponent::SwitchRenderModels(const AActor* Actor)
{
	// Don't touch visibility if the component is not visible
	if (!IsVisible())
	{
		return;
	}

	TInlineComponentArray<UPrimitiveComponent*> PrimitiveComponents;
	Actor->GetComponents(PrimitiveComponents);
	for (UPrimitiveComponent* PrimitiveComponent : PrimitiveComponents)
	{
		bool ValidComponent = false;

		if (UStaticMeshComponent* StaticMeshComp = Cast<UStaticMeshComponent>(PrimitiveComponent))
		{
			StaticMeshComp->SetVisibility(false);
		}
		else if (UGeometryCollectionComponent* GeometryCollectionComponent = Cast<UGeometryCollectionComponent>(PrimitiveComponent))
		{
			if (!GeometryCollectionComponent->IsVisible())
			{
				continue;
			}

			GeometryCollectionComponent->SetVisibility(true);
		}
	}

	TInlineComponentArray<UChildActorComponent*> ChildActorComponents;
	Actor->GetComponents(ChildActorComponents);
	for (UChildActorComponent* ChildComponent : ChildActorComponents)
	{
		AActor* ChildActor = ChildComponent->GetChildActor();
		if (ChildActor)
		{
			SwitchRenderModels(ChildActor);
		}
	}

}

bool UGeometryCollectionComponent::IsEqual(const TArray<FMatrix> &A, const TArray<FMatrix> &B, const float Tolerance)
{
	if (A.Num() != B.Num())
	{
		return false;
	}

	for (int Index = 0; Index < A.Num(); ++Index)
	{
		if (!A[Index].Equals(B[Index], Tolerance))
		{
			return false;
		}
	}

	return true;
}

#if GEOMETRYCOLLECTION_EDITOR_SELECTION
void UGeometryCollectionComponent::EnableTransformSelectionMode(bool bEnable)
{
	if (SceneProxy && RestCollection && RestCollection->HasVisibleGeometry())
	{
		static_cast<FGeometryCollectionSceneProxy*>(SceneProxy)->UseSubSections(bEnable, true);
	}
	bIsTransformSelectionModeEnabled = bEnable;
}
#endif  // #if GEOMETRYCOLLECTION_EDITOR_SELECTION
