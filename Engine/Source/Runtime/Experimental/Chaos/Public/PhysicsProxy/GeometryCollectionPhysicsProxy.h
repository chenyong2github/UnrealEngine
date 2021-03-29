// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GeometryCollection/GeometryCollection.h"
#include "GeometryCollectionProxyData.h"

#include "Chaos/Framework/PhysicsProxy.h"
#include "GeometryCollection/ManagedArray.h"
#include "GeometryCollection/GeometryCollectionCollisionStructureManager.h"
#include "Chaos/CollisionFilterData.h"
#include "Chaos/Framework/BufferedData.h"
#include "Chaos/GeometryParticlesfwd.h"
#include "Chaos/ParticleHandle.h"
#include "Chaos/ParticleHandleFwd.h"
#include "Containers/Array.h"
#include "PBDRigidsSolver.h"
#include "Chaos/Defines.h"
#include "Chaos/GeometryParticlesfwd.h"

namespace Chaos
{
	template <typename T> class TSerializablePtr;
	class FErrorReporter;
	struct FClusterCreationParameters;
	struct FDirtyGeometryCollectionData;
	
	class FPBDRigidsEvolutionBase;
}

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
class CHAOS_API FGeometryCollectionPhysicsProxy : public TPhysicsProxy<FGeometryCollectionPhysicsProxy, FStubGeometryCollectionData>
{
public:
	typedef TPhysicsProxy<FGeometryCollectionPhysicsProxy, FStubGeometryCollectionData> Base;
	typedef FCollisionStructureManager::FSimplicial FSimplicial;
	typedef Chaos::TPBDRigidParticleHandle<float, 3> FParticleHandle;
	typedef Chaos::TPBDRigidClusteredParticleHandle<float, 3> FClusterHandle;

	/** Proxy publics */
	using FInitFunc = TFunction<void(FSimulationParameters&)>;
	using FCacheSyncFunc = TFunction<void(const FGeometryCollectionResults&)>;
	using FFinalSyncFunc = TFunction<void(const FRecordedTransformTrack&)>;
	using IPhysicsProxyBase::GetSolver;

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
		FGeometryDynamicCollection& GameThreadCollection,
		const FSimulationParameters& SimulationParameters,
		FCollisionFilterData InSimFilter,
		FCollisionFilterData InQueryFilter,
		FInitFunc InInitFunc, 
		FCacheSyncFunc InCacheSyncFunc, 
		FFinalSyncFunc InFinalSyncFunc  ,
		const Chaos::EMultiBufferMode BufferMode=Chaos::EMultiBufferMode::TripleGuarded);
	virtual ~FGeometryCollectionPhysicsProxy();

	/**
	 * Construct \c PTDynamicCollection, copying attributes from the game thread, 
	 * and prepare for simulation.
	 */
	void Initialize(Chaos::FPBDRigidsEvolutionBase* Evolution);
	void Reset() { }

	/** 
	 * Finish initialization on the physics thread. 
	 *
	 * Called by solver command registered by \c FPBDRigidsSolver::RegisterObject().
	 */
	void InitializeBodiesPT(
		Chaos::FPBDRigidsSolver* RigidsSolver,
		typename Chaos::FPBDRigidsSolver::FParticlesType& Particles);

	/** */
	static void InitializeDynamicCollection(FGeometryDynamicCollection& DynamicCollection, const FGeometryCollection& RestCollection, const FSimulationParameters& Params);

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

	/** Called at the end of \c FPBDRigidsSolver::PushPhysicsStateExec(). */
	void ClearAccumulatedData() {}

	/** Push physics state into the \c PhysToGameInterchange. */
	void BufferPhysicsResults(Chaos::FPBDRigidsSolver* CurrentSolver, Chaos::FDirtyGeometryCollectionData& BufferData);

	/** Does nothing as \c BufferPhysicsResults() already did this. */
	void FlipBuffer();
	
	/** 
	 * Pulls data out of the PhysToGameInterchange and updates \c GTDynamicCollection. 
	 * Called from FPhysScene_ChaosInterface::SyncBodies(), NOT the solver.
	 */
	bool PullFromPhysicsState(const Chaos::FDirtyGeometryCollectionData& BufferData, const int32 SolverSyncTimestamp);

	bool IsDirty() { return false; }

	EPhysicsProxyType ConcreteType() { return EPhysicsProxyType::GeometryCollectionType; }

	void SyncBeforeDestroy();
	void OnRemoveFromSolver(Chaos::FPBDRigidsSolver *RBDSolver);
	void OnRemoveFromScene();

	void SetCollisionParticlesPerObjectFraction(float CollisionParticlesPerObjectFractionIn) 
	{CollisionParticlesPerObjectFraction = CollisionParticlesPerObjectFractionIn;}

	TArray<FClusterHandle*>& GetSolverClusterHandles() { return SolverClusterHandles; }

	TArray<FClusterHandle*>& GetSolverParticleHandles() { return SolverParticleHandles; }

	const FGeometryCollectionResults* GetConsumerResultsGT() const 
	{ return PhysToGameInterchange.PeekConsumerBuffer(); }

	/** Enqueue a field \p Command to be processed by \c ProcessCommands() or 
	 * \c FieldForcesUpdateCallback(). 
	 */
	void BufferCommand(Chaos::FPBDRigidsSolver* , const FFieldSystemCommand& Command)
	{ Commands.Add(Command); }

	static void InitializeSharedCollisionStructures(Chaos::FErrorReporter& ErrorReporter, FGeometryCollection& RestCollection, const FSharedSimulationParameters& SharedParams);
	static void InitRemoveOnFracture(FGeometryCollection& RestCollection, const FSharedSimulationParameters& SharedParams);

	void FieldForcesUpdateCallback(Chaos::FPBDRigidsSolver* RigidSolver);

	void FieldParameterUpdateCallback(Chaos::FPBDRigidsSolver* RigidSolver, const bool bUpdateViews = true);

	void UpdateKinematicBodiesCallback(const FParticlesType& InParticles, const float InDt, const float InTime, FKinematicProxy& InKinematicProxy) {}
	void StartFrameCallback(const float InDt, const float InTime) {}
	void EndFrameCallback(const float InDt) {}
	void BindParticleCallbackMapping(Chaos::TArrayCollectionArray<PhysicsProxyWrapper>& PhysicsProxyReverseMap, Chaos::TArrayCollectionArray<int32>& ParticleIDReverseMap) {}
	void CreateRigidBodyCallback(FParticlesType& InOutParticles) {}
	void DisableCollisionsCallback(TSet<TTuple<int32, int32>>& InPairs) {}
	void AddForceCallback(FParticlesType& InParticles, const float InDt, const int32 InIndex) {}

	bool IsGTCollectionDirty() const { return GameThreadCollection.IsDirty(); }

	const TArray<FClusterHandle*> GetParticles() const
	{
		return SolverParticleHandles;
	}

	const FSimulationParameters& GetSimParameters() const
	{
		return Parameters;
	}

	FSimulationParameters& GetSimParameters()
	{
		return Parameters;
	}

	FGeometryDynamicCollection& GetPhysicsCollection()
	{
		return PhysicsThreadCollection;
	}

	TManagedArray<TUniquePtr<Chaos::FGeometryParticle>>& GetExternalParticles()
	{
		return GTParticles;
	}

	/**
	*  * Get all the geometry collection particle handles based on the processing resolution
	 */
	void GetRelevantParticleHandles(
		TArray<Chaos::TGeometryParticleHandle<float, 3>*>& Handles,
		const Chaos::FPBDRigidsSolver* RigidSolver,
		EFieldResolutionType ResolutionType);

	/**
	 * Get all the geometry collection particle handles filtered by object state
	 */
	void GetFilteredParticleHandles(
		TArray<Chaos::TGeometryParticleHandle<float, 3>*>& Handles,
		const Chaos::FPBDRigidsSolver* RigidSolver,
		const EFieldFilterType FilterType);
		
	/* Implemented so we can construct TAccelerationStructureHandle. */
	virtual void* GetHandleUnsafe() const override { return nullptr; }

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
		const Chaos::FClusterCreationParameters & Parameters,
		const Chaos::FUniqueIdx* ExistingIndex);

	/** 
	 * Traverses the parents of \p TransformIndex in \p GeometryCollection, counting
	 * the number of levels until the next parent is \c INDEX_NONE.
	 */
	int32 CalculateHierarchyLevel(const FGeometryDynamicCollection& GeometryCollection, int32 TransformIndex) const;

	void InitializeRemoveOnFracture(FParticlesType& Particles, const TManagedArray<int32>& DynamicState);

private:

	FSimulationParameters Parameters;
	TArray<FFieldSystemCommand> Commands;

	//
	//  Proxy State Information
	//
	int32 NumParticles;
	int32 BaseParticleIndex;
	TArray<FParticleHandle*> SolverClusterID;
	TArray<FClusterHandle*> SolverClusterHandles; // make a TArray of the base clase with type
	TArray<FClusterHandle*> SolverParticleHandles;// make a TArray of base class and join with above
	TMap<FParticleHandle*, int32> HandleToTransformGroupIndex;


	//
	// Buffer Results State Information
	//
	bool IsObjectDynamic; // Records current dynamic state
	bool IsObjectLoading; // Indicate when loaded

	TManagedArray<TUniquePtr<Chaos::FGeometryParticle>> GTParticles;
	FCollisionFilterData SimFilter;
	FCollisionFilterData QueryFilter;

	// This is a subset of the geometry group that are used in the transform hierarchy to represent geometry
	TArray<FBox> ValidGeometryBoundingBoxes;
	TArray<int32> ValidGeometryTransformIndices;

#ifdef TODO_REIMPLEMENT_RIGID_CACHING
	TFunction<void(void)> ResetAnimationCacheCallback;
	TFunction<void(const TArrayView<FTransform> &)> UpdateTransformsCallback;
	TFunction<void(const int32 & CurrentFrame, const TManagedArray<int32> & RigidBodyID, const TManagedArray<int32>& Level, const TManagedArray<int32>& Parent, const TManagedArray<TSet<int32>>& Children, const TManagedArray<uint32>& SimulationType, const TManagedArray<uint32>& StatusFlags, const FParticlesType& Particles)> UpdateRestStateCallback;
	TFunction<void(float SolverTime, const TManagedArray<int32> & RigidBodyID, const FParticlesType& Particles, const FCollisionConstraintsType& CollisionRule)> UpdateRecordedStateCallback;
	TFunction<void(FRecordedTransformTrack& InTrack)> CommitRecordedStateCallback;

	// Index of the first particles for this collection in the larger particle array
	// Time since this object started simulating
	float ProxySimDuration;

	// Sync frame numbers so we don't do many syncs when physics is running behind
	uint32 LastSyncCountGT;

	// Storage for the recorded frame information when we're caching the geometry component results.
	// Synced back to the component with SyncBeforeDestroy
	FRecordedTransformTrack RecordedTracks;
#endif

	// Functions to handle engine-side events
	FInitFunc InitFunc;
	FCacheSyncFunc CacheSyncFunc;
	FFinalSyncFunc FinalSyncFunc;


	// Per object collision fraction.
	float CollisionParticlesPerObjectFraction;

	// The Simulation data is copied between the game and physics thread. It is 
	// expected that the two data sets will diverge, based on how the simulation
	// uses the data, but at the start of the simulation the PhysicsThreadCollection
	// is a deep copy from the GameThreadCollection. 
	FGeometryDynamicCollection PhysicsThreadCollection;
	FGeometryDynamicCollection& GameThreadCollection;

	// Currently this is using triple buffers for game-physics and 
	// physics-game thread communication, but not for any reason other than this 
	// is the only implementation we currently have of a guarded buffer - a buffer 
	// that tracks it's own state, rather than having other mechanisms determine 
	// whether or not the contents of the buffer have been updated.  A double 
	// buffer would probably be fine, as that seems to be the assumption the logic
	// currently managing the exchange is built upon.  However, I believe that 
	// logic locks, and the triple buffer would enable a decoupled lock-free 
	// paradigm, at least for this component of the handshake.
	Chaos::FGuardedTripleBuffer<FGeometryCollectionResults> PhysToGameInterchange;

};

CHAOS_API Chaos::FTriangleMesh* CreateTriangleMesh(const int32 FaceStart,const int32 FaceCount,const TManagedArray<bool>& Visible,const TManagedArray<FIntVector>& Indices, bool bRotateWinding = true);
CHAOS_API void BuildSimulationData(Chaos::FErrorReporter& ErrorReporter, FGeometryCollection& GeometryCollection, const FSharedSimulationParameters& SharedParams);
