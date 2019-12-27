// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Chaos/ArrayCollectionArray.h"
#include "Chaos/Declares.h"
#include "Chaos/Framework/PhysicsSolverBase.h"
#include "Chaos/Framework/PhysicsProxyBase.h"
#include "Chaos/PBDCollisionConstraints.h"
#include "Chaos/PBDRigidParticles.h"
#include "UObject/GCObject.h"

struct FKinematicProxy;
class FFieldSystemCommand;
struct FBodyInstance;


/**
 * Base object interface for solver objects. Defines the expected API for objects
 * uses CRTP for static dispatch, entire API considered "pure-virtual" and must be* defined.
 * Forgetting to implement any of the interface functions will give errors regarding
 * recursion on all control paths for TPhysicsProxy<T> where T will be the type
 * that has not correctly implemented the API.
 *
 * PersistentTask uses IPhysicsProxyBase, so when implementing a new specialized type
 * it is necessary to include its header file in PersistentTask.cpp allowing the linker
 * to properly resolve the new type. 
 *
 * May not be necessary overall once the engine has solidified - we can just use the
 * final concrete objects but this gives us almost the same flexibility as the old
 * callbacks while solving most of the drawbacks (virtual dispatch, cross-object interaction)
 *
 * #BG TODO - rename the callbacks functions, document for the base solver object
 */
template<class Concrete, class ConcreteData>
class TPhysicsProxy : public IPhysicsProxyBase
{

public:
	using FParticleType = Concrete;
	using FParticleData = ConcreteData;

	using FParticlesType = Chaos::TPBDRigidParticles<float, 3>;
	using FCollisionConstraintsType = Chaos::TPBDCollisionConstraints<float, 3>;
	using FIntArray = Chaos::TArrayCollectionArray<int32>;

	TPhysicsProxy()
		: IPhysicsProxyBase(ConcreteType())
		, Owner(nullptr)
	{
	}

	explicit TPhysicsProxy(UObject* InOwner)
		: IPhysicsProxyBase(ConcreteType())
		, Owner(InOwner)
	{
	}

	/** Virtual destructor for derived objects, ideally no other virtuals should exist in this chain */
	virtual ~TPhysicsProxy() {}

	/**
	 * The following functions are to be implemented by all solver objects as we're using CRTP / F-Bound to
	 * statically dispatch the calls. Any common functions should be added here and to the derived solver objects
	 */

	// Previously callback related functions, all called in the context of the physics thread if enabled.
	bool IsSimulating() const { return static_cast<const Concrete*>(this)->IsSimulating(); }
	void UpdateKinematicBodiesCallback(const FParticlesType& InParticles, const float InDt, const float InTime, FKinematicProxy& InKinematicProxy) { static_cast<Concrete*>(this)->UpdateKinematicBodiesCallback(InParticles, InDt, InTime, InKinematicProxy); }
	void StartFrameCallback(const float InDt, const float InTime) { static_cast<Concrete*>(this)->StartFrameCallback(InDt, InTime); }
	void EndFrameCallback(const float InDt) { static_cast<Concrete*>(this)->EndFrameCallback(InDt); }
	void CreateRigidBodyCallback(FParticlesType& InOutParticles) { static_cast<Concrete*>(this)->CreateRigidBodyCallback(InOutParticles); }
	void ParameterUpdateCallback(FParticlesType& InParticles, const float InTime) { static_cast<Concrete*>(this)->ParameterUpdateCallback(InParticles, InTime); }
	void DisableCollisionsCallback(TSet<TTuple<int32, int32>>& InPairs) { static_cast<Concrete*>(this)->DisableCollisionsCallback(InPairs); }
	void AddForceCallback(FParticlesType& InParticles, const float InDt, const int32 InIndex) { static_cast<Concrete*>(this)->AddForceCallback(InParticles, InDt, InIndex); }
	void FieldForcesUpdateCallback(Chaos::FPhysicsSolver* InSolver, FParticlesType& Particles, Chaos::TArrayCollectionArray<FVector> & Force, Chaos::TArrayCollectionArray<FVector> & Torque, const float Time) { static_cast<Concrete*>(this)->FieldForcesUpdateCallback(InSolver, Particles, Force, Torque, Time); }

	/** The Particle Binding creates a connection between the particles in the simulation and the solver objects dataset. */
	void BindParticleCallbackMapping(Chaos::TArrayCollectionArray<PhysicsProxyWrapper> & PhysicsProxyReverseMap, Chaos::TArrayCollectionArray<int32> & ParticleIDReverseMap) {static_cast<Concrete*>(this)->BindParticleCallbackMapping(PhysicsProxyReverseMap, ParticleIDReverseMap);}

	/** Called to buffer a command to be processed at the next available safe opportunity */
	void BufferCommand(Chaos::FPhysicsSolver* InSolver, const FFieldSystemCommand& InCommand) { static_cast<Concrete*>(this)->BufferCommand(InSolver, InCommand); }

	/** Returns the concrete type of the derived class*/
	EPhysicsProxyType ConcreteType() { return static_cast<Concrete*>(this)->ConcreteType(); }

	/**
	 * CONTEXT: GAMETHREAD
	* Returns a new unmanaged allocation of the data saved on the handle, otherwise nullptr
	*/
	Chaos::FParticleData* NewData() { return static_cast<Concrete*>(this)->NewData(); }

	/**
	* CONTEXT: GAMETHREAD -> to -> PHYSICSTHREAD
	* Called on the game thread when the solver is about to advance forward. This
	* callback should Enqueue commands on the PhysicsThread to update the state of
	* the solver
	*/
	void PushToPhysicsState(const Chaos::FParticleData* InData) { static_cast<Concrete*>(this)->PushToPhysicsState(InData); }

	/**
	* CONTEXT: GAMETHREAD
	* Called on game thread after NewData has been called to buffer the particle data
	* for physics. The purpose of this method is to clear data, such as external force
	* and torque, which have been accumulated over a game tick. Buffering these values
	* once means they'll be accounted for in physics. If they are not cleared, then
	* they may "overaccumulate".
	*/
	void ClearAccumulatedData() { static_cast<Concrete*>(this)->ClearAccumulatedData(); }

	/**
	 * CONTEXT: PHYSICSTHREAD
	 * Called per-tick after the simulation has completed. The proxy should cache the results of their
	 * simulation into the local buffer. 
	 */
	void BufferPhysicsResults() { static_cast<Concrete*>(this)->BufferPhysicsResults(); }

	/**
	 * CONTEXT: PHYSICSTHREAD (Write Locked)
	 * Called by the physics thread to signal that it is safe to perform any double-buffer flips here.
	 * The physics thread has pre-locked an RW lock for this operation so the game thread won't be reading
	 * the data
	 */
	void FlipBuffer() { static_cast<Concrete*>(this)->FlipBuffer(); }

	/**
	 * CONTEXT: GAMETHREAD (Read Locked)
	 * Perform a similar operation to Sync, but take the data from a gamethread-safe buffer. This will be called
	 * from the game thread when it cannot sync to the physics thread. The simulation is very likely to be running
	 * when this happens so never read any physics thread data here!
	 *
	 * Note: A read lock will have been acquired for this - so the physics thread won't force a buffer flip while this
	 * sync is ongoing
	 */
	void PullFromPhysicsState() { static_cast<Concrete*>(this)->PullFromPhysicsState(); }

	/**
	 * CONTEXT: GAMETHREAD
	 * Called during the gamethread sync after the proxy has been removed from its solver
	 * intended for final handoff of any data the proxy has that the gamethread may
	 * be interested in
	*/
	void SyncBeforeDestroy() { static_cast<Concrete*>(this)->SyncBeforeDestroy(); }

	/**
	 * CONTEXT: PHYSICSTHREAD
	 * Called on the physics thread when the engine is shutting down the proxy and we need to remove it from
	 * any active simulations. Proxies are expected to entirely clean up their simulation
	 * state within this method. This is run in the task command step by the scene
	 * so the simulation will currently be idle
	 */
	void OnRemoveFromScene() { static_cast<Concrete*>(this)->OnRemoveFromScene(); }

	bool IsDirty() { return static_cast<Concrete*>(this)->IsDirty(); }



	/** Gets the owning external object for this solver object, never used internally */
	virtual UObject* GetOwner() const override { return Owner; }

	void* GetUserData() const { return nullptr; }

	Chaos::TRigidTransform<float, 3> GetTransform() const { return Chaos::TRigidTransform<float, 3>(); }


private:

	/** 
	 * The owner for this solver object, essentially user-data managed by the caller 
	 * @see GetOwner
	 */
	UObject* Owner;
};
