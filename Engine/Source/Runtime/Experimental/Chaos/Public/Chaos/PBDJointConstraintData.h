// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "Chaos/PBDJointConstraintTypes.h"
#include "Chaos/ParticleDirtyFlags.h"
#include "Chaos/Framework/PhysicsProxyBase.h"
#include "Chaos/Framework/PhysicsSolverBase.h"
#include "Chaos/PBDConstraintBaseData.h"
#include "PhysicsProxy/SingleParticlePhysicsProxyFwd.h"


namespace Chaos
{

	enum class EJointConstraintFlags : uint64_t
	{
		Position                    = 0,
		CollisionEnabled            = static_cast<uint64_t>(1) << 1,
		Projection                  = static_cast<uint64_t>(1) << 2,
		ParentInvMassScale          = static_cast<uint64_t>(1) << 3,
		LinearBreakForce            = static_cast<uint64_t>(1) << 4,
		AngularBreakTorque          = static_cast<uint64_t>(1) << 5,
		UserData                    = static_cast<uint64_t>(1) << 6,
		LinearDrive                 = static_cast<uint64_t>(1) << 7,
		AngularDrive                = static_cast<uint64_t>(1) << 8,
		Stiffness                   = static_cast<uint64_t>(1) << 9,
		Limits                      = static_cast<uint64_t>(1) << 10,

		DummyFlag
	};


	using FJointConstraintDirtyFlags = TDirtyFlags<EJointConstraintFlags>;

	class CHAOS_API FJointConstraint : public FConstraintBase
	{
	public:
		typedef FPBDJointSettings FData;
		typedef FPBDJointConstraintHandle FHandle;
		typedef TVector<FTransform, 2> FTransformPair;
		friend FData;

		template <typename Traits>
		friend class TPBDRigidsSolver; // friend so we can call ReleaseKinematicEndPoint when unregistering joint.

		FJointConstraint();
		virtual ~FJointConstraint() override {}

		bool IsDirty() const { return MDirtyFlags.IsDirty(); }
		bool IsDirty(const EJointConstraintFlags CheckBits) const { return MDirtyFlags.IsDirty(CheckBits); }
		void ClearDirtyFlags() { MDirtyFlags.Clear(); }

		void SetJointTransforms(const Chaos::FJointConstraint::FTransformPair& InJoinTransforms);
		const FTransformPair GetJointTransforms() const;
		FTransformPair GetJointTransforms();

		const FData& GetJointSettings()const { return JointSettings; }

		// If we created particle to serve as kinematic endpoint, track so we can release later. This will add particle to solver.
		void SetKinematicEndPoint(FSingleParticlePhysicsProxy* InParticle, FPBDRigidsSolver* Solver);

		CONSTRAINT_JOINT_PROPERPETY_IMPL(bool, CollisionEnabled, EJointConstraintFlags::CollisionEnabled, JointSettings.bCollisionEnabled);
		//void SetCollisionEnabled(bool InValue);
		//bool GetCollisionEnabled() const;

		CONSTRAINT_JOINT_PROPERPETY_IMPL(bool, ProjectionEnabled, EJointConstraintFlags::Projection, JointSettings.bProjectionEnabled);
		//void SetProjectionEnabled(bool bInProjectionEnabled);
		//bool GetProjectionEnabled() const;

		CONSTRAINT_JOINT_PROPERPETY_IMPL(float, ProjectionLinearAlpha, EJointConstraintFlags::Projection, JointSettings.LinearProjection);
		//void SetProjectionLinearAlpha(float InProjectionLinearAlpha);
		//float GetProjectionLinearAlpha() const;

		CONSTRAINT_JOINT_PROPERPETY_IMPL(float, ProjectionAngularAlpha, EJointConstraintFlags::Projection, JointSettings.AngularProjection);
		//void SetProjectionAngularAlpha(float InProjectionAngularAlpha);
		//float GetProjectionAngularAlpha() const;

		CONSTRAINT_JOINT_PROPERPETY_IMPL(FReal, ParentInvMassScale, EJointConstraintFlags::ParentInvMassScale, JointSettings.ParentInvMassScale);
		//void SetParentInvMassScale(FReal InParentInvMassScale);
		//FReal GetParentInvMassScale() const

		CONSTRAINT_JOINT_PROPERPETY_IMPL(FReal, LinearBreakForce, EJointConstraintFlags::LinearBreakForce, JointSettings.LinearBreakForce);
		//void SetLinearBreakForce(FReal InLinearBreakForce);
		//FReal GetLinearBreakForce() const

		CONSTRAINT_JOINT_PROPERPETY_IMPL(FReal, LinearPlasticityLimit, EJointConstraintFlags::LinearBreakForce, JointSettings.LinearPlasticityLimit);
		//void SetLinearPlasticityLimit(FReal InLinearPlasticityLimit);
		//FReal GetLinearPlasticityLimit() const

		CONSTRAINT_JOINT_PROPERPETY_IMPL(FReal, AngularBreakTorque, EJointConstraintFlags::AngularBreakTorque, JointSettings.AngularBreakTorque);
		//void SetAngularBreakTorque(FReal InAngularBreakTorque);
		//FReal GetAngularBreakTorque() const

		CONSTRAINT_JOINT_PROPERPETY_IMPL(FReal, AngularPlasticityLimit, EJointConstraintFlags::AngularBreakTorque, JointSettings.AngularPlasticityLimit);
		//void SetAngularPlasticityLimit(FReal InAngularPlasticityLimit);
		//FReal GetAngularPlasticityLimit() const

		CONSTRAINT_JOINT_PROPERPETY_IMPL(void*, UserData, EJointConstraintFlags::UserData, UserData);
		//void SetUserData(void* InUserData);
		//void* GetUserData() const

		void SetLinearPositionDriveEnabled( TVector<bool,3> Enabled);
		
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

		void SetLinearVelocityDriveEnabled(TVector<bool,3> Enabled);

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

		CONSTRAINT_JOINT_PROPERPETY_IMPL(FReal, LinearDriveStiffness, EJointConstraintFlags::LinearDrive, JointSettings.LinearDriveStiffness);
		//void SetLinearDriveStiffness(FReal InLinearDriveStffness);
		//FReal GetLinearDriveStiffness() const

		CONSTRAINT_JOINT_PROPERPETY_IMPL(FReal, LinearDriveDamping, EJointConstraintFlags::LinearDrive, JointSettings.LinearDriveDamping);
		//void SetLinearDriveDamping(FReal InLinearDriveStffness);
		//FReal GetLinearDriveDamping() const



		CONSTRAINT_JOINT_PROPERPETY_IMPL(bool, AngularSLerpPositionDriveEnabled, EJointConstraintFlags::AngularDrive, JointSettings.bAngularSLerpPositionDriveEnabled);
		//void SetAngularSLerpPositionDriveEnabled(bool InAngularSLerpPositionDriveEnabled);
		//bool GetAngularSLerpPositionDriveEnabled() const

		CONSTRAINT_JOINT_PROPERPETY_IMPL(bool, AngularTwistPositionDriveEnabled, EJointConstraintFlags::AngularDrive, JointSettings.bAngularTwistPositionDriveEnabled);
		//void SetAngularTwistPositionDriveEnabled(bool InAngularTwistPositionDriveEnabled);
		//bool GetAngularTwistPositionDriveEnabled() const

		CONSTRAINT_JOINT_PROPERPETY_IMPL(bool, AngularSwingPositionDriveEnabled, EJointConstraintFlags::AngularDrive, JointSettings.bAngularSwingPositionDriveEnabled);
		//void SetAngularSwingPositionDriveEnabled(bool InAngularSwingPositionDriveEnabled);
		//bool GetAngularSwingPositionDriveEnabled() const

		CONSTRAINT_JOINT_PROPERPETY_IMPL(FRotation3, AngularDrivePositionTarget, EJointConstraintFlags::AngularDrive, JointSettings.AngularDrivePositionTarget);
		//void SetAngularDrivePositionTarget(FRotation3 InAngularDrivePositionTarget);
		//FRotation3 GetAngularDrivePositionTarget() const

		CONSTRAINT_JOINT_PROPERPETY_IMPL(bool, AngularSLerpVelocityDriveEnabled, EJointConstraintFlags::AngularDrive, JointSettings.bAngularSLerpVelocityDriveEnabled);
		//void SetAngularSLerpVelocityDriveEnabled(bool InAngularSLerpVelocityDriveEnabled);
		//bool GetAngularSLerpVelocityDriveEnabled() const

		CONSTRAINT_JOINT_PROPERPETY_IMPL(bool, AngularTwistVelocityDriveEnabled, EJointConstraintFlags::AngularDrive, JointSettings.bAngularTwistVelocityDriveEnabled);
		//void SetAngularTwistVelocityDriveEnabled(bool InAngularTwistVelocityDriveEnabled);
		//bool GetAngularTwistVelocityDriveEnabled() const

		CONSTRAINT_JOINT_PROPERPETY_IMPL(bool, AngularSwingVelocityDriveEnabled, EJointConstraintFlags::AngularDrive, JointSettings.bAngularSwingVelocityDriveEnabled);
		//void SetAngularSwingVelocityDriveEnabled(bool InAngularSwingVelocityDriveEnabled);
		//bool GetAngularSwingVelocityDriveEnabled() const

		CONSTRAINT_JOINT_PROPERPETY_IMPL(FVec3, AngularDriveVelocityTarget, EJointConstraintFlags::AngularDrive, JointSettings.AngularDriveVelocityTarget);
		//void SetAngularDriveVelocityTarget(FVec3 InAngularDriveVelocityTarget);
		//FVec3 GetAngularDriveVelocityTarget() const

		CONSTRAINT_JOINT_PROPERPETY_IMPL(EJointForceMode, AngularDriveForceMode, EJointConstraintFlags::AngularDrive, JointSettings.AngularDriveForceMode);
		//void SetAngularDriveForceMode(EJointForceMode InAngularDriveForceMode);
		//EJointForceMode GetAngularDriveForceMode() const

		CONSTRAINT_JOINT_PROPERPETY_IMPL(EJointMotionType, AngularMotionTypesX, EJointConstraintFlags::AngularDrive, JointSettings.AngularMotionTypes[0]);
		//void SetAngularMotionTypesX(EJointMotionType InAngularMotionTypesX);
		//EJointMotionType GetAngularMotionTypesX() const

		CONSTRAINT_JOINT_PROPERPETY_IMPL(EJointMotionType, AngularMotionTypesY, EJointConstraintFlags::AngularDrive, JointSettings.AngularMotionTypes[1]);
		//void SetAngularMotionTypesY(EJointMotionType InAngularMotionTypesY);
		//EJointMotionType GetAngularMotionTypesY() const

		CONSTRAINT_JOINT_PROPERPETY_IMPL(EJointMotionType, AngularMotionTypesZ, EJointConstraintFlags::AngularDrive, JointSettings.AngularMotionTypes[2]);
		//void SetAngularMotionTypesZ(EJointMotionType AngularMotionTypesZ);
		//EJointMotionType GetAngularMotionTypesZ() const

		CONSTRAINT_JOINT_PROPERPETY_IMPL(FReal, AngularDriveStiffness, EJointConstraintFlags::AngularDrive, JointSettings.AngularDriveStiffness);
		//void SetAngularDriveStiffness(FReal InAngularDriveStiffness);
		//FReal GetAngularDriveStiffness() const

		CONSTRAINT_JOINT_PROPERPETY_IMPL(FReal, AngularDriveDamping, EJointConstraintFlags::AngularDrive, JointSettings.AngularDriveDamping);
		//void SetAngularDriveDamping(FReal InAngularDriveDamping);
		//FReal GetAngularDriveDamping() const

		CONSTRAINT_JOINT_PROPERPETY_IMPL(FReal, Stiffness, EJointConstraintFlags::Stiffness, JointSettings.Stiffness);
		//void SetStiffness(FReal Stiffness);
		//FReal GetStiffness() const

		CONSTRAINT_JOINT_PROPERPETY_IMPL(bool, SoftLinearLimitsEnabled, EJointConstraintFlags::Limits, JointSettings.bSoftLinearLimitsEnabled);
		//void SetSoftLinearLimitsEnabled(bool bInSoftLinearLimitsEnabled);
		//bool GetSoftLinearLimitsEnabled() const

		CONSTRAINT_JOINT_PROPERPETY_IMPL(bool, SoftTwistLimitsEnabled, EJointConstraintFlags::Limits, JointSettings.bSoftTwistLimitsEnabled);
		//void SetSoftTwistLimitsEnabled(FReal bInSoftTwistLimitsEnabled);
		//bool GetSoftTwistLimitsEnabled() const

		CONSTRAINT_JOINT_PROPERPETY_IMPL(bool, SoftSwingLimitsEnabled, EJointConstraintFlags::Limits, JointSettings.bSoftSwingLimitsEnabled);
		//void SetSoftSwingLimitsEnabled(FReal bInSoftSwingLimitsEnabled);
		//bool GetSoftSwingLimitsEnabled() const

		CONSTRAINT_JOINT_PROPERPETY_IMPL(EJointForceMode, LinearSoftForceMode, EJointConstraintFlags::Limits, JointSettings.LinearSoftForceMode);
		//void SetLinearSoftForceMode(FRealIn LinearSoftForceMode);
		//EJointForceMode GetLinearSoftForceMode() const

		CONSTRAINT_JOINT_PROPERPETY_IMPL(EJointForceMode, AngularSoftForceMode, EJointConstraintFlags::Limits, JointSettings.AngularSoftForceMode);
		//void SetAngularSoftForceMode(FReal InAngularSoftForceMode);
		//EJointForceMode GetAngularSoftForceMode() const

		CONSTRAINT_JOINT_PROPERPETY_IMPL(FReal, SoftLinearStiffness, EJointConstraintFlags::Limits, JointSettings.SoftLinearStiffness);
		//void SetSoftLinearStiffness(FReal InSoftLinearStiffness);
		//FReal GetSoftLinearStiffness() const

		CONSTRAINT_JOINT_PROPERPETY_IMPL(FReal, SoftLinearDamping, EJointConstraintFlags::Limits, JointSettings.SoftLinearDamping);
		//void SetSoftLinearDamping(FReal InSoftLinearDamping);
		//FReal GetSoftLinearDamping() const

		CONSTRAINT_JOINT_PROPERPETY_IMPL(FReal, SoftTwistStiffness, EJointConstraintFlags::Limits, JointSettings.SoftTwistStiffness);
		//void SetSoftTwistStiffness(FReal InSoftTwistStiffness);
		//FReal GetSoftTwistStiffness() const

		CONSTRAINT_JOINT_PROPERPETY_IMPL(FReal, SoftTwistDamping, EJointConstraintFlags::Limits, JointSettings.SoftTwistDamping);
		//void SetSoftTwistDamping(FReal InSoftTwistDamping);
		//FReal GetSoftTwistDamping() const

		CONSTRAINT_JOINT_PROPERPETY_IMPL(FReal, SoftSwingStiffness, EJointConstraintFlags::Limits, JointSettings.SoftSwingStiffness);
		//void SetSoftSwingStiffness(FReal InSoftSwingStiffness);
		//FReal GetSoftSwingStiffness() const

		CONSTRAINT_JOINT_PROPERPETY_IMPL(FReal, SoftSwingDamping, EJointConstraintFlags::Limits, JointSettings.SoftSwingDamping);
		//void SetSoftSwingDamping(FReal InSoftSwingDamping);
		//FReal GetSoftSwingDamping() const

		CONSTRAINT_JOINT_PROPERPETY_IMPL(FReal, LinearLimit, EJointConstraintFlags::Limits, JointSettings.LinearLimit);
		//void SetLinearLimit(FReal InLinearLimit);
		//FReal GetLinearLimit() const

		CONSTRAINT_JOINT_PROPERPETY_IMPL(FVec3, AngularLimits, EJointConstraintFlags::Limits, JointSettings.AngularLimits);
		//void SetAngularLimits(FVec3 AngularLimits);
		//FVec3 GetAngularLimits() const

		CONSTRAINT_JOINT_PROPERPETY_IMPL(FReal, LinearContactDistance, EJointConstraintFlags::Limits, JointSettings.LinearContactDistance);
		//void SetLinearContactDistance(FReal InLinearContactDistance);
		//FReal GetLinearContactDistance() const

		CONSTRAINT_JOINT_PROPERPETY_IMPL(FReal, TwistContactDistance, EJointConstraintFlags::Limits, JointSettings.TwistContactDistance);
		//void SetAngularTwistContactDistance(FReal InAngularTwistContactDistance);
		//FReal GetAngularTwistContactDistance() const

		CONSTRAINT_JOINT_PROPERPETY_IMPL(FReal, SwingContactDistance, EJointConstraintFlags::Limits, JointSettings.SwingContactDistance);
		//void SetAngularSwingContactDistance(FReal InAngularSwingContactDistance);
		//FReal GetAngularSwingContactDistance() const

		CONSTRAINT_JOINT_PROPERPETY_IMPL(FReal, LinearRestitution, EJointConstraintFlags::Limits, JointSettings.LinearRestitution);
		//void SetLinearRestitution(FReal InLinearRestitution);
		//FReal GetLinearRestitution() const

		CONSTRAINT_JOINT_PROPERPETY_IMPL(FReal, TwistRestitution, EJointConstraintFlags::Limits, JointSettings.TwistRestitution);
		//void SetAngularTwistRestitution(FReal InAngularTwistRestitution);
		//FReal GetAngularTwistRestitution() const

		CONSTRAINT_JOINT_PROPERPETY_IMPL(FReal, SwingRestitution, EJointConstraintFlags::Limits, JointSettings.SwingRestitution);
		//void SetAngularSwingRestitution(FReal InAngularSwingRestitution);
		//FReal GetAngularSwingRestitution() const

		struct FOutputData
		{
			// Output properties
			bool bIsBroken = false;
			FVector Force = FVector(0);
			FVector Torque = FVector(0);
		};
		FOutputData& GetOutputData() { return Output; }

	protected:

		template <typename Traits>
		void ReleaseKinematicEndPoint(TPBDRigidsSolver<Traits>* Solver)
		{
			if (KinematicEndPoint)
			{
				Solver->UnregisterObject(KinematicEndPoint);
				KinematicEndPoint = nullptr;
			}
		}

		FJointConstraintDirtyFlags MDirtyFlags;
		FData JointSettings;

		FTransformPair JointTransforms;
		void* UserData;
		FOutputData Output;

	private:
		// TODO: When we build constraint with only one actor, we spawn this particle to serve as kinematic endpoint
		// to attach to, as Chaos requires two particles currently. This tracks particle that will need to be released with joint.
		FSingleParticlePhysicsProxy* KinematicEndPoint;
	};

} // Chaos



