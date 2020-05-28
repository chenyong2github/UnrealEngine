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
		DummyFlag
	};

	using FJointConstraintDirtyFlags = TDirtyFlags<EJointConstraintFlags>;

	class FJointConstraint
	{
	public:
		typedef FPBDJointSettings FData;
		typedef FPBDJointConstraintHandle FHandle;
		typedef TVector<FTransform, 2> FTransformPair;
		typedef TVector<TGeometryParticle<FReal, 3>*, 2> FParticlePair;
		typedef TVector<TGeometryParticleHandle<FReal, 3>*, 2> FParticleHandlePair;
		friend FData;

		FJointConstraint()
			: Proxy(nullptr)
			, JointParticles({ nullptr,nullptr })
			, JointTransforms({ FTransform::Identity, FTransform::Identity})
		{
			MDirtyFlags.Clear();
		}


		template<typename T = IPhysicsProxyBase>
		T* GetProxy()
		{
			return static_cast<T*>(Proxy);
		}

		void SetProxy(IPhysicsProxyBase* InProxy)
		{
			Proxy = InProxy;
			if (Proxy)
			{
				if (MDirtyFlags.IsDirty())
				{
					if (FPhysicsSolverBase* PhysicsSolverBase = Proxy->GetSolver<FPhysicsSolverBase>())
					{
						PhysicsSolverBase->AddDirtyProxy(Proxy);
					}
				}
			}
		}

		bool IsValid() const
		{
			return Proxy != nullptr;
		}

		void SetJointParticles(const Chaos::FJointConstraint::FParticlePair& InJointParticles) { JointParticles[0] = InJointParticles[0]; JointParticles[1] = InJointParticles[1]; }
		const FParticlePair GetJointParticles() const { return JointParticles; }
		FParticlePair GetJointParticles() { return JointParticles; }

		void SetJointTransforms(const Chaos::FJointConstraint::FTransformPair& InJointParticles) { JointTransforms[0] = InJointParticles[0]; JointTransforms[1] = InJointParticles[1]; }
		const FTransformPair GetJointTransforms() const { return JointTransforms; }
		FTransformPair GetJointTransforms() { return JointTransforms; }

		void SetJointSettings(const Chaos::FPBDJointSettings& InSettings) {}

	protected:
		FJointConstraintDirtyFlags MDirtyFlags;

		// Pointer to any data that the solver wants to associate with this constraint
		class IPhysicsProxyBase* Proxy;

		FParticlePair JointParticles;
		FTransformPair JointTransforms;

	};

} // Chaos
