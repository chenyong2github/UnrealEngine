// Copyright Epic Games, Inc. All Rights Reserved.

#include "Physics/ImmediatePhysics/ImmediatePhysicsChaos/ImmediatePhysicsJointHandle_Chaos.h"
#include "Physics/ImmediatePhysics/ImmediatePhysicsChaos/ImmediatePhysicsActorHandle_Chaos.h"

#include "Chaos/Particle/ParticleUtilities.h"
#include "Chaos/PBDJointConstraints.h"

#include "PhysicsEngine/ConstraintInstance.h"

//#pragma optimize("", off)

static_assert((int32)Chaos::EJointMotionType::Free == (int32)EAngularConstraintMotion::ACM_Free, "Chaos::EJointMotionType and EAngularConstraintMotion mismatch");
static_assert((int32)Chaos::EJointMotionType::Limited == (int32)EAngularConstraintMotion::ACM_Limited, "Chaos::EJointMotionType and EAngularConstraintMotion mismatch");
static_assert((int32)Chaos::EJointMotionType::Locked == (int32)EAngularConstraintMotion::ACM_Locked, "Chaos::EJointMotionType and EAngularConstraintMotion mismatch");

// NOTE: Hard dependence on EJointAngularConstraintIndex - the following will break if we change the order (but can be easily fixed). See FJointHandle::FJointHandle
static_assert((int32)Chaos::EJointAngularConstraintIndex::Twist == 0, "Angular drive targets have hard dependency on constraint order");
static_assert((int32)Chaos::EJointAngularConstraintIndex::Swing1 == 2, "Angular drive targets have hard dependency on constraint order");

namespace ImmediatePhysics_Chaos
{
	float ChaosImmediate_JointStiffness = 1.0f;
	FAutoConsoleVariableRef CVarJointStiffness(TEXT("p.Chaos.ImmPhys.JointStiffness"), ChaosImmediate_JointStiffness, TEXT("Hard-joint solver stiffness."));

	float ChaosImmediate_DriveStiffnessScale = 0.85f;
	float ChaosImmediate_DriveDampingScale = 1.0f;
	FAutoConsoleVariableRef CVarDriveStiffnessScale(TEXT("p.Chaos.ImmPhys.DriveStiffnessScale"), ChaosImmediate_DriveStiffnessScale, TEXT("Conversion factor for drive stiffness."));
	FAutoConsoleVariableRef CVarDriveDampingScale(TEXT("p.Chaos.ImmPhys.DriveDampingScale"), ChaosImmediate_DriveDampingScale, TEXT("Conversion factor for drive damping."));

	float ChaosImmediate_SoftLinearStiffnessScale = 1.75f;
	float ChaosImmediate_SoftLinearDampingScale = 1.75f;
	FAutoConsoleVariableRef CVarSoftLinearStiffnessScale(TEXT("p.Chaos.ImmPhys.SoftLinearStiffnessScale"), ChaosImmediate_SoftLinearStiffnessScale, TEXT("Conversion factor for soft-joint stiffness."));
	FAutoConsoleVariableRef CVarSoftLinearDampingScale(TEXT("p.Chaos.ImmPhys.SoftLinearDampingScale"), ChaosImmediate_SoftLinearDampingScale, TEXT("Conversion factor for soft-joint damping."));

	int ChaosImmediate_SoftLinearForceMode = 0;
	int ChaosImmediate_SoftAngularForceMode = 0;
	FAutoConsoleVariableRef CVarSoftLinearForceMode(TEXT("p.Chaos.ImmPhys.SoftLinearForceMode"), ChaosImmediate_SoftLinearForceMode, TEXT("Soft Linear constraint force mode (0: Acceleration; 1: Force"));
	FAutoConsoleVariableRef CVarSoftAngularForceMode(TEXT("p.Chaos.ImmPhys.SoftAngularForceMode"), ChaosImmediate_SoftAngularForceMode, TEXT("Soft Angular constraint force mode (0: Acceleration; 1: Force"));

	// This value US passed to PhysX was 100000 UE
	// (In PhysX, Soft constraints were implicitly integrated, and essentially equivalent to XPBD, which for large stiffness is just PBD).
	float ChaosImmediate_SoftAngularStiffnessScale = 100000.0f;
	float ChaosImmediate_SoftAngularDampingScale = 100000.0f;
	FAutoConsoleVariableRef CVarSoftAngularStiffnessScale(TEXT("p.Chaos.ImmPhys.SoftAngularStiffnessScale"), ChaosImmediate_SoftAngularStiffnessScale, TEXT("Conversion factor for soft-joint stiffness."));
	FAutoConsoleVariableRef CVarSoftAngularDampingScale(TEXT("p.Chaos.ImmPhys.SoftAngularDampingScale"), ChaosImmediate_SoftAngularDampingScale, TEXT("Conversion factor for soft-joint damping."));

	float ChaosImmediate_JointMinLinearProjection = 0.0f;
	float ChaosImmediate_JointMaxLinearProjection = 0.8f;
	FAutoConsoleVariableRef CVarJointMinLinearProjection(TEXT("p.Chaos.ImmPhys.JointMinLinearProjection"), ChaosImmediate_JointMinLinearProjection, TEXT("Joint min projection (for joints with projection disabled)."));
	FAutoConsoleVariableRef CVarJointMaxLinearProjection(TEXT("p.Chaos.ImmPhys.JointMaxLinearProjection"), ChaosImmediate_JointMaxLinearProjection, TEXT("Joint max projection (for joints with projection enabled)."));

	float ChaosImmediate_JointMinAngularProjection = 0.0f;
	float ChaosImmediate_JointMaxAngularProjection = 0.1f;
	FAutoConsoleVariableRef CVarJointMinAngularProjection(TEXT("p.Chaos.ImmPhys.JointMinAngularProjection"), ChaosImmediate_JointMinAngularProjection, TEXT("Joint min projection (for joints with projection disabled)."));
	FAutoConsoleVariableRef CVarJointMaxAngularProjection(TEXT("p.Chaos.ImmPhys.JointMaxAngularProjection"), ChaosImmediate_JointMaxAngularProjection, TEXT("Joint max projection (for joints with projection enabled)."));

	FJointHandle::FJointHandle(FChaosConstraintContainer* InConstraints, FConstraintInstance* ConstraintInstance, FActorHandle* Actor1, FActorHandle* Actor2)
		: ActorHandles({ Actor1, Actor2 })
		, Constraints(InConstraints)
	{
		using namespace Chaos;

		const FConstraintProfileProperties& Profile = ConstraintInstance->ProfileInstance;

		FPBDJointSettings ConstraintSettings;
		TVector<FRigidTransform3, 2> ConstraintFrames;

		// BodyInstance/PhysX has the constraint locations in actor-space, but we need them in Center-of-Mass space
		ConstraintFrames[0] = FParticleUtilities::ActorLocalToParticleLocal(TGenericParticleHandle<FReal, 3>(Actor1->GetParticle()), ConstraintInstance->GetRefFrame(EConstraintFrame::Frame1));
		ConstraintFrames[1] = FParticleUtilities::ActorLocalToParticleLocal(TGenericParticleHandle<FReal, 3>(Actor2->GetParticle()), ConstraintInstance->GetRefFrame(EConstraintFrame::Frame2));

		ConstraintSettings.Stiffness = ChaosImmediate_JointStiffness;

		ConstraintSettings.LinearMotionTypes = 
		{
			static_cast<EJointMotionType>(ConstraintInstance->GetLinearXMotion()),
			static_cast<EJointMotionType>(ConstraintInstance->GetLinearYMotion()),
			static_cast<EJointMotionType>(ConstraintInstance->GetLinearZMotion()),
		};
		ConstraintSettings.LinearLimit = ConstraintInstance->GetLinearLimit();

		ConstraintSettings.AngularMotionTypes[(int32)EJointAngularConstraintIndex::Twist] = static_cast<EJointMotionType>(ConstraintInstance->GetAngularTwistMotion());
		ConstraintSettings.AngularMotionTypes[(int32)EJointAngularConstraintIndex::Swing1] = static_cast<EJointMotionType>(ConstraintInstance->GetAngularSwing1Motion());
		ConstraintSettings.AngularMotionTypes[(int32)EJointAngularConstraintIndex::Swing2] = static_cast<EJointMotionType>(ConstraintInstance->GetAngularSwing2Motion());
		ConstraintSettings.AngularLimits[(int32)EJointAngularConstraintIndex::Twist] = FMath::DegreesToRadians(ConstraintInstance->GetAngularTwistLimit());
		ConstraintSettings.AngularLimits[(int32)EJointAngularConstraintIndex::Swing1] = FMath::DegreesToRadians(ConstraintInstance->GetAngularSwing1Limit());
		ConstraintSettings.AngularLimits[(int32)EJointAngularConstraintIndex::Swing2] = FMath::DegreesToRadians(ConstraintInstance->GetAngularSwing2Limit());

		ConstraintSettings.LinearProjection = ConstraintInstance->IsProjectionEnabled() ? ChaosImmediate_JointMaxLinearProjection : ChaosImmediate_JointMinLinearProjection;
		ConstraintSettings.AngularProjection = ConstraintInstance->IsProjectionEnabled() ? ChaosImmediate_JointMaxAngularProjection : ChaosImmediate_JointMinAngularProjection;
		ConstraintSettings.ParentInvMassScale = Profile.bParentDominates ? (FReal)0 : (FReal)1;

		ConstraintSettings.bSoftLinearLimitsEnabled = ConstraintInstance->GetIsSoftLinearLimit();
		ConstraintSettings.bSoftTwistLimitsEnabled = ConstraintInstance->GetIsSoftTwistLimit();
		ConstraintSettings.bSoftSwingLimitsEnabled = ConstraintInstance->GetIsSoftSwingLimit();
		ConstraintSettings.SoftLinearStiffness = ChaosImmediate_SoftLinearStiffnessScale * ConstraintInstance->GetSoftLinearLimitStiffness();
		ConstraintSettings.SoftLinearDamping = ChaosImmediate_SoftLinearDampingScale * ConstraintInstance->GetSoftLinearLimitDamping();
		ConstraintSettings.SoftTwistStiffness = ChaosImmediate_SoftAngularStiffnessScale * ConstraintInstance->GetSoftTwistLimitStiffness();
		ConstraintSettings.SoftTwistDamping = ChaosImmediate_SoftAngularDampingScale * ConstraintInstance->GetSoftTwistLimitDamping();
		ConstraintSettings.SoftSwingStiffness = ChaosImmediate_SoftAngularStiffnessScale * ConstraintInstance->GetSoftSwingLimitStiffness();
		ConstraintSettings.SoftSwingDamping = ChaosImmediate_SoftAngularDampingScale * ConstraintInstance->GetSoftSwingLimitDamping();
		ConstraintSettings.LinearSoftForceMode = (ChaosImmediate_SoftLinearForceMode == 0)? EJointForceMode::Acceleration : EJointForceMode::Force;
		ConstraintSettings.AngularSoftForceMode = (ChaosImmediate_SoftAngularForceMode == 0)? EJointForceMode::Acceleration : EJointForceMode::Force;

		ConstraintSettings.LinearDriveTarget = Profile.LinearDrive.PositionTarget;
		ConstraintSettings.bLinearPositionDriveEnabled[0] = Profile.LinearDrive.XDrive.bEnablePositionDrive;
		ConstraintSettings.bLinearPositionDriveEnabled[1] = Profile.LinearDrive.YDrive.bEnablePositionDrive;
		ConstraintSettings.bLinearPositionDriveEnabled[2] = Profile.LinearDrive.ZDrive.bEnablePositionDrive;
		ConstraintSettings.bLinearVelocityDriveEnabled[0] = Profile.LinearDrive.XDrive.bEnableVelocityDrive;
		ConstraintSettings.bLinearVelocityDriveEnabled[1] = Profile.LinearDrive.YDrive.bEnableVelocityDrive;
		ConstraintSettings.bLinearVelocityDriveEnabled[2] = Profile.LinearDrive.ZDrive.bEnableVelocityDrive;
		ConstraintSettings.LinearDriveStiffness = Profile.LinearDrive.XDrive.Stiffness;
		ConstraintSettings.LinearDriveDamping = Profile.LinearDrive.XDrive.Damping;
		ConstraintSettings.LinearDriveForceMode = EJointForceMode::Acceleration;

		ConstraintSettings.AngularDrivePositionTarget = FQuat(Profile.AngularDrive.OrientationTarget);
		ConstraintSettings.AngularDriveVelocityTarget = Profile.AngularDrive.AngularVelocityTarget;
		ConstraintSettings.AngularDriveTargetAngles[(int32)EJointAngularConstraintIndex::Twist] = FMath::DegreesToRadians(Profile.AngularDrive.OrientationTarget.Roll);
		ConstraintSettings.AngularDriveTargetAngles[(int32)EJointAngularConstraintIndex::Swing1] = FMath::DegreesToRadians(Profile.AngularDrive.OrientationTarget.Yaw);
		ConstraintSettings.AngularDriveTargetAngles[(int32)EJointAngularConstraintIndex::Swing2] = FMath::DegreesToRadians(Profile.AngularDrive.OrientationTarget.Pitch);

		if (Profile.AngularDrive.AngularDriveMode == EAngularDriveMode::SLERP)
		{
			ConstraintSettings.bAngularSLerpPositionDriveEnabled = Profile.AngularDrive.SlerpDrive.bEnablePositionDrive;
			ConstraintSettings.bAngularSLerpVelocityDriveEnabled = Profile.AngularDrive.SlerpDrive.bEnableVelocityDrive;
		}
		else
		{
			ConstraintSettings.bAngularTwistPositionDriveEnabled = Profile.AngularDrive.TwistDrive.bEnablePositionDrive;
			ConstraintSettings.bAngularTwistVelocityDriveEnabled = Profile.AngularDrive.TwistDrive.bEnableVelocityDrive;
			ConstraintSettings.bAngularSwingPositionDriveEnabled = Profile.AngularDrive.SwingDrive.bEnablePositionDrive;
			ConstraintSettings.bAngularSwingVelocityDriveEnabled = Profile.AngularDrive.SwingDrive.bEnableVelocityDrive;
		}
		ConstraintSettings.AngularDriveStiffness = ChaosImmediate_DriveStiffnessScale * Profile.AngularDrive.TwistDrive.Stiffness;
		ConstraintSettings.AngularDriveDamping = ChaosImmediate_DriveDampingScale * Profile.AngularDrive.TwistDrive.Damping;
		ConstraintSettings.AngularDriveForceMode = EJointForceMode::Acceleration;

		// UE Disables Soft Limits when the Limit is less than some threshold. This is not necessary in Chaos but for now we also do it for parity's sake (See FLinearConstraint::UpdateLinearLimit_AssumesLocked).
		if (ConstraintSettings.LinearLimit < RB_MinSizeToLockDOF)
		{
			for (int32 AxisIndex = 0; AxisIndex < 3; ++AxisIndex)
			{
				if (ConstraintSettings.LinearMotionTypes[AxisIndex] == EJointMotionType::Limited)
				{
					ConstraintSettings.LinearMotionTypes[AxisIndex] = EJointMotionType::Locked;
				}
			}
		}

		// Disable Soft Limits when stiffness is extremely high
		//const FReal MaxLinearStiffness = 10000;
		//const FReal MaxAngularStiffness = 10000;
		//if (ConstraintSettings.bSoftLinearLimitsEnabled && ConstraintSettings.SoftLinearStiffness > MaxLinearStiffness)
		//{
		//	ConstraintSettings.bSoftLinearLimitsEnabled = false;
		//}
		//if (ConstraintSettings.bSoftTwistLimitsEnabled && ConstraintSettings.SoftTwistStiffness > MaxAngularStiffness)
		//{
		//	ConstraintSettings.bSoftTwistLimitsEnabled = false;
		//}
		//if (ConstraintSettings.bSoftSwingLimitsEnabled && ConstraintSettings.SoftSwingStiffness > MaxAngularStiffness)
		//{
		//	ConstraintSettings.bSoftSwingLimitsEnabled = false;
		//}

		ConstraintSettings.Sanitize();

		ConstraintHandle = Constraints->AddConstraint({ Actor1->ParticleHandle, Actor2->ParticleHandle }, ConstraintFrames, ConstraintSettings);
	}

	FJointHandle::~FJointHandle()
	{
		ConstraintHandle->RemoveConstraint();
	}

	typename FJointHandle::FChaosConstraintHandle* FJointHandle::GetConstraint()
	{
		return ConstraintHandle;
	}
	
	const typename FJointHandle::FChaosConstraintHandle* FJointHandle::GetConstraint() const
	{
		return ConstraintHandle;
	}

	const Chaos::TVector<FActorHandle*, 2>& FJointHandle::GetActorHandles()
	{
		return ActorHandles;
	}

	const Chaos::TVector<const FActorHandle*, 2>& FJointHandle::GetActorHandles() const
	{
		return reinterpret_cast<const Chaos::TVector<const FActorHandle*, 2>&>(ActorHandles);
	}

	void FJointHandle::UpdateLevels()
	{
		ConstraintHandle->SetParticleLevels({ ActorHandles[0]->GetLevel(), ActorHandles[1]->GetLevel() });
	}
}
