// Copyright Epic Games, Inc. All Rights Reserved.

#include "GeometryCollection/GeometryCollectionComponent.h"

#include "Async/ParallelFor.h"
#include "Components/BoxComponent.h"
#include "ComponentRecreateRenderStateContext.h"
#include "GeometryCollection/GeometryCollectionObject.h"
#include "GeometryCollection/GeometryCollectionAlgo.h"
#include "GeometryCollection/GeometryCollectionComponentPluginPrivate.h"
#include "GeometryCollection/GeometryCollectionSceneProxy.h"
#include "GeometryCollection/GeometryCollectionSQAccelerator.h"
#include "GeometryCollection/GeometryCollectionUtility.h"
#include "GeometryCollection/GeometryCollectionClusteringUtility.h"
#include "GeometryCollection/GeometryCollectionProximityUtility.h"
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
#include "PhysicsField/PhysicsFieldComponent.h"
#include "Engine/InstancedStaticMesh.h"

#if WITH_EDITOR
#include "AssetToolsModule.h"
#include "Editor.h"
#endif

#include "PhysicsEngine/BodySetup.h"
#include "PhysicsEngine/BodyInstance.h"
#include "Chaos/ChaosGameplayEventDispatcher.h"

#include "Rendering/NaniteResources.h"
#include "PrimitiveSceneInfo.h"

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

static_assert(sizeof(ispc::FMatrix) == sizeof(FMatrix), "sizeof(ispc::FMatrix) != sizeof(FMatrix)");
static_assert(sizeof(ispc::FBox) == sizeof(FBox), "sizeof(ispc::FBox) != sizeof(FBox)");
#endif

#if INTEL_ISPC && !UE_BUILD_SHIPPING
bool bChaos_BoxCalcBounds_ISPC_Enabled = true;
FAutoConsoleVariableRef CVarChaosBoxCalcBoundsISPCEnabled(TEXT("p.Chaos.BoxCalcBounds.ISPC"), bChaos_BoxCalcBounds_ISPC_Enabled, TEXT("Whether to use ISPC optimizations in calculating box bounds in geometry collections"));
#endif

DEFINE_LOG_CATEGORY_STATIC(UGCC_LOG, Error, All);

extern FGeometryCollectionDynamicDataPool GDynamicDataPool;

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

int32 GGeometryCollectionNanite = 1;
FAutoConsoleVariableRef CVarGeometryCollectionNanite(
	TEXT("r.GeometryCollection.Nanite"),
	GGeometryCollectionNanite,
	TEXT("Render geometry collections using Nanite."),
	FConsoleVariableDelegate::CreateLambda([](IConsoleVariable* InVariable)
	{
		FGlobalComponentRecreateRenderStateContext Context;
	}),
	ECVF_RenderThreadSafe
);

// Size in CM used as a threshold for whether a geometry in the collection is collected and exported for
// navigation purposes. Measured as the diagonal of the leaf node bounds.
float GGeometryCollectionNavigationSizeThreshold = 20.0f;
FAutoConsoleVariableRef CVarGeometryCollectionNavigationSizeThreshold(TEXT("p.GeometryCollectionNavigationSizeThreshold"), GGeometryCollectionNavigationSizeThreshold, TEXT("Size in CM used as a threshold for whether a geometry in the collection is collected and exported for navigation purposes. Measured as the diagonal of the leaf node bounds."));

// Single-Threaded Bounds
bool bGeometryCollectionSingleThreadedBoundsCalculation = false;
FAutoConsoleVariableRef CVarGeometryCollectionSingleThreadedBoundsCalculation(TEXT("p.GeometryCollectionSingleThreadedBoundsCalculation"), bGeometryCollectionSingleThreadedBoundsCalculation, TEXT("[Debug Only] Single threaded bounds calculation. [def:false]"));



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
	, InitializationState(ESimulationInitializationState::Unintialized)
	, ObjectType(EObjectStateTypeEnum::Chaos_Object_Dynamic)
	, bForceMotionBlur()
	, EnableClustering(true)
	, ClusterGroupIndex(0)
	, MaxClusterLevel(100)
	, DamageThreshold({ 500000.f, 50000.f, 5000.f })
	, bUseSizeSpecificDamageThreshold(false)
	, ClusterConnectionType_DEPRECATED(EClusterConnectionTypeEnum::Chaos_MinimalSpanningSubsetDelaunayTriangulation)
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
	, bNotifyRemovals(false)
	, bShowBoneColors(false)
	, bEnableReplication(false)
	, bEnableAbandonAfterLevel(false)
	, ReplicationAbandonClusterLevel(0)
	, bRenderStateDirty(true)
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
	, bIsMoving(false)
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

	SetGenerateOverlapEvents(false);

	// By default use the destructible object channel unless the user specifies otherwise
	BodyInstance.SetObjectType(ECC_Destructible);

	// By default, we initialize immediately. If this is set false, we defer initialization.
	BodyInstance.bSimulatePhysics = true;

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

#if WITH_EDITOR
	if (RestCollection != nullptr)
	{
		if (RestCollection->GetGeometryCollection()->HasAttribute("ExplodedVector", FGeometryCollection::TransformGroup))
		{
			RestCollection->GetGeometryCollection()->RemoveAttribute("ExplodedVector", FGeometryCollection::TransformGroup);
		}
	}
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
	else if (RestCollection)
	{			
		const FMatrix LocalToWorldWithScale = LocalToWorldIn.ToMatrixWithScale();

		FBox BoundingBox(ForceInit);

		//Hold on to reference so it doesn't get GC'ed
		auto HackGeometryCollectionPtr = RestCollection->GetGeometryCollection();

		const TManagedArray<FBox>& BoundingBoxes = GetBoundingBoxArray();
		const TManagedArray<int32>& TransformIndices = GetTransformIndexArray();
		const TManagedArray<int32>& ParentIndices = GetParentArray();
		const TManagedArray<int32>& TransformToGeometryIndex = GetTransformToGeometryIndexArray();
		const TManagedArray<FTransform>& Transforms = GetTransformArray();

		const int32 NumBoxes = BoundingBoxes.Num();
	
		int32 NumElements = HackGeometryCollectionPtr->NumElements(FGeometryCollection::TransformGroup);
		if (RestCollection->EnableNanite && HackGeometryCollectionPtr->HasAttribute("BoundingBox", FGeometryCollection::TransformGroup) && NumElements)
		{
			TArray<FMatrix> TmpGlobalMatrices;
			GeometryCollectionAlgo::GlobalMatrices(Transforms, ParentIndices, TmpGlobalMatrices);

			TManagedArray<FBox>& TransformBounds = HackGeometryCollectionPtr->GetAttribute<FBox>("BoundingBox", "Transform");
			for (int32 TransformIndex = 0; TransformIndex < HackGeometryCollectionPtr->NumElements(FGeometryCollection::TransformGroup); TransformIndex++)
			{
				BoundingBox += TransformBounds[TransformIndex].TransformBy(TmpGlobalMatrices[TransformIndex] * LocalToWorldWithScale);
			}
		}
		else if (NumElements == 0 || GlobalMatrices.Num() != RestCollection->NumElements(FGeometryCollection::TransformGroup))
		{
			// #todo(dmp): we could do the bbox transform in parallel with a bit of reformulating		
			// #todo(dmp):  there are some cases where the calcbounds function is called before the component
			// has set the global matrices cache while in the editor.  This is a somewhat weak guard against this
			// to default to just calculating tmp global matrices.  This should be removed or modified somehow
			// such that we always cache the global matrices and this method always does the correct behavior

			TArray<FMatrix> TmpGlobalMatrices;
			
			GeometryCollectionAlgo::GlobalMatrices(Transforms, ParentIndices, TmpGlobalMatrices);
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
		else if (bGeometryCollectionSingleThreadedBoundsCalculation)
		{
			CHAOS_ENSURE(false); // this is slower and only enabled through a pvar debugging, disable bGeometryCollectionSingleThreadedBoundsCalculation in a production environment. 
			for (int32 BoxIdx = 0; BoxIdx < NumBoxes; ++BoxIdx)
			{
				const int32 TransformIndex = TransformIndices[BoxIdx];

				if (RestCollection->GetGeometryCollection()->IsGeometry(TransformIndex))
				{
					BoundingBox += BoundingBoxes[BoxIdx].TransformBy(GlobalMatrices[TransformIndex] * LocalToWorldWithScale);
				}
			}
		}
		else
		{
			if (bChaos_BoxCalcBounds_ISPC_Enabled)
			{
#if INTEL_ISPC
				ispc::BoxCalcBounds(
					(int32*)&TransformToGeometryIndex[0],
					(int32*)&TransformIndices[0],
					(ispc::FMatrix*)&GlobalMatrices[0],
					(ispc::FBox*)&BoundingBoxes[0],
					(ispc::FMatrix&)LocalToWorldWithScale,
					(ispc::FBox&)BoundingBox,
					NumBoxes);
#endif
			}
			else
			{
				for (int32 BoxIdx = 0; BoxIdx < NumBoxes; ++BoxIdx)
				{
					const int32 TransformIndex = TransformIndices[BoxIdx];

					if (RestCollection->GetGeometryCollection()->IsGeometry(TransformIndex))
					{
						BoundingBox += BoundingBoxes[BoxIdx].TransformBy(GlobalMatrices[TransformIndex] * LocalToWorldWithScale);
					}
				}
			}
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
	static const auto NaniteProxyRenderModeVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.Nanite.ProxyRenderMode"));
	const int32 NaniteProxyRenderMode = (NaniteProxyRenderModeVar != nullptr) ? (NaniteProxyRenderModeVar->GetInt() != 0) : 0;

	FPrimitiveSceneProxy* LocalSceneProxy = nullptr;

	if (RestCollection)
	{
		if (UseNanite(GetScene()->GetShaderPlatform()) &&
			RestCollection->EnableNanite &&
			RestCollection->NaniteData != nullptr &&
			GGeometryCollectionNanite != 0)
		{
			LocalSceneProxy = new FNaniteGeometryCollectionSceneProxy(this);

			// ForceMotionBlur means we maintain bIsMoving, regardless of actual state.
			if (bForceMotionBlur)
			{
				bIsMoving = true;
				if (LocalSceneProxy)
				{
					FNaniteGeometryCollectionSceneProxy* NaniteProxy = static_cast<FNaniteGeometryCollectionSceneProxy*>(LocalSceneProxy);
					ENQUEUE_RENDER_COMMAND(NaniteProxyOnMotionEnd)(
						[NaniteProxy](FRHICommandListImmediate& RHICmdList)
						{
							NaniteProxy->OnMotionBegin();
						}
					);
				}
			}
		}
		// If we didn't get a proxy, but Nanite was enabled on the asset when it was built, evaluate proxy creation
		else if (RestCollection->EnableNanite && NaniteProxyRenderMode != 0)
		{
			// Do not render Nanite proxy
			return nullptr;
		}
		else
		{
			LocalSceneProxy = new FGeometryCollectionSceneProxy(this);
		}

		if (RestCollection->HasVisibleGeometry())
		{
			FGeometryCollectionConstantData* const ConstantData = ::new FGeometryCollectionConstantData;
			InitConstantData(ConstantData);

			FGeometryCollectionDynamicData* const DynamicData = InitDynamicData(true /* initialization */);

			if (LocalSceneProxy->IsNaniteMesh())
			{
				FNaniteGeometryCollectionSceneProxy* const GeometryCollectionSceneProxy = static_cast<FNaniteGeometryCollectionSceneProxy*>(LocalSceneProxy);

				// ...

			#if GEOMETRYCOLLECTION_EDITOR_SELECTION
				if (bIsTransformSelectionModeEnabled)
				{
					// ...
				}
			#endif

				ENQUEUE_RENDER_COMMAND(CreateRenderState)(
					[GeometryCollectionSceneProxy, ConstantData, DynamicData](FRHICommandListImmediate& RHICmdList)
					{
						GeometryCollectionSceneProxy->SetConstantData_RenderThread(ConstantData);

						if (DynamicData)
						{
							GeometryCollectionSceneProxy->SetDynamicData_RenderThread(DynamicData);
						}

						bool bValidUpdate = false;
						if (FPrimitiveSceneInfo* PrimitiveSceneInfo = GeometryCollectionSceneProxy->GetPrimitiveSceneInfo())
						{
							bValidUpdate = PrimitiveSceneInfo->RequestGPUSceneUpdate();
						}

						// Deferred the GPU Scene update if the primitive scene info is not yet initialized with a valid index.
						GeometryCollectionSceneProxy->SetRequiresGPUSceneUpdate_RenderThread(!bValidUpdate);
					}
				);
			}
			else
			{
				FGeometryCollectionSceneProxy* const GeometryCollectionSceneProxy = static_cast<FGeometryCollectionSceneProxy*>(LocalSceneProxy);

			#if GEOMETRYCOLLECTION_EDITOR_SELECTION
				// Re-init subsections
				if (bIsTransformSelectionModeEnabled)
				{
					GeometryCollectionSceneProxy->UseSubSections(true, false);  // Do not force reinit now, it'll be done in SetConstantData_RenderThread
				}
			#endif

				ENQUEUE_RENDER_COMMAND(CreateRenderState)(
					[GeometryCollectionSceneProxy, ConstantData, DynamicData](FRHICommandListImmediate& RHICmdList)
					{
						GeometryCollectionSceneProxy->SetConstantData_RenderThread(ConstantData);
						if (DynamicData)
						{
							GeometryCollectionSceneProxy->SetDynamicData_RenderThread(DynamicData);
						}
					}
				);
			}
		}
	}

	return LocalSceneProxy;
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

void UGeometryCollectionComponent::SetNotifyRemovals(bool bNewNotifyRemovals)
{
	if (bNotifyRemovals != bNewNotifyRemovals)
	{
		bNotifyRemovals = bNewNotifyRemovals;
		UpdateRemovalEventRegistration();
	}
}

FBodyInstance* UGeometryCollectionComponent::GetBodyInstance(FName BoneName /*= NAME_None*/, bool bGetWelded /*= true*/, int32 Index /*=INDEX_NONE*/) const
{
	return nullptr;// const_cast<FBodyInstance*>(&DummyBodyInstance);
}

void UGeometryCollectionComponent::SetNotifyRigidBodyCollision(bool bNewNotifyRigidBodyCollision)
{
	Super::SetNotifyRigidBodyCollision(bNewNotifyRigidBodyCollision);
	UpdateRBCollisionEventRegistration();
}

bool UGeometryCollectionComponent::CanEditSimulatePhysics()
{
	return true;
}

void UGeometryCollectionComponent::SetSimulatePhysics(bool bEnabled)
{
	Super::SetSimulatePhysics(bEnabled);

	if (bEnabled && !PhysicsProxy)
	{
		RegisterAndInitializePhysicsProxy();
	}

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

void UGeometryCollectionComponent::DispatchRemovalEvent(const FChaosRemovalEvent& Event)
{
	// native
	NotifyRemoval(Event);

	// bp
	if (OnChaosRemovalEvent.IsBound())
	{
		OnChaosRemovalEvent.Broadcast(Event);
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
	const TManagedArray<FVector3f>& Vertex = Collection->Vertex;

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
				FVector const VertexInWorldSpace = GeomToComponent[SubsetIndex].TransformPosition((FVector)Vertex[SourceGeometryVertexIndex]);

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

void UGeometryCollectionComponent::RefreshEmbeddedGeometry()
{
	
	const TManagedArray<int32>& ExemplarIndexArray = GetExemplarIndexArray();
	const int32 TransformCount = GlobalMatrices.Num();
	
	const TManagedArray<bool>* HideArray = nullptr;
	if (RestCollection->GetGeometryCollection()->HasAttribute("Hide", FGeometryCollection::TransformGroup))
	{
		HideArray = &RestCollection->GetGeometryCollection()->GetAttribute<bool>("Hide", FGeometryCollection::TransformGroup);
	}

#if WITH_EDITOR	
	EmbeddedInstanceIndex.Init(INDEX_NONE, RestCollection->GetGeometryCollection()->NumElements(FGeometryCollection::TransformGroup));
#endif

	const int32 ExemplarCount = EmbeddedGeometryComponents.Num();
	for (int32 ExemplarIndex = 0; ExemplarIndex < ExemplarCount; ++ExemplarIndex)
	{		
#if WITH_EDITOR
		EmbeddedBoneMaps[ExemplarIndex].Empty(TransformCount);
		EmbeddedBoneMaps[ExemplarIndex].Reserve(TransformCount); // Allocate for worst case
#endif
		
		TArray<FTransform> InstanceTransforms;
		InstanceTransforms.Reserve(TransformCount); // Allocate for worst case

		// Construct instance transforms for this exemplar
		for (int32 Idx = 0; Idx < TransformCount; ++Idx)
		{
			if (ExemplarIndexArray[Idx] == ExemplarIndex)
			{
				if (!HideArray || !(*HideArray)[Idx])
				{ 
					InstanceTransforms.Add(FTransform(GlobalMatrices[Idx]));
#if WITH_EDITOR
					int32 InstanceIndex = EmbeddedBoneMaps[ExemplarIndex].Add(Idx);
					EmbeddedInstanceIndex[Idx] = InstanceIndex;
#endif
				}
			}
		}

		if (EmbeddedGeometryComponents[ExemplarIndex])
		{
			const int32 InstanceCount = EmbeddedGeometryComponents[ExemplarIndex]->GetInstanceCount();

			// If the number of instances has changed, we rebuild the structure.
			if (InstanceCount != InstanceTransforms.Num())
			{
				EmbeddedGeometryComponents[ExemplarIndex]->ClearInstances();
				EmbeddedGeometryComponents[ExemplarIndex]->PreAllocateInstancesMemory(InstanceTransforms.Num());
				for (const FTransform& InstanceTransform : InstanceTransforms)
				{
					EmbeddedGeometryComponents[ExemplarIndex]->AddInstance(InstanceTransform);
				}
				EmbeddedGeometryComponents[ExemplarIndex]->MarkRenderStateDirty();
			}
			else
			{
				// #todo (bmiller) When ISMC has been changed to be able to update transforms in place, we need to switch this function call over.

				EmbeddedGeometryComponents[ExemplarIndex]->BatchUpdateInstancesTransforms(0, InstanceTransforms, false, true, false);

				// EmbeddedGeometryComponents[ExemplarIndex]->UpdateKinematicTransforms(InstanceTransforms);
			}	
		}
	}
}

#if WITH_EDITOR
void UGeometryCollectionComponent::SetEmbeddedGeometrySelectable(bool bSelectableIn)
{
	for (TObjectPtr<UInstancedStaticMeshComponent> EmbeddedGeometryComponent : EmbeddedGeometryComponents)
	{
		EmbeddedGeometryComponent->bSelectable = bSelectable;
		EmbeddedGeometryComponent->bHasPerInstanceHitProxies = bSelectable;
	}
}

int32 UGeometryCollectionComponent::EmbeddedIndexToTransformIndex(const UInstancedStaticMeshComponent* ISMComponent, int32 InstanceIndex) const
{
	for (int32 ISMIdx = 0; ISMIdx < EmbeddedGeometryComponents.Num(); ++ISMIdx)
	{
		if (EmbeddedGeometryComponents[ISMIdx].Get() == ISMComponent)
		{
			return (EmbeddedBoneMaps[ISMIdx][InstanceIndex]);
		}
	}

	return INDEX_NONE;
}
#endif

void UGeometryCollectionComponent::SetRestState(TArray<FTransform>&& InRestTransforms)
{	
	RestTransforms = InRestTransforms;
	
	if (DynamicCollection)
	{
		SetInitialTransforms(RestTransforms);
	}

	FGeometryCollectionDynamicData* DynamicData = GDynamicDataPool.Allocate();
	DynamicData->SetPrevTransforms(GlobalMatrices);
	CalculateGlobalMatrices();
	DynamicData->SetTransforms(GlobalMatrices);
	DynamicData->IsDynamic = true;

	if (SceneProxy)
	{
		if (SceneProxy->IsNaniteMesh())
		{

#if WITH_EDITOR
			// We need to do this in case we're controlled by Sequencer in editor, which doesn't invoke PostEditChangeProperty
			SendRenderTransform_Concurrent();
#endif

			FNaniteGeometryCollectionSceneProxy* GeometryCollectionSceneProxy = static_cast<FNaniteGeometryCollectionSceneProxy*>(SceneProxy);
			ENQUEUE_RENDER_COMMAND(SendRenderDynamicData)(
				[GeometryCollectionSceneProxy, DynamicData](FRHICommandListImmediate& RHICmdList)
				{
					GeometryCollectionSceneProxy->SetDynamicData_RenderThread(DynamicData);
				}
			);
		}
		else
		{
			FGeometryCollectionSceneProxy* GeometryCollectionSceneProxy = static_cast<FGeometryCollectionSceneProxy*>(SceneProxy);
			ENQUEUE_RENDER_COMMAND(SendRenderDynamicData)(
				[GeometryCollectionSceneProxy, DynamicData](FRHICommandListImmediate& RHICmdList)
				{
					GeometryCollectionSceneProxy->SetDynamicData_RenderThread(DynamicData);
				}
			);
		}
	}

	RefreshEmbeddedGeometry();
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
			DynamicCollection->AddAttribute<FVector3f>("LinearVelocity", FTransformCollection::TransformGroup);
			DynamicCollection->AddAttribute<FVector3f>("AngularVelocity", FTransformCollection::TransformGroup);

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

#if WITH_EDITOR
void UGeometryCollectionComponent::PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent)
{
	Super::PostEditChangeChainProperty(PropertyChangedEvent);

	if (PropertyChangedEvent.Property && PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(UGeometryCollectionComponent, bShowBoneColors))
	{
		FScopedColorEdit EditBoneColor(this, true /*bForceUpdate*/); // the property has already changed; this will trigger the color update + render state updates
	}
}
#endif

static void DispatchGeometryCollectionBreakEvent(const FChaosBreakEvent& Event)
{
	if (UGeometryCollectionComponent* const GC = Cast<UGeometryCollectionComponent>(Event.Component))
	{
		GC->DispatchBreakEvent(Event);
	}
}

static void DispatchGeometryCollectionRemovalEvent(const FChaosRemovalEvent& Event)
{
	if (UGeometryCollectionComponent* const GC = Cast<UGeometryCollectionComponent>(Event.Component))
	{
		GC->DispatchRemovalEvent(Event);
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
	if (BodyInstance.bNotifyRigidBodyCollision || bNotifyBreaks || bNotifyCollisions || bNotifyRemovals)
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

			if (bNotifyRemovals)
			{
				EventDispatcher->RegisterForRemovalEvents(this, &DispatchGeometryCollectionRemovalEvent);

				Solver->EnqueueCommandImmediate([Solver]()
					{
						Solver->SetGenerateRemovalData(true);
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

void UGeometryCollectionComponent::UpdateRemovalEventRegistration()
{
	if (bNotifyRemovals)
	{
		EventDispatcher->RegisterForRemovalEvents(this, &DispatchGeometryCollectionRemovalEvent);
	}
	else
	{
		EventDispatcher->UnRegisterForRemovalEvents(this);
	}
}

void ActivateClusters(Chaos::FRigidClustering& Clustering, Chaos::TPBDRigidClusteredParticleHandle<Chaos::FReal, 3>* Cluster)
{
	if(!Cluster)
	{
		return;
	}

	if(Cluster->ClusterIds().Id)
	{
		ActivateClusters(Clustering, Cluster->Parent());
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
				Chaos::TPBDRigidClusteredParticleHandle<Chaos::FReal, 3>* Particle = Prox->GetParticles()[SourcePose.ParticleIndex];

				Chaos::FPhysicsSolver* Solver = Prox->GetSolver<Chaos::FPhysicsSolver>();
				Chaos::FPBDRigidsEvolution* Evo = Solver->GetEvolution();
				check(Evo);
				Chaos::FRigidClustering& Clustering = Evo->GetRigidClustering();
				
				// Set X/R/V/W for next sim step from the replicated state
				Particle->SetX(SourcePose.Position);
				Particle->SetR(SourcePose.Rotation);
				Particle->SetV(SourcePose.LinearVelocity);
				Particle->SetW(SourcePose.AngularVelocity);

				if(Particle->ClusterIds().Id)
				{
					// This particle is clustered but the remote authority has it activated. Fracture the parent cluster
					ActivateClusters(Clustering, Particle->Parent());
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

		TManagedArray<FVector3f>* LinearVelocity = DynamicCollection->FindAttributeTyped<FVector3f>("LinearVelocity", FTransformCollection::TransformGroup);
		TManagedArray<FVector3f>* AngularVelocity = DynamicCollection->FindAttributeTyped<FVector3f>("AngularVelocity", FTransformCollection::TransformGroup);

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
				Pose.LinearVelocity = (FVector)(*LinearVelocity)[Index];
				Pose.AngularVelocity = (FVector)(*AngularVelocity)[Index];
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

void UGeometryCollectionComponent::SetDynamicState(const Chaos::EObjectStateType& NewDynamicState)
{
	if (DynamicCollection)
	{
		TManagedArray<int32>& DynamicState = DynamicCollection->DynamicState;
		for (int i = 0; i < DynamicState.Num(); i++)
		{
			DynamicState[i] = static_cast<int32>(NewDynamicState);
		}
	}
}

void UGeometryCollectionComponent::SetInitialTransforms(const TArray<FTransform>& InitialTransforms)
{
	if (DynamicCollection)
	{
		TManagedArray<FTransform>& Transform = DynamicCollection->Transform;
		int32 MaxIdx = FMath::Min(Transform.Num(), InitialTransforms.Num());
		for (int32 Idx = 0; Idx < MaxIdx; ++Idx)
		{
			Transform[Idx] = InitialTransforms[Idx];
		}
	}
}

void UGeometryCollectionComponent::SetInitialClusterBreaks(const TArray<int32>& ReleaseIndices)
{
	if (DynamicCollection)
	{
		TManagedArray<int32>& Parent = DynamicCollection->Parent;
		TManagedArray <TSet<int32>>& Children = DynamicCollection->Children;
		const int32 NumTransforms = Parent.Num();

		for (int32 ReleaseIndex : ReleaseIndices)
		{
			if (ReleaseIndex < NumTransforms)
			{
				if (Parent[ReleaseIndex] > INDEX_NONE)
				{
					Children[Parent[ReleaseIndex]].Remove(ReleaseIndex);
					Parent[ReleaseIndex] = INDEX_NONE;
				}
			}
		}
	}
}

void SetHierarchyStrain(Chaos::TPBDRigidClusteredParticleHandle<Chaos::FReal, 3>* P, TMap<Chaos::TPBDRigidClusteredParticleHandle<Chaos::FReal, 3>*, TArray<Chaos::TPBDRigidParticleHandle<Chaos::FReal, 3>*>>& Map, float Strain)
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
				Chaos::FRigidClustering& Clustering = Solver->GetEvolution()->GetRigidClustering();
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

	if (!RestCollection->EnableNanite)
	{
		const int32 NumPoints = Collection->NumElements(FGeometryCollection::VerticesGroup);
		const TManagedArray<FVector3f>& Vertex = Collection->Vertex;
		const TManagedArray<int32>& BoneMap = Collection->BoneMap;
		const TManagedArray<FVector3f>& TangentU = Collection->TangentU;
		const TManagedArray<FVector3f>& TangentV = Collection->TangentV;
		const TManagedArray<FVector3f>& Normal = Collection->Normal;
		const TManagedArray<TArray<FVector2f>>& UVs = Collection->UVs;
		const TManagedArray<FLinearColor>& Color = Collection->Color;
		const TManagedArray<FLinearColor>& BoneColors = Collection->BoneColor;
		
		const int32 NumGeom = Collection->NumElements(FGeometryCollection::GeometryGroup);
		const TManagedArray<int32>& TransformIndex = Collection->TransformIndex;
		const TManagedArray<int32>& FaceStart = Collection->FaceStart;
		const TManagedArray<int32>& FaceCount = Collection->FaceCount;

		ConstantData->Vertices = TArray<FVector3f>(Vertex.GetData(), Vertex.Num());
		ConstantData->BoneMap = TArray<int32>(BoneMap.GetData(), BoneMap.Num());
		ConstantData->TangentU = TArray<FVector3f>(TangentU.GetData(), TangentU.Num());
		ConstantData->TangentV = TArray<FVector3f>(TangentV.GetData(), TangentV.Num());
		ConstantData->Normals = TArray<FVector3f>(Normal.GetData(), Normal.Num());
		ConstantData->UVs = TArray<TArray<FVector2f>>(UVs.GetData(), UVs.Num());
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
		
#if WITH_EDITOR
		// We will override visibility with the Hide array (if available).
		TArray<bool> VisibleOverride;
		VisibleOverride.Init(true, Visible.Num());
		bool bUsingHideArray = false;

		if (Collection->HasAttribute("Hide", FGeometryCollection::TransformGroup))
		{
			bUsingHideArray = true;

			bool bAllHidden = true;

			const TManagedArray<bool>& Hide = Collection->GetAttribute<bool>("Hide", FGeometryCollection::TransformGroup);
			for (int32 GeomIdx = 0; GeomIdx < NumGeom; ++GeomIdx)
			{
				if (Hide[TransformIndex[GeomIdx]])
				{
					// (Temporarily) hide faces of this hidden geometry
					for (int32 FaceIdxOffset = 0; FaceIdxOffset < FaceCount[GeomIdx]; ++FaceIdxOffset)
					{
						VisibleOverride[FaceStart[GeomIdx]+FaceIdxOffset] = false;
					}
				}
				else
				{
					bAllHidden = false;
				}
			}
			if (!ensure(!bAllHidden)) // if they're all hidden, rendering would crash -- unhide them
			{
				for (int32 FaceIdx = 0; FaceIdx < VisibleOverride.Num(); ++FaceIdx)
				{
					VisibleOverride[FaceIdx] = true;
				}
			}
		}
#endif
		
		const TManagedArray<int32>& MaterialIndex = Collection->MaterialIndex;

		const int32 NumFaceGroupEntries = Collection->NumElements(FGeometryCollection::FacesGroup);

		for (int FaceIndex = 0; FaceIndex < NumFaceGroupEntries; ++FaceIndex)
		{
#if WITH_EDITOR
			NumIndices += bUsingHideArray ? static_cast<int>(VisibleOverride[FaceIndex]) : static_cast<int>(Visible[FaceIndex]);
#else
			NumIndices += static_cast<int>(Visible[FaceIndex]);
#endif
		}

		ConstantData->Indices.AddUninitialized(NumIndices);
		for (int IndexIdx = 0, cdx = 0; IndexIdx < NumFaceGroupEntries; ++IndexIdx)
		{
#if WITH_EDITOR
			const bool bUseVisible = bUsingHideArray ? VisibleOverride[MaterialIndex[IndexIdx]] : Visible[MaterialIndex[IndexIdx]];
			if (bUseVisible)
#else
			if (Visible[MaterialIndex[IndexIdx]])
#endif
			{
				ConstantData->Indices[cdx++] = Indices[MaterialIndex[IndexIdx]];
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
#if WITH_EDITOR
				const bool bUseVisible = bUsingHideArray ? VisibleOverride[MaterialIndex[TriangleIndex]] : Visible[MaterialIndex[TriangleIndex]];
				if (!bUseVisible)
#else
				if (!Visible[MaterialIndex[TriangleIndex]])
#endif
				{
					Section.FirstIndex -= 3;
				}
			}

			for (int32 TriangleIndex = 0; TriangleIndex < Sections[SectionIndex].NumTriangles; TriangleIndex++)
			{
				int32 FaceIdx = MaterialIndex[Sections[SectionIndex].FirstIndex / 3 + TriangleIndex];
#if WITH_EDITOR
				const bool bUseVisible = bUsingHideArray ? VisibleOverride[FaceIdx] : Visible[FaceIdx];
				if (!bUseVisible)
#else
				if (!Visible[FaceIdx])
#endif
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
#if WITH_EDITOR
			const bool bUseVisible = bUsingHideArray ? VisibleOverride[FaceIndex] : Visible[FaceIndex];
			if (bUseVisible && (MaterialID[FaceIndex] % 2 == 0 || bUsingHideArray))
#else
			if (Visible[FaceIndex] && MaterialID[FaceIndex] % 2 == 0)
#endif
			{
				BaseMeshIndices.Add(Indices[FaceIndex]);
				BaseMeshOriginalFaceIndices.Add(FaceIndex);
			}
		}

		// We should always have external faces of a geometry collection
		ensure(BaseMeshIndices.Num() > 0);

		ConstantData->OriginalMeshSections = Collection->BuildMeshSections(BaseMeshIndices, BaseMeshOriginalFaceIndices, ConstantData->OriginalMeshIndices);
	}
	
	TArray<FMatrix> RestMatrices;
	GeometryCollectionAlgo::GlobalMatrices(RestCollection->GetGeometryCollection()->Transform, RestCollection->GetGeometryCollection()->Parent, RestMatrices);

	ConstantData->SetRestTransforms(RestMatrices);
}

FGeometryCollectionDynamicData* UGeometryCollectionComponent::InitDynamicData(bool bInitialization)
{
	SCOPE_CYCLE_COUNTER(STAT_GCInitDynamicData);

	FGeometryCollectionDynamicData* DynamicData = nullptr;

	const bool bEditorMode = bShowBoneColors || bEnableBoneSelection;
	const bool bIsDynamic  = GetIsObjectDynamic() || bEditorMode || bInitialization;

	if (bIsDynamic)
	{
		DynamicData = GDynamicDataPool.Allocate();
		DynamicData->IsDynamic = true;
		DynamicData->IsLoading = GetIsObjectLoading();

		// If we have no transforms stored in the dynamic data, then assign both prev and current to the same global matrices
		if (GlobalMatrices.Num() == 0)
		{
			// Copy global matrices over to DynamicData
			CalculateGlobalMatrices();

			DynamicData->SetAllTransforms(GlobalMatrices);
		}
		else
		{
			// Copy existing global matrices into prev transforms
			DynamicData->SetPrevTransforms(GlobalMatrices);

			// Copy global matrices over to DynamicData
			CalculateGlobalMatrices();

			bool bComputeChanges = true;

			// if the number of matrices has changed between frames, then sync previous to current
			if (GlobalMatrices.Num() != DynamicData->PrevTransforms.Num())
			{
				DynamicData->SetPrevTransforms(GlobalMatrices);
				DynamicData->ChangedCount = GlobalMatrices.Num();
				bComputeChanges = false; // Optimization to just force all transforms as changed and skip comparison
			}

			DynamicData->SetTransforms(GlobalMatrices);

			// The number of transforms for current and previous should match now
			check(DynamicData->PrevTransforms.Num() == DynamicData->Transforms.Num());

			if (bComputeChanges)
			{
				DynamicData->DetermineChanges();
			}
		}
	}

	if (!bEditorMode && !bInitialization)
	{
		if (DynamicData && DynamicData->ChangedCount == 0)
		{
			GDynamicDataPool.Release(DynamicData);
			DynamicData = nullptr;

			// Change of state?
			if (bIsMoving && !bForceMotionBlur)
			{
				bIsMoving = false;
				if (SceneProxy && SceneProxy->IsNaniteMesh())
				{
					FNaniteGeometryCollectionSceneProxy* NaniteProxy = static_cast<FNaniteGeometryCollectionSceneProxy*>(SceneProxy);
					ENQUEUE_RENDER_COMMAND(NaniteProxyOnMotionEnd)(
						[NaniteProxy](FRHICommandListImmediate& RHICmdList)
						{
							NaniteProxy->OnMotionEnd();
						}
					);
				}
			}
		}
		else
		{
			// Change of state?
			if (!bIsMoving && !bForceMotionBlur)
			{
				bIsMoving = true;
				if (SceneProxy && SceneProxy->IsNaniteMesh())
				{
					FNaniteGeometryCollectionSceneProxy* NaniteProxy = static_cast<FNaniteGeometryCollectionSceneProxy*>(SceneProxy);
					ENQUEUE_RENDER_COMMAND(NaniteProxyOnMotionBegin)(
						[NaniteProxy](FRHICommandListImmediate& RHICmdList)
						{
							NaniteProxy->OnMotionBegin();
						}
					);
				}
			}
		}
	}

	return DynamicData;
}

void UGeometryCollectionComponent::OnUpdateTransform(EUpdateTransformFlags UpdateTransformFlags, ETeleportType Teleport)
{
	Super::OnUpdateTransform(UpdateTransformFlags, Teleport);

#if WITH_CHAOS
	if (PhysicsProxy)
	{
		PhysicsProxy->SetWorldTransform(GetComponentTransform());
	}
#endif
}

void UGeometryCollectionComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction)
{
	//UE_LOG(UGCC_LOG, Log, TEXT("GeometryCollectionComponent[%p]::TickComponent()"), this);
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

#if WITH_EDITOR
	if (IsRegistered() && SceneProxy && RestCollection)
	{
		const bool bWantNanite = RestCollection->EnableNanite && GGeometryCollectionNanite != 0;
		const bool bHaveNanite = SceneProxy->IsNaniteMesh();
		bool bRecreateProxy = bWantNanite != bHaveNanite;
		if (bRecreateProxy)
		{
			// Wait until resources are released
			FlushRenderingCommands();

			FComponentReregisterContext ReregisterContext(this);
			UpdateAllPrimitiveSceneInfosForSingleComponent(this);
		}
	}
#endif

#if WITH_CHAOS
	//if (bRenderStateDirty && DynamicCollection)	//todo: always send for now
	if (RestCollection)
	{
		// In editor mode we have no DynamicCollection so this test is necessary
		if(DynamicCollection) //, TEXT("No dynamic collection available for component %s during tick."), *GetName()))
		{
			if (RestCollection->bRemoveOnMaxSleep)
			{ 
				IncrementSleepTimer(DeltaTime);
			}

			if (RestCollection->HasVisibleGeometry() || DynamicCollection->IsDirty())
			{
				// #todo review: When we've made changes to ISMC, we need to move this function call to SetRenderDynamicData_Concurrent
				RefreshEmbeddedGeometry();

				if (SceneProxy && SceneProxy->IsNaniteMesh())
				{
					FNaniteGeometryCollectionSceneProxy* NaniteProxy = static_cast<FNaniteGeometryCollectionSceneProxy*>(SceneProxy);
					NaniteProxy->FlushGPUSceneUpdate_GameThread();
				}
				
				MarkRenderTransformDirty();
				MarkRenderDynamicDataDirty();
				bRenderStateDirty = false;
				
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


#endif // WITH_CHAOS

	SetIsReplicated(bEnableReplication);

	InitializeEmbeddedGeometry();

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

		if (RestCollection->bRemoveOnMaxSleep)
		{
			if (!DynamicCollection->HasAttribute("SleepTimer", FGeometryCollection::TransformGroup))
			{
				TManagedArray<float>& SleepTimer = DynamicCollection->AddAttribute<float>("SleepTimer", FGeometryCollection::TransformGroup);
				SleepTimer.Fill(0.f);
			}

			if (!DynamicCollection->HasAttribute("UniformScale", FGeometryCollection::TransformGroup))
			{
				TManagedArray<FTransform>& UniformScale = DynamicCollection->AddAttribute<FTransform>("UniformScale", FGeometryCollection::TransformGroup);
				UniformScale.Fill(FTransform::Identity);
			}

			if (!DynamicCollection->HasAttribute("MaxSleepTime", FGeometryCollection::TransformGroup))
			{
				float MinTime = FMath::Max(0.0f, RestCollection->MaximumSleepTime.X);
				float MaxTime = FMath::Max(MinTime, RestCollection->MaximumSleepTime.Y);
				TManagedArray<float>& MaxSleepTime = DynamicCollection->AddAttribute<float>("MaxSleepTime", FGeometryCollection::TransformGroup);
				for (int32 Idx = 0; Idx < MaxSleepTime.Num(); ++Idx)
				{
					MaxSleepTime[Idx] = FMath::RandRange(MinTime, MaxTime);
				}
			}

			if (!DynamicCollection->HasAttribute("RemovalDuration", FGeometryCollection::TransformGroup))
			{
				float MinTime = FMath::Max(0.0f, RestCollection->RemovalDuration.X);
				float MaxTime = FMath::Max(MinTime, RestCollection->RemovalDuration.Y);
				TManagedArray<float>& RemovalDuration = DynamicCollection->AddAttribute<float>("RemovalDuration", FGeometryCollection::TransformGroup);
				for (int32 Idx = 0; Idx < RemovalDuration.Num(); ++Idx)
				{
					RemovalDuration[Idx] = FMath::RandRange(MinTime, MaxTime);
				}
			}
		}

		SetRenderStateDirty();
	}

	if (RestTransforms.Num() > 0)
	{
		SetInitialTransforms(RestTransforms);
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
	if (!BodyInstance.bSimulatePhysics) IsObjectLoading = false; // just mark as loaded if we are simulating.

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
			UGeometryCollection* RestCollectionMutable = const_cast<UGeometryCollection*>(ToRawPtr(RestCollection));
			RestCollectionMutable->CreateSimulationData();
		}
#endif
		const bool bValidWorld = GetWorld() && GetWorld()->IsGameWorld();
		const bool bValidCollection = DynamicCollection && DynamicCollection->Transform.Num() > 0;
		if (bValidWorld && bValidCollection)
		{
			FPhysxUserData::Set<UPrimitiveComponent>(&PhysicsUserData, this);

			// If the Component is set to Dynamic, we look to the RestCollection for initial dynamic state override per transform.
			TManagedArray<int32> & DynamicState = DynamicCollection->DynamicState;

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

			TManagedArray<bool>& Active = DynamicCollection->Active;
			if (RestCollection->GetGeometryCollection()->HasAttribute(FGeometryCollection::SimulatableParticlesAttribute, FTransformCollection::TransformGroup))
			{
				TManagedArray<bool>* SimulatableParticles = RestCollection->GetGeometryCollection()->FindAttribute<bool>(FGeometryCollection::SimulatableParticlesAttribute, FTransformCollection::TransformGroup);
				for (int i = 0; i < Active.Num(); i++)
				{
					Active[i] = (*SimulatableParticles)[i];
				}
			}
			else
			{
				// If no simulation data is available then default to the simulation of just the rigid geometry.
				for (int i = 0; i < Active.Num(); i++)
				{
					Active[i] = RestCollection->GetGeometryCollection()->IsRigid(i);
				}
			}
			
			TManagedArray<int32> & CollisionGroupArray = DynamicCollection->CollisionGroup;
			{
				for (int i = 0; i < CollisionGroupArray.Num(); i++)
				{
					CollisionGroupArray[i] = CollisionGroup;
				}
			}
	
			// Set up initial filter data for our particles
			// #BGTODO We need a dummy body setup for now to allow the body instance to generate filter information. Change body instance to operate independently.
			DummyBodySetup = NewObject<UBodySetup>(this, UBodySetup::StaticClass());
			BodyInstance.BodySetup = DummyBodySetup;

			FBodyCollisionFilterData FilterData;
			FMaskFilter FilterMask = BodyInstance.GetMaskFilter();
			BodyInstance.BuildBodyFilterData(FilterData);

			InitialSimFilter = FilterData.SimFilter;
			InitialQueryFilter = FilterData.QuerySimpleFilter;

			// since InitBody has not been called on the bodyInstance, OwnerComponent is nullptr
			// we then need to set the owner on the query filters to allow for actor filtering 
			if (const AActor* Owner = GetOwner())
			{
				InitialQueryFilter.Word0 = Owner->GetUniqueID();
			}

			// Enable for complex and simple (no dual representation currently like other meshes)
			InitialQueryFilter.Word3 |= (EPDF_SimpleCollision | EPDF_ComplexCollision);
			InitialSimFilter.Word3 |= (EPDF_SimpleCollision | EPDF_ComplexCollision);
			
			if (bNotifyCollisions)
			{
				InitialQueryFilter.Word3 |= EPDF_ContactNotify;
				InitialSimFilter.Word3 |= EPDF_ContactNotify;
			}

			if (BodyInstance.bSimulatePhysics)
			{
				RegisterAndInitializePhysicsProxy();
			}
			
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

void UGeometryCollectionComponent::RegisterAndInitializePhysicsProxy()
{
#if WITH_CHAOS
	FSimulationParameters SimulationParameters;
	{
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
		SimulationParameters.Name = GetPathName();
#endif
		EClusterConnectionTypeEnum ClusterCollectionType = ClusterConnectionType_DEPRECATED;
		if (RestCollection)
		{
			RestCollection->GetSharedSimulationParams(SimulationParameters.Shared);
			SimulationParameters.RestCollection = RestCollection->GetGeometryCollection().Get();
			ClusterCollectionType = RestCollection->ClusterConnectionType;
		}
		SimulationParameters.Simulating = BodyInstance.bSimulatePhysics;
		SimulationParameters.EnableClustering = EnableClustering;
		SimulationParameters.ClusterGroupIndex = EnableClustering ? ClusterGroupIndex : 0;
		SimulationParameters.MaxClusterLevel = MaxClusterLevel;
		SimulationParameters.bUseSizeSpecificDamageThresholds = bUseSizeSpecificDamageThreshold;
		SimulationParameters.DamageThreshold = DamageThreshold;
		SimulationParameters.ClusterConnectionMethod = (Chaos::FClusterCreationParameters::EConnectionMethod)ClusterCollectionType;
		SimulationParameters.CollisionGroup = CollisionGroup;
		SimulationParameters.CollisionSampleFraction = CollisionSampleFraction;
		SimulationParameters.InitialVelocityType = InitialVelocityType;
		SimulationParameters.InitialLinearVelocity = InitialLinearVelocity;
		SimulationParameters.InitialAngularVelocity = InitialAngularVelocity;
		SimulationParameters.bClearCache = true;
		SimulationParameters.ObjectType = ObjectType;
		SimulationParameters.CacheType = CacheParameters.CacheMode;
		SimulationParameters.ReverseCacheBeginTime = CacheParameters.ReverseCacheBeginTime;
		SimulationParameters.bGenerateBreakingData = bNotifyBreaks;
		SimulationParameters.bGenerateCollisionData = bNotifyCollisions;
		SimulationParameters.bGenerateTrailingData = bNotifyTrailing;
		SimulationParameters.bGenerateRemovalsData = bNotifyRemovals;
		SimulationParameters.RemoveOnFractureEnabled = SimulationParameters.Shared.RemoveOnFractureIndices.Num() > 0;
		SimulationParameters.WorldTransform = GetComponentToWorld();
		SimulationParameters.UserData = static_cast<void*>(&PhysicsUserData);

		UPhysicalMaterial* EnginePhysicalMaterial = GetPhysicalMaterial();
		if (ensure(EnginePhysicalMaterial))
		{
			SimulationParameters.PhysicalMaterialHandle = EnginePhysicalMaterial->GetPhysicsMaterial();
		}
		GetInitializationCommands(SimulationParameters.InitializationCommands);
	}

	PhysicsProxy = new FGeometryCollectionPhysicsProxy(this, *DynamicCollection, SimulationParameters, InitialSimFilter, InitialQueryFilter);
	FPhysScene_Chaos* Scene = GetInnerChaosScene();
	Scene->AddObject(this, PhysicsProxy);

	RegisterForEvents();
#endif
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
		FGeometryCollectionDynamicData* DynamicData = InitDynamicData(false /* initialization */);

		if (DynamicData || SceneProxy->IsNaniteMesh())
		{
			INC_DWORD_STAT_BY(STAT_GCTotalTransforms, DynamicData ? DynamicData->Transforms.Num() : 0);
			INC_DWORD_STAT_BY(STAT_GCChangedTransforms, DynamicData ? DynamicData->ChangedCount : 0);

			// #todo (bmiller) Once ISMC changes have been complete, this is the best place to call this method
			// but we can't currently because it's an inappropriate place to call MarkRenderStateDirty on the ISMC.
			// RefreshEmbeddedGeometry();

			// Enqueue command to send to render thread
			if (SceneProxy->IsNaniteMesh())
			{
				FNaniteGeometryCollectionSceneProxy* GeometryCollectionSceneProxy = static_cast<FNaniteGeometryCollectionSceneProxy*>(SceneProxy);
				ENQUEUE_RENDER_COMMAND(SendRenderDynamicData)(
					[GeometryCollectionSceneProxy, DynamicData](FRHICommandListImmediate& RHICmdList)
					{
						if (DynamicData)
						{
							GeometryCollectionSceneProxy->SetDynamicData_RenderThread(DynamicData);
						}
						else
						{
							// No longer dynamic, make sure previous transforms are reset
							GeometryCollectionSceneProxy->ResetPreviousTransforms_RenderThread();
						}
					}
				);
			}
			else
			{
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

		const int32 NumTransforms = RestCollection->GetGeometryCollection()->NumElements(FGeometryCollection::TransformGroup);
		RestTransforms.SetNum(NumTransforms);
		for (int32 Idx = 0; Idx < NumTransforms; ++Idx)
		{
			RestTransforms[Idx] = RestCollection->GetGeometryCollection()->Transform[Idx];
		}

		CalculateGlobalMatrices();
		CalculateLocalBounds();

		if (!IsEmbeddedGeometryValid())
		{
			InitializeEmbeddedGeometry();
		}
		
		//ResetDynamicCollection();
	}
}

FGeometryCollectionEdit::FGeometryCollectionEdit(UGeometryCollectionComponent* InComponent, GeometryCollection::EEditUpdate InEditUpdate, bool bShapeIsUnchanged)
	: Component(InComponent)
	, EditUpdate(InEditUpdate)
	, bShapeIsUnchanged(bShapeIsUnchanged)
{
	bHadPhysicsState = Component->HasValidPhysicsState();
	if (EnumHasAnyFlags(EditUpdate, GeometryCollection::EEditUpdate::Physics) && bHadPhysicsState)
	{
		Component->DestroyPhysicsState();
	}

	if (EnumHasAnyFlags(EditUpdate, GeometryCollection::EEditUpdate::Rest) && GetRestCollection())
	{
		Component->Modify();
		GetRestCollection()->Modify();
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
			if (!bShapeIsUnchanged)
			{
				GetRestCollection()->UpdateConvexGeometry();
			}
			GetRestCollection()->InvalidateCollection();
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
		return const_cast<UGeometryCollection*>(ToRawPtr(Component->RestCollection));	//const cast is ok here since we are explicitly in edit mode. Should all this editor code be in an editor module?
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
	Component->SelectEmbeddedGeometry();
}

void FScopedColorEdit::AppendSelectedBones(const TArray<int32>& SelectedBonesIn)
{
	bUpdated = true;
	Component->SelectedBones.Append(SelectedBonesIn);
}

void FScopedColorEdit::ToggleSelectedBones(const TArray<int32>& SelectedBonesIn, bool bAdd)
{
	bUpdated = true;
	
	const UGeometryCollection* GeometryCollection = Component->GetRestCollection();
	if (GeometryCollection)
	{ 
		TSharedPtr<FGeometryCollection, ESPMode::ThreadSafe> GeometryCollectionPtr = GeometryCollection->GetGeometryCollection();
		for (int32 BoneIndex : SelectedBonesIn)
		{
		
			int32 ContextBoneIndex = (GetViewLevel() > -1) ? FGeometryCollectionClusteringUtility::GetParentOfBoneAtSpecifiedLevel(GeometryCollectionPtr.Get(), BoneIndex, GetViewLevel()) : BoneIndex;
		
			if (bAdd) // shift select
			{
				Component->SelectedBones.Add(BoneIndex);
			}
			else // ctrl select (toggle)
			{ 
				if (Component->SelectedBones.Contains(ContextBoneIndex))
				{
					Component->SelectedBones.Remove(ContextBoneIndex);
				}
				else
				{
					Component->SelectedBones.Add(ContextBoneIndex);
				}
			}
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
				if (GetViewLevel() == -1)
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
				else
				{
					TArray<int32> ViewLevelBones;
					FGeometryCollectionClusteringUtility::GetChildBonesAtLevel(GeometryCollectionPtr.Get(), RootElement, GetViewLevel(), ViewLevelBones);
					for (int32 ViewLevelBone : ViewLevelBones)
					{
						if (!IsBoneSelected(ViewLevelBone))
						{
							NewSelection.Push(ViewLevelBone);
							TArray<int32> ChildBones;
							FGeometryCollectionClusteringUtility::GetChildBonesFromLevel(GeometryCollectionPtr.Get(), ViewLevelBone, GetViewLevel(), ChildBones);
							NewSelection.Append(ChildBones);
						}
					}
				}
			}
			
			ResetBoneSelection();
			AppendSelectedBones(NewSelection);
		}
		break;


		case GeometryCollection::ESelectionMode::Neighbors:
		{
			FGeometryCollectionProximityUtility ProximityUtility(GeometryCollectionPtr.Get());
			ProximityUtility.UpdateProximity();

			const TManagedArray<int32>& TransformIndex = GeometryCollectionPtr->TransformIndex;
			const TManagedArray<int32>& TransformToGeometryIndex = GeometryCollectionPtr->TransformToGeometryIndex;
			const TManagedArray<TSet<int32>>& Proximity = GeometryCollectionPtr->GetAttribute<TSet<int32>>("Proximity", FGeometryCollection::GeometryGroup);

			const TArray<int32> SelectedBones = GetSelectedBones();

			TArray<int32> NewSelection;
			for (int32 Bone : SelectedBones)
			{
				NewSelection.AddUnique(Bone);
				int32 GeometryIdx = TransformToGeometryIndex[Bone];
				if (GeometryIdx != INDEX_NONE)
				{
					const TSet<int32>& Neighbors = Proximity[GeometryIdx];
					for (int32 NeighborGeometryIndex : Neighbors)
					{
						NewSelection.AddUnique(TransformIndex[NeighborGeometryIndex]);
					}
				}
			}

			ResetBoneSelection();
			AppendSelectedBones(NewSelection);
		}
		break;

		case GeometryCollection::ESelectionMode::Parent:
		{
			const TManagedArray<int32>& Parents = GeometryCollectionPtr->Parent;
			
			const TArray<int32> SelectedBones = GetSelectedBones();

			TArray<int32> NewSelection;
			for (int32 Bone : SelectedBones)
			{
				int32 ParentBone = Parents[Bone];
				if (ParentBone != FGeometryCollection::Invalid)
				{
					NewSelection.AddUnique(ParentBone);
				}
			}

			ResetBoneSelection();
			AppendSelectedBones(NewSelection);
		}
		break;

		case GeometryCollection::ESelectionMode::Children:
		{
			const TManagedArray<TSet<int32>>& Children = GeometryCollectionPtr->Children;

			const TArray<int32> SelectedBones = GetSelectedBones();

			TArray<int32> NewSelection;
			for (int32 Bone : SelectedBones)
			{
				for (int32 Child : Children[Bone])
				{
					NewSelection.AddUnique(Child);
				}
			}

			ResetBoneSelection();
			AppendSelectedBones(NewSelection);
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

		case GeometryCollection::ESelectionMode::Level:
		{
			if (GeometryCollectionPtr->HasAttribute("Level", FTransformCollection::TransformGroup))
			{
				const TManagedArray<int32>& Levels = GeometryCollectionPtr->GetAttribute<int32>("Level", FTransformCollection::TransformGroup);

				const TArray<int32> SelectedBones = GetSelectedBones();

				TArray<int32> NewSelection;
				for (int32 Bone : SelectedBones)
				{
					int32 Level = Levels[Bone];
					for (int32 TransformIdx = 0; TransformIdx < GeometryCollectionPtr->NumElements(FTransformCollection::TransformGroup); ++TransformIdx)
					{
						if (Levels[TransformIdx] == Level)
						{
							NewSelection.AddUnique(TransformIdx);
						}
					}
				}

				ResetBoneSelection();
				AppendSelectedBones(NewSelection);
			}	
		}
		break;

		default: 
			check(false); // unexpected selection mode
		break;
		}

		const TArray<int32>& SelectedBones = GetSelectedBones();
		TArray<int32> HighlightBones;
		for (int32 SelectedBone: SelectedBones)
		{ 
			FGeometryCollectionClusteringUtility::RecursiveAddAllChildren(GeometryCollectionPtr->Children, SelectedBone, HighlightBones);
		}
		SetHighlightedBones(HighlightBones);
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
	FFieldSystemCommand Command = FFieldObjectCommands::CreateFieldCommand(EFieldPhysicsType::Field_DynamicState, new FRadialIntMask(Radius, Position, (int32)Chaos::EObjectStateType::Dynamic,
		(int32)Chaos::EObjectStateType::Kinematic, ESetMaskConditionType::Field_Set_IFF_NOT_Interior));
	DispatchFieldCommand(Command);
}

void UGeometryCollectionComponent::ApplyPhysicsField(bool Enabled, EGeometryCollectionPhysicsTypeEnum Target, UFieldSystemMetaData* MetaData, UFieldNodeBase* Field)
{
	if (Enabled && Field)
	{
		FFieldSystemCommand Command = FFieldObjectCommands::CreateFieldCommand(GetGeometryCollectionPhysicsType(Target), Field, MetaData);
		DispatchFieldCommand(Command);
	}
}

bool UGeometryCollectionComponent::GetIsObjectDynamic() const
{ 
	return PhysicsProxy ? PhysicsProxy->GetIsObjectDynamic() : IsObjectDynamic;
}

void UGeometryCollectionComponent::DispatchFieldCommand(const FFieldSystemCommand& InCommand)
{
	if (PhysicsProxy && InCommand.RootNode)
	{
		FChaosSolversModule* ChaosModule = FChaosSolversModule::GetModule();
		checkSlow(ChaosModule);

		auto Solver = PhysicsProxy->GetSolver<Chaos::FPBDRigidsSolver>();
		const FName Name = GetOwner() ? *GetOwner()->GetName() : TEXT("");

		FFieldSystemCommand LocalCommand = InCommand;
		LocalCommand.InitFieldNodes(Solver->GetSolverTime(), Name);

		Solver->EnqueueCommandImmediate([Solver, PhysicsProxy = this->PhysicsProxy, NewCommand = LocalCommand]()
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
						const FFieldSystemCommand NewCommand = FieldSystemActor->GetFieldSystemComponent()->ConstructionCommands.BuildFieldCommand(CommandIndex);
						if (NewCommand.RootNode)
						{
							CombinedCommmands.Emplace(NewCommand);
						}
					}
				}
				// Legacy path : only there for old levels. New ones will have the commands directly stored onto the component
				else if (FieldSystemActor->GetFieldSystemComponent()->GetFieldSystem())
				{
					const FName Name = GetOwner() ? *GetOwner()->GetName() : TEXT("");
					for (const FFieldSystemCommand& Command : FieldSystemActor->GetFieldSystemComponent()->GetFieldSystem()->Commands)
					{
						if (Command.RootNode)
						{
							FFieldSystemCommand NewCommand = { Command.TargetAttribute, Command.RootNode->NewCopy() };
							NewCommand.InitFieldNodes(0.0, Name);

							for (const TPair<FFieldSystemMetaData::EMetaType, TUniquePtr<FFieldSystemMetaData>>& Elem : Command.MetaData)
							{
								NewCommand.MetaData.Add(Elem.Key, TUniquePtr<FFieldSystemMetaData>(Elem.Value->NewCopy()));
							}
							CombinedCommmands.Emplace(NewCommand);
						}
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
#if WITH_CHAOS
	if (ChaosSolverActor)
	{
		return ChaosSolverActor;
	}
	else
	{
		FPhysScene_Chaos const* const Scene = GetInnerChaosScene();
		return Scene ? Cast<AChaosSolverActor>(Scene->GetSolverActor()) : nullptr;
	}

#endif
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
		GlobalMatrices.Reset();
		GlobalMatrices.Append(Results->GlobalTransforms);	
	}
	else
	{
		// If hierarchy topology has changed, the RestTransforms is invalidated.
		if (RestTransforms.Num() != GetTransformArray().Num())
		{
			RestTransforms.Empty();
		}

		if (!DynamicCollection && RestTransforms.Num() > 0)
		{
			GeometryCollectionAlgo::GlobalMatrices(RestTransforms, GetParentArray(), GlobalMatrices);
		}
		else
		{
			// Have to fully rebuild
			if (DynamicCollection 
				&& RestCollection->bRemoveOnMaxSleep 
				&& DynamicCollection->HasAttribute("SleepTimer", FGeometryCollection::TransformGroup) 
				&& DynamicCollection->HasAttribute("UniformScale", FGeometryCollection::TransformGroup)
				&& DynamicCollection->HasAttribute("MaxSleepTime", FGeometryCollection::TransformGroup)
				&& DynamicCollection->HasAttribute("RemovalDuration", FGeometryCollection::TransformGroup))
			{
				const TManagedArray<float>& SleepTimer = DynamicCollection->GetAttribute<float>("SleepTimer", FGeometryCollection::TransformGroup);
				const TManagedArray<float>& MaxSleepTime = DynamicCollection->GetAttribute<float>("MaxSleepTime", FGeometryCollection::TransformGroup);
				const TManagedArray<float>& RemovalDuration = DynamicCollection->GetAttribute<float>("RemovalDuration", FGeometryCollection::TransformGroup);
				TManagedArray<FTransform>& UniformScale = DynamicCollection->GetAttribute<FTransform>("UniformScale", FGeometryCollection::TransformGroup);

				for (int32 Idx = 0; Idx < GetTransformArray().Num(); ++Idx)
				{
					if (SleepTimer[Idx] > MaxSleepTime[Idx])
					{
						float Scale = 1.0 - FMath::Min(1.0, (SleepTimer[Idx] - MaxSleepTime[Idx]) / RemovalDuration[Idx]);
						
						if ((Scale < 1.0) && (Scale > 0.0))
						{
							float ShrinkRadius = 0.0f;
							FSphere AccumulatedSphere;
							if (CalculateInnerSphere(Idx, AccumulatedSphere))
							{
								ShrinkRadius = -AccumulatedSphere.W;
							}
							
							FQuat LocalRotation = (GetComponentTransform().Inverse() * FTransform(GlobalMatrices[Idx]).Inverse()).GetRotation();
							FTransform LocalDown(LocalRotation.RotateVector(FVector(0.f, 0.f, ShrinkRadius)));
							FTransform ToCOM(DynamicCollection->MassToLocal[Idx].GetTranslation());
							UniformScale[Idx] = ToCOM.Inverse() * LocalDown.Inverse() * FTransform(FQuat::Identity, FVector(0.f, 0.f, 0.f), FVector(Scale)) * LocalDown * ToCOM;
						}
	
					}
				}
				
				GeometryCollectionAlgo::GlobalMatrices(GetTransformArray(), GetParentArray(), UniformScale, GlobalMatrices);
			}
			else
			{ 
				GeometryCollectionAlgo::GlobalMatrices(GetTransformArray(), GetParentArray(), GlobalMatrices);		
			}
		}
	}
	
#if WITH_EDITOR
	if (GlobalMatrices.Num() > 0)
	{
		if (RestCollection->GetGeometryCollection()->HasAttribute("ExplodedVector", FGeometryCollection::TransformGroup))
		{
			const TManagedArray<FVector3f>& ExplodedVectors = RestCollection->GetGeometryCollection()->GetAttribute<FVector3f>("ExplodedVector", FGeometryCollection::TransformGroup);

			check(GlobalMatrices.Num() == ExplodedVectors.Num());

			for (int32 tt = 0, nt = GlobalMatrices.Num(); tt < nt; ++tt)
			{
				GlobalMatrices[tt] = GlobalMatrices[tt].ConcatTranslation((FVector)ExplodedVectors[tt]);
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

#if WITH_EDITOR
void UGeometryCollectionComponent::SelectEmbeddedGeometry()
{
	// First reset the selections
	for (TObjectPtr<UInstancedStaticMeshComponent> EmbeddedGeometryComponent : EmbeddedGeometryComponents)
	{
		EmbeddedGeometryComponent->ClearInstanceSelection();
	}
	
	const TManagedArray<int32>& ExemplarIndex = GetExemplarIndexArray();
	for (int32 SelectedBone : SelectedBones)
	{
		if (EmbeddedGeometryComponents.IsValidIndex(ExemplarIndex[SelectedBone]))
		{
			EmbeddedGeometryComponents[ExemplarIndex[SelectedBone]]->SelectInstance(true, EmbeddedInstanceIndex[SelectedBone], 1);
		}
	}
}
#endif

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
			// unhacked.
			//StaticMeshComp->SetVisibility(false);
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

#if GEOMETRYCOLLECTION_EDITOR_SELECTION
void UGeometryCollectionComponent::EnableTransformSelectionMode(bool bEnable)
{
	// TODO: Support for Nanite?
	if (SceneProxy && !SceneProxy->IsNaniteMesh() && RestCollection && RestCollection->HasVisibleGeometry())
	{
		static_cast<FGeometryCollectionSceneProxy*>(SceneProxy)->UseSubSections(bEnable, true);
	}
	bIsTransformSelectionModeEnabled = bEnable;
}
#endif  // #if GEOMETRYCOLLECTION_EDITOR_SELECTION

bool UGeometryCollectionComponent::IsEmbeddedGeometryValid() const
{
	// Check that the array of ISMCs that implement embedded geometry matches RestCollection Exemplar array.
	if (!RestCollection)
	{
		return false;
	}

	if (RestCollection->EmbeddedGeometryExemplar.Num() != EmbeddedGeometryComponents.Num())
	{
		return false;
	}

	for (int32 Idx = 0; Idx < EmbeddedGeometryComponents.Num(); ++Idx)
	{
		UStaticMesh* ExemplarStaticMesh = Cast<UStaticMesh>(RestCollection->EmbeddedGeometryExemplar[Idx].StaticMeshExemplar.TryLoad());
		if (!ExemplarStaticMesh)
		{
			return false;
		}

		if (ExemplarStaticMesh != EmbeddedGeometryComponents[Idx]->GetStaticMesh())
		{
			return false;
		}
	}

	return true;
}

void UGeometryCollectionComponent::ClearEmbeddedGeometry()
{
	AActor* OwningActor = GetOwner();
	TArray<UActorComponent*> TargetComponents;
	OwningActor->GetComponents(TargetComponents, false);

	for (UActorComponent* TargetComponent : TargetComponents)
	{
		if ((TargetComponent->GetOuter() == this) || !IsValidChecked(TargetComponent->GetOuter()))
		{
			if (UInstancedStaticMeshComponent* ISMComponent = Cast<UInstancedStaticMeshComponent>(TargetComponent))
			{
				ISMComponent->ClearInstances();
				ISMComponent->DestroyComponent();
			}
		}
	}

	EmbeddedGeometryComponents.Empty();
}

void UGeometryCollectionComponent::InitializeEmbeddedGeometry()
{
	if (RestCollection)
	{
		ClearEmbeddedGeometry();
		
		AActor* ActorOwner = GetOwner();
		check(ActorOwner);

		// Construct an InstancedStaticMeshComponent for each exemplar
		for (const FGeometryCollectionEmbeddedExemplar& Exemplar : RestCollection->EmbeddedGeometryExemplar)
		{
			if (UStaticMesh* ExemplarStaticMesh = Cast<UStaticMesh>(Exemplar.StaticMeshExemplar.TryLoad()))
			{
				if (UInstancedStaticMeshComponent* ISMC = NewObject<UInstancedStaticMeshComponent>(this))
				{
					ISMC->SetStaticMesh(ExemplarStaticMesh);
					ISMC->SetCullDistances(Exemplar.StartCullDistance, Exemplar.EndCullDistance);
					ISMC->SetCanEverAffectNavigation(false);
					ISMC->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
					ISMC->SetCastShadow(false);
					ISMC->SetMobility(EComponentMobility::Stationary);
					ISMC->SetupAttachment(this);
					ActorOwner->AddInstanceComponent(ISMC);
					ISMC->RegisterComponent();

					EmbeddedGeometryComponents.Add(ISMC);
				}
			}
		}

#if WITH_EDITOR
		EmbeddedBoneMaps.SetNum(RestCollection->EmbeddedGeometryExemplar.Num());
		EmbeddedInstanceIndex.Init(INDEX_NONE,RestCollection->GetGeometryCollection()->NumElements(FGeometryCollection::TransformGroup));
#endif

		CalculateGlobalMatrices();
		RefreshEmbeddedGeometry();
		
	}
}

void UGeometryCollectionComponent::IncrementSleepTimer(float DeltaTime)
{
	// If a particle is sleeping, increment its sleep timer, otherwise reset it.
	if (DynamicCollection && PhysicsProxy 
		&& DynamicCollection->HasAttribute("SleepTimer", FGeometryCollection::TransformGroup) 
		&& DynamicCollection->HasAttribute("MaxSleepTime", FGeometryCollection::TransformGroup)
		&& DynamicCollection->HasAttribute("RemovalDuration", FGeometryCollection::TransformGroup))
	{
		TManagedArray<float>& SleepTimer = DynamicCollection->GetAttribute<float>("SleepTimer", FGeometryCollection::TransformGroup);
		const TManagedArray<float>& RemovalDuration = DynamicCollection->GetAttribute<float>("RemovalDuration", FGeometryCollection::TransformGroup);
		const TManagedArray<float>& MaxSleepTime = DynamicCollection->GetAttribute<float>("MaxSleepTime", FGeometryCollection::TransformGroup);
		TArray<int32> ToDisable;
		for (int32 TransformIdx = 0; TransformIdx < SleepTimer.Num(); ++TransformIdx)
		{
			bool PreviouslyAwake = SleepTimer[TransformIdx] < MaxSleepTime[TransformIdx];
			if (SleepTimer[TransformIdx] < (MaxSleepTime[TransformIdx] + RemovalDuration[TransformIdx]))
			{
				SleepTimer[TransformIdx] = (DynamicCollection->DynamicState[TransformIdx] == (int)EObjectStateTypeEnum::Chaos_Object_Sleeping) ? SleepTimer[TransformIdx] + DeltaTime : 0.0f;

				if (SleepTimer[TransformIdx] > MaxSleepTime[TransformIdx])
				{ 
					DynamicCollection->MakeDirty();
					if (PreviouslyAwake)
					{
						// Disable the particle if it has been asleep for the requisite time
						ToDisable.Add(TransformIdx);
					}
				}
			}
		}

		if (ToDisable.Num())
		{
			PhysicsProxy->DisableParticles(ToDisable);	
		}
	}
}

bool UGeometryCollectionComponent::CalculateInnerSphere(int32 TransformIndex, FSphere& SphereOut) const
{
	// Approximates the inscribed sphere. Returns false if no such sphere exists, if for instance the index is to an embedded geometry. 

	const TManagedArray<int32>& TransformToGeometryIndex = RestCollection->GetGeometryCollection()->TransformToGeometryIndex;
	const TManagedArray<Chaos::FRealSingle>& InnerRadius = RestCollection->GetGeometryCollection()->InnerRadius;
	const TManagedArray<TSet<int32>>& Children = RestCollection->GetGeometryCollection()->Children;
	const TManagedArray<FTransform>& MassToLocal = RestCollection->GetGeometryCollection()->GetAttribute<FTransform>("MassToLocal", FGeometryCollection::TransformGroup);

	if (RestCollection->GetGeometryCollection()->IsRigid(TransformIndex))
	{
		// Sphere in component space, centered on body's COM.
		FVector COM = MassToLocal[TransformIndex].GetLocation();
		SphereOut = FSphere(COM, InnerRadius[TransformToGeometryIndex[TransformIndex]]);
		return true;
	}
	else if (RestCollection->GetGeometryCollection()->IsClustered(TransformIndex))
	{
		// Recursively accumulate the cluster's child spheres. 
		bool bSphereFound = false;
		for (int32 ChildIndex: Children[TransformIndex])
		{
			FSphere LocalSphere;
			if (CalculateInnerSphere(ChildIndex, LocalSphere))
			{
				if (!bSphereFound)
				{
					bSphereFound = true;
					SphereOut = LocalSphere;
				}
				else
				{
					SphereOut += LocalSphere;
				}
			}
		}
		return bSphereFound;
	}
	else
	{
		// Likely an embedded geometry, which doesn't count towards volume.
		return false;
	}
}