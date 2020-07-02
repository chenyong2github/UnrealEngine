// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "Chaos/PBDJointConstraintTypes.h"
#include "Chaos/ParticleDirtyFlags.h"
#include "Chaos/Framework/PhysicsProxyBase.h"
#include "Chaos/Framework/PhysicsSolverBase.h"

namespace Chaos
{

	enum class EJointConstraintFlags : uint64_t
	{
		Position                    = 0,
		CollisionEnabled            = static_cast<uint64_t>(1) << 1,
		ProjectionEnabled           = static_cast<uint64_t>(1) << 2,
		ParentInvMassScale          = static_cast<uint64_t>(1) << 3,
		LinearBreakForce            = static_cast<uint64_t>(1) << 4,
		AngularBreakTorque          = static_cast<uint64_t>(1) << 5,
		UserData                    = static_cast<uint64_t>(1) << 6,
		LinearDrive = static_cast<uint64_t>(1) << 7,
		AngularDrive = static_cast<uint64_t>(1) << 8,
		Stiffness = static_cast<uint64_t>(1) << 9,

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

		CONSTRAINT_JOINT_PROPERPETY_IMPL(bool, LinearPositionDriveXEnabled, EJointConstraintFlags::LinearDrive, JointSettings.bLinearPositionDriveEnabled[0]);
		//void SetLinearPositionDriveXEnabled(bool InLinearPositionDriveXEnabled);
		//bool GetLinearPositionDriveXEnabled() const

		CONSTRAINT_JOINT_PROPERPETY_IMPL(bool, LinearPositionDriveYEnabled, EJointConstraintFlags::LinearDrive, JointSettings.bLinearPositionDriveEnabled[1]);
		//void SetLinearPositionDriveYEnabled(bool InLinearPositionDriveYEnabled);
		//bool GetLinearPositionDriveYEnabled() const

		CONSTRAINT_JOINT_PROPERPETY_IMPL(bool, LinearPositionDriveZEnabled, EJointConstraintFlags::LinearDrive, JointSettings.bLinearPositionDriveEnabled[2]);
		//void SetLinearPositionDriveZEnabled(bool InLinearPositionDriveZEnabled);
		//bool GetLinearPositionDriveZEnabled() const

		CONSTRAINT_JOINT_PROPERPETY_IMPL(FVec3, LinearDrivePositionTarget, EJointConstraintFlags::LinearDrive, JointSettings.LinearDrivePositionTarget);
		//void SetLinearDrivePositionTarget(FVec3 InLinearDrivePositionTarget);
		//FVec3 GetLinearDrivePositionTarget() const

		CONSTRAINT_JOINT_PROPERPETY_IMPL(bool, LinearVelocityDriveXEnabled, EJointConstraintFlags::LinearDrive, JointSettings.bLinearVelocityDriveEnabled[0]);
		//void SetLinearVelocityDriveXEnabled(bool InLinearVelocityDriveXEnabled);
		//bool GetLinearVelocityDriveXEnabled() const

		CONSTRAINT_JOINT_PROPERPETY_IMPL(bool, LinearVelocityDriveYEnabled, EJointConstraintFlags::LinearDrive, JointSettings.bLinearVelocityDriveEnabled[1]);
		//void SetLinearVelocityDriveYEnabled(bool InLinearVelocityDriveYEnabled);
		//bool GetLinearVelocityDriveYEnabled() const

		CONSTRAINT_JOINT_PROPERPETY_IMPL(bool, LinearVelocityDriveZEnabled, EJointConstraintFlags::LinearDrive, JointSettings.bLinearVelocityDriveEnabled[2]);
		//void SetLinearVelocityDriveZEnabled(bool InLinearVelocityDriveZEnabled);
		//bool GetLinearVelocityDriveZEnabled() const

		CONSTRAINT_JOINT_PROPERPETY_IMPL(FVec3, LinearDriveVelocityTarget, EJointConstraintFlags::LinearDrive, JointSettings.LinearDriveVelocityTarget);
		//void SetLinearDriveVelocityTarget(FVec3 InLinearDriveVelocityTarget);
		//FVec3 GetLinearDriveVelocityTarget() const

		CONSTRAINT_JOINT_PROPERPETY_IMPL(EJointForceMode, LinearDriveForceMode, EJointConstraintFlags::LinearDrive, JointSettings.LinearDriveForceMode);
		//void SetLinearDriveVelocityTarget(EJointForceMode InEJointForceMode);
		//EJointForceMode GetLinearDriveVelocityTarget() const

		CONSTRAINT_JOINT_PROPERPETY_IMPL(EJointMotionType, LinearMotionTypesX, EJointConstraintFlags::LinearDrive, JointSettings.LinearMotionTypes[0]);
		//void SetLinearMotionTypesX(EJointMotionType InLinearMotionTypesX);
		//EJointMotionType GetLinearMotionTypesX() const

		CONSTRAINT_JOINT_PROPERPETY_IMPL(EJointMotionType, LinearMotionTypesY, EJointConstraintFlags::LinearDrive, JointSettings.LinearMotionTypes[1]);
		//void SetLinearMotionTypesY(EJointMotionType InLinearMotionTypesY);
		//EJointMotionType GetLinearMotionTypesY() const

		CONSTRAINT_JOINT_PROPERPETY_IMPL(EJointMotionType, LinearMotionTypesZ, EJointConstraintFlags::LinearDrive, JointSettings.LinearMotionTypes[2]);
		//void SetLinearMotionTypesZ(EJointMotionType InLinearMotionTypesZ);
		//EJointMotionType GetLinearMotionTypesZ() const

		CONSTRAINT_JOINT_PROPERPETY_IMPL(FReal, LinearLimit, EJointConstraintFlags::LinearDrive, JointSettings.LinearLimit);
		//void SeLinearLimit(FReal InLinearLimit);
		//FReal GetLinearLimit() const

		CONSTRAINT_JOINT_PROPERPETY_IMPL(FReal, LinearDriveStiffness, EJointConstraintFlags::LinearDrive, JointSettings.LinearDriveStiffness);
		//void SetLinearDriveStiffness(FReal InLinearDriveStffness);
		//FReal GetLinearDriveStiffness() const

		CONSTRAINT_JOINT_PROPERPETY_IMPL(FReal, LinearDriveDamping, EJointConstraintFlags::LinearDrive, JointSettings.LinearDriveDamping);
		//void SetLinearDriveDamping(FReal InLinearDriveStffness);
		//FReal GetLinearDriveDamping() const


		CONSTRAINT_JOINT_PROPERPETY_IMPL(bool, AngularSLerpPositionDriveEnabled, EJointConstraintFlags::AngularDrive, JointSettings.bAngularSLerpPositionDriveEnabled);
		//void SetAngularSLerpPositionDriveEnabled(bool AngularSLerpPositionDriveEnabled);
		//bool GetAngularSLerpPositionDriveEnabled() const

		CONSTRAINT_JOINT_PROPERPETY_IMPL(bool, AngularTwistPositionDriveEnabled, EJointConstraintFlags::AngularDrive, JointSettings.bAngularTwistPositionDriveEnabled);
		//void SetAngularTwistPositionDriveEnabled(bool AngularTwistPositionDriveEnabled);
		//bool GetAngularTwistPositionDriveEnabled() const

		CONSTRAINT_JOINT_PROPERPETY_IMPL(bool, AngularSwingPositionDriveEnabled, EJointConstraintFlags::AngularDrive, JointSettings.bAngularSwingPositionDriveEnabled);
		//void SetAngularSwingPositionDriveEnabled(bool AngularSwingPositionDriveEnabled);
		//bool GetAngularSwingPositionDriveEnabled() const

		CONSTRAINT_JOINT_PROPERPETY_IMPL(FRotation3, AngularDrivePositionTarget, EJointConstraintFlags::AngularDrive, JointSettings.AngularDrivePositionTarget);
		//void SetAngularDrivePositionTarget(FRotation3 AngularDrivePositionTarget);
		//FRotation3 GetAngularDrivePositionTarget() const

		CONSTRAINT_JOINT_PROPERPETY_IMPL(bool, AngularSLerpVelocityDriveEnabled, EJointConstraintFlags::AngularDrive, JointSettings.bAngularSLerpVelocityDriveEnabled);
		//void SetAngularSLerpVelocityDriveEnabled(bool AngularSLerpVelocityDriveEnabled);
		//bool GetAngularSLerpVelocityDriveEnabled() const

		CONSTRAINT_JOINT_PROPERPETY_IMPL(bool, AngularTwistVelocityDriveEnabled, EJointConstraintFlags::AngularDrive, JointSettings.bAngularTwistVelocityDriveEnabled);
		//void SetAngularTwistVelocityDriveEnabled(bool AngularTwistVelocityDriveEnabled);
		//bool GetAngularTwistVelocityDriveEnabled() const

		CONSTRAINT_JOINT_PROPERPETY_IMPL(bool, AngularSwingVelocityDriveEnabled, EJointConstraintFlags::AngularDrive, JointSettings.bAngularSwingVelocityDriveEnabled);
		//void SetAngularSwingVelocityDriveEnabled(bool AngularSwingVelocityDriveEnabled);
		//bool GetAngularSwingVelocityDriveEnabled() const

		CONSTRAINT_JOINT_PROPERPETY_IMPL(FVec3, AngularDriveVelocityTarget, EJointConstraintFlags::AngularDrive, JointSettings.AngularDriveVelocityTarget);
		//void SetAngularDriveVelocityTarget(FVec3 AngularDriveVelocityTarget);
		//FVec3 GetAngularDriveVelocityTarget() const

		CONSTRAINT_JOINT_PROPERPETY_IMPL(EJointForceMode, AngularDriveForceMode, EJointConstraintFlags::AngularDrive, JointSettings.AngularDriveForceMode);
		//void SetAngularDriveForceMode(EJointForceMode AngularDriveForceMode);
		//EJointForceMode GetAngularDriveForceMode() const

		CONSTRAINT_JOINT_PROPERPETY_IMPL(EJointMotionType, AngularMotionTypesX, EJointConstraintFlags::AngularDrive, JointSettings.AngularMotionTypes[0]);
		//void SetAngularMotionTypesX(EJointMotionType AngularMotionTypesX);
		//EJointMotionType GetAngularMotionTypesX() const

		CONSTRAINT_JOINT_PROPERPETY_IMPL(EJointMotionType, AngularMotionTypesY, EJointConstraintFlags::AngularDrive, JointSettings.AngularMotionTypes[1]);
		//void SetAngularMotionTypesY(EJointMotionType AngularMotionTypesY);
		//EJointMotionType GetAngularMotionTypesY() const

		CONSTRAINT_JOINT_PROPERPETY_IMPL(EJointMotionType, AngularMotionTypesZ, EJointConstraintFlags::AngularDrive, JointSettings.AngularMotionTypes[2]);
		//void SetAngularMotionTypesZ(EJointMotionType AngularMotionTypesZ);
		//EJointMotionType GetAngularMotionTypesZ() const

		CONSTRAINT_JOINT_PROPERPETY_IMPL(FVec3, AngularLimits, EJointConstraintFlags::AngularDrive, JointSettings.AngularLimits);
		//void SetAngularLimits(FVec3 AngularLimits);
		//FVec3 GetAngularLimits() const

		CONSTRAINT_JOINT_PROPERPETY_IMPL(FReal, AngularDriveStiffness, EJointConstraintFlags::AngularDrive, JointSettings.AngularDriveStiffness);
		//void SetAngularDriveStiffness(FReal AngularDriveStiffness);
		//FReal GetAngularDriveStiffness() const

		CONSTRAINT_JOINT_PROPERPETY_IMPL(FReal, AngularDriveDamping, EJointConstraintFlags::AngularDrive, JointSettings.AngularDriveDamping);
		//void SetAngularDriveDamping(FReal AngularDriveDamping);
		//FReal GetAngularDriveDamping() const

		CONSTRAINT_JOINT_PROPERPETY_IMPL(FReal, Stiffness, EJointConstraintFlags::Stiffness, JointSettings.Stiffness);
		//void SetStiffness(FReal Stiffness);
		//FReal GetStiffness() const


	protected:
		class IPhysicsProxyBase* Proxy;


		FJointConstraintDirtyFlags MDirtyFlags;
		FData JointSettings;

		FParticlePair JointParticles;
		FTransformPair JointTransforms;
		void* UserData;

	};






} // Chaos
