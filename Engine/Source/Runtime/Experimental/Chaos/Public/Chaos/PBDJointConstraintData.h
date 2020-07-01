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
		Position                    = 0,
		CollisionEnabled            = 1 << 1,
		ProjectionEnabled           = 1 << 2,
		ParentInvMassScale          = 1 << 3,
		LinearBreakForce            = 1 << 4,
		AngularBreakTorque          = 1 << 5,
		UserData                    = 1 << 6,
		DummyFlag
	};

#define CONSTRAINT_JOINT_PROPERPETY_IMPL(TYPE, FNAME, ENAME, VNAME)\
	void Set##FNAME(TYPE InValue){if (InValue != VNAME){VNAME = InValue;MDirtyFlags.MarkDirty(ENAME);SetProxy(Proxy);}}\
	TYPE Get##FNAME() const{return VNAME;}\


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
		bool IsDirty(const EJointConstraintFlags CheckBits) const { return MDirtyFlags.IsDirty(CheckBits); }
		void ClearDirtyFlags() { MDirtyFlags.Clear(); }

		void SetJointParticles(const Chaos::FJointConstraint::FParticlePair& InJointParticles);
		const FParticlePair GetJointParticles() const;
		FParticlePair GetJointParticles();

		void SetJointTransforms(const Chaos::FJointConstraint::FTransformPair& InJointParticles);
		const FTransformPair GetJointTransforms() const;
		FTransformPair GetJointTransforms();

		CONSTRAINT_JOINT_PROPERPETY_IMPL(bool, CollisionEnabled, EJointConstraintFlags::CollisionEnabled, JointSettings.bCollisionEnabled);
		//void SetCollisionEnabled(bool InValue);
		//bool GetCollisionEnabled() const;

		CONSTRAINT_JOINT_PROPERPETY_IMPL(bool, ProjectionEnabled, EJointConstraintFlags::ProjectionEnabled, JointSettings.bProjectionEnabled);
		//void SetProjectionEnabled(bool bInProjectionEnabled);
		//bool GetProjectionEnabled() const;

		CONSTRAINT_JOINT_PROPERPETY_IMPL(FReal, ParentInvMassScale, EJointConstraintFlags::ParentInvMassScale, JointSettings.ParentInvMassScale);
		//void SetParentInvMassScale(FReal InParentInvMassScale);
		//FReal GetParentInvMassScale() const

		CONSTRAINT_JOINT_PROPERPETY_IMPL(FReal, LinearBreakForce, EJointConstraintFlags::LinearBreakForce, JointSettings.LinearBreakForce);
		//void SetLinearBreakForce(FReal InLinearBreakForce);
		//FReal GetLinearBreakForce() const

		CONSTRAINT_JOINT_PROPERPETY_IMPL(FReal, AngularBreakTorque, EJointConstraintFlags::AngularBreakTorque, JointSettings.AngularBreakTorque);
		//void SetAngularBreakTorque(FReal InAngularBreakTorque);
		//FReal GetAngularBreakTorque() const

		CONSTRAINT_JOINT_PROPERPETY_IMPL(void*, UserData, EJointConstraintFlags::UserData, UserData);
		//void SetUserData(void* InUserData);
		//void* GetUserData() const

		const FData& GetJointSettings()const {return JointSettings; }

	protected:
		class IPhysicsProxyBase* Proxy;

		FJointConstraintDirtyFlags MDirtyFlags;
		FData JointSettings;

		FParticlePair JointParticles;
		FTransformPair JointTransforms;
		void* UserData;

	};






} // Chaos
