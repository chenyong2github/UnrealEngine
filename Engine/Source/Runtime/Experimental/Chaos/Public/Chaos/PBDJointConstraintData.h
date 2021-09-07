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
		JointTransforms             = static_cast<uint64_t>(1) << 0,
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
		using Base = FConstraintBase;
		typedef TVector<FTransform, 2> FTransformPair;

		friend class FPBDRigidsSolver; // friend so we can call ReleaseKinematicEndPoint when unregistering joint.

		FJointConstraint();
		virtual ~FJointConstraint() override {}

		const FPBDJointSettings& GetJointSettings()const { return JointSettings.Read(); }

		// If we created particle to serve as kinematic endpoint, track so we can release later. This will add particle to solver.
		void SetKinematicEndPoint(FSingleParticlePhysicsProxy* InParticle, FPBDRigidsSolver* Solver);

		FSingleParticlePhysicsProxy* GetKinematicEndPoint() const;
		CONSTRAINT_JOINT_PROPERPETY_IMPL2(FTransformPair, JointTransforms, JointSettings, ConnectorTransforms);
		//void SetJointTransforms(const Chaos::FJointConstraint::FTransformPair& InJointTransforms);
		//const FTransformPair GetJointTransforms() const;

		CONSTRAINT_JOINT_PROPERPETY_IMPL2(bool, CollisionEnabled, JointSettings, bCollisionEnabled);
		//void SetCollisionEnabled(bool InValue);
		//bool GetCollisionEnabled() const;

		CONSTRAINT_JOINT_PROPERPETY_IMPL2(bool, ProjectionEnabled, JointSettings, bProjectionEnabled);
		//void SetProjectionEnabled(bool bInProjectionEnabled);
		//bool GetProjectionEnabled() const;

		CONSTRAINT_JOINT_PROPERPETY_IMPL2(FReal, ProjectionLinearAlpha, JointSettings, LinearProjection);
		//void SetProjectionLinearAlpha(FReal InProjectionLinearAlpha);
		//FReal GetProjectionLinearAlpha() const;

		CONSTRAINT_JOINT_PROPERPETY_IMPL2(FReal, ProjectionAngularAlpha, JointSettings, AngularProjection);
		//void SetProjectionAngularAlpha(FReal InProjectionAngularAlpha);
		//FReal GetProjectionAngularAlpha() const;

		CONSTRAINT_JOINT_PROPERPETY_IMPL2(FReal, ParentInvMassScale, JointSettings, ParentInvMassScale);
		//void SetParentInvMassScale(FReal InParentInvMassScale);
		//FReal GetParentInvMassScale() const

		CONSTRAINT_JOINT_PROPERPETY_IMPL2(FReal, LinearBreakForce, JointSettings, LinearBreakForce);
		//void SetLinearBreakForce(FReal InLinearBreakForce);
		//FReal GetLinearBreakForce() const

		CONSTRAINT_JOINT_PROPERPETY_IMPL2(FReal, LinearPlasticityLimit, JointSettings, LinearPlasticityLimit);
		//void SetLinearPlasticityLimit(FReal InLinearPlasticityLimit);
		//FReal GetLinearPlasticityLimit() const

		CONSTRAINT_JOINT_PROPERPETY_IMPL2(EPlasticityType, LinearPlasticityType, JointSettings, LinearPlasticityType);
		//void SetLinearPlasticityType(EPlasticityType InLinearPlasticityType);
		//EPlasticityType GetLinearPlasticityType() const

		CONSTRAINT_JOINT_PROPERPETY_IMPL2(FReal, AngularBreakTorque, JointSettings, AngularBreakTorque);
		//void SetAngularBreakTorque(FReal InAngularBreakTorque);
		//FReal GetAngularBreakTorque() const

		CONSTRAINT_JOINT_PROPERPETY_IMPL2(FReal, AngularPlasticityLimit, JointSettings, AngularPlasticityLimit);
		//void SetAngularPlasticityLimit(FReal InAngularPlasticityLimit);
		//FReal GetAngularPlasticityLimit() const

		CONSTRAINT_JOINT_PROPERPETY_IMPL2(void*, UserData, JointSettings, UserData);
		//void SetUserData(void* InUserData);
		//void* GetUserData() const

		void SetLinearPositionDriveEnabled(TVector<bool, 3> Enabled);

		CONSTRAINT_JOINT_PROPERPETY_IMPL2(bool, LinearPositionDriveXEnabled, JointSettings, bLinearPositionDriveEnabled[0]);
		//void SetLinearPositionDriveXEnabled(bool InLinearPositionDriveXEnabled);
		//bool GetLinearPositionDriveXEnabled() const

		CONSTRAINT_JOINT_PROPERPETY_IMPL2(bool, LinearPositionDriveYEnabled, JointSettings, bLinearPositionDriveEnabled[1]);
		//void SetLinearPositionDriveYEnabled(bool InLinearPositionDriveYEnabled);
		//bool GetLinearPositionDriveYEnabled() const

		CONSTRAINT_JOINT_PROPERPETY_IMPL2(bool, LinearPositionDriveZEnabled, JointSettings, bLinearPositionDriveEnabled[2]);
		//void SetLinearPositionDriveZEnabled(bool InLinearPositionDriveZEnabled);
		//bool GetLinearPositionDriveZEnabled() const

		CONSTRAINT_JOINT_PROPERPETY_IMPL2(FVec3, LinearDrivePositionTarget, JointSettings, LinearDrivePositionTarget);
		//void SetLinearDrivePositionTarget(FVec3 InLinearDrivePositionTarget);
		//FVec3 GetLinearDrivePositionTarget() const

		void SetLinearVelocityDriveEnabled(TVector<bool, 3> Enabled);

		CONSTRAINT_JOINT_PROPERPETY_IMPL2(bool, LinearVelocityDriveXEnabled, JointSettings, bLinearVelocityDriveEnabled[0]);
		//void SetLinearVelocityDriveXEnabled(bool InLinearVelocityDriveXEnabled);
		//bool GetLinearVelocityDriveXEnabled() const

		CONSTRAINT_JOINT_PROPERPETY_IMPL2(bool, LinearVelocityDriveYEnabled, JointSettings, bLinearVelocityDriveEnabled[1]);
		//void SetLinearVelocityDriveYEnabled(bool InLinearVelocityDriveYEnabled);
		//bool GetLinearVelocityDriveYEnabled() const

		CONSTRAINT_JOINT_PROPERPETY_IMPL2(bool, LinearVelocityDriveZEnabled, JointSettings, bLinearVelocityDriveEnabled[2]);
		//void SetLinearVelocityDriveZEnabled(bool InLinearVelocityDriveZEnabled);
		//bool GetLinearVelocityDriveZEnabled() const

		CONSTRAINT_JOINT_PROPERPETY_IMPL2(FVec3, LinearDriveVelocityTarget, JointSettings, LinearDriveVelocityTarget);
		//void SetLinearDriveVelocityTarget(FVec3 InLinearDriveVelocityTarget);
		//FVec3 GetLinearDriveVelocityTarget() const

		CONSTRAINT_JOINT_PROPERPETY_IMPL2(EJointForceMode, LinearDriveForceMode, JointSettings, LinearDriveForceMode);
		//void SetLinearDriveVelocityTarget(EJointForceMode InEJointForceMode);
		//EJointForceMode GetLinearDriveVelocityTarget() const

		CONSTRAINT_JOINT_PROPERPETY_IMPL2(EJointMotionType, LinearMotionTypesX, JointSettings, LinearMotionTypes[0]);
		//void SetLinearMotionTypesX(EJointMotionType InLinearMotionTypesX);
		//EJointMotionType GetLinearMotionTypesX() const

		CONSTRAINT_JOINT_PROPERPETY_IMPL2(EJointMotionType, LinearMotionTypesY, JointSettings, LinearMotionTypes[1]);
		//void SetLinearMotionTypesY(EJointMotionType InLinearMotionTypesY);
		//EJointMotionType GetLinearMotionTypesY() const

		CONSTRAINT_JOINT_PROPERPETY_IMPL2(EJointMotionType, LinearMotionTypesZ, JointSettings, LinearMotionTypes[2]);
		//void SetLinearMotionTypesZ(EJointMotionType InLinearMotionTypesZ);
		//EJointMotionType GetLinearMotionTypesZ() const

		CONSTRAINT_JOINT_PROPERPETY_IMPL2(FReal, LinearDriveStiffness, JointSettings, LinearDriveStiffness);
		//void SetLinearDriveStiffness(FReal InLinearDriveStffness);
		//FReal GetLinearDriveStiffness() const

		CONSTRAINT_JOINT_PROPERPETY_IMPL2(FReal, LinearDriveDamping, JointSettings, LinearDriveDamping);
		//void SetLinearDriveDamping(FReal InLinearDriveStffness);
		//FReal GetLinearDriveDamping() const

		CONSTRAINT_JOINT_PROPERPETY_IMPL2(FReal, ContactTransferScale, JointSettings, ContactTransferScale);
		//void SetContactTransferScale(FReal InContactTransferScale);
		//FReal GetContactTransferScale() const

		CONSTRAINT_JOINT_PROPERPETY_IMPL2(bool, AngularSLerpPositionDriveEnabled, JointSettings, bAngularSLerpPositionDriveEnabled);
		//void SetAngularSLerpPositionDriveEnabled(bool InAngularSLerpPositionDriveEnabled);
		//bool GetAngularSLerpPositionDriveEnabled() const

		CONSTRAINT_JOINT_PROPERPETY_IMPL2(bool, AngularTwistPositionDriveEnabled, JointSettings, bAngularTwistPositionDriveEnabled);
		//void SetAngularTwistPositionDriveEnabled(bool InAngularTwistPositionDriveEnabled);
		//bool GetAngularTwistPositionDriveEnabled() const

		CONSTRAINT_JOINT_PROPERPETY_IMPL2(bool, AngularSwingPositionDriveEnabled, JointSettings, bAngularSwingPositionDriveEnabled);
		//void SetAngularSwingPositionDriveEnabled(bool InAngularSwingPositionDriveEnabled);
		//bool GetAngularSwingPositionDriveEnabled() const

		CONSTRAINT_JOINT_PROPERPETY_IMPL2(FRotation3, AngularDrivePositionTarget, JointSettings, AngularDrivePositionTarget);
		//void SetAngularDrivePositionTarget(FRotation3 InAngularDrivePositionTarget);
		//FRotation3 GetAngularDrivePositionTarget() const

		CONSTRAINT_JOINT_PROPERPETY_IMPL2(bool, AngularSLerpVelocityDriveEnabled, JointSettings, bAngularSLerpVelocityDriveEnabled);
		//void SetAngularSLerpVelocityDriveEnabled(bool InAngularSLerpVelocityDriveEnabled);
		//bool GetAngularSLerpVelocityDriveEnabled() const

		CONSTRAINT_JOINT_PROPERPETY_IMPL2(bool, AngularTwistVelocityDriveEnabled, JointSettings, bAngularTwistVelocityDriveEnabled);
		//void SetAngularTwistVelocityDriveEnabled(bool InAngularTwistVelocityDriveEnabled);
		//bool GetAngularTwistVelocityDriveEnabled() const

		CONSTRAINT_JOINT_PROPERPETY_IMPL2(bool, AngularSwingVelocityDriveEnabled, JointSettings, bAngularSwingVelocityDriveEnabled);
		//void SetAngularSwingVelocityDriveEnabled(bool InAngularSwingVelocityDriveEnabled);
		//bool GetAngularSwingVelocityDriveEnabled() const

		CONSTRAINT_JOINT_PROPERPETY_IMPL2(FVec3, AngularDriveVelocityTarget, JointSettings, AngularDriveVelocityTarget);
		//void SetAngularDriveVelocityTarget(FVec3 InAngularDriveVelocityTarget);
		//FVec3 GetAngularDriveVelocityTarget() const

		CONSTRAINT_JOINT_PROPERPETY_IMPL2(EJointForceMode, AngularDriveForceMode, JointSettings, AngularDriveForceMode);
		//void SetAngularDriveForceMode(EJointForceMode InAngularDriveForceMode);
		//EJointForceMode GetAngularDriveForceMode() const

		CONSTRAINT_JOINT_PROPERPETY_IMPL2(EJointMotionType, AngularMotionTypesX, JointSettings, AngularMotionTypes[0]);
		//void SetAngularMotionTypesX(EJointMotionType InAngularMotionTypesX);
		//EJointMotionType GetAngularMotionTypesX() const

		CONSTRAINT_JOINT_PROPERPETY_IMPL2(EJointMotionType, AngularMotionTypesY, JointSettings, AngularMotionTypes[1]);
		//void SetAngularMotionTypesY(EJointMotionType InAngularMotionTypesY);
		//EJointMotionType GetAngularMotionTypesY() const

		CONSTRAINT_JOINT_PROPERPETY_IMPL2(EJointMotionType, AngularMotionTypesZ, JointSettings, AngularMotionTypes[2]);
		//void SetAngularMotionTypesZ(EJointMotionType AngularMotionTypesZ);
		//EJointMotionType GetAngularMotionTypesZ() const

		CONSTRAINT_JOINT_PROPERPETY_IMPL2(FReal, AngularDriveStiffness, JointSettings, AngularDriveStiffness);
		//void SetAngularDriveStiffness(FReal InAngularDriveStiffness);
		//FReal GetAngularDriveStiffness() const

		CONSTRAINT_JOINT_PROPERPETY_IMPL2(FReal, AngularDriveDamping, JointSettings, AngularDriveDamping);
		//void SetAngularDriveDamping(FReal InAngularDriveDamping);
		//FReal GetAngularDriveDamping() const

		CONSTRAINT_JOINT_PROPERPETY_IMPL2(FReal, Stiffness, JointSettings, Stiffness);
		//void SetStiffness(FReal Stiffness);
		//FReal GetStiffness() const

		CONSTRAINT_JOINT_PROPERPETY_IMPL2(bool, SoftLinearLimitsEnabled, JointSettings, bSoftLinearLimitsEnabled);
		//void SetSoftLinearLimitsEnabled(bool bInSoftLinearLimitsEnabled);
		//bool GetSoftLinearLimitsEnabled() const

		CONSTRAINT_JOINT_PROPERPETY_IMPL2(bool, SoftTwistLimitsEnabled, JointSettings, bSoftTwistLimitsEnabled);
		//void SetSoftTwistLimitsEnabled(FReal bInSoftTwistLimitsEnabled);
		//bool GetSoftTwistLimitsEnabled() const

		CONSTRAINT_JOINT_PROPERPETY_IMPL2(bool, SoftSwingLimitsEnabled, JointSettings, bSoftSwingLimitsEnabled);
		//void SetSoftSwingLimitsEnabled(FReal bInSoftSwingLimitsEnabled);
		//bool GetSoftSwingLimitsEnabled() const

		CONSTRAINT_JOINT_PROPERPETY_IMPL2(EJointForceMode, LinearSoftForceMode, JointSettings, LinearSoftForceMode);
		//void SetLinearSoftForceMode(FRealIn LinearSoftForceMode);
		//EJointForceMode GetLinearSoftForceMode() const

		CONSTRAINT_JOINT_PROPERPETY_IMPL2(EJointForceMode, AngularSoftForceMode, JointSettings, AngularSoftForceMode);
		//void SetAngularSoftForceMode(FReal InAngularSoftForceMode);
		//EJointForceMode GetAngularSoftForceMode() const

		CONSTRAINT_JOINT_PROPERPETY_IMPL2(FReal, SoftLinearStiffness, JointSettings, SoftLinearStiffness);
		//void SetSoftLinearStiffness(FReal InSoftLinearStiffness);
		//FReal GetSoftLinearStiffness() const

		CONSTRAINT_JOINT_PROPERPETY_IMPL2(FReal, SoftLinearDamping, JointSettings, SoftLinearDamping);
		//void SetSoftLinearDamping(FReal InSoftLinearDamping);
		//FReal GetSoftLinearDamping() const

		CONSTRAINT_JOINT_PROPERPETY_IMPL2(FReal, SoftTwistStiffness, JointSettings, SoftTwistStiffness);
		//void SetSoftTwistStiffness(FReal InSoftTwistStiffness);
		//FReal GetSoftTwistStiffness() const

		CONSTRAINT_JOINT_PROPERPETY_IMPL2(FReal, SoftTwistDamping, JointSettings, SoftTwistDamping);
		//void SetSoftTwistDamping(FReal InSoftTwistDamping);
		//FReal GetSoftTwistDamping() const

		CONSTRAINT_JOINT_PROPERPETY_IMPL2(FReal, SoftSwingStiffness, JointSettings, SoftSwingStiffness);
		//void SetSoftSwingStiffness(FReal InSoftSwingStiffness);
		//FReal GetSoftSwingStiffness() const

		CONSTRAINT_JOINT_PROPERPETY_IMPL2(FReal, SoftSwingDamping, JointSettings, SoftSwingDamping);
		//void SetSoftSwingDamping(FReal InSoftSwingDamping);
		//FReal GetSoftSwingDamping() const

		CONSTRAINT_JOINT_PROPERPETY_IMPL2(FReal, LinearLimit, JointSettings, LinearLimit);
		//void SetLinearLimit(FReal InLinearLimit);
		//FReal GetLinearLimit() const

		CONSTRAINT_JOINT_PROPERPETY_IMPL2(FVec3, AngularLimits, JointSettings, AngularLimits);
		//void SetAngularLimits(FVec3 AngularLimits);
		//FVec3 GetAngularLimits() const

		CONSTRAINT_JOINT_PROPERPETY_IMPL2(FReal, LinearContactDistance, JointSettings, LinearContactDistance);
		//void SetLinearContactDistance(FReal InLinearContactDistance);
		//FReal GetLinearContactDistance() const

		CONSTRAINT_JOINT_PROPERPETY_IMPL2(FReal, TwistContactDistance, JointSettings, TwistContactDistance);
		//void SetAngularTwistContactDistance(FReal InAngularTwistContactDistance);
		//FReal GetAngularTwistContactDistance() const

		CONSTRAINT_JOINT_PROPERPETY_IMPL2(FReal, SwingContactDistance, JointSettings, SwingContactDistance);
		//void SetAngularSwingContactDistance(FReal InAngularSwingContactDistance);
		//FReal GetAngularSwingContactDistance() const

		CONSTRAINT_JOINT_PROPERPETY_IMPL2(FReal, LinearRestitution, JointSettings, LinearRestitution);
		//void SetLinearRestitution(FReal InLinearRestitution);
		//FReal GetLinearRestitution() const

		CONSTRAINT_JOINT_PROPERPETY_IMPL2(FReal, TwistRestitution, JointSettings, TwistRestitution);
		//void SetAngularTwistRestitution(FReal InAngularTwistRestitution);
		//FReal GetAngularTwistRestitution() const

		CONSTRAINT_JOINT_PROPERPETY_IMPL2(FReal, SwingRestitution, JointSettings, SwingRestitution);
		//void SetAngularSwingRestitution(FReal InAngularSwingRestitution);
		//FReal GetAngularSwingRestitution() const

		struct FOutputData
		{
			// Output properties
			bool bIsBreaking = false;
			bool bIsBroken = false;
			bool bDriveTargetChanged = false;
			FVector Force = FVector(0);
			FVector Torque = FVector(0);
		};
		FOutputData& GetOutputData() { return Output; }

		virtual void SyncRemoteDataImp(FDirtyPropertiesManager& Manager, int32 DataIdx, FDirtyChaosProperties& RemoteData) override
		{
			Base::SyncRemoteDataImp(Manager, DataIdx, RemoteData);
			JointSettings.SyncRemote(Manager, DataIdx, RemoteData);
		}

	protected:

		void ReleaseKinematicEndPoint(FPBDRigidsSolver* Solver);
		
		TChaosProperty<FPBDJointSettings, EChaosProperty::JointSettings> JointSettings;


		FOutputData Output;

	private:
		// TODO: When we build constraint with only one actor, we spawn this particle to serve as kinematic endpoint
		// to attach to, as Chaos requires two particles currently. This tracks particle that will need to be released with joint.
		FSingleParticlePhysicsProxy* KinematicEndPoint;
	};

} // Chaos



