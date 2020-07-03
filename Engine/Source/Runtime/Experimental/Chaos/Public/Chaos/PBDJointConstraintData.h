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
		LinearPositionDriveXEnabled = 1 << 7,
		LinearPositionDriveYEnabled = 1 << 8,
		LinearPositionDriveZEnabled = 1 << 9,
		LinearDrivePositionTarget   = 1 << 10,
		LinearVelocityDriveXEnabled = 1 << 11,
		LinearVelocityDriveYEnabled = 1 << 12,
		LinearVelocityDriveZEnabled = 1 << 13,
		LinearDriveVelocityTarget   = 1 << 14,
		LinearDriveForceMode        = 1 << 15,
		LinearMotionTypesX          = 1 << 16,
		LinearMotionTypesY          = 1 << 17,
		LinearMotionTypesZ          = 1 << 18,
		LinearLimit                 = 1 << 19,
		LinearDriveStiffness        = 1 << 20,
		LinearDriveDamping          = 1 << 21,
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

		const FData& GetJointSettings()const { return JointSettings; }

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

		CONSTRAINT_JOINT_PROPERPETY_IMPL(bool, LinearPositionDriveXEnabled, EJointConstraintFlags::LinearPositionDriveXEnabled, JointSettings.bLinearPositionDriveEnabled[0]);
		//void SetLinearPositionDriveXEnabled(bool InLinearPositionDriveXEnabled);
		//bool GetLinearPositionDriveXEnabled() const

		CONSTRAINT_JOINT_PROPERPETY_IMPL(bool, LinearPositionDriveYEnabled, EJointConstraintFlags::LinearPositionDriveYEnabled, JointSettings.bLinearPositionDriveEnabled[1]);
		//void SetLinearPositionDriveYEnabled(bool InLinearPositionDriveYEnabled);
		//bool GetLinearPositionDriveYEnabled() const

		CONSTRAINT_JOINT_PROPERPETY_IMPL(bool, LinearPositionDriveZEnabled, EJointConstraintFlags::LinearPositionDriveZEnabled, JointSettings.bLinearPositionDriveEnabled[2]);
		//void SetLinearPositionDriveZEnabled(bool InLinearPositionDriveZEnabled);
		//bool GetLinearPositionDriveZEnabled() const

		CONSTRAINT_JOINT_PROPERPETY_IMPL(FVec3, LinearDrivePositionTarget, EJointConstraintFlags::LinearDrivePositionTarget, JointSettings.LinearDrivePositionTarget);
		//void SetLinearDrivePositionTarget(FVec3 InLinearDrivePositionTarget);
		//FVec3 GetLinearDrivePositionTarget() const

		CONSTRAINT_JOINT_PROPERPETY_IMPL(bool, LinearVelocityDriveXEnabled, EJointConstraintFlags::LinearVelocityDriveXEnabled, JointSettings.bLinearVelocityDriveEnabled[0]);
		//void SetLinearVelocityDriveXEnabled(bool InLinearVelocityDriveXEnabled);
		//bool GetLinearVelocityDriveXEnabled() const

		CONSTRAINT_JOINT_PROPERPETY_IMPL(bool, LinearVelocityDriveYEnabled, EJointConstraintFlags::LinearVelocityDriveYEnabled, JointSettings.bLinearVelocityDriveEnabled[1]);
		//void SetLinearVelocityDriveYEnabled(bool InLinearVelocityDriveYEnabled);
		//bool GetLinearVelocityDriveYEnabled() const

		CONSTRAINT_JOINT_PROPERPETY_IMPL(bool, LinearVelocityDriveZEnabled, EJointConstraintFlags::LinearVelocityDriveZEnabled, JointSettings.bLinearVelocityDriveEnabled[2]);
		//void SetLinearVelocityDriveZEnabled(bool InLinearVelocityDriveZEnabled);
		//bool GetLinearVelocityDriveZEnabled() const

		CONSTRAINT_JOINT_PROPERPETY_IMPL(FVec3, LinearDriveVelocityTarget, EJointConstraintFlags::LinearDriveVelocityTarget, JointSettings.LinearDriveVelocityTarget);
		//void SetLinearDriveVelocityTarget(FVec3 InLinearDriveVelocityTarget);
		//FVec3 GetLinearDriveVelocityTarget() const

		CONSTRAINT_JOINT_PROPERPETY_IMPL(EJointForceMode, LinearDriveForceMode, EJointConstraintFlags::LinearDriveForceMode, JointSettings.LinearDriveForceMode);
		//void SetLinearDriveVelocityTarget(EJointForceMode InEJointForceMode);
		//EJointForceMode GetLinearDriveVelocityTarget() const

		CONSTRAINT_JOINT_PROPERPETY_IMPL(EJointMotionType, LinearMotionTypesX, EJointConstraintFlags::LinearMotionTypesX, JointSettings.LinearMotionTypes[0]);
		//void SetLinearMotionTypesX(EJointMotionType InLinearMotionTypesX);
		//EJointMotionType GetLinearMotionTypesX() const

		CONSTRAINT_JOINT_PROPERPETY_IMPL(EJointMotionType, LinearMotionTypesY, EJointConstraintFlags::LinearMotionTypesY, JointSettings.LinearMotionTypes[1]);
		//void SetLinearMotionTypesY(EJointMotionType InLinearMotionTypesY);
		//EJointMotionType GetLinearMotionTypesY() const

		CONSTRAINT_JOINT_PROPERPETY_IMPL(EJointMotionType, LinearMotionTypesZ, EJointConstraintFlags::LinearMotionTypesZ, JointSettings.LinearMotionTypes[2]);
		//void SetLinearMotionTypesZ(EJointMotionType InLinearMotionTypesZ);
		//EJointMotionType GetLinearMotionTypesZ() const

		CONSTRAINT_JOINT_PROPERPETY_IMPL(FReal, LinearLimit, EJointConstraintFlags::LinearLimit, JointSettings.LinearLimit);
		//void SeLinearLimit(FReal InLinearLimit);
		//FReal GetLinearLimit() const

		CONSTRAINT_JOINT_PROPERPETY_IMPL(FReal, LinearDriveStiffness, EJointConstraintFlags::LinearDriveStiffness, JointSettings.LinearDriveStiffness);
		//void SetLinearDriveStiffness(FReal InLinearDriveStffness);
		//FReal GetLinearDriveStiffness() const

		CONSTRAINT_JOINT_PROPERPETY_IMPL(FReal, LinearDriveDamping, EJointConstraintFlags::LinearDriveDamping, JointSettings.LinearDriveDamping);
		//void SetLinearDriveDamping(FReal InLinearDriveStffness);
		//FReal GetLinearDriveDamping() const


	protected:
		class IPhysicsProxyBase* Proxy;


		FJointConstraintDirtyFlags MDirtyFlags;
		FData JointSettings;

		FParticlePair JointParticles;
		FTransformPair JointTransforms;
		void* UserData;

	};






} // Chaos
