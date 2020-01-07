// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Physics/ImmediatePhysics/ImmediatePhysicsChaos/ImmediatePhysicsJointHandle_Chaos.h"
#include "Physics/ImmediatePhysics/ImmediatePhysicsChaos/ImmediatePhysicsActorHandle_Chaos.h"

#include "Chaos/Particle/ParticleUtilities.h"
#include "Chaos/PBDJointConstraints.h"

#include "PhysicsEngine/ConstraintInstance.h"

//#pragma optimize("", off)

static_assert((int32)Chaos::EJointMotionType::Free == (int32)EAngularConstraintMotion::ACM_Free, "Chaos::EJointMotionType and EAngularConstraintMotion mismatch");
static_assert((int32)Chaos::EJointMotionType::Limited == (int32)EAngularConstraintMotion::ACM_Limited, "Chaos::EJointMotionType and EAngularConstraintMotion mismatch");
static_assert((int32)Chaos::EJointMotionType::Locked == (int32)EAngularConstraintMotion::ACM_Locked, "Chaos::EJointMotionType and EAngularConstraintMotion mismatch");

namespace ImmediatePhysics_Chaos
{
	float ChaosImmediate_JointStiffness = 1.0f;
	FAutoConsoleVariableRef CVarJointStiffness(TEXT("p.Chaos.ImmPhys.JointStiffness"), ChaosImmediate_JointStiffness, TEXT("Hard-joint solver stiffness."));

	float ChaosImmediate_DriveStiffnessScale = 0.85f;
	float ChaosImmediate_DriveDampingScale = 1.0f;
	FAutoConsoleVariableRef CVarDriveStiffnessScale(TEXT("p.Chaos.ImmPhys.DriveStiffnessScale"), ChaosImmediate_DriveStiffnessScale, TEXT("Conversion factor for drive stiffness."));
	FAutoConsoleVariableRef CVarDriveDampingScale(TEXT("p.Chaos.ImmPhys.DriveDampingScale"), ChaosImmediate_DriveDampingScale, TEXT("Conversion factor for drive damping."));

	float ChaosImmediate_SoftStiffnessScale = 100000.0f;
	float ChaosImmediate_SoftDampingScale = 100000.0f;
	FAutoConsoleVariableRef CVarSoftStiffnessScale(TEXT("p.Chaos.ImmPhys.SoftStiffnessScale"), ChaosImmediate_SoftStiffnessScale, TEXT("Conversion factor for soft-joint stiffness."));
	FAutoConsoleVariableRef CVarSoftDampingScale(TEXT("p.Chaos.ImmPhys.SoftDampingScale"), ChaosImmediate_SoftDampingScale, TEXT("Conversion factor for soft-joint damping."));

	float ChaosImmediate_JointMinLinearProjection = 0.0f;
	float ChaosImmediate_JointMaxLinearProjection = 0.5f;
	FAutoConsoleVariableRef CVarJointMinLinearProjection(TEXT("p.Chaos.ImmPhys.JointMinLinearProjection"), ChaosImmediate_JointMinLinearProjection, TEXT("Joint min projection (for joints with projection disabled)."));
	FAutoConsoleVariableRef CVarJointMaxLinearProjection(TEXT("p.Chaos.ImmPhys.JointMaxLinearProjection"), ChaosImmediate_JointMaxLinearProjection, TEXT("Joint max projection (for joints with projection enabled)."));

	float ChaosImmediate_JointMinAngularProjection = 0.0f;
	float ChaosImmediate_JointMaxAngularProjection = 0.5f;
	FAutoConsoleVariableRef CVarJointMinAngularProjection(TEXT("p.Chaos.ImmPhys.JointMinAngularProjection"), ChaosImmediate_JointMinAngularProjection, TEXT("Joint min projection (for joints with projection disabled)."));
	FAutoConsoleVariableRef CVarJointMaxAngularProjection(TEXT("p.Chaos.ImmPhys.JointMaxAngularProjection"), ChaosImmediate_JointMaxAngularProjection, TEXT("Joint max projection (for joints with projection enabled)."));

	FJointHandle::FJointHandle(FChaosConstraintContainer* InConstraints, FConstraintInstance* ConstraintInstance, FActorHandle* Actor1, FActorHandle* Actor2)
		: ActorHandles({ Actor1, Actor2 })
		, Constraints(InConstraints)
	{
		using namespace Chaos;

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
		ConstraintSettings.ParentInvMassScale = ConstraintInstance->ProfileInstance.bParentDominates ? (FReal)0 : (FReal)1;

		ConstraintSettings.bSoftLinearLimitsEnabled = ConstraintInstance->GetIsSoftLinearLimit();
		ConstraintSettings.bSoftTwistLimitsEnabled = ConstraintInstance->GetIsSoftTwistLimit();
		ConstraintSettings.bSoftSwingLimitsEnabled = ConstraintInstance->GetIsSoftSwingLimit();
		ConstraintSettings.SoftLinearStiffness = 1.0f;	// @todo(ccaulfield): xpbd soft linear constraints
		ConstraintSettings.SoftLinearDamping = 0.0f;		// @todo(ccaulfield): xpbd soft linear constraints
		ConstraintSettings.SoftTwistStiffness = ChaosImmediate_SoftStiffnessScale * ConstraintInstance->GetSoftTwistLimitStiffness();
		ConstraintSettings.SoftTwistDamping = ChaosImmediate_SoftDampingScale * ConstraintInstance->GetSoftTwistLimitDamping();
		ConstraintSettings.SoftSwingStiffness = ChaosImmediate_SoftStiffnessScale * ConstraintInstance->GetSoftSwingLimitStiffness();
		ConstraintSettings.SoftSwingDamping = ChaosImmediate_SoftDampingScale * ConstraintInstance->GetSoftSwingLimitDamping();

		ConstraintSettings.LinearDriveTarget = ConstraintInstance->ProfileInstance.LinearDrive.PositionTarget;
		ConstraintSettings.bLinearDriveEnabled[0] = ConstraintInstance->ProfileInstance.LinearDrive.XDrive.bEnablePositionDrive;
		ConstraintSettings.bLinearDriveEnabled[1] = ConstraintInstance->ProfileInstance.LinearDrive.YDrive.bEnablePositionDrive;
		ConstraintSettings.bLinearDriveEnabled[2] = ConstraintInstance->ProfileInstance.LinearDrive.ZDrive.bEnablePositionDrive;
		ConstraintSettings.LinearDriveStiffness = 0.3f;// ConstraintInstance->ProfileInstance.LinearDrive.XDrive.bEnablePositionDrive;
		ConstraintSettings.LinearDriveDamping = 0.0f;// ConstraintInstance->ProfileInstance.LinearDrive.XDrive.bEnablePositionDrive;

		ConstraintSettings.AngularDriveTarget = FQuat(ConstraintInstance->ProfileInstance.AngularDrive.OrientationTarget);
		// NOTE: Hard dependence on EJointAngularConstraintIndex - the following will break if we change the order
		ConstraintSettings.AngularDriveTargetAngles[(int32)EJointAngularConstraintIndex::Twist] = FMath::DegreesToRadians(ConstraintInstance->ProfileInstance.AngularDrive.OrientationTarget.Roll);
		ConstraintSettings.AngularDriveTargetAngles[(int32)EJointAngularConstraintIndex::Swing1] = FMath::DegreesToRadians(ConstraintInstance->ProfileInstance.AngularDrive.OrientationTarget.Yaw);
		ConstraintSettings.AngularDriveTargetAngles[(int32)EJointAngularConstraintIndex::Swing2] = FMath::DegreesToRadians(ConstraintInstance->ProfileInstance.AngularDrive.OrientationTarget.Pitch);

		if (ConstraintInstance->ProfileInstance.AngularDrive.AngularDriveMode == EAngularDriveMode::SLERP)
		{
			ConstraintSettings.bAngularSLerpDriveEnabled = ConstraintInstance->ProfileInstance.AngularDrive.SlerpDrive.bEnablePositionDrive;
			ConstraintSettings.bAngularTwistDriveEnabled = false;
			ConstraintSettings.bAngularSwingDriveEnabled = false;
			
		}
		else
		{
			ConstraintSettings.bAngularSLerpDriveEnabled = false;
			ConstraintSettings.bAngularTwistDriveEnabled = ConstraintInstance->ProfileInstance.AngularDrive.TwistDrive.bEnablePositionDrive;
			ConstraintSettings.bAngularSwingDriveEnabled = ConstraintInstance->ProfileInstance.AngularDrive.SwingDrive.bEnablePositionDrive;
		}

		ConstraintSettings.AngularDriveStiffness = ChaosImmediate_DriveStiffnessScale * ConstraintInstance->ProfileInstance.AngularDrive.TwistDrive.Stiffness;
		ConstraintSettings.AngularDriveDamping = ChaosImmediate_DriveDampingScale * ConstraintInstance->ProfileInstance.AngularDrive.TwistDrive.Damping;

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
