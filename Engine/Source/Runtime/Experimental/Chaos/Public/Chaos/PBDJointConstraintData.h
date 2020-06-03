// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "Chaos/PBDJointConstraintTypes.h"
#include "Chaos/ParticleDirtyFlags.h"
#include "Chaos/Framework/PhysicsProxyBase.h"
#include "Chaos/Framework/PhysicsSolverBase.h"

namespace Chaos
{

	enum class EJointConstraintFlags : uint32
	{
		Position = 0,
		CollisionEnabled=1,
		DummyFlag
	};

	using FJointConstraintDirtyFlags = TDirtyFlags<EJointConstraintFlags>;

	class CHAOS_API FJointConstraint
	{
	public:
		typedef FPBDJointSettings FData;
		typedef FPBDJointConstraintHandle FHandle;
		typedef TVector<FTransform, 2> FTransformPair;
		typedef TVector<TGeometryParticle<FReal, 3>*, 2> FParticlePair;
		typedef TVector<TGeometryParticleHandle<FReal, 3>*, 2> FParticleHandlePair;
		friend FData;

		FJointConstraint();

		template<typename T = IPhysicsProxyBase> T* GetProxy() { return static_cast<T*>(Proxy); }

		void SetProxy(IPhysicsProxyBase* InProxy);

		bool IsValid() const;
		bool IsDirty() const { return MDirtyFlags.IsDirty(); }
		void ClearDirtyFlags() { MDirtyFlags.Clear(); }

		void SetJointParticles(const Chaos::FJointConstraint::FParticlePair& InJointParticles);
		const FParticlePair GetJointParticles() const;
		FParticlePair GetJointParticles();

		void SetJointTransforms(const Chaos::FJointConstraint::FTransformPair& InJointParticles);
		const FTransformPair GetJointTransforms() const;
		FTransformPair GetJointTransforms();

		void SetCollisionEnabled(bool InValue);
		bool GetCollisionEnabled() const;

	protected:
		class IPhysicsProxyBase* Proxy;

		FJointConstraintDirtyFlags MDirtyFlags;

		FData JointSettings;
		FParticlePair JointParticles;
		FTransformPair JointTransforms;

	};

} // Chaos
