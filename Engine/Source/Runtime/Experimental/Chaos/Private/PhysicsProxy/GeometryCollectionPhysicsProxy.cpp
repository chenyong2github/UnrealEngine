// Copyright Epic Games, Inc. All Rights Reserved.


#include "PhysicsProxy/GeometryCollectionPhysicsProxy.h"
#include "PhysicsProxy/FieldSystemProxyHelper.h"

#include "PhysicsSolver.h"
#include "ChaosStats.h"
#include "Chaos/ArrayCollectionArray.h"
#include "Chaos/BVHParticles.h"
#include "Chaos/Transform.h"
#include "Chaos/ParallelFor.h"
#include "Chaos/Particles.h"
#include "Chaos/TriangleMesh.h"
#include "Chaos/MassProperties.h"
#include "ChaosSolversModule.h"
#include "Chaos/PBDCollisionConstraintsUtil.h"
#include "Chaos/PerParticleGravity.h"
#include "Chaos/ImplicitObject.h"
#include "Chaos/Serializable.h"
#include "Chaos/ErrorReporter.h"
#include "Chaos/PBDRigidClustering.h"
#include "GeometryCollection/GeometryCollectionAlgo.h"
#include "GeometryCollection/GeometryCollectionUtility.h"
#include "GeometryCollection/GeometryCollectionSimulationTypes.h"
#include "GeometryCollection/ManagedArrayCollection.h"
#include "Modules/ModuleManager.h"
#include "Chaos/PullPhysicsDataImp.h"
#include "Chaos/PBDRigidsEvolution.h"

#ifndef TODO_REIMPLEMENT_INIT_COMMANDS
#define TODO_REIMPLEMENT_INIT_COMMANDS 0
#endif

#ifndef TODO_REIMPLEMENT_FRACTURE
#define TODO_REIMPLEMENT_FRACTURE 0
#endif

#ifndef TODO_REIMPLEMENT_RIGID_CACHING
#define TODO_REIMPLEMENT_RIGID_CACHING 0
#endif

float CollisionParticlesPerObjectFractionDefault = 1.0f;
FAutoConsoleVariableRef CVarCollisionParticlesPerObjectFractionDefault(
	TEXT("p.CollisionParticlesPerObjectFractionDefault"), 
	CollisionParticlesPerObjectFractionDefault, 
	TEXT("Fraction of verts"));

bool DisableGeometryCollectionGravity = false;
FAutoConsoleVariableRef CVarGeometryCollectionDisableGravity(
	TEXT("p.GeometryCollectionDisableGravity"),
	DisableGeometryCollectionGravity,
	TEXT("Disable gravity for geometry collections"));

bool GeometryCollectionCollideAll = false;
FAutoConsoleVariableRef CVarGeometryCollectionCollideAll(
	TEXT("p.GeometryCollectionCollideAll"),
	GeometryCollectionCollideAll,
	TEXT("Bypass the collision matrix and make geometry collections collide against everything"));

DEFINE_LOG_CATEGORY_STATIC(UGCC_LOG, Error, All);

//==============================================================================
// FGeometryCollectionResults
//==============================================================================

FGeometryCollectionResults::FGeometryCollectionResults()
	: BaseIndex(0)
	, NumParticlesAdded(0)
	, IsObjectDynamic(false)
	, IsObjectLoading(false)
	, WorldBounds(ForceInit)
{}

void FGeometryCollectionResults::Reset()
{
	SolverDt = 0.0f;
	BaseIndex = 0;
	NumParticlesAdded = 0;
	DisabledStates.SetNum(0);
	GlobalTransforms.SetNum(0);
	ParticleToWorldTransforms.SetNum(0);
	IsObjectDynamic = false;
	IsObjectLoading = false;
	WorldBounds = FBoxSphereBounds(ForceInit);
}

//==============================================================================
// FGeometryCollectionPhysicsProxy helper functions
//==============================================================================


Chaos::FTriangleMesh* CreateTriangleMesh(
	const int32 FaceStart,
	const int32 FaceCount, 
	const TManagedArray<bool>& Visible, 
	const TManagedArray<FIntVector>& Indices,
	bool bRotateWinding)
{
	TArray<Chaos::TVector<int32, 3>> Faces;
	Faces.Reserve(FaceCount);
	
	const int32 FaceEnd = FaceStart + FaceCount;
	for (int Idx = FaceStart; Idx < FaceEnd; ++Idx)
	{
		// Note: This function used to cull small triangles.  As one of the purposes 
		// of the tri mesh this function creates is for level set rasterization, we 
		// don't want to do that.  Keep the mesh intact, which hopefully is water tight.
		if (Visible[Idx])
		{
			const FIntVector& Tri = Indices[Idx];

			if(bRotateWinding)
			{
				Faces.Add(Chaos::TVector<int32, 3>(Tri.Z, Tri.Y, Tri.X));
			}
			else
			{
				Faces.Add(Chaos::TVector<int32, 3>(Tri.X, Tri.Y, Tri.Z));
			}
		}
	}
	return new Chaos::FTriangleMesh(MoveTemp(Faces)); // Culls geometrically degenerate faces
}

TArray<int32> ComputeTransformToGeometryMap(const FGeometryCollection& Collection)
{
	const int32 NumTransforms = Collection.NumElements(FGeometryCollection::TransformGroup);
	const int32 NumGeometries = Collection.NumElements(FGeometryCollection::GeometryGroup);
	const TManagedArray<int32>& TransformIndex = Collection.TransformIndex;

	TArray<int32> TransformToGeometryMap;
	TransformToGeometryMap.AddUninitialized(NumTransforms);
	for(int32 GeometryIndex = 0; GeometryIndex < NumGeometries; ++GeometryIndex)
	{
		const int32 TransformGroupIndex = TransformIndex[GeometryIndex];
		TransformToGeometryMap[TransformGroupIndex] = GeometryIndex;
	}

	return TransformToGeometryMap;
}

//Computes the order of transform indices so that children in a tree always appear before their parents. Handles forests
TArray<int32> ComputeRecursiveOrder(const FGeometryCollection& Collection)
{
	const int32 NumTransforms = Collection.NumElements(FGeometryCollection::TransformGroup);
	const TManagedArray<int32>& Parent = Collection.Parent;
	const TManagedArray<TSet<int32>>& Children = Collection.Children;

	//traverse cluster hierarchy in depth first and record order
	struct FClusterProcessing
	{
		int32 TransformGroupIndex;
		enum
		{
			None,
			VisitingChildren
		} State;

		FClusterProcessing(int32 InIndex) : TransformGroupIndex(InIndex), State(None) {};
	};

	TArray<FClusterProcessing> ClustersToProcess;
	//enqueue all roots
	for(int32 TransformGroupIndex = 0; TransformGroupIndex < NumTransforms; TransformGroupIndex++)
	{
		if(Parent[TransformGroupIndex] == FGeometryCollection::Invalid && Children[TransformGroupIndex].Num() > 0)
		{
			ClustersToProcess.Emplace(TransformGroupIndex);
		}
	}

	TArray<int32> TransformOrder;
	TransformOrder.Reserve(NumTransforms);

	while(ClustersToProcess.Num())
	{
		FClusterProcessing CurCluster = ClustersToProcess.Pop();
		const int32 ClusterTransformIdx = CurCluster.TransformGroupIndex;
		if(CurCluster.State == FClusterProcessing::VisitingChildren)
		{
			//children already visited
			TransformOrder.Add(ClusterTransformIdx);
		}
		else
		{
			if(Children[ClusterTransformIdx].Num())
			{
				CurCluster.State = FClusterProcessing::VisitingChildren;
				ClustersToProcess.Add(CurCluster);

				//order of children doesn't matter as long as all children appear before parent
				for(int32 ChildIdx : Children[ClusterTransformIdx])
				{
					ClustersToProcess.Emplace(ChildIdx);
				}
			}
			else
			{
				TransformOrder.Add(ClusterTransformIdx);
			}
		}
	}

	return TransformOrder;
}

DECLARE_CYCLE_STAT(TEXT("FGeometryCollectionPhysicsProxy::PopulateSimulatedParticle"), STAT_PopulateSimulatedParticle, STATGROUP_Chaos);
void PopulateSimulatedParticle(
	Chaos::TPBDRigidParticleHandle<Chaos::FReal,3>* Handle,
	const FSharedSimulationParameters& SharedParams,
	const FCollisionStructureManager::FSimplicial* Simplicial,
	FGeometryDynamicCollection::FSharedImplicit Implicit,
	const FCollisionFilterData SimFilterIn,
	const FCollisionFilterData QueryFilterIn,
	float MassIn,
	FVector InertiaTensorVec,
	const FTransform& WorldTransform, 
	const uint8 DynamicState, 
	const int16 CollisionGroup)
{
	SCOPE_CYCLE_COUNTER(STAT_PopulateSimulatedParticle);

	Handle->SetDisabledLowLevel(false);
	Handle->SetX(WorldTransform.GetTranslation());
	Handle->SetV(Chaos::FVec3(0.f));
	Handle->SetR(WorldTransform.GetRotation().GetNormalized());
	Handle->SetW(Chaos::FVec3(0.f));
	Handle->SetP(Handle->X());
	Handle->SetQ(Handle->R());
	Handle->SetIsland(INDEX_NONE);
	Handle->SetCenterOfMass(FVector::ZeroVector);
	Handle->SetRotationOfMass(FQuat::Identity);

	//
	// Setup Mass
	//
	{
		Handle->SetObjectStateLowLevel(Chaos::EObjectStateType::Uninitialized);

		if (!CHAOS_ENSURE_MSG(FMath::IsWithinInclusive(MassIn, SharedParams.MinimumMassClamp, SharedParams.MaximumMassClamp),
			TEXT("Clamped mass[%3.5f] to range [%3.5f,%3.5f]"), MassIn, SharedParams.MinimumMassClamp, SharedParams.MaximumMassClamp))
		{
			MassIn = FMath::Clamp(MassIn, SharedParams.MinimumMassClamp, SharedParams.MaximumMassClamp);
		}

		if (!CHAOS_ENSURE_MSG(!FMath::IsNaN(InertiaTensorVec[0]) && !FMath::IsNaN(InertiaTensorVec[1]) && !FMath::IsNaN(InertiaTensorVec[2]),
			TEXT("Nan Tensor, reset to unit tesor")))
		{
			InertiaTensorVec = FVector(1);
		}
		else if (!CHAOS_ENSURE_MSG(FMath::IsWithinInclusive(InertiaTensorVec[0], SharedParams.MinimumInertiaTensorDiagonalClamp, SharedParams.MaximumInertiaTensorDiagonalClamp)
			&& FMath::IsWithinInclusive(InertiaTensorVec[1], SharedParams.MinimumInertiaTensorDiagonalClamp, SharedParams.MaximumInertiaTensorDiagonalClamp)
			&& FMath::IsWithinInclusive(InertiaTensorVec[2], SharedParams.MinimumInertiaTensorDiagonalClamp, SharedParams.MaximumInertiaTensorDiagonalClamp),
			TEXT("Clamped Inertia tensor[%3.5f,%3.5f,%3.5f]. Clamped each element to [%3.5f, %3.5f,]"), InertiaTensorVec[0], InertiaTensorVec[1], InertiaTensorVec[2],
			SharedParams.MinimumInertiaTensorDiagonalClamp, SharedParams.MaximumInertiaTensorDiagonalClamp))
		{
			InertiaTensorVec[0] = FMath::Clamp(InertiaTensorVec[0], SharedParams.MinimumInertiaTensorDiagonalClamp, SharedParams.MaximumInertiaTensorDiagonalClamp);
			InertiaTensorVec[1] = FMath::Clamp(InertiaTensorVec[1], SharedParams.MinimumInertiaTensorDiagonalClamp, SharedParams.MaximumInertiaTensorDiagonalClamp);
			InertiaTensorVec[2] = FMath::Clamp(InertiaTensorVec[2], SharedParams.MinimumInertiaTensorDiagonalClamp, SharedParams.MaximumInertiaTensorDiagonalClamp);
		}

		Handle->SetM(MassIn);
		Handle->SetI(Chaos::PMatrix<float, 3, 3>(InertiaTensorVec[0], InertiaTensorVec[1], InertiaTensorVec[2]));
		Handle->SetObjectStateLowLevel(Chaos::EObjectStateType::Dynamic); // this step sets InvM, InvInertia, P, Q
	}

	Handle->SetCollisionGroup(CollisionGroup);

	if (Implicit)	//todo(ocohen): this is only needed for cases where clusters have no proxy. Kind of gross though, should refactor
	{
		TSharedPtr<Chaos::FImplicitObject, ESPMode::ThreadSafe> SharedImplicitTS(Implicit->DeepCopy().Release());
		FCollisionStructureManager::UpdateImplicitFlags(SharedImplicitTS.Get(), SharedParams.SizeSpecificData[0].CollisionType); // @todo(chaos) : Implicit constructor clobbers CollisionType
		Handle->SetSharedGeometry(SharedImplicitTS);
		Handle->SetHasBounds(true);
		Handle->SetLocalBounds(SharedImplicitTS->BoundingBox());
		const Chaos::FAABB3& LocalBounds = Handle->LocalBounds();
		const Chaos::FRigidTransform3 Xf(Handle->X(), Handle->R());
		const Chaos::FAABB3 TransformedBBox = LocalBounds.TransformedAABB(Xf);
		Handle->SetWorldSpaceInflatedBounds(TransformedBBox);
	}

	if (Simplicial && SharedParams.SizeSpecificData[0].CollisionType==ECollisionTypeEnum::Chaos_Surface_Volumetric)
	{
		Handle->CollisionParticlesInitIfNeeded();

		TUniquePtr<Chaos::FBVHParticles>& CollisionParticles = Handle->CollisionParticles();
		if (Simplicial->Size())
		{
			const Chaos::FAABB3 ImplicitShapeDomain = 
				Implicit && Implicit->GetType() == Chaos::ImplicitObjectType::LevelSet && Implicit->HasBoundingBox() ? 
				Implicit->BoundingBox() : Chaos::FAABB3::FullAABB();

			CollisionParticles->Resize(0);
			CollisionParticles->AddParticles(Simplicial->Size());
			for (int32 VertexIndex = 0; VertexIndex < (int32)Simplicial->Size(); ++VertexIndex)
			{
				CollisionParticles->X(VertexIndex) = Simplicial->X(VertexIndex);

				// Make sure the collision particles are at least in the domain 
				// of the implicit shape.
				ensure(ImplicitShapeDomain.Contains(CollisionParticles->X(VertexIndex)));
			}
		}

		// @todo(remove): IF there is no simplicial we should not be forcing one. 
		if (!CollisionParticles->Size())
		{
			CollisionParticles->AddParticles(1);
			CollisionParticles->X(0) = Chaos::FVec3(0);
		}
		CollisionParticles->UpdateAccelerationStructures();
	}

	if (GeometryCollectionCollideAll) // cvar
	{
		// Override collision filters and make this body collide with everything.
		int32 CurrShape = 0;
		FCollisionFilterData FilterData;
		FilterData.Word1 = 0xFFFF; // this body channel
		FilterData.Word3 = 0xFFFF; // collision candidate channels
		for (const TUniquePtr<Chaos::FPerShapeData>& Shape : Handle->ShapesArray())
		{
			Shape->SetSimEnabled(true);
			Shape->SetCollisionTraceType(Chaos::EChaosCollisionTraceFlag::Chaos_CTF_UseDefault);
			//Shape->CollisionTraceType = Chaos::EChaosCollisionTraceFlag::Chaos_CTF_UseSimpleAndComplex;
			Shape->SetSimData(FilterData);
			Shape->SetQueryData(FCollisionFilterData());
		}
	}
	else
	{
		for (const TUniquePtr<Chaos::FPerShapeData>& Shape : Handle->ShapesArray())
		{
			Shape->SetSimData(SimFilterIn);
			Shape->SetQueryData(QueryFilterIn);
		}
	}

	//
	//  Manage Object State
	//

	// Only sleep if we're not replaying a simulation
	// #BG TODO If this becomes an issue, recorded tracks should track awake state as well as transforms
	if (DynamicState == (uint8)EObjectStateTypeEnum::Chaos_Object_Sleeping)
	{
		Handle->SetObjectStateLowLevel(Chaos::EObjectStateType::Sleeping);
	}
	else if (DynamicState == (uint8)EObjectStateTypeEnum::Chaos_Object_Kinematic)
	{
		Handle->SetObjectStateLowLevel(Chaos::EObjectStateType::Kinematic);
	}
	else if (DynamicState == (uint8)EObjectStateTypeEnum::Chaos_Object_Static)
	{
		Handle->SetObjectStateLowLevel(Chaos::EObjectStateType::Static);
	}
	else
	{
		Handle->SetObjectStateLowLevel(Chaos::EObjectStateType::Dynamic);
	}
}

//==============================================================================
// FGeometryCollectionPhysicsProxy
//==============================================================================


FGeometryCollectionPhysicsProxy::FGeometryCollectionPhysicsProxy(
	UObject* InOwner,
	FGeometryDynamicCollection& GameThreadCollectionIn,
	const FSimulationParameters& SimulationParameters,
	FCollisionFilterData InSimFilter,
	FCollisionFilterData InQueryFilter,
	FInitFunc InInitFunc,
	FCacheSyncFunc InCacheSyncFunc,
	FFinalSyncFunc InFinalSyncFunc,
	const Chaos::EMultiBufferMode BufferMode)
	: Base(InOwner)
	, Parameters(SimulationParameters)
	, NumParticles(INDEX_NONE)
	, BaseParticleIndex(INDEX_NONE)
	, IsObjectDynamic(false)
	, IsObjectLoading(true)
	, SimFilter(InSimFilter)
	, QueryFilter(InQueryFilter)
#if TODO_REIMPLEMENT_RIGID_CACHING
	, ProxySimDuration(0.0f)
	, LastSyncCountGT(MAX_uint32)
#endif
	, InitFunc(InInitFunc)
	, CacheSyncFunc(InCacheSyncFunc)
	, FinalSyncFunc(InFinalSyncFunc)

	, CollisionParticlesPerObjectFraction(CollisionParticlesPerObjectFractionDefault)

	, GameThreadCollection(GameThreadCollectionIn)
{
	// We rely on a guarded buffer.
	check(BufferMode == Chaos::EMultiBufferMode::TripleGuarded);
}


FGeometryCollectionPhysicsProxy::~FGeometryCollectionPhysicsProxy()
{}

float ReportHighParticleFraction = -1.f;
FAutoConsoleVariableRef CVarReportHighParticleFraction(TEXT("p.gc.ReportHighParticleFraction"), ReportHighParticleFraction, TEXT("Report any objects with particle fraction above this threshold"));

void FGeometryCollectionPhysicsProxy::Initialize(Chaos::FPBDRigidsEvolutionBase *Evolution)
{
	check(IsInGameThread());

	//
	// Game thread initilization. 
	//
	//  1) Create a input buffer to store all game thread side data. 
	//  2) Populate the buffer with the necessary data.
	//  3) Deep copy the data to the other buffers. 
	//
	FGeometryDynamicCollection& DynamicCollection = GameThreadCollection;

	InitializeDynamicCollection(DynamicCollection, *Parameters.RestCollection, Parameters);

	NumParticles = DynamicCollection.NumElements(FGeometryCollection::TransformGroup);
	BaseParticleIndex = 0; // Are we always zero indexed now?
	SolverClusterID.Init(nullptr, NumParticles);
	SolverClusterHandles.Init(nullptr, NumParticles);
	SolverParticleHandles.Init(nullptr, NumParticles);

	//	
	//  Give clients the opportunity to update the parameters before the simualtion is setup. 
	//
	if (InitFunc)
	{
		InitFunc(Parameters);
	}

	//
	// Collision vertices down sampling validation.  
	//
	CollisionParticlesPerObjectFraction = Parameters.CollisionSampleFraction * CollisionParticlesPerObjectFractionDefault;
	if (ReportHighParticleFraction > 0)
	{
		for (const FSharedSimulationSizeSpecificData& Data : Parameters.Shared.SizeSpecificData)
		{
			if (Data.CollisionParticlesFraction >= ReportHighParticleFraction)
			{
				ensureMsgf(false, TEXT("Collection with small particle fraction"));
				UE_LOG(LogChaos, Warning, TEXT("Collection with small particle fraction(%f):%s"), Data.CollisionParticlesFraction, *Parameters.Name);
			}
		}
	}


#if TODO_REIMPLEMENT_RIGID_CACHING

	// todo : Replace with normal funcs in this class
	Callbacks->SetUpdateRecordedStateFunction([this](float SolverTime, const TManagedArray<int32>& RigidBodyID, const TManagedArray<FGeometryCollectionBoneNode>& Hierarchy, const FSolverCallbacks::FParticlesType& Particles, const FSolverCallbacks::FCollisionConstraintsType& CollisionRule)
		{
			UpdateRecordedState(SolverTime, RigidBodyID, Hierarchy, Particles, CollisionRule);
		});

	Callbacks->SetCommitRecordedStateFunction([this](FRecordedTransformTrack& InTrack)
		{
			InTrack = FRecordedTransformTrack::ProcessRawRecordedData(RecordedTracks);
		});

	// Old proxy init
	RecordedTracks.Records.Reset();

	if (Parameters.bClearCache)
	{
		if (ResetAnimationCacheCallback)
		{
			ResetAnimationCacheCallback();
		}
	}

	ProxySimDuration = T(0);
	LastSyncCountGT = 0;


#endif

	/*
	// Initialize data for faster bound calculations
	// precompute data used for bounds calculation
	{
		const TManagedArray<FBox>& BoundingBoxes = Parameters.RestCollection->BoundingBox;
		const TManagedArray<int32>& TransformIndices = Parameters.RestCollection->TransformIndex;

		const int32 NumBoxes = BoundingBoxes.Num();

		ValidGeometryBoundingBoxes.Reset();
		ValidGeometryTransformIndices.Reset();
		for (int32 BoxIdx = 0; BoxIdx < NumBoxes; ++BoxIdx)
		{
			const int32 CurrTransformIndex = TransformIndices[BoxIdx];

			if (Parameters.RestCollection->IsGeometry(CurrTransformIndex))
			{
				ValidGeometryBoundingBoxes.Add(BoundingBoxes[BoxIdx]);
				ValidGeometryTransformIndices.Add(CurrTransformIndex);
			}
		}

		FBox BoundingBox(ForceInit);
		const FMatrix& ActorToWorld = Parameters.WorldTransform.ToMatrixWithScale();
		for (int i = 0; i < ValidGeometryBoundingBoxes.Num(); ++i)
		{
			BoundingBox += ValidGeometryBoundingBoxes[i].TransformBy(
				TmpGlobalTransforms[ValidGeometryTransformIndices[i]] * ActorToWorld);
		}

		//Results.Get(0).WorldBounds = FBoxSphereBounds(BoundingBox);
		//Results.Get(1).WorldBounds = FBoxSphereBounds(BoundingBox);
	}
	*/

	// Initialise GT/External particles
	const int32 NumTransforms = GameThreadCollection.Transform.Num();

	// Attach the external particles to the gamethread collection
	if (GameThreadCollection.HasAttribute(FGeometryCollection::ParticlesAttribute, FTransformCollection::TransformGroup))
	{ 
		GameThreadCollection.RemoveAttribute(FGeometryCollection::ParticlesAttribute, FTransformCollection::TransformGroup);
	}
		
	GameThreadCollection.AddExternalAttribute<TUniquePtr<Chaos::FGeometryParticle>>(FGeometryCollection::ParticlesAttribute, FTransformCollection::TransformGroup, GTParticles);
	

	if(ensure(NumTransforms == GameThreadCollection.Implicits.Num() && NumTransforms == GTParticles.Num())) // Implicits are in the transform group so this invariant should always hold
	{
		for(int32 Index = 0; Index < NumTransforms; ++Index)
		{
			GTParticles[Index] = Chaos::FGeometryParticle::CreateParticle();
			Chaos::FGeometryParticle* P = GTParticles[Index].Get();

			const FTransform& T = Parameters.WorldTransform * GameThreadCollection.Transform[Index];
			P->SetX(T.GetTranslation(), false);
			P->SetR(T.GetRotation(), false);
			P->SetUserData(Parameters.UserData);
			P->SetProxy(this);
			P->SetGeometry(GameThreadCollection.Implicits[Index]);
			
			P->SetUniqueIdx(Evolution->GenerateUniqueIdx());


			const Chaos::FShapesArray& Shapes = P->ShapesArray();
			const int32 NumShapes = Shapes.Num();
			for(int32 ShapeIndex = 0; ShapeIndex < NumShapes; ++ShapeIndex)
			{
				Chaos::FPerShapeData* Shape = Shapes[ShapeIndex].Get();
				Shape->SetSimData(SimFilter);
				Shape->SetQueryData(QueryFilter);
				Shape->SetProxy(this);
				Shape->SetMaterial(Parameters.PhysicalMaterialHandle);
			}
		}
	}

	// Skip simplicials, as they're owned by unique pointers.
	TMap<FName, TSet<FName>> SkipList;
	TSet<FName>& TransformGroupSkipList = SkipList.Emplace(FTransformCollection::TransformGroup);
	TransformGroupSkipList.Add(DynamicCollection.SimplicialsAttribute);

	PhysicsThreadCollection.CopyMatchingAttributesFrom(DynamicCollection, &SkipList);

	// Copy simplicials.
	// TODO: Ryan - Should we just transfer ownership of the SimplicialsAttribute from the DynamicCollection to
	// the PhysicsThreadCollection?
	{
		if (DynamicCollection.HasAttribute(DynamicCollection.SimplicialsAttribute, FTransformCollection::TransformGroup))
		{
			const auto& SourceSimplicials = DynamicCollection.GetAttribute<TUniquePtr<FSimplicial>>(
				DynamicCollection.SimplicialsAttribute, FTransformCollection::TransformGroup);
			for (int32 Index = PhysicsThreadCollection.NumElements(FTransformCollection::TransformGroup) - 1; 0 <= Index; Index--)
			{
				PhysicsThreadCollection.Simplicials[Index].Reset(
					SourceSimplicials[Index] ? SourceSimplicials[Index]->NewCopy() : nullptr);
			}
		}
		else
		{
			for (int32 Index = PhysicsThreadCollection.NumElements(FTransformCollection::TransformGroup) - 1; 0 <= Index; Index--)
			{
				PhysicsThreadCollection.Simplicials[Index].Reset();
			}
		}
	}
}


void FGeometryCollectionPhysicsProxy::InitializeDynamicCollection(FGeometryDynamicCollection& DynamicCollection, const FGeometryCollection& RestCollection, const FSimulationParameters& Params)
{
	// 
	// This function will use the rest collection to populate the dynamic collection. 
	//

	TMap<FName, TSet<FName>> SkipList;
	TSet<FName>& TransformGroupSkipList = SkipList.Emplace(FTransformCollection::TransformGroup);
	TransformGroupSkipList.Add(DynamicCollection.SimplicialsAttribute);
	DynamicCollection.CopyMatchingAttributesFrom(RestCollection, &SkipList);


	//
	// User defined initial velocities need to be populated. 
	//
	{
		if (Params.InitialVelocityType == EInitialVelocityTypeEnum::Chaos_Initial_Velocity_User_Defined)
		{
			DynamicCollection.InitialLinearVelocity.Fill(Params.InitialLinearVelocity);
			DynamicCollection.InitialAngularVelocity.Fill(Params.InitialAngularVelocity);
		}
	}

	// process simplicials
	{
		// CVar defined in BodyInstance but pertinent here as we will need to copy simplicials in the case that this is set.
		// Original CVar is read-only so taking a static ptr here is fine as the value cannot be changed
		static IConsoleVariable* AnalyticDisableCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("p.IgnoreAnalyticCollisionsOverride"));
		static const bool bAnalyticsDisabled = (AnalyticDisableCVar && AnalyticDisableCVar->GetBool());

		if (RestCollection.HasAttribute(DynamicCollection.SimplicialsAttribute, FTransformCollection::TransformGroup)
			&& (Params.Shared.SizeSpecificData[0].CollisionType == ECollisionTypeEnum::Chaos_Surface_Volumetric || bAnalyticsDisabled))
		{
			const auto& RestSimplicials = RestCollection.GetAttribute<TUniquePtr<FSimplicial>>(
				DynamicCollection.SimplicialsAttribute, FTransformCollection::TransformGroup);
			for (int32 Index = DynamicCollection.NumElements(FTransformCollection::TransformGroup) - 1; 0 <= Index; Index--)
			{
				DynamicCollection.Simplicials[Index].Reset(
					RestSimplicials[Index] ? RestSimplicials[Index]->NewCopy() : nullptr);
			}
		}
		else
		{
			for (int32 Index = DynamicCollection.NumElements(FTransformCollection::TransformGroup) - 1; 0 <= Index; Index--)
			{
				DynamicCollection.Simplicials[Index].Reset();
			}
		}
	}

	// Process Activity
	{
		const int32 NumTransforms = DynamicCollection.SimulatableParticles.Num();
		if (!RestCollection.HasAttribute(FGeometryCollection::SimulatableParticlesAttribute, FTransformCollection::TransformGroup))
		{
			// If no simulation data is available then default to the simulation of just the rigid geometry.
			for (int32 TransformIdx = 0; TransformIdx < NumTransforms; TransformIdx++)
			{
				if (DynamicCollection.Children[TransformIdx].Num())
				{
					DynamicCollection.SimulatableParticles[TransformIdx] = false;
				}
				else
				{
					DynamicCollection.SimulatableParticles[TransformIdx] = DynamicCollection.Active[TransformIdx];
				}
			}
		}
	}

}

int32 ReportTooManyChildrenNum = -1;
FAutoConsoleVariableRef CVarReportTooManyChildrenNum(TEXT("p.ReportTooManyChildrenNum"), ReportTooManyChildrenNum, TEXT("Issue warning if more than this many children exist in a single cluster"));

void FGeometryCollectionPhysicsProxy::InitializeBodiesPT(Chaos::FPBDRigidsSolver* RigidsSolver, typename Chaos::FPBDRigidsSolver::FParticlesType& Particles)
{
	const FGeometryCollection* RestCollection = Parameters.RestCollection;
	const FGeometryDynamicCollection& DynamicCollection = PhysicsThreadCollection;

	if (Parameters.Simulating)
	{
		const TManagedArray<int32>& TransformIndex = RestCollection->TransformIndex;
		const TManagedArray<int32>& BoneMap = RestCollection->BoneMap;
		const TManagedArray<int32>& Parent = RestCollection->Parent;
		const TManagedArray<TSet<int32>>& Children = RestCollection->Children;
		const TManagedArray<int32>& SimulationType = RestCollection->SimulationType;
		const TManagedArray<FVector>& Vertex = RestCollection->Vertex;
		const TManagedArray<float>& Mass = RestCollection->GetAttribute<float>("Mass", FTransformCollection::TransformGroup);
		const TManagedArray<FVector>& InertiaTensor = RestCollection->GetAttribute<FVector>("InertiaTensor", FTransformCollection::TransformGroup);

		const int32 NumTransforms = DynamicCollection.NumElements(FTransformCollection::TransformGroup);
		const TManagedArray<int32>& DynamicState = DynamicCollection.DynamicState;
		const TManagedArray<int32>& CollisionGroup = DynamicCollection.CollisionGroup;
		const TManagedArray<bool>& SimulatableParticles = DynamicCollection.SimulatableParticles;
		const TManagedArray<FTransform>& MassToLocal = DynamicCollection.MassToLocal;
		const TManagedArray<FVector>& InitialAngularVelocity = DynamicCollection.InitialAngularVelocity;
		const TManagedArray<FVector>& InitialLinearVelocity = DynamicCollection.InitialLinearVelocity;
		const TManagedArray<FGeometryDynamicCollection::FSharedImplicit>& Implicits = DynamicCollection.Implicits;
		const TManagedArray<TUniquePtr<FCollisionStructureManager::FSimplicial>>& Simplicials = DynamicCollection.Simplicials;

		TArray<FTransform> Transform;
		GeometryCollectionAlgo::GlobalMatrices(DynamicCollection.Transform, DynamicCollection.Parent, Transform);

		int NumRigids = 0; // ryan - Since we're doing SOA, we start at zero?
		BaseParticleIndex = NumRigids;

		// Gather unique indices from GT to pass into PT handle creation
		TArray<Chaos::FUniqueIdx> UniqueIndices;
		UniqueIndices.Reserve(SimulatableParticles.Num());

		// Count geometry collection leaf node particles to add
		int NumSimulatedParticles = 0;
		for (int32 Idx = 0; Idx < SimulatableParticles.Num(); ++Idx)
		{
			NumSimulatedParticles += SimulatableParticles[Idx];
			if (SimulatableParticles[Idx] && !RestCollection->IsClustered(Idx))
			{
				NumRigids++;
				UniqueIndices.Add(GTParticles[Idx]->UniqueIdx());
			}
		}

		// Add entries into simulation array
		RigidsSolver->GetEvolution()->ReserveParticles(NumSimulatedParticles);
		TArray<Chaos::TPBDGeometryCollectionParticleHandle<float, 3>*> Handles = RigidsSolver->GetEvolution()->CreateGeometryCollectionParticles(NumRigids, UniqueIndices.GetData());

		int32 NextIdx = 0;
		for (int32 Idx = 0; Idx < SimulatableParticles.Num(); ++Idx)
		{
			if (SimulatableParticles[Idx] && !RestCollection->IsClustered(Idx))
			{
				// todo: Unblocked read access of game thread data on the physics thread.

				Chaos::TPBDGeometryCollectionParticleHandle<float, 3>* Handle = Handles[NextIdx++];

				RigidsSolver->AddParticleToProxy(Handle, this);

				SolverParticleHandles[Idx] = Handle;
				HandleToTransformGroupIndex.Add(Handle, Idx);

				// We're on the physics thread here but we've already set up the GT particles and we're just linking here
				Handle->GTGeometryParticle() = GTParticles[Idx].Get();

				check(SolverParticleHandles[Idx]->GetParticleType() == Handle->GetParticleType());
				RigidsSolver->GetEvolution()->CreateParticle(Handle);
			}
		}

		const float StrainDefault = Parameters.DamageThreshold.Num() ? Parameters.DamageThreshold[0] : 0;
		// Add the rigid bodies

		// Iterating over the geometry group is a fast way of skipping everything that's
		// not a leaf node, as each geometry has a transform index, which is a shortcut
		// for the case when there's a 1-to-1 mapping between transforms and geometries.
		// At the point that we start supporting instancing, this assumption will no longer
		// hold, and those reverse mappints will be INDEX_NONE.

		const int32 NumGeometries = DynamicCollection.NumElements(FGeometryCollection::GeometryGroup);
		ParallelFor(NumGeometries, [&](int32 GeometryIndex)
		//for (int32 GeometryIndex = 0; GeometryIndex < NumGeometries; ++GeometryIndex)
		{
			const int32 TransformGroupIndex = TransformIndex[GeometryIndex];
			if (FClusterHandle* Handle = SolverParticleHandles[TransformGroupIndex])
			{
				// Mass space -> Composed parent space -> world
				const FTransform WorldTransform = 
					MassToLocal[TransformGroupIndex] * Transform[TransformGroupIndex] * Parameters.WorldTransform;

				PopulateSimulatedParticle(
					Handle,
					Parameters.Shared,
					Simplicials[TransformGroupIndex].Get(),
					Implicits[TransformGroupIndex],
					SimFilter,
					QueryFilter,
					Mass[TransformGroupIndex],
					InertiaTensor[TransformGroupIndex],
					WorldTransform,
					(uint8)DynamicState[TransformGroupIndex],
					CollisionGroup[TransformGroupIndex]);

				if (Parameters.EnableClustering)
				{
					Handle->SetClusterGroupIndex(Parameters.ClusterGroupIndex);
					Handle->SetStrain(StrainDefault);
				}

				TUniquePtr<Chaos::FBVHParticles>& CollisionParticles = Handle->CollisionParticles();
				CollisionParticles.Reset(Simplicials[TransformGroupIndex]?Simplicials[TransformGroupIndex]->NewCopy():nullptr); // @chaos(optimize) : maybe just move this memory instead. 
				if (CollisionParticles)
				{
					int32 NumCollisionParticles = CollisionParticles->Size();
					int32 CollisionParticlesSize = FMath::Max(0, FMath::Min(int(NumCollisionParticles * CollisionParticlesPerObjectFraction), NumCollisionParticles));
					CollisionParticles->Resize(CollisionParticlesSize); // Truncates!
				}

				// #BGTODO - non-updating parameters - remove lin/ang drag arrays and always query material if this stays a material parameter
				Chaos::FChaosPhysicsMaterial* SolverMaterial = RigidsSolver->GetSimMaterials().Get(Parameters.PhysicalMaterialHandle.InnerHandle);
				if(SolverMaterial)
				{
					Handle->SetLinearEtherDrag(SolverMaterial->LinearEtherDrag);
					Handle->SetAngularEtherDrag(SolverMaterial->AngularEtherDrag);
				}

				const Chaos::FShapesArray& Shapes = Handle->ShapesArray();
				for(const TUniquePtr<Chaos::FPerShapeData>& Shape : Shapes)
				{
					Shape->SetMaterial(Parameters.PhysicalMaterialHandle);
				}
			}
		});

		// After population, the states of each particle could have changed
		Particles.UpdateGeometryCollectionViews();

		for (FFieldSystemCommand& Cmd : Parameters.InitializationCommands)
		{
			if(Cmd.MetaData.Contains(FFieldSystemMetaData::EMetaType::ECommandData_ProcessingResolution))
			{
				Cmd.MetaData.Remove(FFieldSystemMetaData::EMetaType::ECommandData_ProcessingResolution);
			}

			FFieldSystemMetaDataProcessingResolution* ResolutionData = new FFieldSystemMetaDataProcessingResolution(EFieldResolutionType::Field_Resolution_Maximum);

			Cmd.MetaData.Add(FFieldSystemMetaData::EMetaType::ECommandData_ProcessingResolution, TUniquePtr<FFieldSystemMetaDataProcessingResolution>(ResolutionData));
			Commands.Add(Cmd);
		}
		Parameters.InitializationCommands.Empty();
		FieldParameterUpdateCallback(RigidsSolver, false);

		if (Parameters.InitialVelocityType == EInitialVelocityTypeEnum::Chaos_Initial_Velocity_User_Defined)
		{
			// A previous implementation of this went wide on this loop.  The general 
			// rule of thumb for parallelization is that each thread needs at least
			// 1000 operations in order to overcome the expense of threading.  I don't
			// think that's generally going to be the case here...
			for (int32 TransformGroupIndex = 0; TransformGroupIndex < NumTransforms; ++TransformGroupIndex)
			{
				if (Chaos::TPBDRigidParticleHandle<float, 3>* Handle = SolverParticleHandles[TransformGroupIndex])
				{
					if (DynamicState[TransformGroupIndex] == (int32)EObjectStateTypeEnum::Chaos_Object_Dynamic)
					{
						Handle->SetV(InitialLinearVelocity[TransformGroupIndex]);
						Handle->SetW(InitialAngularVelocity[TransformGroupIndex]);
					}
				}
			}
		}

#if TODO_REIMPLEMENT_FRACTURE
		InitializeRemoveOnFracture(Particles, DynamicState);
#endif // TODO_REIMPLEMENT_FRACTURE

		// #BG Temporary - don't cluster when playing back. Needs to be changed when kinematics are per-proxy to support
		// kinematic to dynamic transition for clusters.
		if (Parameters.EnableClustering)// && Parameters.CacheType != EGeometryCollectionCacheType::Play)
		{
			// "RecursiveOrder" means bottom up - children come before their parents.
			const TArray<int32> RecursiveOrder = ComputeRecursiveOrder(*RestCollection);

			// Propagate simulated particle flags up the hierarchy from children 
			// to their parents, grandparents, etc...
			TArray<bool> SubTreeContainsSimulatableParticle;
			SubTreeContainsSimulatableParticle.SetNumZeroed(RecursiveOrder.Num());
			for (const int32 TransformGroupIndex : RecursiveOrder)
			{
				if (SimulatableParticles[TransformGroupIndex] && !RestCollection->IsClustered(TransformGroupIndex))
				{
					// Rigid node
					SubTreeContainsSimulatableParticle[TransformGroupIndex] =
						SolverParticleHandles[TransformGroupIndex] != nullptr;
				}
				else
				{
					// Cluster parent
					const TSet<int32>& ChildIndices = Children[TransformGroupIndex];
					for (const int32 ChildIndex : ChildIndices)
					{
						if(SubTreeContainsSimulatableParticle[ChildIndex])
						{
							SubTreeContainsSimulatableParticle[TransformGroupIndex] = true;
							break;
						}
					}
				}
			}

			TArray<Chaos::TPBDRigidClusteredParticleHandle<float, 3>*> ClusterHandles;
			// Ryan - It'd be better to batch allocate cluster particles ahead of time,
			// but if ClusterHandles is empty, then new particles will be allocated
			// on the fly by TPBDRigidClustering::CreateClusterParticle(), which 
			// needs to work before this does...
			//ClusterHandles = GetSolver()->GetEvolution()->CreateClusteredParticles(NumClusters);

			int32 ClusterHandlesIndex = 0;
			TArray<Chaos::TPBDRigidParticleHandle<float, 3>*> RigidChildren;
			TArray<int32> RigidChildrenTransformGroupIndex;
			for (const int32 TransformGroupIndex : RecursiveOrder)
			{
				// Don't construct particles for branches of the hierarchy that  
				// don't contain any simulated particles.
				if (!SubTreeContainsSimulatableParticle[TransformGroupIndex])
				{
					continue;
				}

				RigidChildren.Reset(Children.Num());
				RigidChildrenTransformGroupIndex.Reset(Children.Num());
				for (const int32 ChildIndex : Children[TransformGroupIndex])
				{
					if (Chaos::TPBDRigidClusteredParticleHandle<float, 3>* Handle = SolverParticleHandles[ChildIndex])
					{
						RigidChildren.Add(Handle);
						RigidChildrenTransformGroupIndex.Add(ChildIndex);
					}
				}

				if (RigidChildren.Num())
				{
					if (ReportTooManyChildrenNum >= 0 && RigidChildren.Num() > ReportTooManyChildrenNum)
					{
						UE_LOG(LogChaos, Warning, TEXT("Too many children (%d) in a single cluster:%s"), 
							RigidChildren.Num(), *Parameters.Name);
					}

					Chaos::FClusterCreationParameters CreationParameters;
					CreationParameters.ClusterParticleHandle = ClusterHandles.Num() ? ClusterHandles[ClusterHandlesIndex++] : nullptr;

					// Hook the handle up with the GT particle
					Chaos::FGeometryParticle* GTParticle = GTParticles[TransformGroupIndex].Get();

					Chaos::FUniqueIdx ExistingIndex = GTParticle->UniqueIdx();
					Chaos::TPBDRigidClusteredParticleHandle<float, 3>* Handle = BuildClusters(TransformGroupIndex, RigidChildren, RigidChildrenTransformGroupIndex, CreationParameters, &ExistingIndex);
					Handle->GTGeometryParticle() = GTParticle;

					int32 RigidChildrenIdx = 0;
					for(const int32 ChildTransformIndex : RigidChildrenTransformGroupIndex)
					{
						SolverClusterID[ChildTransformIndex] = RigidChildren[RigidChildrenIdx++]->CastToClustered()->ClusterIds().Id;;
					}
					SolverClusterID[TransformGroupIndex] = Handle->ClusterIds().Id;					
					
					// Cluster transform has been recalculated based on children - copy to the GT particle (not threadsafe - just testing)
					GTParticle->SetX(Handle->X());
					GTParticle->SetR(Handle->R());
					GTParticle->UpdateShapeBounds();

					SolverClusterHandles[TransformGroupIndex] = Handle;
					SolverParticleHandles[TransformGroupIndex] = Handle;
					HandleToTransformGroupIndex.Add(Handle, TransformGroupIndex);
					RigidsSolver->AddParticleToProxy(Handle, this);

					RigidsSolver->GetEvolution()->DirtyParticle(*Handle);
				}
			}

			// We've likely changed the state of leaf nodes, which are geometry
			// collection particles.  Update which particle views they belong in,
			// as well as views of clustered particles.
			Particles.UpdateGeometryCollectionViews(true); 

			// Set cluster connectivity.  TPBDRigidClustering::CreateClusterParticle() 
			// will optionally do this, but we switch that functionality off in BuildClusters().
			for(int32 TransformGroupIndex = 0; TransformGroupIndex < NumTransforms; ++TransformGroupIndex)
			{
				if (RestCollection->IsClustered(TransformGroupIndex))
				{
					if (SolverClusterHandles[TransformGroupIndex])
					{
						Chaos::FClusterCreationParameters ClusterParams;
						// #todo: should other parameters be set here?  Previously, there was no parameters being sent, and it is unclear
						// where some of these parameters are defined (ie: CoillisionThicknessPercent)
						ClusterParams.ConnectionMethod = Parameters.ClusterConnectionMethod;
						
						RigidsSolver->GetEvolution()->GetRigidClustering().GenerateConnectionGraph(SolverClusterHandles[TransformGroupIndex], ClusterParams);
					}
				}
			}
		} // end if EnableClustering
 

#if TODO_REIMPLEMENT_RIGID_CACHING
		// If we're recording and want to start immediately caching then we should cache the rest state
		if (Parameters.IsCacheRecording() && Parameters.CacheBeginTime == 0.0f)
		{
			if (UpdateRecordedStateCallback)
			{
				UpdateRecordedStateCallback(0.0f, RigidBodyID, Particles, RigidSolver->GetCollisionConstraints());
			}
		}
#endif // TODO_REIMPLEMENT_RIGID_CACHING



		if (DisableGeometryCollectionGravity) // cvar
		{
			// Our assumption is that you'd only ever want to wholesale opt geometry 
			// collections out of gravity for debugging, so we keep this conditional
			// out of the loop above and on it's own.  This means we can't turn gravity
			// back on once it's off, but even if we didn't enclose this in an if(),
			// this function won't be called again unless something dirties the proxy.

			Chaos::FPerParticleGravity& GravityForces = RigidsSolver->GetEvolution()->GetGravityForces();
			for (int32 HandleIdx = 0; HandleIdx < SolverParticleHandles.Num(); ++HandleIdx)
			{
				if (Chaos::TPBDRigidParticleHandle<float, 3>* Handle = SolverParticleHandles[HandleIdx])
				{
					Handle->SetGravityEnabled(false);
				}
			}
		}

	} // end if simulating...

}

int32 ReportNoLevelsetCluster = 0;
FAutoConsoleVariableRef CVarReportNoLevelsetCluster(TEXT("p.gc.ReportNoLevelsetCluster"), ReportNoLevelsetCluster, TEXT("Report any cluster objects without levelsets"));

DECLARE_CYCLE_STAT(TEXT("FGeometryCollectionPhysicsProxy::BuildClusters"), STAT_BuildClusters, STATGROUP_Chaos);
DECLARE_CYCLE_STAT(TEXT("FGeometryCollectionPhysicsProxy::BuildClusters:GlobalMatrices"), STAT_BuildClustersGlobalMatrices, STATGROUP_Chaos);


Chaos::TPBDRigidClusteredParticleHandle<float, 3>* 
FGeometryCollectionPhysicsProxy::BuildClusters(
	const uint32 CollectionClusterIndex, // TransformGroupIndex
	TArray<Chaos::TPBDRigidParticleHandle<float,3>*>& ChildHandles,
	const TArray<int32>& ChildTransformGroupIndices,
	const Chaos::FClusterCreationParameters & ClusterParameters,
	const Chaos::FUniqueIdx* ExistingIndex)
{
	SCOPE_CYCLE_COUNTER(STAT_BuildClusters);

	check(CollectionClusterIndex != INDEX_NONE);
	check(ChildHandles.Num() != 0);

	FGeometryDynamicCollection& DynamicCollection = PhysicsThreadCollection;
	TManagedArray<int32>& DynamicState = DynamicCollection.DynamicState;
	TManagedArray<int32>& ParentIndex = DynamicCollection.Parent;
	TManagedArray<TSet<int32>>& Children = DynamicCollection.Children;
	TManagedArray<FTransform>& Transform = DynamicCollection.Transform;
	TManagedArray<FTransform>& MassToLocal = DynamicCollection.MassToLocal;
	//TManagedArray<TSharedPtr<FCollisionStructureManager::FSimplicial> >& Simplicials = DynamicCollection.Simplicials;
	TManagedArray<FGeometryDynamicCollection::FSharedImplicit>& Implicits = DynamicCollection.Implicits;

	//If we are a root particle use the world transform, otherwise set the relative transform
	const FTransform CollectionSpaceTransform = GeometryCollectionAlgo::GlobalMatrix(Transform, ParentIndex, CollectionClusterIndex);
	const Chaos::TRigidTransform<float, 3> ParticleTM = MassToLocal[CollectionClusterIndex] * CollectionSpaceTransform * Parameters.WorldTransform;

	//create new cluster particle
	//The reason we need to pass in a mass orientation override is as follows:
	//Consider a pillar made up of many boxes along the Y-axis. In this configuration we could generate a proxy pillar along the Y with identity rotation.
	//Now if we instantiate the pillar and rotate it so that it is along the X-axis, we would still like to use the same pillar proxy.
	//Since the mass orientation is computed in world space in both cases we'd end up with a diagonal inertia matrix and identity rotation that looks like this: [big, small, big] or [small, big, big].
	//Because of this we need to know how to rotate collision particles and geometry to match with original computation. If it was just geometry we could transform it before passing, but we need collision particles as well
	Chaos::FClusterCreationParameters ClusterCreationParameters = ClusterParameters;
	ClusterCreationParameters.bGenerateConnectionGraph = false;
	// fix... ClusterCreationParameters.CollisionParticles = Simplicials[CollectionClusterIndex];
	ClusterCreationParameters.ConnectionMethod = Parameters.ClusterConnectionMethod;
	if (ClusterCreationParameters.CollisionParticles)
	{
		const int32 NumCollisionParticles = ClusterCreationParameters.CollisionParticles->Size();
		const int32 ClampedCollisionParticlesSize = 
			FMath::Max(0, FMath::Min(int(NumCollisionParticles * CollisionParticlesPerObjectFraction), NumCollisionParticles));
		ClusterCreationParameters.CollisionParticles->Resize(ClampedCollisionParticlesSize);
	}
	TArray<Chaos::TPBDRigidParticleHandle<float, 3>*> ChildHandlesCopy(ChildHandles);

	// Construct an active cluster particle, disable children, derive M and I from children:
	Chaos::TPBDRigidClusteredParticleHandle<float, 3>* Parent = 
		static_cast<Chaos::FPBDRigidsSolver*>(Solver)->GetEvolution()->GetRigidClustering().CreateClusterParticle(
			Parameters.ClusterGroupIndex, 
			MoveTemp(ChildHandlesCopy),
			ClusterCreationParameters,
			Implicits[CollectionClusterIndex], // union from children if null
			&ParticleTM,
			ExistingIndex
			);

	if (ReportNoLevelsetCluster && 
		Parent->DynamicGeometry())
	{
		//ensureMsgf(false, TEXT("Union object generated for cluster"));
		UE_LOG(LogChaos, Warning, TEXT("Union object generated for cluster:%s"), *Parameters.Name);
	}

	if (Parent->InvM() == 0.0)
	{
		if (Parent->ObjectState() == Chaos::EObjectStateType::Static)
		{
			DynamicState[CollectionClusterIndex] = (uint8)EObjectStateTypeEnum::Chaos_Object_Static;
		}
		else //if (Particles.ObjectState(NewSolverClusterID) == Chaos::EObjectStateType::Kinematic)
		{
			DynamicState[CollectionClusterIndex] = (uint8)EObjectStateTypeEnum::Chaos_Object_Kinematic;
		}
	}

	check(Parameters.RestCollection);
	const TManagedArray<float>& Mass = 
		Parameters.RestCollection->GetAttribute<float>("Mass", FTransformCollection::TransformGroup);
	const TManagedArray<FVector>& InertiaTensor = 
		Parameters.RestCollection->GetAttribute<FVector>("InertiaTensor", FTransformCollection::TransformGroup);

	PopulateSimulatedParticle(
		Parent,
		Parameters.Shared, 
		nullptr, // CollisionParticles is optionally set from CreateClusterParticle()
		nullptr, // Parent->Geometry() ? Parent->Geometry() : Implicits[CollectionClusterIndex], 
		SimFilter,
		QueryFilter,
		Parent->M() > 0.0 ? Parent->M() : Mass[CollectionClusterIndex], 
		Parent->I().GetDiagonal() != Chaos::FVec3(0.0) ? Parent->I().GetDiagonal() : InertiaTensor[CollectionClusterIndex],
		ParticleTM, 
		(uint8)DynamicState[CollectionClusterIndex], 
		0); // CollisionGroup

	// two-way mapping
	SolverClusterHandles[CollectionClusterIndex] = Parent;

	const int32 NumThresholds = Parameters.DamageThreshold.Num();
	const int32 Level = FMath::Clamp(CalculateHierarchyLevel(DynamicCollection, CollectionClusterIndex), 0, INT_MAX);
	const float DefaultDamage = NumThresholds > 0 ? Parameters.DamageThreshold[NumThresholds - 1] : 0.f;
	float Damage = Level < NumThresholds ? Parameters.DamageThreshold[Level] : DefaultDamage;

	if(Level >= Parameters.MaxClusterLevel)
	{
		Damage = FLT_MAX;
	}

	Parent->SetStrains(Damage);

	// #BGTODO This will not automatically update - material properties should only ever exist in the material, not in other arrays
	const Chaos::FChaosPhysicsMaterial* CurMaterial = static_cast<Chaos::FPBDRigidsSolver*>(Solver)->GetSimMaterials().Get(Parameters.PhysicalMaterialHandle.InnerHandle);
	if(CurMaterial)
	{
		Parent->SetLinearEtherDrag(CurMaterial->LinearEtherDrag);
		Parent->SetAngularEtherDrag(CurMaterial->AngularEtherDrag);
	}

	const Chaos::FShapesArray& Shapes = Parent->ShapesArray();
	for(const TUniquePtr<Chaos::FPerShapeData>& Shape : Shapes)
	{
		Shape->SetMaterial(Parameters.PhysicalMaterialHandle);
	}

	const FTransform ParentTransform = GeometryCollectionAlgo::GlobalMatrix(DynamicCollection.Transform, DynamicCollection.Parent, CollectionClusterIndex);

	int32 MinCollisionGroup = INT_MAX;
	for(int32 Idx=0; Idx < ChildHandles.Num(); Idx++)
	{
		Chaos::TPBDRigidParticleHandle<float, 3>* Child = ChildHandles[Idx];
		if (Chaos::TPBDRigidClusteredParticleHandle<float, 3>* ClusteredChild = Child->CastToClustered())
		{
			ClusteredChild->SetStrains(Damage);
		}

		const int32 ChildTransformGroupIndex = ChildTransformGroupIndices[Idx];
		SolverClusterHandles[ChildTransformGroupIndex] = Parent;

		MinCollisionGroup = FMath::Min(Child->CollisionGroup(), MinCollisionGroup);
	}
	Parent->SetCollisionGroup(MinCollisionGroup);

	// Populate bounds as we didn't pass a shared implicit to PopulateSimulatedParticle this will have been skipped, now that we have the full cluster we can build it
	if(Parent->Geometry() && Parent->Geometry()->HasBoundingBox())
	{
		Parent->SetHasBounds(true);
		Parent->SetLocalBounds(Parent->Geometry()->BoundingBox());
		const Chaos::FAABB3& LocalBounds = Parent->LocalBounds();
		const Chaos::TRigidTransform<float, 3> Xf(Parent->X(), Parent->R());
		const Chaos::FAABB3 TransformedBBox = LocalBounds.TransformedAABB(Xf);
		Parent->SetWorldSpaceInflatedBounds(TransformedBBox);

		static_cast<Chaos::FPBDRigidsSolver*>(Solver)->GetEvolution()->DirtyParticle(*Parent);
	}

	return Parent;
}

void FGeometryCollectionPhysicsProxy::GetFilteredParticleHandles(
	TArray<Chaos::TGeometryParticleHandle<float, 3>*>& Handles,
	const Chaos::FPBDRigidsSolver* RigidSolver,
	const EFieldFilterType FilterType)
{
	Handles.SetNum(0, false);

	// only the local handles
	TArray<FClusterHandle*>& ParticleHandles = GetSolverParticleHandles();
	Handles.Reserve(ParticleHandles.Num());

	if (FilterType == EFieldFilterType::Field_Filter_Dynamic)
	{
		for (FClusterHandle* ClusterHandle : ParticleHandles)
		{
			if (ClusterHandle && ClusterHandle->ObjectState() == Chaos::EObjectStateType::Dynamic)
			{
				Handles.Add(ClusterHandle);
			}
		}
	}
	else if (FilterType == EFieldFilterType::Field_Filter_Kinematic)
	{
		for (FClusterHandle* ClusterHandle : ParticleHandles)
		{
			if (ClusterHandle && ClusterHandle->ObjectState() == Chaos::EObjectStateType::Kinematic)
			{
				Handles.Add(ClusterHandle);
			}
		}
	}
	else if (FilterType == EFieldFilterType::Field_Filter_Static)
	{
		for (FClusterHandle* ClusterHandle : ParticleHandles)
		{
			if (ClusterHandle && ClusterHandle->ObjectState() == Chaos::EObjectStateType::Static)
			{
				Handles.Add(ClusterHandle);
			}
		}
	}
	else if (FilterType == EFieldFilterType::Field_Filter_All)
	{
		for (FClusterHandle* ClusterHandle : ParticleHandles)
		{
			if (ClusterHandle && ClusterHandle->ObjectState() != Chaos::EObjectStateType::Uninitialized)
			{
				Handles.Add(ClusterHandle);
			}
		}
	}
}

void FGeometryCollectionPhysicsProxy::GetRelevantParticleHandles(
	TArray<Chaos::TGeometryParticleHandle<float, 3>*>& Handles,
	const Chaos::FPBDRigidsSolver* RigidSolver, 
	EFieldResolutionType ResolutionType)
{
	Handles.SetNum(0, false);

	// only the local handles
	TArray<FClusterHandle*>& ParticleHandles = GetSolverParticleHandles();
	Handles.Reserve(ParticleHandles.Num());

	if (ResolutionType == EFieldResolutionType::Field_Resolution_Maximum)
	{
		for (FClusterHandle* ClusterHandle : ParticleHandles)
		{
			if (ClusterHandle )
			{
				Handles.Add(ClusterHandle);
			}
		}
	}
	else if (ResolutionType == EFieldResolutionType::Field_Resolution_DisabledParents)
	{
		for (FClusterHandle* ClusterHandle : ParticleHandles)
		{
			if (ClusterHandle && ClusterHandle->ClusterIds().Id == nullptr)
			{
				Handles.Add(ClusterHandle);
			}
		}
	}
	else if (ResolutionType == EFieldResolutionType::Field_Resolution_Minimal)
	{
		const auto& Clustering = RigidSolver->GetEvolution()->GetRigidClustering();
		const auto& ClusterMap = Clustering.GetChildrenMap();

		for (FClusterHandle* ClusterHandle : ParticleHandles)
		{
			if (ClusterHandle && !ClusterHandle->Disabled())
			{
				Handles.Add(ClusterHandle);
				if (ClusterHandle->ClusterIds().NumChildren)
				{
					Chaos::TPBDRigidParticleHandle<float, 3>* RigidHandle = ClusterHandle->CastToRigidParticle();
					if (ClusterMap.Contains(RigidHandle))
					{
						for (Chaos::TPBDRigidParticleHandle<float, 3> * Child : ClusterMap[RigidHandle])
						{
							Handles.Add(Child);
						}
					}
				}
			}
		}
	}

#if TODO_REIMPLEMENT_GET_RIGID_PARTICLES
		const Chaos::FPhysicsSolver::FParticlesType & Particles = RigidSolver->GetRigidParticles();
		if (ResolutionType == EFieldResolutionType::Field_Resolution_Minimal)
		{
			const Chaos::TArrayCollectionArray<Chaos::ClusterId>& ClusterIdArray = RigidSolver->GetRigidClustering().GetClusterIdsArray();


			//  Generate a Index mapping between the rigid body indices and 
			//  the particle indices. This allows the geometry collection to
			//  evaluate only its own ACTIVE particles + ClusterChildren
			int32 NumIndices = 0;
			Array.SetNumUninitialized(RigidBodyID.Num());
			for (int32 i = 0; i < RigidBodyID.Num(); i++)
			{
				const int32 RigidBodyIndex = RigidBodyID[i];
				if (RigidBodyIndex != INDEX_NONE && !Particles.Disabled(RigidBodyIndex)) // active bodies
				{
					Array[NumIndices] = { RigidBodyID[i],i };
					NumIndices++;
				}
				if (ClusterIdArray[RigidBodyIndex].Id != INDEX_NONE && !Particles.Disabled(ClusterIdArray[RigidBodyIndex].Id)) // children
				{
					Array[NumIndices] = { RigidBodyID[i],i };
					NumIndices++;
				}
			}
			Array.SetNum(NumIndices);
		}
		else if (ResolutionType == EFieldResolutionType::Field_Resolution_Maximum)
		{
			//  Generate a Index mapping between the rigid body indices and 
			//  the particle indices. This allows the geometry collection to
			//  evaluate only its own particles. 
			int32 NumIndices = 0;
			Array.SetNumUninitialized(RigidBodyID.Num());
			for (int32 i = 0; i < RigidBodyID.Num(); i++)
			{
				const int32 RigidBodyIndex = RigidBodyID[i];
				if (RigidBodyIndex != INDEX_NONE)
				{
					Array[NumIndices] = { RigidBodyIndex, i };
					NumIndices++;
				}
			}
			Array.SetNum(NumIndices);
		}
#endif
}

int32 FGeometryCollectionPhysicsProxy::CalculateHierarchyLevel(const FGeometryDynamicCollection& GeometryCollection, int32 TransformIndex) const
{
	int32 Level = 0;
	while (GeometryCollection.Parent[TransformIndex] != -1)
	{
		TransformIndex = GeometryCollection.Parent[TransformIndex];
		Level++;
	}
	return Level;
}

void FGeometryCollectionPhysicsProxy::InitializeRemoveOnFracture(FParticlesType& Particles, const TManagedArray<int32>& DynamicState)
{
	/*
	@todo break everything
	if (Parameters.DynamicCollection && Parameters.RemoveOnFractureEnabled)
	{
	//	TManagedArray<FGeometryCollectionBoneNode>& Hierarchy = Parameters.DynamicCollection->BoneHierarchy;

		for (int TransformGroupIndex = 0; TransformGroupIndex < RigidBodyID.Num(); TransformGroupIndex++)
		{
			if (RigidBodyID[TransformGroupIndex] != INDEX_NONE)
			{
				int32 RigidBodyIndex = RigidBodyID[TransformGroupIndex];

				if (Parameters.DynamicCollection->StatusFlags[TransformGroupIndex] & FGeometryCollection::FS_RemoveOnFracture)
				{
					Particles.ToBeRemovedOnFracture(RigidBodyIndex) = true;
				}
			}
		}
	}
	*/
}

void FGeometryCollectionPhysicsProxy::OnRemoveFromSolver(Chaos::FPBDRigidsSolver *RBDSolver)
{
	const FGeometryDynamicCollection& DynamicCollection = PhysicsThreadCollection;

	Chaos::FPBDRigidsEvolutionGBF* Evolution = RBDSolver->GetEvolution();

	for (const FClusterHandle* Handle : SolverClusterHandles)
	{
		RBDSolver->RemoveParticleToProxy(Handle);
	}

	for (FClusterHandle* Handle : SolverParticleHandles)
	{	
		if (Handle)
		{
			if (Chaos::TPBDRigidClusteredParticleHandle<Chaos::FReal, 3>* Cluster = Handle->CastToClustered())
			{
				Evolution->GetRigidClustering().GetTopLevelClusterParents().Remove(Cluster);
				Evolution->GetRigidClustering().GetChildrenMap().Remove(Cluster);
				Evolution->DestroyParticle(Cluster);
			}
			else
			{
				Evolution->DestroyParticle(Handle);
			}
		}
	}
}

void FGeometryCollectionPhysicsProxy::OnRemoveFromScene()
{
#if TODO_REIMPLEMENT_GET_RIGID_PARTICLES
	// #BG TODO This isn't great - we currently cannot handle things being removed from the solver.
	// need to refactor how we handle this and actually remove the particles instead of just constantly
	// growing the array. Currently everything is just tracked by index though so the solver will have
	// to notify all the proxies that a chunk of data was removed - or use a sparse array (undesireable)
	Chaos::FPhysicsSolver::FParticlesType& Particles = GetSolver<FSolver>()->GetRigidParticles();

	// #BG TODO Special case here because right now we reset/realloc the evolution per geom component
	// in endplay which clears this out. That needs to not happen and be based on world shutdown
	if(Particles.Size() == 0)
	{
		return;
	}

	const int32 Begin = BaseParticleIndex;
	const int32 Count = NumParticles;

	if (ensure((int32)Particles.Size() > 0 && (Begin + Count) <= (int32)Particles.Size()))
	{
		for (int32 ParticleIndex = 0; ParticleIndex < Count; ++ParticleIndex)
		{
			GetSolver<FSolver>()->GetEvolution()->DisableParticle(Begin + ParticleIndex);
			GetSolver<FSolver>()->GetRigidClustering().GetTopLevelClusterParents().Remove(Begin + ParticleIndex);
		}
	}
#endif
}

void FGeometryCollectionPhysicsProxy::SyncBeforeDestroy()
{
	if(FinalSyncFunc)
	{
#if TODO_REIMPLEMENT_RIGID_CACHING
		FinalSyncFunc(RecordedTracks);
#endif
	}
}

void FGeometryCollectionPhysicsProxy::BufferGameState() 
{
	//
	// There is currently no per advance updates to the GeometryCollection
	//
}

void FGeometryCollectionPhysicsProxy::BufferPhysicsResults(Chaos::FPBDRigidsSolver* CurrentSolver, Chaos::FDirtyGeometryCollectionData& BufferData)
{
	/**
	 * CONTEXT: PHYSICSTHREAD
	 * Called per-tick after the simulation has completed. The proxy should cache the results of their
	 * simulation into the local buffer. 
	 */
	using namespace Chaos;
	SCOPE_CYCLE_COUNTER(STAT_CacheResultGeomCollection);

	BufferData.SetProxy(*this);

	IsObjectDynamic = false;
	FGeometryCollectionResults& TargetResults = BufferData.Results;
	TargetResults.SolverDt = CurrentSolver->GetLastDt();	//todo: should this use timestamp for async mode?

	int32 NumTransformGroupElements = PhysicsThreadCollection.NumElements(FGeometryCollection::TransformGroup);
	if (TargetResults.NumTransformGroup() != NumTransformGroupElements)
	{
		TargetResults.InitArrays(PhysicsThreadCollection);

		// Base particle index to calculate index from a global particle index on the game thread
		TargetResults.BaseIndex = BaseParticleIndex;
		TargetResults.NumParticlesAdded = NumParticles;
	}

	
	const FTransform& ActorToWorld = Parameters.WorldTransform;
	const TManagedArray<int32>& Parent = PhysicsThreadCollection.Parent;
	const TManagedArray<TSet<int32>>& Children = PhysicsThreadCollection.Children;

	if(NumTransformGroupElements > 0)
	{ 
		SCOPE_CYCLE_COUNTER(STAT_CalcParticleToWorld);

		// initialize Target Results
		TargetResults.Transforms.Init(PhysicsThreadCollection.Transform);
		TargetResults.Children.Init(PhysicsThreadCollection.Children);
		TargetResults.Parent.Init(PhysicsThreadCollection.Parent);

		for (int32 TransformGroupIndex = 0; TransformGroupIndex < NumTransformGroupElements; ++TransformGroupIndex)
		{
			TargetResults.DisabledStates[TransformGroupIndex] = true;
			Chaos::TPBDRigidClusteredParticleHandle<float, 3>* Handle = SolverParticleHandles[TransformGroupIndex];
			if (!Handle)
			{
				PhysicsThreadCollection.Active[TransformGroupIndex] = !TargetResults.DisabledStates[TransformGroupIndex];
				continue;
			}

			// Dynamic state is also updated by the solver during field interaction.
			if (!Handle->Sleeping())
			{
				const Chaos::EObjectStateType ObjectState = Handle->ObjectState();
				switch (ObjectState)
				{
				case Chaos::EObjectStateType::Kinematic:
					TargetResults.DynamicState[TransformGroupIndex] = (int)EObjectStateTypeEnum::Chaos_Object_Kinematic;
					break;
				case Chaos::EObjectStateType::Static:
					TargetResults.DynamicState[TransformGroupIndex] = (int)EObjectStateTypeEnum::Chaos_Object_Static;
					break;
				case Chaos::EObjectStateType::Sleeping:
					TargetResults.DynamicState[TransformGroupIndex] = (int)EObjectStateTypeEnum::Chaos_Object_Sleeping;
					break;
				case Chaos::EObjectStateType::Dynamic:
				case Chaos::EObjectStateType::Uninitialized:
				default:
					TargetResults.DynamicState[TransformGroupIndex] = (int)EObjectStateTypeEnum::Chaos_Object_Dynamic;
					break;
				}
			}
			else
			{
				TargetResults.DynamicState[TransformGroupIndex] = (int)EObjectStateTypeEnum::Chaos_Object_Sleeping;
			}

			// Update the transform and parent hierarchy of the active rigid bodies. Active bodies can be either
			// rigid geometry defined from the leaf nodes of the collection, or cluster bodies that drive an entire
			// branch of the hierarchy within the GeometryCollection.
			// - Active bodies are directly driven from the global position of the corresponding
			//   rigid bodies within the solver ( cases where RigidBodyID[TransformGroupIndex] is not disabled ). 
			// - Deactivated bodies are driven from the transforms of their active parents. However the solver can
			//   take ownership of the parents during the simulation, so it might be necessary to force deactivated
			//   bodies out of the collections hierarchy during the simulation.  
			if (!Handle->Disabled())
			{
				// Update the transform of the active body. The active body can be either a single rigid
				// or a collection of rigidly attached geometries (Clustering). The cluster is represented as a
				// single transform in the GeometryCollection, and all children are stored in the local space
				// of the parent cluster.
	
				FTransform& ParticleToWorld = TargetResults.ParticleToWorldTransforms[TransformGroupIndex];
				ParticleToWorld = FTransform(Handle->R(), Handle->X());
				const FTransform MassToLocal = PhysicsThreadCollection.MassToLocal[TransformGroupIndex];

				TargetResults.Transforms[TransformGroupIndex] = MassToLocal.GetRelativeTransformReverse(ParticleToWorld).GetRelativeTransform(ActorToWorld);
				TargetResults.Transforms[TransformGroupIndex].NormalizeRotation();

				PhysicsThreadCollection.Transform[TransformGroupIndex] = TargetResults.Transforms[TransformGroupIndex];

				// Indicate that this object needs to be updated and the proxy is active.
				TargetResults.DisabledStates[TransformGroupIndex] = false;
				IsObjectDynamic = true;

				// If the parent of this NON DISABLED body is set to anything other than INDEX_NONE,
				// then it was just unparented, likely either by rigid clustering or by fields.  We
				// need to force all such enabled rigid bodies out of the transform hierarchy.
				TargetResults.Parent[TransformGroupIndex] = INDEX_NONE;
				if (PhysicsThreadCollection.Parent[TransformGroupIndex] != INDEX_NONE)
				{
					//GeometryCollectionAlgo::UnparentTransform(&PhysicsThreadCollection,TransformGroupIndex);
					PhysicsThreadCollection.Children[PhysicsThreadCollection.Parent[TransformGroupIndex]].Remove(TransformGroupIndex);
					PhysicsThreadCollection.Parent[TransformGroupIndex] = INDEX_NONE;
				}

				// When a leaf node rigid body is removed from a cluster, the rigid
				// body will become active and needs its clusterID updated.  This just
				// syncs the clusterID all the time.
				TPBDRigidParticleHandle<float, 3>* ClusterParentId = Handle->ClusterIds().Id;
				SolverClusterID[TransformGroupIndex] = ClusterParentId;
			}
			else    // Handle->Disabled()
			{
				// The rigid body parent cluster has changed within the solver, and its
				// parent body is not tracked within the geometry collection. So we need to
				// pull the rigid bodies out of the transform hierarchy, and just drive
				// the positions directly from the solvers cluster particle.
				if(TPBDRigidParticleHandle<float, 3>* ClusterParentBase = Handle->ClusterIds().Id)
				{
					if(Chaos::TPBDRigidClusteredParticleHandle<float, 3>* ClusterParent = ClusterParentBase->CastToClustered())
					{
						// syncronize parents if it has changed.
						if(SolverClusterID[TransformGroupIndex] != ClusterParent)
						{
							// Force all driven rigid bodies out of the transform hierarchy
							if(Parent[TransformGroupIndex] != INDEX_NONE)
							{
								// If the parent of this NON DISABLED body is set to anything other than INDEX_NONE,
								// then it was just unparented, likely either by rigid clustering or by fields.  We
								// need to force all such enabled rigid bodies out of the transform hierarchy.
								TargetResults.Parent[TransformGroupIndex] = INDEX_NONE;

								// GeometryCollectionAlgo::UnparentTransform(&PhysicsThreadCollection, ChildIndex);
								PhysicsThreadCollection.Children[PhysicsThreadCollection.Parent[TransformGroupIndex]].Remove(TransformGroupIndex);
								PhysicsThreadCollection.Parent[TransformGroupIndex] = INDEX_NONE;

								// Indicate that this object needs to be updated and the proxy is active.
								TargetResults.DisabledStates[TransformGroupIndex] = false;
								IsObjectDynamic                                   = true;
							}
							SolverClusterID[TransformGroupIndex] = Handle->ClusterIds().Id;
						}

						if(ClusterParent->InternalCluster())
						{
							Chaos::TPBDRigidClusteredParticleHandle<float, 3>* ProxyElementHandle = SolverParticleHandles[TransformGroupIndex];

							FTransform& ParticleToWorld = TargetResults.ParticleToWorldTransforms[TransformGroupIndex];
							ParticleToWorld             = ProxyElementHandle->ChildToParent() * FTransform(ClusterParent->R(), ClusterParent->X());    // aka ClusterChildToWorld

							// GeomToActor = ActorToWorld.Inv() * ClusterChildToWorld * MassToLocal.Inv();
							const FTransform MassToLocal                  = PhysicsThreadCollection.MassToLocal[TransformGroupIndex];
							TargetResults.Transforms[TransformGroupIndex] = MassToLocal.GetRelativeTransformReverse(ParticleToWorld).GetRelativeTransform(ActorToWorld);
							TargetResults.Transforms[TransformGroupIndex].NormalizeRotation();

							PhysicsThreadCollection.Transform[TransformGroupIndex] = TargetResults.Transforms[TransformGroupIndex];

							// Indicate that this object needs to be updated and the proxy is active.
							TargetResults.DisabledStates[TransformGroupIndex] = false;
							IsObjectDynamic                                   = true;

							ProxyElementHandle->X() = ParticleToWorld.GetTranslation();
							ProxyElementHandle->R() = ParticleToWorld.GetRotation();
							CurrentSolver->GetEvolution()->DirtyParticle(*ProxyElementHandle);
						}
					}
				}
			}    // end if

			PhysicsThreadCollection.Active[TransformGroupIndex] = !TargetResults.DisabledStates[TransformGroupIndex];
		}    // end for
	}        // STAT_CalcParticleToWorld scope

	// If object is dynamic, compute global matrices	
	if (IsObjectDynamic || TargetResults.GlobalTransforms.Num() == 0)
	{
		SCOPE_CYCLE_COUNTER(STAT_CalcGlobalGCMatrices);
		check(TargetResults.Transforms.Num() == TargetResults.Parent.Num());
		GeometryCollectionAlgo::GlobalMatrices(TargetResults.Transforms, TargetResults.Parent, TargetResults.GlobalTransforms);

		// Compute world bounds.  This is a loose bounds based on the circumscribed box 
		// of a bounding sphere for the geometry.		
		SCOPE_CYCLE_COUNTER(STAT_CalcGlobalGCBounds);
		FBox BoundingBox(ForceInit);
		for (int i = 0; i < ValidGeometryBoundingBoxes.Num(); ++i)
		{
			BoundingBox += ValidGeometryBoundingBoxes[i].TransformBy(TargetResults.GlobalTransforms[ValidGeometryTransformIndices[i]] * ActorToWorld);
		}
		TargetResults.WorldBounds = FBoxSphereBounds(BoundingBox);
	}

	// Advertise to game thread
	TargetResults.IsObjectDynamic = IsObjectDynamic;
	TargetResults.IsObjectLoading = IsObjectLoading;
}

void FGeometryCollectionPhysicsProxy::FlipBuffer()
{
	/**
	 * CONTEXT: PHYSICSTHREAD (Write Locked)
	 * Called by the physics thread to signal that it is safe to perform any double-buffer flips here.
	 * The physics thread has pre-locked an RW lock for this operation so the game thread won't be reading
	 * the data
	 */

	PhysToGameInterchange.FlipProducer();
}

// Called from FPhysScene_ChaosInterface::SyncBodies(), NOT the solver.
bool FGeometryCollectionPhysicsProxy::PullFromPhysicsState(const Chaos::FDirtyGeometryCollectionData& BufferData, const int32 SolverSyncTimestamp)
{
	/**
	 * CONTEXT: GAMETHREAD (Read Locked)
	 * Perform a similar operation to Sync, but take the data from a gamethread-safe buffer. This will be called
	 * from the game thread when it cannot sync to the physics thread. The simulation is very likely to be running
	 * when this happens so never read any physics thread data here!
	 *
	 * Note: A read lock will have been acquired for this - so the physics thread won't force a buffer flip while this
	 * sync is ongoing
	 */

	const FGeometryCollectionResults& TargetResults = BufferData.Results;

	FGeometryDynamicCollection& DynamicCollection = GameThreadCollection;

	TManagedArray<FVector>* LinearVelocity = DynamicCollection.FindAttributeTyped<FVector>("LinearVelocity", FTransformCollection::TransformGroup);

	// We should never be changing the number of entries, this would break other 
	// attributes in the transform group.
	const int32 NumTransforms = DynamicCollection.Transform.Num();
	if (ensure(NumTransforms == TargetResults.Transforms.Num()))
	{
		for (int32 TransformGroupIndex = 0; TransformGroupIndex < NumTransforms; ++TransformGroupIndex)
		{
			if (!TargetResults.DisabledStates[TransformGroupIndex])
			{
				DynamicCollection.Parent[TransformGroupIndex] = TargetResults.Parent[TransformGroupIndex];
				const FTransform& LocalTransform = TargetResults.Transforms[TransformGroupIndex];
				const FTransform& ParticleToWorld = TargetResults.ParticleToWorldTransforms[TransformGroupIndex];

				DynamicCollection.Transform[TransformGroupIndex] = LocalTransform;

				Chaos::FGeometryParticle* GTParticle = GTParticles[TransformGroupIndex].Get();

				if(LinearVelocity)
				{
					TManagedArray<FVector>* AngularVelocity = DynamicCollection.FindAttributeTyped<FVector>("AngularVelocity", FTransformCollection::TransformGroup);
					check(AngularVelocity);
					FVector DiffX = ParticleToWorld.GetTranslation() - GTParticle->X();
					FVector DiffR = (ParticleToWorld.GetRotation().Euler() - GTParticle->R().Euler()) * (PI / 180.0f);

					(*LinearVelocity)[TransformGroupIndex] = DiffX / TargetResults.SolverDt;
					(*AngularVelocity)[TransformGroupIndex] = DiffR / TargetResults.SolverDt;
				}

				GTParticles[TransformGroupIndex]->SetX(ParticleToWorld.GetTranslation());
				GTParticles[TransformGroupIndex]->SetR(ParticleToWorld.GetRotation());
			}

			DynamicCollection.DynamicState[TransformGroupIndex] = TargetResults.DynamicState[TransformGroupIndex];
			DynamicCollection.Active[TransformGroupIndex] = !TargetResults.DisabledStates[TransformGroupIndex];
		}

		//question: why do we need this? Sleeping objects will always have to update GPU
		DynamicCollection.MakeDirty();

		if (CacheSyncFunc)
		{
			CacheSyncFunc(TargetResults);
		}
	}

	return true;
}

//==============================================================================
// STATIC SETUP FUNCTIONS
//==============================================================================


int32 FindSizeSpecificIdx(const TArray<FSharedSimulationSizeSpecificData>& SizeSpecificData, const FBox& Bounds)
{
	const FVector Extents = Bounds.GetExtent();
	const float Size = Extents.GetAbsMin();
	check(SizeSpecificData.Num());
	int32 UseIdx = 0;
	float PreSize = FLT_MAX;
	for (int32 Idx = SizeSpecificData.Num() - 1; Idx >=0 ; --Idx)
	{
		ensureMsgf(PreSize >= SizeSpecificData[Idx].MaxSize, TEXT("SizeSpecificData is not sorted"));
		PreSize = SizeSpecificData[Idx].MaxSize;
		if (Size < SizeSpecificData[Idx].MaxSize)
			UseIdx = Idx;
		else
			break;
	}
	return UseIdx;
}

/** 
	NOTE - Making any changes to data stored on the rest collection below MUST be accompanied
	by a rotation of the DDC key in FDerivedDataGeometryCollectionCooker::GetVersionString
*/
void FGeometryCollectionPhysicsProxy::InitializeSharedCollisionStructures(
	Chaos::FErrorReporter& ErrorReporter,
	FGeometryCollection& RestCollection,
	const FSharedSimulationParameters& SharedParams)
{
	FString BaseErrorPrefix = ErrorReporter.GetPrefix();

	// fracture tools can create an empty GC before appending new geometry
	if (RestCollection.NumElements(FGeometryCollection::GeometryGroup) == 0)
	{
		return;
	}

	// clamps
	const float MinBoundsExtents = SharedParams.MinimumBoundingExtentClamp;
	const float MaxBoundsExtents = SharedParams.MaximumBoundingExtentClamp;
	const float MinVolume = SharedParams.MinimumVolumeClamp();
	const float MaxVolume = SharedParams.MaximumVolumeClamp();
	const float MinMass = FMath::Max(SMALL_NUMBER, SharedParams.MaximumMassClamp);
	const float MaxMass = SharedParams.MinimumMassClamp;


	//TArray<TArray<TArray<int32>>> BoundaryVertexIndices;
	//GeometryCollectionAlgo::FindOpenBoundaries(&RestCollection, 1e-2, BoundaryVertexIndices);
	//GeometryCollectionAlgo::TriangulateBoundaries(&RestCollection, BoundaryVertexIndices);
	//RestCollection.ReindexMaterials();

	using namespace Chaos;

	// TransformGroup
	const TManagedArray<int32>& BoneMap = RestCollection.BoneMap;
	const TManagedArray<int32>& Parent = RestCollection.Parent;
	const TManagedArray<TSet<int32>>& Children = RestCollection.Children;
	const TManagedArray<int32>& SimulationType = RestCollection.SimulationType;
	TManagedArray<bool>& CollectionSimulatableParticles =
		RestCollection.GetAttribute<bool>(
			FGeometryCollection::SimulatableParticlesAttribute, FTransformCollection::TransformGroup);
	TManagedArray<FVector>& CollectionInertiaTensor =
		RestCollection.AddAttribute<FVector>(
			TEXT("InertiaTensor"), FTransformCollection::TransformGroup);
	TManagedArray<float>& CollectionMass =
		RestCollection.AddAttribute<float>(
			TEXT("Mass"), FTransformCollection::TransformGroup);
	TManagedArray<TUniquePtr<FSimplicial>>& CollectionSimplicials =
		RestCollection.AddAttribute<TUniquePtr<FSimplicial>>(
			FGeometryDynamicCollection::SimplicialsAttribute, FTransformCollection::TransformGroup);

	RestCollection.RemoveAttribute(
		FGeometryDynamicCollection::ImplicitsAttribute, FTransformCollection::TransformGroup);
	TManagedArray<FGeometryDynamicCollection::FSharedImplicit>& CollectionImplicits =
		RestCollection.AddAttribute<FGeometryDynamicCollection::FSharedImplicit>(
			FGeometryDynamicCollection::ImplicitsAttribute, FTransformCollection::TransformGroup);

	// @todo(chaos_transforms) : do we still use this?
	TManagedArray<FTransform>& CollectionMassToLocal =
		RestCollection.AddAttribute<FTransform>(
			TEXT("MassToLocal"), FTransformCollection::TransformGroup);
	FTransform IdentityXf(FQuat::Identity, FVector(0));
	IdentityXf.NormalizeRotation();
	CollectionMassToLocal.Fill(IdentityXf);

	// VerticesGroup
	const TManagedArray<FVector>& Vertex = RestCollection.Vertex;

	// FacesGroup
	const TManagedArray<bool>& Visible = RestCollection.Visible;
	const TManagedArray<FIntVector>& Indices = RestCollection.Indices;

	// GeometryGroup
	const TManagedArray<int32>& TransformIndex = RestCollection.TransformIndex;
	const TManagedArray<FBox>& BoundingBox = RestCollection.BoundingBox;
	TManagedArray<float>& InnerRadius = RestCollection.InnerRadius;
	TManagedArray<float>& OuterRadius = RestCollection.OuterRadius;
	const TManagedArray<int32>& VertexStart = RestCollection.VertexStart;
	const TManagedArray<int32>& VertexCount = RestCollection.VertexCount;
	const TManagedArray<int32>& FaceStart = RestCollection.FaceStart;
	const TManagedArray<int32>& FaceCount = RestCollection.FaceCount;


	TArray<FTransform> CollectionSpaceTransforms;
	{ // tmp scope
		const TManagedArray<FTransform>& HierarchyTransform = RestCollection.Transform;
		GeometryCollectionAlgo::GlobalMatrices(HierarchyTransform, Parent, CollectionSpaceTransforms);
	} // tmp scope

	const int32 NumTransforms = CollectionSpaceTransforms.Num();
	const int32 NumGeometries = RestCollection.NumElements(FGeometryCollection::GeometryGroup);

	TArray<TUniquePtr<FTriangleMesh>> TriangleMeshesArray;	//use to union trimeshes in cluster case
	TriangleMeshesArray.AddDefaulted(NumTransforms);

	FParticles MassSpaceParticles;
	MassSpaceParticles.AddParticles(Vertex.Num());
	for (int32 Idx = 0; Idx < Vertex.Num(); ++Idx)
	{
		MassSpaceParticles.X(Idx) = Vertex[Idx];	//mass space computation done later down
	}

	TArray<FMassProperties> MassPropertiesArray;
	MassPropertiesArray.AddUninitialized(NumGeometries);

	TArray<bool> InertiaComputationNeeded;
	InertiaComputationNeeded.Init(false, NumGeometries);

	float TotalVolume = 0.f;
	// The geometry group has a set of transform indices that maps a geometry index
	// to a transform index, but only in the case where there is a 1-to-1 mapping 
	// between the two.  In the event where a geometry is instanced for multiple
	// transforms, the transform index on the geometry group should be INDEX_NONE.
	// Otherwise, iterating over the geometry group is a convenient way to iterate
	// over all the leaves of the hierarchy.
	check(!TransformIndex.Contains(INDEX_NONE)); // TODO: implement support for instanced bodies
	for (int32 GeometryIndex = 0; GeometryIndex < NumGeometries; GeometryIndex++)
	{
		const int32 TransformGroupIndex = TransformIndex[GeometryIndex];
		if (SimulationType[TransformGroupIndex] > FGeometryCollection::ESimulationTypes::FST_None)
		{
			TUniquePtr<FTriangleMesh> TriMesh(
				CreateTriangleMesh(
					FaceStart[GeometryIndex],
					FaceCount[GeometryIndex],
					Visible,
					Indices));

			FMassProperties& MassProperties = MassPropertiesArray[GeometryIndex];

			{
				MassProperties.CenterOfMass = FVector::ZeroVector;
				MassProperties.RotationOfMass = FRotation3(FQuat::Identity).GetNormalized();
				MassProperties.Volume = 0.f;
				MassProperties.InertiaTensor = FMatrix33(1,1,1);
				MassProperties.Mass = 1.0f; // start with unit mass, scaled later by density

				if (!ensureMsgf(BoundingBox[GeometryIndex].GetExtent().GetAbsMin() > MinVolume, TEXT("Geometry too small to simulate. Idx (%d)"), GeometryIndex))
				{
					CollectionSimulatableParticles[TransformGroupIndex] = false;	//do not simulate tiny particles
					MassProperties.Mass = 0.f;
					MassProperties.InertiaTensor = FMatrix33(0,0,0);
				}
				else
				{
					CalculateVolumeAndCenterOfMass(MassSpaceParticles, TriMesh->GetElements(), MassProperties.Volume, MassProperties.CenterOfMass);
					InertiaComputationNeeded[GeometryIndex] = true;

					if(MassProperties.Volume == 0)
					{
						FVector Extents = BoundingBox[GeometryIndex].GetExtent();
						MassProperties.Volume = Extents.X * Extents.Y * Extents.Z;
						float ExtentsYZ = Extents.Y * Extents.Y + Extents.Z * Extents.Z;
						float ExtentsXZ = Extents.X * Extents.X + Extents.Z * Extents.Z;
						float ExtentsXY = Extents.X * Extents.X + Extents.Y * Extents.Y;
						MassProperties.InertiaTensor = PMatrix<float, 3, 3>(ExtentsYZ / 12., ExtentsXZ / 12., ExtentsXY / 12.);
						MassProperties.CenterOfMass = BoundingBox[GeometryIndex].GetCenter();
						InertiaComputationNeeded[GeometryIndex] = false;
					}

					if (MassProperties.Volume < MinVolume)
					{
						// For rigid bodies outside of range just defaut to a clamped bounding box, and warn the user.
						MassProperties.Volume = MinVolume;
						CollectionMassToLocal[TransformGroupIndex] = FTransform(FQuat::Identity, BoundingBox[GeometryIndex].GetCenter());
						InertiaComputationNeeded[GeometryIndex] = false;
					}
					else if (MaxVolume < MassProperties.Volume)
					{
						// For rigid bodies outside of range just defaut to a clamped bounding box, and warn the user
						MassProperties.Volume = MaxVolume;
						CollectionMassToLocal[TransformGroupIndex] = FTransform(FQuat::Identity, BoundingBox[GeometryIndex].GetCenter());
						InertiaComputationNeeded[GeometryIndex] = false;
					}
					else
					{
						CollectionMassToLocal[TransformGroupIndex] = FTransform(FQuat::Identity, MassProperties.CenterOfMass);
					}

					FVector MassTranslation = CollectionMassToLocal[TransformGroupIndex].GetTranslation();
					if (!FMath::IsNearlyZero(MassTranslation.SizeSquared()))
					{
						const int32 IdxStart = VertexStart[GeometryIndex];
						const int32 IdxEnd = IdxStart + VertexCount[GeometryIndex];
						for (int32 Idx = IdxStart; Idx < IdxEnd; ++Idx)
						{
							MassSpaceParticles.X(Idx) -= MassTranslation;
						}
					}
				}
			}

			if (InnerRadius[GeometryIndex] == 0.0f || OuterRadius[GeometryIndex] == 0.0f)
			{
				const int32 VCount = VertexCount[GeometryIndex];
				if (VCount != 0)
				{
					const FVector Center = BoundingBox[GeometryIndex].GetCenter();
					const int32 VStart = VertexStart[GeometryIndex];

					InnerRadius[GeometryIndex] = VCount ? TNumericLimits<float>::Max() : 0.0f;
					OuterRadius[GeometryIndex] = 0.0f;
					for (int32 VIdx = 0; VIdx < VCount; ++VIdx)
					{
						const int32 PtIdx = VStart + VIdx;
						const FVector& Pt = Vertex[PtIdx];
						const float DistSq = FVector::DistSquared(Pt, Center);
						if (InnerRadius[GeometryIndex] > DistSq)
						{
							InnerRadius[GeometryIndex] = DistSq;
						}
						if (OuterRadius[GeometryIndex] < DistSq)
						{
							OuterRadius[GeometryIndex] = DistSq;
						}
					}
					InnerRadius[GeometryIndex] = FMath::Sqrt(InnerRadius[GeometryIndex]);
					OuterRadius[GeometryIndex] = FMath::Sqrt(OuterRadius[GeometryIndex]);
				}
			}

			TotalVolume += MassProperties.Volume;
			TriangleMeshesArray[TransformGroupIndex] = MoveTemp(TriMesh);
		}
		else
		{
			CollectionSimulatableParticles[TransformGroupIndex] = false;
		}
	}

	//User provides us with total mass or density.
	//Density must be the same for individual parts and the total. Density_i = Density = Mass_i / Volume_i
	//Total mass must equal sum of individual parts. Mass_i = TotalMass * Volume_i / TotalVolume => Density_i = TotalMass / TotalVolume
	TotalVolume = FMath::Max(TotalVolume, MinBoundsExtents * MinBoundsExtents * MinBoundsExtents);
	const float DesiredTotalMass = SharedParams.bMassAsDensity ? SharedParams.Mass * TotalVolume : SharedParams.Mass;
	const float ClampedTotalMass = FMath::Clamp(DesiredTotalMass, SharedParams.MinimumMassClamp, SharedParams.MaximumMassClamp);
	const float DesiredDensity = ClampedTotalMass / TotalVolume;

	FVec3 MaxChildBounds(1);
	ParallelFor(NumGeometries, [&](int32 GeometryIndex)
	//for (int32 GeometryIndex = 0; GeometryIndex < NumGeometries; GeometryIndex++)
	{
		// Need a new error reporter for parallel for loop here as it wouldn't be thread-safe to write to the prefix
		Chaos::FErrorReporter LocalErrorReporter;
		const int32 TransformGroupIndex = TransformIndex[GeometryIndex];

		const float Volume_i = MassPropertiesArray[GeometryIndex].Volume;
		if (CollectionSimulatableParticles[TransformGroupIndex])
		{
			//Must clamp each individual mass regardless of desired density
			if (DesiredDensity * Volume_i > SharedParams.MaximumMassClamp)
			{
				// For rigid bodies outside of range just defaut to a clamped bounding box, and warn the user.
				LocalErrorReporter.ReportError(*FString::Printf(TEXT("Geometry has invalid mass (too large)")));
				LocalErrorReporter.HandleLatestError();

				CollectionSimulatableParticles[TransformGroupIndex] = false;
			}
		}

		if (CollectionSimulatableParticles[TransformGroupIndex])
		{
			TUniquePtr<FTriangleMesh>& TriMesh = TriangleMeshesArray[TransformGroupIndex];
			FMassProperties& MassProperties = MassPropertiesArray[GeometryIndex];

			const float Mass_i = FMath::Max(DesiredDensity * Volume_i, SharedParams.MinimumMassClamp);
			const float Density_i = Mass_i / Volume_i;
			CollectionMass[TransformGroupIndex] = Mass_i;

			if (InertiaComputationNeeded[GeometryIndex])
			{
				// Note: particles already in CoM space, so passing in zero as CoM
				CalculateInertiaAndRotationOfMass(MassSpaceParticles, TriMesh->GetSurfaceElements(), Density_i, FVec3(0), MassProperties.InertiaTensor, MassProperties.RotationOfMass);
				CollectionInertiaTensor[TransformGroupIndex] = FVec3(MassProperties.InertiaTensor.M[0][0], MassProperties.InertiaTensor.M[1][1], MassProperties.InertiaTensor.M[2][2]);
#if false
				CollectionMassToLocal[TransformGroupIndex] = FTransform(MassProperties.RotationOfMass, MassProperties.CenterOfMass);


				if (!MassProperties.RotationOfMass.Equals(FQuat::Identity))
				{
					FTransform InverseMassRotation = FTransform(MassProperties.RotationOfMass.Inverse());
					const int32 IdxStart = VertexStart[GeometryIndex];
					const int32 IdxEnd = IdxStart + VertexCount[GeometryIndex];
					for (int32 Idx = IdxStart; Idx < IdxEnd; ++Idx)
					{
						MassSpaceParticles.X(Idx) = InverseMassRotation.TransformPosition(MassSpaceParticles.X(Idx));
					}
				}
#endif
			}
			else
			{
				const FVec3 DiagonalInertia(MassProperties.InertiaTensor.M[0][0], MassProperties.InertiaTensor.M[1][1], MassProperties.InertiaTensor.M[2][2]);
				CollectionInertiaTensor[TransformGroupIndex] = DiagonalInertia * Mass_i;
			}

			FBox InstanceBoundingBox(EForceInit::ForceInitToZero);
			if (TriMesh->GetElements().Num())
			{
				const TSet<int32> MeshVertices = TriMesh->GetVertices();
				for (const int32 Idx : MeshVertices)
				{
					InstanceBoundingBox += MassSpaceParticles.X(Idx);
				}
			}
			else if(VertexCount[GeometryIndex])
			{
				const int32 IdxStart = VertexStart[GeometryIndex];
				const int32 IdxEnd = IdxStart + VertexCount[GeometryIndex];
				for (int32 Idx = IdxStart; Idx < IdxEnd; ++Idx)
				{
					InstanceBoundingBox += MassSpaceParticles.X(Idx);
				}
			}
			else
			{
				InstanceBoundingBox = FBox(MassProperties.CenterOfMass, MassProperties.CenterOfMass);
			}

			const int32 SizeSpecificIdx = FindSizeSpecificIdx(SharedParams.SizeSpecificData, InstanceBoundingBox);
			const FSharedSimulationSizeSpecificData& SizeSpecificData = SharedParams.SizeSpecificData[SizeSpecificIdx];

			//
			//  Build the simplicial for the rest collection. This will be used later in the DynamicCollection to 
			//  populate the collision structures of the simulation. 
			//
			if (ensureMsgf(TriMesh, TEXT("No Triangle representation")))
			{
				Chaos::FBVHParticles* Simplicial =
					FCollisionStructureManager::NewSimplicial(
						MassSpaceParticles,
						BoneMap,
						SizeSpecificData.CollisionType,
						*TriMesh,
						SizeSpecificData.CollisionParticlesFraction);
				CollectionSimplicials[TransformGroupIndex] = TUniquePtr<FSimplicial>(Simplicial); // CollectionSimplicials is in the TransformGroup
				//ensureMsgf(CollectionSimplicials[TransformGroupIndex], TEXT("No simplicial representation."));
				if (!CollectionSimplicials[TransformGroupIndex]->Size())
				{
					ensureMsgf(false, TEXT("Simplicial is empty."));
				}

				if (SizeSpecificData.ImplicitType == EImplicitTypeEnum::Chaos_Implicit_LevelSet)
				{
					LocalErrorReporter.SetPrefix(BaseErrorPrefix + " | Transform Index: " + FString::FromInt(TransformGroupIndex) + " of " + FString::FromInt(TransformIndex.Num()));
					CollectionImplicits[TransformGroupIndex] = FGeometryDynamicCollection::FSharedImplicit(
						FCollisionStructureManager::NewImplicitLevelset(
							LocalErrorReporter,
							MassSpaceParticles,
							*TriMesh,
							InstanceBoundingBox,
							SizeSpecificData.MinLevelSetResolution,
							SizeSpecificData.MaxLevelSetResolution,
							SizeSpecificData.CollisionObjectReductionPercentage,
							SizeSpecificData.CollisionType));
					// Fall back on sphere if level set rasterization failed.
					if (!CollectionImplicits[TransformGroupIndex])
					{
						CollectionImplicits[TransformGroupIndex] = FGeometryDynamicCollection::FSharedImplicit(
							FCollisionStructureManager::NewImplicitSphere(
								InnerRadius[GeometryIndex],
								SizeSpecificData.CollisionObjectReductionPercentage,
								SizeSpecificData.CollisionType));
					}
				}
				else if (SizeSpecificData.ImplicitType == EImplicitTypeEnum::Chaos_Implicit_Box)
				{
					CollectionImplicits[TransformGroupIndex] = FGeometryDynamicCollection::FSharedImplicit(
						FCollisionStructureManager::NewImplicitBox(
							InstanceBoundingBox,
							SizeSpecificData.CollisionObjectReductionPercentage,
							SizeSpecificData.CollisionType));
				}
				else if (SizeSpecificData.ImplicitType == EImplicitTypeEnum::Chaos_Implicit_Sphere)
				{
					CollectionImplicits[TransformGroupIndex] = FGeometryDynamicCollection::FSharedImplicit(
						FCollisionStructureManager::NewImplicitSphere(
							InnerRadius[GeometryIndex],
							SizeSpecificData.CollisionObjectReductionPercentage,
							SizeSpecificData.CollisionType));
				}
				else if (SizeSpecificData.ImplicitType == EImplicitTypeEnum::Chaos_Implicit_None)
				{
					CollectionImplicits[TransformGroupIndex] = nullptr;
				}
				else
				{
					ensure(false); // unsupported implicit type!
				}

				if (CollectionImplicits[TransformGroupIndex] && CollectionImplicits[TransformGroupIndex]->HasBoundingBox())
				{
					const auto Implicit = CollectionImplicits[TransformGroupIndex];
					const auto BBox = Implicit->BoundingBox();
					const FVec3 Extents = BBox.Extents(); // Chaos::FAABB3::Extents() is Max - Min
					MaxChildBounds = MaxChildBounds.ComponentwiseMax(Extents);
				}
			}
		}
	});

	// question: at the moment we always build cluster data in the asset. This 
	// allows for per instance toggling. Is this needed? It increases memory 
	// usage for all geometry collection assets.
	const bool bEnableClustering = true;	
	if (bEnableClustering)
	{
		//Put all children into collection space so we can compute mass properties.
		TUniquePtr<TPBDRigidClusteredParticles<float, 3>> CollectionSpaceParticles(new TPBDRigidClusteredParticles<float, 3>());
		CollectionSpaceParticles->AddParticles(NumTransforms);

		// Init to -FLT_MAX for debugging purposes
		for (int32 Idx = 0; Idx < NumTransforms; Idx++)
		{
			CollectionSpaceParticles->X(Idx) = Chaos::FVec3(-TNumericLimits<float>::Max());
		}

		//
		// TODO: We generate particles & handles for leaf nodes so that we can use some 
		// runtime clustering functions.  That's adding a lot of work and dependencies
		// just so we can make an API happy.  We should refactor the common routines
		// to have a handle agnostic implementation.
		//

		TMap<const TGeometryParticleHandle<float, 3>*, int32> HandleToTransformIdx;
		TArray<TUniquePtr<TPBDRigidClusteredParticleHandle<float, 3>>> Handles;
		Handles.Reserve(NumTransforms);
		for (int32 Idx = 0; Idx < NumTransforms; Idx++)
		{
			Handles.Add(TPBDRigidClusteredParticleHandle<float, 3>::CreateParticleHandle(
				MakeSerializable(CollectionSpaceParticles), Idx, Idx));
			HandleToTransformIdx.Add(Handles[Handles.Num() - 1].Get(), Idx);
		}

		// We use PopulateSimulatedParticle here just to give us some valid particles to operate on - with correct
		// position, mass and inertia so we can accumulate data for clusters just below.
 		for (int32 GeometryIdx = 0; GeometryIdx < NumGeometries; ++GeometryIdx)
 		{
 			const int32 TransformGroupIndex = TransformIndex[GeometryIdx];

 			if (CollectionSimulatableParticles[TransformGroupIndex])
 			{
				FTransform GeometryWorldTransform = CollectionSpaceTransforms[TransformGroupIndex] * CollectionMassToLocal[TransformGroupIndex];

 				PopulateSimulatedParticle(
 					Handles[TransformGroupIndex].Get(),
 					SharedParams, 
 					CollectionSimplicials[TransformGroupIndex].Get(),
 					CollectionImplicits[TransformGroupIndex],
 					FCollisionFilterData(),		// SimFilter
 					FCollisionFilterData(),		// QueryFilter
 					CollectionMass[TransformGroupIndex],
 					CollectionInertiaTensor[TransformGroupIndex], 
					GeometryWorldTransform,
 					(uint8)EObjectStateTypeEnum::Chaos_Object_Dynamic, 
 					INDEX_NONE); // CollisionGroup
 			}
 		}

		const TArray<int32> RecursiveOrder = ComputeRecursiveOrder(RestCollection);
		const TArray<int32> TransformToGeometry = ComputeTransformToGeometryMap(RestCollection);

		TArray<bool> IsClusterSimulated;
		IsClusterSimulated.Init(false, CollectionSpaceParticles->Size());
		//build collision structures depth first
		for (const int32 TransformGroupIndex : RecursiveOrder)
		{
			if (RestCollection.IsClustered(TransformGroupIndex))
			{
				const int32 ClusterTransformIdx = TransformGroupIndex;
				//update mass 
				TSet<TPBDRigidParticleHandle<float,3>*> ChildrenIndices;
				{ // tmp scope
					ChildrenIndices.Reserve(Children[ClusterTransformIdx].Num());
					for (int32 ChildIdx : Children[ClusterTransformIdx])
					{
						if (CollectionSimulatableParticles[ChildIdx] || IsClusterSimulated[ChildIdx])
						{
							ChildrenIndices.Add(Handles[ChildIdx].Get());
						}
					}
					if (!ChildrenIndices.Num())
					{
						continue;
					}
				} // tmp scope

				//CollectionSimulatableParticles[TransformGroupIndex] = true;
				IsClusterSimulated[TransformGroupIndex] = true;

				UpdateClusterMassProperties(Handles[ClusterTransformIdx].Get(), ChildrenIndices);	//compute mass properties
				const FTransform ClusterMassToCollection = 
					FTransform(CollectionSpaceParticles->R(ClusterTransformIdx), 
							   CollectionSpaceParticles->X(ClusterTransformIdx));

				CollectionMassToLocal[ClusterTransformIdx] = 
					ClusterMassToCollection.GetRelativeTransform(
						CollectionSpaceTransforms[ClusterTransformIdx]);

				//update geometry
				//merge children meshes and move them into cluster's mass space
				TArray<TVector<int32, 3>> UnionMeshIndices;
				int32 BiggestNumElements = 0;
				{ // tmp scope
					int32 NumChildIndices = 0;
					for (TPBDRigidParticleHandle<float, 3>* Child : ChildrenIndices)
					{
						const int32 ChildTransformIdx = HandleToTransformIdx[Child];
						if (Chaos::FTriangleMesh* ChildMesh = TriangleMeshesArray[ChildTransformIdx].Get())
						{
							BiggestNumElements = FMath::Max(BiggestNumElements, ChildMesh->GetNumElements());
							NumChildIndices += ChildMesh->GetNumElements();
						}
					}
					UnionMeshIndices.Reserve(NumChildIndices);
				} // tmp scope

				FBox InstanceBoundingBox(EForceInit::ForceInitToZero);
				{ // tmp scope
					TSet<int32> VertsAdded;
					VertsAdded.Reserve(BiggestNumElements);
					for (TPBDRigidParticleHandle<float, 3>* Child : ChildrenIndices)
					{
						const int32 ChildTransformIdx = HandleToTransformIdx[Child];
						if (Chaos::FTriangleMesh* ChildMesh = TriangleMeshesArray[ChildTransformIdx].Get())
						{
							const TArray<TVector<int32, 3>>& ChildIndices = ChildMesh->GetSurfaceElements();
							UnionMeshIndices.Append(ChildIndices);

							// To move a particle from mass-space in the child to mass-space in the cluster parent, calculate
							// the relative transform between the mass-space origin for both the parent and child before
							// transforming the mass space particles into the parent mass-space.
							const FTransform ChildMassToClusterMass = (CollectionSpaceTransforms[ChildTransformIdx] * CollectionMassToLocal[ChildTransformIdx]).GetRelativeTransform(CollectionSpaceTransforms[ClusterTransformIdx] * CollectionMassToLocal[ClusterTransformIdx]);

							ChildMesh->GetVertexSet(VertsAdded);
							for (const int32 VertIdx : VertsAdded)
							{
								//Update particles so they are in the cluster's mass space
								MassSpaceParticles.X(VertIdx) =
									ChildMassToClusterMass.TransformPosition(MassSpaceParticles.X(VertIdx));
								InstanceBoundingBox += MassSpaceParticles.X(VertIdx);
							}
						}
					}
				} // tmp scope

				TUniquePtr<FTriangleMesh> UnionMesh(new FTriangleMesh(MoveTemp(UnionMeshIndices)));
				const FMatrix& InertiaMatrix = CollectionSpaceParticles->I(ClusterTransformIdx);
				const FVector InertiaDiagonal(InertiaMatrix.M[0][0], InertiaMatrix.M[1][1], InertiaMatrix.M[2][2]);
				CollectionInertiaTensor[ClusterTransformIdx] = InertiaDiagonal;
				CollectionMass[ClusterTransformIdx] = CollectionSpaceParticles->M(ClusterTransformIdx);

				const int32 SizeSpecificIdx = FindSizeSpecificIdx(SharedParams.SizeSpecificData, InstanceBoundingBox);
				const FSharedSimulationSizeSpecificData& SizeSpecificData = SharedParams.SizeSpecificData[SizeSpecificIdx];

				if (SizeSpecificData.ImplicitType == EImplicitTypeEnum::Chaos_Implicit_LevelSet)
				{
					const FVec3 Scale = 2 * InstanceBoundingBox.GetExtent() / MaxChildBounds; // FBox's extents are 1/2 (Max - Min)
					const float ScaleMax = Scale.GetAbsMax();
					const float ScaleMin = Scale.GetAbsMin();

					float MinResolution = ScaleMin * SizeSpecificData.MinLevelSetResolution;
					MinResolution = FMath::Clamp<float>(MinResolution, SizeSpecificData.MinLevelSetResolution, SizeSpecificData.MinClusterLevelSetResolution);

					float MaxResolution = ScaleMax * SizeSpecificData.MaxLevelSetResolution;
					MaxResolution = FMath::Clamp<float>(MaxResolution, SizeSpecificData.MaxLevelSetResolution, SizeSpecificData.MaxClusterLevelSetResolution);

					//don't support non level-set serialization
					ErrorReporter.SetPrefix(BaseErrorPrefix + " | Cluster Transform Index: " + FString::FromInt(ClusterTransformIdx));
					CollectionImplicits[ClusterTransformIdx] = FGeometryDynamicCollection::FSharedImplicit(
						FCollisionStructureManager::NewImplicitLevelset(
							ErrorReporter,
							MassSpaceParticles,
							*UnionMesh,
							InstanceBoundingBox,
							MinResolution,
							MaxResolution,
							SizeSpecificData.CollisionObjectReductionPercentage,
							SizeSpecificData.CollisionType));
					// Fall back on sphere if level set rasterization failed.
					if (!CollectionImplicits[ClusterTransformIdx])
					{
						CollectionImplicits[ClusterTransformIdx] = FGeometryDynamicCollection::FSharedImplicit(
							FCollisionStructureManager::NewImplicitSphere(
								InstanceBoundingBox.GetExtent().GetAbsMin(), // FBox's extents are 1/2 (Max - Min)
								SizeSpecificData.CollisionObjectReductionPercentage,
								SizeSpecificData.CollisionType));
					}

					CollectionSimplicials[ClusterTransformIdx] = TUniquePtr<FSimplicial>(
						FCollisionStructureManager::NewSimplicial(MassSpaceParticles, *UnionMesh, CollectionImplicits[ClusterTransformIdx].Get(),
						SharedParams.MaximumCollisionParticleCount));
				}
				else if (SizeSpecificData.ImplicitType == EImplicitTypeEnum::Chaos_Implicit_Box)
				{
					CollectionImplicits[ClusterTransformIdx] = FGeometryDynamicCollection::FSharedImplicit(
						FCollisionStructureManager::NewImplicitBox(
							InstanceBoundingBox,
							SizeSpecificData.CollisionObjectReductionPercentage,
							SizeSpecificData.CollisionType));

					CollectionSimplicials[ClusterTransformIdx] = TUniquePtr<FSimplicial>(
						FCollisionStructureManager::NewSimplicial(MassSpaceParticles, *UnionMesh, CollectionImplicits[ClusterTransformIdx].Get(),
						SharedParams.MaximumCollisionParticleCount));
				}
				else if (SizeSpecificData.ImplicitType == EImplicitTypeEnum::Chaos_Implicit_Sphere)
				{
 					CollectionImplicits[ClusterTransformIdx] = FGeometryDynamicCollection::FSharedImplicit(
						FCollisionStructureManager::NewImplicitSphere(
							InstanceBoundingBox.GetExtent().GetAbsMin(), // FBox's extents are 1/2 (Max - Min)
							SizeSpecificData.CollisionObjectReductionPercentage,
							SizeSpecificData.CollisionType));

					CollectionSimplicials[ClusterTransformIdx] = TUniquePtr<FSimplicial>(
						FCollisionStructureManager::NewSimplicial(MassSpaceParticles, *UnionMesh, CollectionImplicits[ClusterTransformIdx].Get(),
						SharedParams.MaximumCollisionParticleCount));
				}
				else if(SizeSpecificData.ImplicitType == EImplicitTypeEnum::Chaos_Implicit_Capsule)
				{
					ensure(false); // unsupported implicit type
					CollectionImplicits[ClusterTransformIdx].Reset();
					CollectionSimplicials[ClusterTransformIdx].Reset();
				}
				else // Assume it's a union???
				{
					CollectionImplicits[ClusterTransformIdx].Reset();	//union so just set as null
					CollectionSimplicials[ClusterTransformIdx].Reset();
				}

				TriangleMeshesArray[ClusterTransformIdx] = MoveTemp(UnionMesh);
			}
		}

		InitRemoveOnFracture(RestCollection, SharedParams);
	}
}

void FGeometryCollectionPhysicsProxy::InitRemoveOnFracture(FGeometryCollection& RestCollection, const FSharedSimulationParameters& SharedParams)
{
	if (SharedParams.RemoveOnFractureIndices.Num() == 0)
	{
		return;
	}

	// Markup Node Hierarchy Status with FS_RemoveOnFracture flags where geometry is ALL glass
	const int32 NumGeometries = RestCollection.NumElements(FGeometryCollection::GeometryGroup);
	for (int32 Idx = 0; Idx < NumGeometries; Idx++)
	{
		const int32 TransformIndex = RestCollection.TransformIndex[Idx];
		const int32 Start = RestCollection.FaceStart[Idx];
		const int32 End = RestCollection.FaceCount[Idx];
		bool IsToBeRemoved = true;
		for (int32 Face = Start; Face < Start + End; Face++)
		{
			bool FoundMatch = false;
			for (int32 MaterialIndex : SharedParams.RemoveOnFractureIndices)
			{
				if (RestCollection.MaterialID[Face] == MaterialIndex)
				{
					FoundMatch = true;
					break;
				}
			}
			if (!FoundMatch)
			{
				IsToBeRemoved = false;
				break;
			}
		}
		if (IsToBeRemoved)
		{
			RestCollection.SetFlags(TransformIndex, FGeometryCollection::FS_RemoveOnFracture);
		}
		else
		{
			RestCollection.ClearFlags(TransformIndex, FGeometryCollection::FS_RemoveOnFracture);
		}
	}
}

void IdentifySimulatableElements(Chaos::FErrorReporter& ErrorReporter, FGeometryCollection& GeometryCollection)
{
	// Determine which collection particles to simulate

	// Geometry group
	const TManagedArray<int32>& TransformIndex = GeometryCollection.TransformIndex;
	const TManagedArray<FBox>& BoundingBox = GeometryCollection.BoundingBox;
	const TManagedArray<int32>& VertexCount = GeometryCollection.VertexCount;

	const int32 NumTransforms = GeometryCollection.NumElements(FGeometryCollection::TransformGroup);
	const int32 NumTransformMappings = TransformIndex.Num();

	// Faces group
	const TManagedArray<FIntVector>& Indices = GeometryCollection.Indices;
	const TManagedArray<bool>& Visible = GeometryCollection.Visible;
	// Vertices group
	const TManagedArray<int32>& BoneMap = GeometryCollection.BoneMap;

	// Do not simulate hidden geometry
	TArray<bool> HiddenObject;
	HiddenObject.Init(true, NumTransforms);
	int32 PrevObject = INDEX_NONE;
	bool bContiguous = true;
	for(int32 i = 0; i < Indices.Num(); i++)
	{
		if(Visible[i]) // Face index i is visible
		{
			const int32 ObjIdx = BoneMap[Indices[i][0]]; // Look up associated bone to the faces X coord.
			HiddenObject[ObjIdx] = false;

			if (!ensure(ObjIdx >= PrevObject))
			{
				bContiguous = false;
			}

			PrevObject = ObjIdx;
		}
	}

	if (!bContiguous)
	{
		// What assumptions???  How are we ever going to know if this is still the case?
		ErrorReporter.ReportError(TEXT("Objects are not contiguous. This breaks assumptions later in the pipeline"));
		ErrorReporter.HandleLatestError();
	}

	//For now all simulation data is a non compiled attribute. Not clear what we want for simulated vs kinematic collections
	TManagedArray<bool>& SimulatableParticles = 
		GeometryCollection.AddAttribute<bool>(
			FGeometryCollection::SimulatableParticlesAttribute, FTransformCollection::TransformGroup);
	SimulatableParticles.Fill(false);

	for(int i = 0; i < NumTransformMappings; i++)
	{
		int32 Tdx = TransformIndex[i];
		checkSlow(0 <= Tdx && Tdx < NumTransforms);
		if (GeometryCollection.IsGeometry(Tdx) && // checks that TransformToGeometryIndex[Tdx] != INDEX_NONE
			VertexCount[i] &&					 // must have vertices to be simulated?
			0.f < BoundingBox[i].GetSize().SizeSquared() && // must have a non-zero bbox to be simulated?  No single point?
			!HiddenObject[Tdx])					 // must have 1 associated face
		{
			SimulatableParticles[Tdx] = true;
		}
	}
}

void BuildSimulationData(Chaos::FErrorReporter& ErrorReporter, FGeometryCollection& GeometryCollection, const FSharedSimulationParameters& SharedParams)
{
	IdentifySimulatableElements(ErrorReporter, GeometryCollection);
	FGeometryCollectionPhysicsProxy::InitializeSharedCollisionStructures(ErrorReporter, GeometryCollection, SharedParams);
}

//==============================================================================
// FIELDS
//==============================================================================

void FGeometryCollectionPhysicsProxy::FieldParameterUpdateCallback(Chaos::FPBDRigidsSolver* RigidSolver, const bool bUpdateViews)
{
	SCOPE_CYCLE_COUNTER(STAT_ParamUpdateField_Object);

	// We are updating the Collection from the InitializeBodiesPT, so we need the PT collection
	FGeometryDynamicCollection& Collection = PhysicsThreadCollection;
	Chaos::FPBDPositionConstraints PositionTarget;
	TMap<int32, int32> TargetedParticles;

	// Process Particle-Collection commands
	int32 NumCommands = Commands.Num();
	if (NumCommands && RigidSolver && !RigidSolver->IsShuttingDown() && Collection.Transform.Num())
	{
		TArray<int32> CommandsToRemove;
		CommandsToRemove.Reserve(NumCommands);

		TArray<Chaos::TGeometryParticleHandle<float, 3>*> ParticleHandles;
		TArray<FVector> SamplePoints;
		TArray<FFieldContextIndex> SampleIndices;

		EFieldResolutionType PrevResolutionType = EFieldResolutionType::Field_Resolution_Max;
		EFieldFilterType PrevFilterType = EFieldFilterType::Field_Filter_Max;

		for (int32 CommandIndex = 0; CommandIndex < NumCommands; CommandIndex++)
		{
			FFieldSystemCommand& FieldCommand = Commands[CommandIndex];
			if (Chaos::BuildFieldSamplePoints(this, RigidSolver, FieldCommand, ParticleHandles, SamplePoints, SampleIndices, PrevResolutionType, PrevFilterType))
			{
				const float TimeSeconds = RigidSolver->GetSolverTime() - FieldCommand.TimeCreation;

				TArrayView<FVector> SamplePointsView(&(SamplePoints[0]), SamplePoints.Num());
				TArrayView<FFieldContextIndex> SampleIndicesView(&(SampleIndices[0]), SampleIndices.Num());

				FFieldContext FieldContext(
					SampleIndicesView,
					SamplePointsView,
					FieldCommand.MetaData,
					TimeSeconds);

				const EFieldOutputType FieldOutput = GetFieldTargetOutput(GetFieldPhysicsType(FieldCommand.TargetAttribute));
				if ((FieldOutput == EFieldOutputType::Field_Output_Integer) && (FieldCommand.RootNode->Type() == FFieldNodeBase::EFieldType::EField_Int32))
				{
					if (FieldCommand.TargetAttribute == GetGeometryCollectionPhysicsTypeName(EGeometryCollectionPhysicsTypeEnum::Chaos_DynamicState))
					{
						TArray<int32> LocalResults; 
						LocalResults.AddUninitialized(ParticleHandles.Num());
						TArrayView<int32> ResultsView(&(LocalResults[0]), LocalResults.Num());

						SCOPE_CYCLE_COUNTER(STAT_ParamUpdateField_DynamicState);
						{
							bool bHasStateChanged = false;
							InitDynamicStateResults(ParticleHandles, FieldContext, LocalResults);

							static_cast<const FFieldNode<int32>*>(FieldCommand.RootNode.Get())->Evaluate(FieldContext, ResultsView);
							for (const FFieldContextIndex& Index : FieldContext.GetEvaluatedSamples())
							{
								Chaos::TPBDRigidParticleHandle<float, 3>* RigidHandle = ParticleHandles[Index.Sample]->CastToRigidParticle();
								if (RigidHandle)
								{
									const int8 ResultState = ResultsView[Index.Result];  
									const int32 TransformIndex = HandleToTransformGroupIndex[RigidHandle];

									// Update of the handles object state. No need to update 
									// the initial velocities since it is done after this function call in InitializeBodiesPT
									if (bUpdateViews && (Parameters.InitialVelocityType == EInitialVelocityTypeEnum::Chaos_Initial_Velocity_User_Defined))
									{
										bHasStateChanged |= ReportDynamicStateResult(RigidSolver, static_cast<Chaos::EObjectStateType>(ResultState), RigidHandle,
											true, Collection.InitialLinearVelocity[TransformIndex], true, Collection.InitialAngularVelocity[TransformIndex]);
									}
									else
									{
										bHasStateChanged |= ReportDynamicStateResult(RigidSolver, static_cast<Chaos::EObjectStateType>(ResultState), RigidHandle,
											false, Chaos::TVector<float, 3>(0), false, Chaos::TVector<float, 3>(0));
									}
									// Update of the Collection dynamic state. It will be used just after to set the initial velocity
									Collection.DynamicState[TransformIndex] = ResultState;
								}
							}
							if (bUpdateViews)
							{
								UpdateSolverParticlesState(RigidSolver, bHasStateChanged);
							}
						}
						CommandsToRemove.Add(CommandIndex);
					}
					else
					{
						Chaos::FieldIntegerParameterUpdate(RigidSolver, FieldCommand, ParticleHandles, FieldContext, CommandsToRemove, PositionTarget, TargetedParticles, CommandIndex);
					}
				}
				else if ((FieldOutput == EFieldOutputType::Field_Output_Vector) && (FieldCommand.RootNode->Type() == FFieldNodeBase::EFieldType::EField_FVector))
				{
					if (FieldCommand.TargetAttribute == GetGeometryCollectionPhysicsTypeName(EGeometryCollectionPhysicsTypeEnum::Chaos_InitialLinearVelocity))
					{
						if (Parameters.InitialVelocityType == EInitialVelocityTypeEnum::Chaos_Initial_Velocity_User_Defined)
						{
							TArray<FVector> LocalResults;
							LocalResults.AddUninitialized(ParticleHandles.Num());
							TArrayView<FVector> ResultsView(&(LocalResults[0]), LocalResults.Num());

							SCOPE_CYCLE_COUNTER(STAT_ParamUpdateField_LinearVelocity);
							{
								static_cast<const FFieldNode<FVector>*>(FieldCommand.RootNode.Get())->Evaluate(FieldContext, ResultsView);
								for (const FFieldContextIndex& Index : FieldContext.GetEvaluatedSamples())
								{
									Chaos::TPBDRigidParticleHandle<float, 3>* RigidHandle = ParticleHandles[Index.Sample]->CastToRigidParticle();
									if (RigidHandle)
									{
										Collection.InitialLinearVelocity[HandleToTransformGroupIndex[RigidHandle]] = ResultsView[Index.Result];
									}
								}
							}
							CommandsToRemove.Add(CommandIndex);
						}
						else
						{
							UE_LOG(LogChaos, Error, TEXT("Field based evaluation of the simulations 'InitialLinearVelocity' requires the geometry collection be set to User Defined Initial Velocity"));
						}
					}
					else if (FieldCommand.TargetAttribute == GetGeometryCollectionPhysicsTypeName(EGeometryCollectionPhysicsTypeEnum::Chaos_InitialAngularVelocity))
					{
						if (Parameters.InitialVelocityType == EInitialVelocityTypeEnum::Chaos_Initial_Velocity_User_Defined)
						{
							TArray<FVector> LocalResults;
							LocalResults.AddUninitialized(ParticleHandles.Num());
							TArrayView<FVector> ResultsView(&(LocalResults[0]), LocalResults.Num());

							SCOPE_CYCLE_COUNTER(STAT_ParamUpdateField_AngularVelocity);
							{
								static_cast<const FFieldNode<FVector>*>(FieldCommand.RootNode.Get())->Evaluate(FieldContext, ResultsView);
								for (const FFieldContextIndex& Index : FieldContext.GetEvaluatedSamples())
								{
									Chaos::TPBDRigidParticleHandle<float, 3>* RigidHandle = ParticleHandles[Index.Sample]->CastToRigidParticle();
									if (RigidHandle)
									{
										Collection.InitialAngularVelocity[HandleToTransformGroupIndex[RigidHandle]] = ResultsView[Index.Result];
									}
								}
							}
							CommandsToRemove.Add(CommandIndex);
						}
						else
						{
							UE_LOG(LogChaos, Error, TEXT("Field based evaluation of the simulations 'InitialAngularVelocity' requires the geometry collection be set to User Defined Initial Velocity"));
						}
					}
					else 
					{
						Chaos::FieldScalarParameterUpdate(RigidSolver, FieldCommand, ParticleHandles, FieldContext, CommandsToRemove, PositionTarget, TargetedParticles, CommandIndex);
					}
				}
			}
		}		
		
		for (int32 Index = CommandsToRemove.Num() - 1; Index >= 0; --Index)
		{
			Commands.RemoveAt(CommandsToRemove[Index]);
		}
	}
}

void FGeometryCollectionPhysicsProxy::FieldForcesUpdateCallback(Chaos::FPBDRigidsSolver* RigidSolver)
{
	SCOPE_CYCLE_COUNTER(STAT_ForceUpdateField_Object);

	const int32 NumCommands = Commands.Num();
	if (NumCommands && RigidSolver && !RigidSolver->IsShuttingDown())
	{
		TArray<int32> CommandsToRemove;
		CommandsToRemove.Reserve(NumCommands);

		TArray<Chaos::TGeometryParticleHandle<float, 3>*> ParticleHandles;
		TArray<FVector> SamplePoints;
		TArray<FFieldContextIndex> SampleIndices;

		EFieldResolutionType PrevResolutionType = EFieldResolutionType::Field_Resolution_Max;
		EFieldFilterType PrevFilterType = EFieldFilterType::Field_Filter_Max;

		for (int32 CommandIndex = 0; CommandIndex < NumCommands; CommandIndex++)
		{
			const FFieldSystemCommand& FieldCommand = Commands[CommandIndex];

			if (Chaos::BuildFieldSamplePoints(this, RigidSolver, FieldCommand, ParticleHandles, SamplePoints, SampleIndices, PrevResolutionType, PrevFilterType))
			{
				const float TimeSeconds = RigidSolver->GetSolverTime() - FieldCommand.TimeCreation;

				TArrayView<FVector> SamplePointsView(&(SamplePoints[0]), SamplePoints.Num());
				TArrayView<FFieldContextIndex> SampleIndicesView(&(SampleIndices[0]), SampleIndices.Num());

				FFieldContext FieldContext(
					SampleIndicesView,
					SamplePointsView,
					FieldCommand.MetaData,
					TimeSeconds);

				if (FieldCommand.RootNode->Type() == FFieldNode<FVector>::StaticType())
				{
					Chaos::FieldVectorForceUpdate(RigidSolver, FieldCommand, ParticleHandles, FieldContext, CommandsToRemove, CommandIndex);
				}
			}
		}
		for (int32 Index = CommandsToRemove.Num() - 1; Index >= 0; --Index)
		{
			Commands.RemoveAt(CommandsToRemove[Index]);
		}
	}
}
