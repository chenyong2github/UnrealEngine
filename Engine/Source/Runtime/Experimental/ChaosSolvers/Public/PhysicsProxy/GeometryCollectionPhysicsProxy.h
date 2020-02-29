// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GeometryCollection/GeometryCollection.h"
#include "GeometryCollection/GeometryDynamicCollection.h"

#include "Chaos/Framework/PhysicsProxy.h"
#include "GeometryCollection/ManagedArray.h"
#include "GeometryCollection/GeometryCollectionCollisionStructureManager.h"
#include "Chaos/CollisionFilterData.h"
#include "Chaos/Framework/BufferedData.h"
#include "Chaos/GeometryParticlesfwd.h"
#include "Chaos/ParticleHandle.h"
#include "Chaos/ParticleHandleFwd.h"
#include "PBDRigidsSolver.h"

namespace Chaos
{
	template <typename T> class TSerializablePtr;
	class FErrorReporter;
	template <typename T> struct FClusterCreationParameters;
}

/**
 * Buffer structure for communicating simulation state between game and physics 
 * threads.
 */
class FGeometryCollectionResults
{
public:
	FGeometryCollectionResults();

	void Reset();

	int32 NumTransformGroup() const { return Transforms.Num(); }

	void InitArrays(const FGeometryDynamicCollection &Other)
	{
		// Managed arrays
		Transforms.Init(Other.Transform);
		DynamicState.Init(Other.DynamicState);
		Parent.Init(Other.Parent);
		Children.Init(Other.Children);
		SimulationType.Init(Other.SimulationType);
		
		// Arrays
		const int32 NumTransforms = Other.NumElements(FGeometryCollection::TransformGroup);
		DisabledStates.SetNumUninitialized(NumTransforms);
		GlobalTransforms.SetNumUninitialized(NumTransforms);
		ParticleToWorldTransforms.SetNumUninitialized(NumTransforms);
	}

	int32 BaseIndex;
	int32 NumParticlesAdded;
	TArray<bool> DisabledStates;
	TArray<FMatrix> GlobalTransforms;
	TArray<FTransform> ParticleToWorldTransforms;

	TManagedArray<int32> TransformIndex;

	TManagedArray<FTransform> Transforms;
	TManagedArray<int32> BoneMap;
	TManagedArray<int32> Parent;
	TManagedArray<TSet<int32>> Children;
	TManagedArray<int32> SimulationType;
	TManagedArray<int32> DynamicState;
	TManagedArray<float> Mass;
	TManagedArray<FVector> InertiaTensor;

	TManagedArray<int32> ClusterId;

	TArray<TSharedPtr<Chaos::FImplicitObject, ESPMode::ThreadSafe>> SharedGeometry;
	TArray<TArray<FCollisionFilterData>> ShapeSimData;
	TArray<TArray<FCollisionFilterData>> ShapeQueryData;

	bool IsObjectDynamic;
	bool IsObjectLoading;

	FBoxSphereBounds WorldBounds;
};


class FStubGeometryCollectionData : public Chaos::FParticleData 
{
public:
	typedef Chaos::FParticleData Base;

	FStubGeometryCollectionData(const FGeometryCollectionResults* DataIn=nullptr)
		: Base(Chaos::EParticleType::GeometryCollection)
		, Data(DataIn)
	{}

	void Reset() 
	{
		Base::Reset(); // Sets Type to EParticleType::Static
	}

	const FGeometryCollectionResults* GetStateData() const { return Data; }

private:
	const FGeometryCollectionResults* Data;
};

/**
 * Class to manage sharing data between the game thread and the simulation thread 
 * (which may not be different than the game thread) for a \c FGeometryDynamicCollection.
 */
class CHAOSSOLVERS_API FGeometryCollectionPhysicsProxy : public TPhysicsProxy<FGeometryCollectionPhysicsProxy, FStubGeometryCollectionData>
{
	typedef TPhysicsProxy<FGeometryCollectionPhysicsProxy, FStubGeometryCollectionData> Base;

public:
	// collection attributes
	static FName SimplicialsAttribute;
	static FName ImplicitsAttribute;
	static FName SharedImplicitsAttribute;
	static FName SolverParticleHandlesAttribute;
	static FName SolverClusterHandlesAttribute;
	static FName GTGeometryParticleAttribute;

	typedef FCollisionStructureManager::FSimplicial FSimplicial;

	/** Proxy publics */
	using FInitFunc = TFunction<void(FSimulationParameters&)>;
	using FCacheSyncFunc = TFunction<void(const FGeometryCollectionResults&)>;
	using FFinalSyncFunc = TFunction<void(const FRecordedTransformTrack&)>;

	FGeometryCollectionPhysicsProxy() = delete;
	/**
	 * \p InOwner
	 * \p InDynamicCollection game thread owned geometry collection.
	 * \p InInitFunc callback invoked from \c Initialize().
	 * \p InCacheSyncFunc callback invoked from \c PullFromPhysicsState().
	 * \p InFinalSyncFunc callback invoked from \c SyncBeforeDestory().
	 */
	FGeometryCollectionPhysicsProxy(
		UObject* InOwner, 
		FGeometryDynamicCollection* InDynamicCollection, 
		FInitFunc InInitFunc, 
		FCacheSyncFunc InCacheSyncFunc, 
		FFinalSyncFunc InFinalSyncFunc  ,
		const Chaos::EMultiBufferMode BufferMode=Chaos::EMultiBufferMode::TripleGuarded);
	virtual ~FGeometryCollectionPhysicsProxy();

	/**
	 * Construct \c PTDynamicCollection, copying attributes from the game thread, 
	 * and prepare for simulation.
	 */
	void Initialize();
	void Reset() { InitializedState = ESimulationInitializationState::Unintialized; }

	/** 
	 * Initialize rigid body particles on the game thread. 
	 *
	 * Called by \c FPBDRigidsSolver::RegisterObject().
	 */
	void InitializeBodiesGT();

	/** 
	 * Finish initialization on the physics thread. 
	 *
	 * Called by solver command registered by \c FPBDRigidsSolver::RegisterObject().
	 */
	void InitializeBodiesPT(
		Chaos::FPBDRigidsSolver* RigidsSolver,
		Chaos::FPBDRigidsSolver::FParticlesType& Particles);

	/** */
	bool IsSimulating() const { return Parameters.Simulating; }

	/**
	 * Pushes current game thread particle state into the \c GameToPhysInterchange.
	 *
	 * Redirects to \c BufferGameState(), and returns nullptr as this class manages 
	 * data transport to the physics thread itself, without allocating memory.
	 */
	Chaos::FParticleData* NewData() { BufferGameState(); return nullptr; }
	void BufferGameState();

	/** 
	 * Push game data to the physics thread.
	 * 
	 * The solver has determined that this proxy needs to update its physics state 
	 * with game data, either as part of the solver registration process, or
	 * a game thread particle instance was updated, which marked this proxy dirty.
	 * 
	 * Pulls data out of the \c GameToPhysInterchange, and updates solver particle 
	 * data.
	 *
	 * Invoked by \c FPBDRigidsSolver::RegisterObject() and via particle dirtying. 
	 */
	void PushToPhysicsState(const Chaos::FParticleData*);

	/** Called at the end of \c FPBDRigidsSolver::PushPhysicsStateExec(). */
	void ClearAccumulatedData() {}

	/** Push physics state into the \c PhysToGameInterchange. */
	void BufferPhysicsResults();

	/** Does nothing as \c BufferPhysicsResults() already did this. */
	void FlipBuffer();
	
	/** 
	 * Pulls data out of the PhysToGameInterchange and updates \c GTDynamicCollection. 
	 * Called from FPhysScene_ChaosInterface::SyncBodies(), NOT the solver.
	 */
	void PullFromPhysicsState();

	bool IsDirty() { return false; }

	EPhysicsProxyType ConcreteType() { return EPhysicsProxyType::GeometryCollectionType; }

	void SyncBeforeDestroy();
	void OnRemoveFromSolver(Chaos::FPBDRigidsSolver *RBDSolver);
	void OnRemoveFromScene();

	void SetCollisionParticlesPerObjectFraction(float CollisionParticlesPerObjectFractionIn) 
	{CollisionParticlesPerObjectFraction = CollisionParticlesPerObjectFractionIn;}

	TManagedArray<Chaos::TPBDRigidClusteredParticleHandle<float, 3>*>& GetSolverParticleHandles() 
	{ return SolverParticleHandles; }

	const FGeometryCollectionResults* GetConsumerResultsGT() const 
	{ return PhysToGameInterchange ? PhysToGameInterchange->PeekConsumerBuffer() : nullptr; }

	/** Enqueue a field \p Command to be processed by \c ProcessCommands() or 
	 * \c FieldForcesUpdateCallback(). 
	 */
	void BufferCommand(Chaos::FPhysicsSolver* , const FFieldSystemCommand& Command) 
	{ Commands.Add(Command); }

	static void InitializeSharedCollisionStructures(Chaos::FErrorReporter& ErrorReporter, FGeometryCollection& RestCollection, const FSharedSimulationParameters& SharedParams);
	static void InitRemoveOnFracture(FGeometryCollection& RestCollection, const FSharedSimulationParameters& SharedParams);

	static void MergeRecordedTracks(const FRecordedTransformTrack& A, const FRecordedTransformTrack& B, FRecordedTransformTrack& Target);
	static FRecordedFrame& InsertRecordedFrame(FRecordedTransformTrack& InTrack, float InTime);

	// 
	// DEPRECATED
	//
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

protected:
	/**
	 * Build a physics thread cluster parent particle.
	 *	\p CollectionClusterIndex - the source geometry collection transform index.
	 *	\p ChildHandles - physics particle handles of the cluster children.
	 *  \p ChildTransformGroupIndices - geometry collection indices of the children.
	 *  \P Parameters - uh, yeah...  Other parameters.
	 */
	Chaos::TPBDRigidClusteredParticleHandle<float, 3>* BuildClusters(
		const uint32 CollectionClusterIndex, 
		TArray<Chaos::TPBDRigidParticleHandle<float, 3>*>& ChildHandles,
		const TArray<int32>& ChildTransformGroupIndices,
		const Chaos::FClusterCreationParameters<float> & Parameters);

	/** 
	 * Copy \p Results into \p TargetCollection, or \c Parameters.DynamicCollection 
	 * if \c nullptr.
	 */
	void UpdateGeometryCollection(
		const FGeometryCollectionResults& Results,
		FGeometryDynamicCollection* TargetCollection=nullptr);

	/**
	 * Generates a mapping between the Position array and the results array. 
	 * When EFieldResolutionType is set to Maximum the complete particle mapping 
	 * is provided from the Particles.X to Particles.Attribute, when Minimum is 
	 * set only the ActiveIndices and the direct children of the active clusters 
	 * are set in the IndicesArray.
	 */
	void ContiguousIndices(
		TArray<ContextIndex>& IndicesArray, 
		const Chaos::FPhysicsSolver* RigidSolver, 
		EFieldResolutionType ResolutionType, 
		bool bForce);

	/** The size of the transform group, as reported by the \c DynamicCollection 
	 * currently stored in \c Parameters. 
	 */
	int32 GetTransformGroupSize() const 
	{ return Parameters.DynamicCollection ? Parameters.DynamicCollection->NumElements(FGeometryCollection::TransformGroup) : 0; }

	/** 
	 * Traverses the parents of \p TransformIndex in \p GeometryCollection, counting
	 * the number of levels until the next parent is \c INDEX_NONE.
	 */
	int32 CalculateHierarchyLevel(
		const FGeometryDynamicCollection* GeometryCollection, 
		int32 TransformIndex) const;

	void CreateDynamicAttributes();
	void InitializeKinematics(FParticlesType& Particles, const TManagedArray<int32>& ObjectType);
	void InitializeRemoveOnFracture(FParticlesType& Particles, const TManagedArray<int32>& DynamicState);
	void PushKinematicStateToSolver(FParticlesType& Particles);

	void ProcessCommands(FParticlesType& Particles, const float Time);

private:
	void UpdateRecordedState(
		float SolverTime, 
		const TManagedArray<int32>& RigidBodyID, 
		const TManagedArray<int32>& CollectionClusterID, 
		const Chaos::TArrayCollectionArray<bool>& InternalCluster, 
		const FParticlesType& Particles, 
		const FCollisionConstraintsType& CollisionRule);

	void AddCollisionToCollisionData(
		FRecordedFrame* ExistingFrame, 
		const FParticlesType& Particles, 
		const Chaos::TPBDCollisionConstraints<float, 3>::FPointContactConstraint& Constraint);

	void UpdateCollisionData(
		const FParticlesType& Particles, 
		const FCollisionConstraintsType& CollisionRule, 
		FRecordedFrame* ExistingFrame);

	void AddBreakingToBreakingData(
		FRecordedFrame* ExistingFrame, 
		const FParticlesType& Particles, 
		const Chaos::TBreakingData<float, 3>& Breaking);

	void UpdateBreakingData(
		const FParticlesType& Particles, 
		FRecordedFrame* ExistingFrame);

	void UpdateTrailingData(
		const FParticlesType& Particles, 
		FRecordedFrame* ExistingFrame);

private:
	FSimulationParameters Parameters;

	ESimulationInitializationState InitializedState;
	// Records current dynamic state
	bool IsObjectDynamic;
	// Indicate when loaded
	bool IsObjectLoading;

	// Dynamic collection on the game thread - used to populate the simulated collection
	FGeometryDynamicCollection* GTDynamicCollection;
	// Duplicated dynamic collection for use on the physics thread, copied to the game thread on sync
	//TUniquePtr<FGeometryDynamicCollection> PTDynamicCollection;
	FGeometryDynamicCollection PTDynamicCollection;

	TManagedArray<Chaos::TPBDRigidClusteredParticleHandle<float, 3>*> SolverParticleHandles;
	TManagedArray<Chaos::TPBDRigidClusteredParticleHandle<float, 3>*> SolverClusterHandles;
	TManagedArray<TUniquePtr<Chaos::TGeometryParticle<float, 3>>> GTGeometryParticles;

	TMap<Chaos::TPBDRigidParticleHandle<float, 3>*, int32> HandleToTransformGroupIndex;

	TManagedArray<FTransform> MassToLocal;
	TManagedArray<int32> CollisionMask;
	TManagedArray<int32> CollisionStructureID;
	TManagedArray<int32> RigidBodyID; // Deprecated.  Not added to dynamic collection.
	TManagedArray<int32> SolverClusterID;
	TManagedArray<FVector> InitialAngularVelocity;
	TManagedArray<FVector> InitialLinearVelocity;
	TManagedArray<bool> SimulatableParticles;
	TManagedArray<TUniquePtr<FSimplicial> > Simplicials; // FSimplicial = Chaos::TBVHParticles<float,3>
	TManagedArray<TSharedPtr<Chaos::FImplicitObject, ESPMode::ThreadSafe>> Implicits;
	TArray<int32> EndFrameUnparentingBuffer;

	// This is a subset of the geometry group that are used in the transform hierarchy to represent geometry
	TArray<FBox> ValidGeometryBoundingBoxes;
	TArray<int32> ValidGeometryTransformIndices;

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

	// Per object collision fraction.
	float CollisionParticlesPerObjectFraction;

	// Double buffer of geom collection result data - TODO (Ryan): deprecated?
	//Chaos::TBufferedData<FGeometryCollectionResults> Results;

	// TODO (Ryan) - Currently this is using triple buffers for game-physics and 
	// physics-game thread communication, but not for any reason other than this 
	// is the only implementation we currently have of a guarded buffer - a buffer 
	// that tracks it's own state, rather than having other mechanisms determine 
	// whether or not the contents of the buffer have been updated.  A double 
	// buffer would probably be fine, as that seems to be the assumption the logic
	// currently managing the exchange is built upon.  However, I believe that 
	// logic locks, and the triple buffer would enable a decoupled lock-free 
	// paradigm, at least for this component of the handshake.
	TUniquePtr<Chaos::FGuardedTripleBuffer<FGeometryCollectionResults>> PhysToGameInterchange;
	TUniquePtr<Chaos::FGuardedTripleBuffer<FGeometryCollectionResults>> GameToPhysInterchange;
};

CHAOSSOLVERS_API void BuildSimulationData(Chaos::FErrorReporter& ErrorReporter, FGeometryCollection& GeometryCollection, const FSharedSimulationParameters& SharedParams);
