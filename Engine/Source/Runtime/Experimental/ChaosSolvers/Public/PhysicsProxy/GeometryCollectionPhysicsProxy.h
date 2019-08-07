// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GeometryCollection/GeometryCollection.h"

#if INCLUDE_CHAOS

#include "Chaos/Framework/PhysicsProxy.h"

#include "GeometryCollection/ManagedArray.h"
#include "GeometryCollection/GeometryCollectionSimulationCoreTypes.h"
#include "GeometryCollection/GeometryCollectionCollisionStructureManager.h"
#include "Chaos/Framework/BufferedData.h"

struct FGeometryCollectionResults
{
	FGeometryCollectionResults();

	int32 BaseIndex;
	int32 NumParticlesAdded;
	TArray<bool> DisabledStates;
	TManagedArray<FTransform> Transforms;
	TArray<FMatrix> GlobalTransforms;
	TManagedArray<int32> RigidBodyIds;
	TArray<FTransform> ParticleToWorldTransforms;
	TManagedArray<int32> Level;
	TManagedArray<int32> Parent;
	TManagedArray<TSet<int32>> Children;
	TManagedArray<int32> SimulationType;
	TManagedArray<int32> StatusFlags;

	bool IsObjectDynamic;
	bool IsObjectLoading;

	FBoxSphereBounds WorldBounds;
};

namespace Chaos
{
template <typename T>
class TSerializablePtr;
class FErrorReporter;
}

class FStubGeometryCollectionData : public Chaos::FParticleData {
	void Reset() {};
};

class CHAOSSOLVERS_API FGeometryCollectionPhysicsProxy : public TPhysicsProxy<FGeometryCollectionPhysicsProxy, FStubGeometryCollectionData>
{
	typedef TPhysicsProxy<FGeometryCollectionPhysicsProxy, FStubGeometryCollectionData> Base;

public:
	// collection attributes
	static FName SimplicialsAttribute;
	static FName ImplicitsAttribute;

	typedef FCollisionStructureManager::FSimplicial FSimplicial;

	/** Proxy publics */
	using FInitFunc = TFunction<void(FSimulationParameters&)>;
	using FCacheSyncFunc = TFunction<void(const FGeometryCollectionResults&)>;
	using FFinalSyncFunc = TFunction<void(const FRecordedTransformTrack&)>;
	/** ----------------------- */

	FGeometryCollectionPhysicsProxy() = delete;
	FGeometryCollectionPhysicsProxy(UObject* InOwner, FGeometryDynamicCollection* InDynamicCollection, FInitFunc InInitFunc, FCacheSyncFunc InCacheSyncFunc, FFinalSyncFunc InFinalSyncFunc);
	virtual ~FGeometryCollectionPhysicsProxy();

	void Initialize();
	void Reset();

	/** Solver Object interface */
	bool IsSimulating() const;
	void UpdateKinematicBodiesCallback(const FParticlesType& InParticles, const float InDt, const float InTime, FKinematicProxy& InKinematicProxy);
	void StartFrameCallback(const float InDt, const float InTime);
	void EndFrameCallback(const float InDt);
	void BindParticleCallbackMapping(Chaos::TArrayCollectionArray<PhysicsProxyWrapper> & PhysicsProxyReverseMap, Chaos::TArrayCollectionArray<int32> & ParticleIDReverseMap);
	void CreateRigidBodyCallback(FParticlesType& InOutParticles);
	void ParameterUpdateCallback(FParticlesType& InParticles, const float InTime);
	void DisableCollisionsCallback(TSet<TTuple<int32, int32>>& InPairs);
	void AddForceCallback(FParticlesType& InParticles, const float InDt, const int32 InIndex);
	void FieldForcesUpdateCallback(Chaos::FPhysicsSolver* InSolver, FParticlesType& Particles, Chaos::TArrayCollectionArray<FVector> & Force, Chaos::TArrayCollectionArray<FVector> & Torque, const float Time);
	void ActivateBodies();

	Chaos::FParticleData* NewData() { return nullptr; }
	void SyncBeforeDestroy();
	void OnRemoveFromScene();
	void PushToPhysicsState(const Chaos::FParticleData*) {};
	void BufferPhysicsResults();
	void FlipBuffer();
	void PullFromPhysicsState();
	EPhysicsProxyType ConcreteType() { return EPhysicsProxyType::GeometryCollectionType; }
	/** ----------------------- */

	void MergeRecordedTracks(const FRecordedTransformTrack& A, const FRecordedTransformTrack& B, FRecordedTransformTrack& Target);
	FRecordedFrame& InsertRecordedFrame(FRecordedTransformTrack& InTrack, float InTime);
	void BuildClusters(uint32 CollectionClusterIndex, const TArray<uint32>& CollectionChildIDs, const TArray<uint32>& ChildIDs, const Chaos::FClusterCreationParameters<float> & Parameters);
	void BufferCommand(Chaos::FPhysicsSolver* InSolver, const FFieldSystemCommand& Command) { Commands.Add(Command); }

	static void InitializeSharedCollisionStructures(Chaos::FErrorReporter& ErrorReporter, FGeometryCollection& RestCollection, const FSharedSimulationParameters& SharedParams);

	static void InitRemoveOnFracture(FGeometryCollection& RestCollection, const FSharedSimulationParameters& SharedParams);
	const Chaos::TBufferedData<FGeometryCollectionResults>& GetPhysicsResults() const { return Results; }

	// Game thread and physics thread access to the dynamic collection. 
	const FGeometryDynamicCollection* GetGeometryDynamicCollection_PhysicsThread() { return SimulationCollection.Get(); }
	const FGeometryDynamicCollection* GetGeometryDynamicCollection_GameThread() { return GTDynamicCollection; }

	const FSimulationParameters& GetSimulationParameters() { return Parameters; }

	int32 GetBaseParticleIndex() const { return BaseParticleIndex; }
	int32 GetNumAddedParticles() const { return NumParticles; }

	/*
	* ContiguousIndices
	*   Generates a mapping between the Position array and the results array. When EFieldResolutionType is set to Maximum the complete
	*   particle mapping is provided from the Particles.X to Particles.Attribute, when Minimum is set only the ActiveIndices and the
	*   direct children of the active clusters are set in the IndicesArray.
	*/
	void ContiguousIndices(TArray<ContextIndex>& IndicesArray, const Chaos::FPhysicsSolver* RigidSolver, EFieldResolutionType ResolutionType, bool bForce);

	//
	void SetCollisionParticlesPerObjectFraction(float CollisionParticlesPerObjectFractionIn) {CollisionParticlesPerObjectFraction = CollisionParticlesPerObjectFractionIn;}

	/*
	* For Testing ONLY!
	*/
	const TManagedArray<int32>& RigidBodyIDArray_TestingAccess() const { return RigidBodyID; }
	const TManagedArray<FTransform>& MassToLocal_GameThreadAccess() const { return MassToLocal; }

protected:
	int32 CalculateHierarchyLevel(FGeometryDynamicCollection* GeometryCollection, int32 TransformIndex);
	void CreateDynamicAttributes();
	void InitializeKinematics(FParticlesType& Particles, const TManagedArray<int32>& ObjectType);
	void InitializeRemoveOnFracture(FParticlesType& Particles, const TManagedArray<int32>& DynamicState);
	void ProcessCommands(FParticlesType& Particles, const float Time);
	void PushKinematicStateToSolver(FParticlesType& Particles);



	
private:
	ESimulationInitializationState InitializedState;

	TManagedArray<FTransform> MassToLocal;
	TManagedArray<int32> CollisionMask;
	TManagedArray<int32> CollisionStructureID;
	TManagedArray<int32> RigidBodyID;
	TManagedArray<int32> SolverClusterID;
	TManagedArray<FVector> InitialAngularVelocity;
	TManagedArray<FVector> InitialLinearVelocity;
	TManagedArray<bool> SimulatableParticles;
	TManagedArray<TUniquePtr<FSimplicial> > Simplicials;
	TManagedArray<Chaos::TSerializablePtr<Chaos::TImplicitObject<float, 3>>> Implicits;
	TArray<int32> EndFrameUnparentingBuffer;

	// This is a subset of the geometry group that are used in the transform hierarchy to represent geometry
	TArray<FBox> ValidGeometryBoundingBoxes;
	TArray<int32> ValidGeometryTransformIndices;

	FSimulationParameters Parameters;
	TArray<FFieldSystemCommand> Commands;

	TFunction<void(void)> ResetAnimationCacheCallback;
	TFunction<void(const TArrayView<FTransform> &)> UpdateTransformsCallback;
	TFunction<void(const int32 & CurrentFrame, const TManagedArray<int32> & RigidBodyID, const TManagedArray<int32>& Level, const TManagedArray<int32>& Parent, const TManagedArray<TSet<int32>>& Children, const TManagedArray<uint32>& SimulationType, const TManagedArray<uint32>& StatusFlags, const FParticlesType& Particles)> UpdateRestStateCallback;
	TFunction<void(float SolverTime, const TManagedArray<int32> & RigidBodyID, const FParticlesType& Particles, const FCollisionConstraintsType& CollisionRule)> UpdateRecordedStateCallback;
	TFunction<void(FRecordedTransformTrack& InTrack)> CommitRecordedStateCallback;

	// Index of the first particles for this collection in the larger particle array
	int32 BaseParticleIndex;

	// Number of particles added by this collection
	int32 NumParticles;

	// Time since this object started simulating
	float ProxySimDuration;

	void UpdateRecordedState(float SolverTime, const TManagedArray<int32>& RigidBodyID, const TManagedArray<int32>& CollectionClusterID, const Chaos::TArrayCollectionArray<bool>& InternalCluster, const FParticlesType& Particles, const FCollisionConstraintsType& CollisionRule);

	void AddCollisionToCollisionData(FRecordedFrame* ExistingFrame, const FParticlesType& Particles, const Chaos::TPBDCollisionConstraint<float, 3>::FRigidBodyContactConstraint& Constraint);

	void UpdateCollisionData(const FParticlesType& Particles, const FCollisionConstraintsType& CollisionRule, FRecordedFrame* ExistingFrame);

	void AddBreakingToBreakingData(FRecordedFrame* ExistingFrame, const FParticlesType& Particles, const Chaos::TBreakingData<float, 3>& Breaking);

	void UpdateBreakingData(const FParticlesType& Particles, FRecordedFrame* ExistingFrame);

	void UpdateTrailingData(const FParticlesType& Particles, FRecordedFrame* ExistingFrame);

	// Duplicated dynamic collection for use on the physics thread, copied to the game thread on sync
	TUniquePtr<FGeometryDynamicCollection> SimulationCollection;

	// Dynamic collection on the game thread - used to populate the simulated collection
	FGeometryDynamicCollection* GTDynamicCollection;

	// bodies waiting to activate on initialization
	TArray<uint32> PendingActivationList;

	// Storage for the recorded frame information when we're caching the geometry component results.
	// Synced back to the component with SyncBeforeDestroy
	FRecordedTransformTrack RecordedTracks;

	// Functions to handle engine-side events
	FInitFunc InitFunc;
	FCacheSyncFunc CacheSyncFunc;
	FFinalSyncFunc FinalSyncFunc;

	// Sync frame numbers so we don't do many syncs when physics is running behind
	uint32 LastSyncCountGT;

	// Records current dynamic state
	bool IsObjectDynamic;

	// Indicate when loaded
	bool IsObjectLoading;

	// Per object collision fraction.
	float CollisionParticlesPerObjectFraction;

	// Double buffer of geom collection result data
	Chaos::TBufferedData<FGeometryCollectionResults> Results;

	/** ----------------------- */

};

CHAOSSOLVERS_API void BuildSimulationData(Chaos::FErrorReporter& ErrorReporter, FGeometryCollection& GeometryCollection, const FSharedSimulationParameters& SharedParams);
#endif
