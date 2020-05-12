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


	/** Concrete can either be the game thread or physics representation, but API stays the same. Useful for keeping initialization and other logic the same*/
	template <typename FConcrete>
	void JointConstraintDefaultConstruct(FConcrete& Concrete, const FPBDJointSettings& Settings)
	{
		// @todo(Chaos::JointConstraints): set defaults
	}

	class FJointConstraint
	{
	public:
		typedef FPBDJointSettings FData;
		typedef FPBDJointConstraintHandle FHandle;

		friend FData;

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

		static TUniquePtr<FJointConstraint> CreateConstraint(const FPBDJointSettings& InInitialSettings)
		{
			return TUniquePtr<FJointConstraint>(new FJointConstraint(InInitialSettings));
		}

	protected:
		FJointConstraintDirtyFlags MDirtyFlags;

		// Pointer to any data that the solver wants to associate with this constraint
		class IPhysicsProxyBase* Proxy;

		FJointConstraint(const FPBDJointSettings& InInitialSettings = FPBDJointSettings())
		{
			JointConstraintDefaultConstruct(*this, InInitialSettings);
		}

	};

} // Chaos
