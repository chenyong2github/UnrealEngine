// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Chaos/ArrayCollectionArray.h"
#include "Chaos/Framework/MultiBufferResource.h"
#include "Chaos/Framework/PhysicsProxy.h"
#include "Chaos/PBDJointConstraints.h"
#include "Chaos/ParticleHandle.h"
#include "PhysicsCoreTypes.h"
#include "Chaos/Defines.h"
#include "Chaos/EvolutionTraits.h"

namespace Chaos
{
	class FJointConstraint;

	template <typename Traits>
	class TPBDRigidsEvolutionGBF;
}

/**
 * \p CONSTRAINT_TYPE is one of:
 *		\c Chaos::FJointConstraint
 *      \c Chaos::FPositionConstraint // @todo(chaos)
 *      \c Chaos::FVelocityConstraint // @todo(chaos)
 */
template<class CONSTRAINT_TYPE>
class TJointConstraintProxy : public TPhysicsProxy<TJointConstraintProxy<CONSTRAINT_TYPE>,void>
{
	typedef TPhysicsProxy<TJointConstraintProxy<CONSTRAINT_TYPE>, void> Base;

public:
	using FConstraintHandle = typename CONSTRAINT_TYPE::FHandle;
	using FConstraintData = typename CONSTRAINT_TYPE::FData;

	TJointConstraintProxy() = delete;
	TJointConstraintProxy(CONSTRAINT_TYPE* InConstraint, FConstraintHandle* InHandle, UObject* InOwner = nullptr, Chaos::FPBDJointSettings InitialState = Chaos::FPBDJointSettings()); // @todo(brice) : make FPBDJointSetting a type defined on the CONSTRAINT_TYPE
	virtual ~TJointConstraintProxy();

	EPhysicsProxyType ConcreteType() { return EPhysicsProxyType::NoneType; }

	//
	//  Lifespan Management
	//

	template <typename Traits>
	void CHAOSSOLVERS_API InitializeOnPhysicsThread(Chaos::TPBDRigidsSolver<Traits>* InSolver);

	template <typename Traits> 
	void CHAOSSOLVERS_API DestroyOnPhysicsThread(Chaos::TPBDRigidsSolver<Traits>* InSolver);

	void SyncBeforeDestroy() {}
	void OnRemoveFromScene() {}


	//
	// Member Access
	//

	const Chaos::FPBDJointSettings& GetInitialState() const
	{
		return InitialState;
	}

	FConstraintHandle* GetHandle()
	{
		return Handle;
	}

	const FConstraintHandle* GetHandle() const
	{
		return Handle;
	}

	virtual void* GetHandleUnsafe() const override
	{
		return Handle;
	}

	void SetHandle(FConstraintHandle* InHandle)
	{
		Handle = InHandle;
	}

	CONSTRAINT_TYPE* GetConstraint()
	{
		return Constraint;
	}

	const CONSTRAINT_TYPE* GetConstraint() const
	{
		return Constraint;
	}

	//
	// Threading API
	//

	/**/
	void FlipBuffer() { }

	template <typename Traits>
	void PushToPhysicsState(Chaos::TPBDRigidsEvolutionGBF<Traits>& Evolution) {}

	/**/
	void ClearAccumulatedData() {}

	/**/
	void BufferPhysicsResults() {}

	/**/
	void PullFromPhysicsState() {}

	/**/
	bool IsDirty() { return true; }
	
private:

	Chaos::FPBDJointSettings InitialState;
	CONSTRAINT_TYPE* Constraint;
	FConstraintHandle* Handle;

public:
	void AddForceCallback(FParticlesType& InParticles, const float InDt, const int32 InIndex) {}
	void DisableCollisionsCallback(TSet<TTuple<int32, int32>>& InPairs) {}
	void BindParticleCallbackMapping(Chaos::TArrayCollectionArray<PhysicsProxyWrapper>& PhysicsProxyReverseMap, Chaos::TArrayCollectionArray<int32>& ParticleIDReverseMap) {}
	void EndFrameCallback(const float InDt) {}
	void ParameterUpdateCallback(FParticlesType& InParticles, const float InTime) {}
	void CreateRigidBodyCallback(FParticlesType& InOutParticles) {}
	bool IsSimulating() const { return true; }
	void UpdateKinematicBodiesCallback(const FParticlesType& InParticles, const float InDt, const float InTime, FKinematicProxy& InKinematicProxy) {}
	void StartFrameCallback(const float InDt, const float InTime) {}

};

template< > CHAOSSOLVERS_API EPhysicsProxyType TJointConstraintProxy<Chaos::FJointConstraint>::ConcreteType();
#define EVOLUTION_TRAIT(Traits)\
template< > template < > CHAOSSOLVERS_API void TJointConstraintProxy<Chaos::FJointConstraint>::InitializeOnPhysicsThread(Chaos::TPBDRigidsSolver<Chaos::Traits>* InSolver);\
template< > template < > CHAOSSOLVERS_API void TJointConstraintProxy<Chaos::FJointConstraint>::DestroyOnPhysicsThread(Chaos::TPBDRigidsSolver<Chaos::Traits>* RBDSolver);\

#include "Chaos/EvolutionTraits.inl"
#undef EVOLUTION_TRAIT
extern template class TJointConstraintProxy< Chaos::FJointConstraint >;
typedef TJointConstraintProxy< Chaos::FJointConstraint > FJointConstraintPhysicsProxy;
