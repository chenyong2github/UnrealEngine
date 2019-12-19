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
	const float ChaosImmediate_StiffnessDt = 0.03f;

	float ChaosImmediate_JointStiffness = 1.0f;
	FAutoConsoleVariableRef CVarJointStiffness(TEXT("p.Chaos.ImmPhys.JointStiffness"), ChaosImmediate_JointStiffness, TEXT("Joint solver stiffness."));

	bool ChaosImmediate_StiffnessUseMass = false;
	FAutoConsoleVariableRef CVarStiffnessUseMass(TEXT("p.Chaos.ImmPhys.StiffnessUseMass"), ChaosImmediate_StiffnessUseMass, TEXT("Whether to use mass to scale stiffness in the conversion."));

	bool ChaosImmediate_StiffnessUseDistance = true;
	FAutoConsoleVariableRef CVarStiffnessUseDistance(TEXT("p.Chaos.ImmPhys.StiffnessUseDistance"), ChaosImmediate_StiffnessUseDistance, TEXT("Whether to use joint connector length to scale stiffness in the conversion."));

	float ChaosImmediate_DriveStiffnessScale = 30.0f;
	FAutoConsoleVariableRef CVarDriveStiffnessScale(TEXT("p.Chaos.ImmPhys.DriveStiffnessScale"), ChaosImmediate_DriveStiffnessScale, TEXT("Conversion factor for drive stiffness."));

	float ChaosImmediate_JointMinLinearProjection = 0.0f;
	float ChaosImmediate_JointMaxLinearProjection = 0.5f;
	FAutoConsoleVariableRef CVarJointMinLinearProjection(TEXT("p.Chaos.ImmPhys.JointMinLinearProjection"), ChaosImmediate_JointMinLinearProjection, TEXT("Joint min projection (for joints with projection disabled)."));
	FAutoConsoleVariableRef CVarJointMaxLinearProjection(TEXT("p.Chaos.ImmPhys.JointMaxLinearProjection"), ChaosImmediate_JointMaxLinearProjection, TEXT("Joint max projection (for joints with projection enabled)."));

	float ChaosImmediate_JointMinAngularProjection = 0.0f;
	float ChaosImmediate_JointMaxAngularProjection = 0.5f;
	FAutoConsoleVariableRef CVarJointMinAngularProjection(TEXT("p.Chaos.ImmPhys.JointMinAngularProjection"), ChaosImmediate_JointMinAngularProjection, TEXT("Joint min projection (for joints with projection disabled)."));
	FAutoConsoleVariableRef CVarJointMaxAngularProjection(TEXT("p.Chaos.ImmPhys.JointMaxAngularProjection"), ChaosImmediate_JointMaxAngularProjection, TEXT("Joint max projection (for joints with projection enabled)."));

	float ChaosImmediate_DriveStiffnessSourceMin = 100.0f;	// PhysX stiffness per inertia that we translate to a Chaos stiffness of ChaosImmediate_DriveStiffnessTargetMin
	float ChaosImmediate_DriveStiffnessSourceMax = 2000.0f;	// PhysX stiffness per inertia that we translate to a Chaos stiffness of ChaosImmediate_DriveStiffnessTargetMax
	float ChaosImmediate_DriveStiffnessTargetMin = 0.6f;
	float ChaosImmediate_DriveStiffnessTargetMax = 1.0f;
	FAutoConsoleVariableRef CVarDriveStiffnessSourceMin(TEXT("p.Chaos.ImmPhys.DriveStiffnessSourceMin"), ChaosImmediate_DriveStiffnessSourceMin, TEXT("Conversion factor for drive stiffness."));
	FAutoConsoleVariableRef CVarDriveStiffnessSourceMax(TEXT("p.Chaos.ImmPhys.DriveStiffnessSourceMax"), ChaosImmediate_DriveStiffnessSourceMax, TEXT("Conversion factor for drive stiffness."));

	float ChaosImmediate_SoftLinearStiffnessSourceMin = 100.0f;
	float ChaosImmediate_SoftLinearStiffnessSourceMax = 500.0f;
	float ChaosImmediate_SoftLinearStiffnessTargetMin = 0.5f;
	float ChaosImmediate_SoftLinearStiffnessTargetMax = 1.0f;
	FAutoConsoleVariableRef CVarSoftLinearStiffnessSourceMin(TEXT("p.Chaos.ImmPhys.SoftyLinearStiffnessSourceMin"), ChaosImmediate_SoftLinearStiffnessSourceMin, TEXT("Conversion factor for soft linear stiffness."));
	FAutoConsoleVariableRef CVarSoftLinearStiffnessSourceMax(TEXT("p.Chaos.ImmPhys.SoftyLinearStiffnessSourceMax"), ChaosImmediate_SoftLinearStiffnessSourceMax, TEXT("Conversion factor for soft linear stiffness."));

	float ChaosImmediate_SoftAngularStiffnessSourceMin = 100.0f;
	float ChaosImmediate_SoftAngularStiffnessSourceMax = 500.0f;
	float ChaosImmediate_SoftAngularStiffnessTargetMin = 0.5f;
	float ChaosImmediate_SoftAngularStiffnessTargetMax = 1.0f;
	FAutoConsoleVariableRef CVarSoftAngularStiffnessSourceMin(TEXT("p.Chaos.ImmPhys.SoftAngularStiffnessSourceMin"), ChaosImmediate_SoftAngularStiffnessSourceMin, TEXT("Conversion factor for soft angular stiffness."));
	FAutoConsoleVariableRef CVarSoftAngularStiffnessSourceMax(TEXT("p.Chaos.ImmPhys.SoftAngularStiffnessSourceMax"), ChaosImmediate_SoftAngularStiffnessSourceMax, TEXT("Conversion factor for soft angular stiffness."));

	// Convert the UE (PhysX) drive spring stiffness to a joint stiffness [0,1] value for use in the solver.
	// We linearly map a range of UE stiffness values to a range of Chaos values, optionally scaling by the effective mass.
	float CalculateDriveAngularStiffnessScale(const Chaos::FVec3& X0, const FReal M0, const Chaos::FVec3& I0, const Chaos::FVec3& X1, const FReal M1, const Chaos::FVec3& I1)
	{
		using namespace Chaos;

		float InvStiffnessScale = (FReal)1.0;
		if (ChaosImmediate_StiffnessUseMass)
		{
			// Calculate inertia of the system about the joint connector using parallel axis theorem
			const FVec3 XI0 = I0 + M0 * (X0 * X0);
			const FVec3 XI1 = I1 + M1 * (X1 * X1);
			InvStiffnessScale = FMath::Max(KINDA_SMALL_NUMBER, XI0.Max() + XI1.Max());
		}
		if (ChaosImmediate_StiffnessUseDistance)
		{
			// Scale stiffness by distance to account for the fact that our rotational drives are applied at
			// the center of mass, not the connector...although maybe that should change
			float Distance0 = (M0 > 0)? X0.Size() : 0;
			float Distance1 = (M1 > 0)? X1.Size() : 0;
			float Distance = FMath::Max(Distance0, Distance1);
			if (Distance > 1)
			{
				InvStiffnessScale = InvStiffnessScale * Distance;
			}
		}

		return ChaosImmediate_DriveStiffnessScale / InvStiffnessScale;
	}


	FJointHandle::FJointHandle(FChaosConstraintContainer* InConstraints, FConstraintInstance* ConstraintInstance, FActorHandle* Actor1, FActorHandle* Actor2)
		: ActorHandles({ Actor1, Actor2 })
		, Constraints(InConstraints)
	{
		using namespace Chaos;

		FPBDJointSettings ConstraintSettings;

		// BodyInstance/PhysX has the constraint locations in actor-space, but we need them in Center-of-Mass space
		FTransform ConstraintFrame1 = FParticleUtilities::ActorLocalToParticleLocal(TGenericParticleHandle<FReal, 3>(Actor1->GetParticle()), ConstraintInstance->GetRefFrame(EConstraintFrame::Frame1));
		FTransform ConstraintFrame2 = FParticleUtilities::ActorLocalToParticleLocal(TGenericParticleHandle<FReal, 3>(Actor2->GetParticle()), ConstraintInstance->GetRefFrame(EConstraintFrame::Frame2));

		// PhysX to Chaos conversions
		const float DriveAngularStiffnessScale = CalculateDriveAngularStiffnessScale(
			ConstraintFrame1.GetTranslation() - Actor1->GetLocalCoMTransform().GetTranslation(),
			Actor1->GetMass(), 
			Actor1->GetInertia(), 
			ConstraintFrame2.GetTranslation() - Actor2->GetLocalCoMTransform().GetTranslation(),
			Actor2->GetMass(), 
			Actor2->GetInertia());

		const float DriveAngularDampingScale = DriveAngularStiffnessScale;

		ConstraintSettings.ConstraintFrames = 
		{ 
			ConstraintFrame1,
			ConstraintFrame2,
		};
		ConstraintSettings.Motion.Stiffness = ChaosImmediate_JointStiffness;

		ConstraintSettings.Motion.LinearMotionTypes = 
		{
			static_cast<EJointMotionType>(ConstraintInstance->GetLinearXMotion()),
			static_cast<EJointMotionType>(ConstraintInstance->GetLinearYMotion()),
			static_cast<EJointMotionType>(ConstraintInstance->GetLinearZMotion()),
		};
		ConstraintSettings.Motion.LinearLimit = ConstraintInstance->GetLinearLimit();

		ConstraintSettings.Motion.AngularMotionTypes[(int32)EJointAngularConstraintIndex::Twist] = static_cast<EJointMotionType>(ConstraintInstance->GetAngularTwistMotion());
		ConstraintSettings.Motion.AngularMotionTypes[(int32)EJointAngularConstraintIndex::Swing1] = static_cast<EJointMotionType>(ConstraintInstance->GetAngularSwing1Motion());
		ConstraintSettings.Motion.AngularMotionTypes[(int32)EJointAngularConstraintIndex::Swing2] = static_cast<EJointMotionType>(ConstraintInstance->GetAngularSwing2Motion());
		ConstraintSettings.Motion.AngularLimits[(int32)EJointAngularConstraintIndex::Twist] = FMath::DegreesToRadians(ConstraintInstance->GetAngularTwistLimit());
		ConstraintSettings.Motion.AngularLimits[(int32)EJointAngularConstraintIndex::Swing1] = FMath::DegreesToRadians(ConstraintInstance->GetAngularSwing1Limit());
		ConstraintSettings.Motion.AngularLimits[(int32)EJointAngularConstraintIndex::Swing2] = FMath::DegreesToRadians(ConstraintInstance->GetAngularSwing2Limit());

		ConstraintSettings.Motion.LinearProjection = ConstraintInstance->IsProjectionEnabled() ? ChaosImmediate_JointMaxLinearProjection : ChaosImmediate_JointMinLinearProjection;
		ConstraintSettings.Motion.AngularProjection = ConstraintInstance->IsProjectionEnabled() ? ChaosImmediate_JointMaxAngularProjection : ChaosImmediate_JointMinAngularProjection;

		ConstraintSettings.Motion.bSoftLinearLimitsEnabled = ConstraintInstance->GetIsSoftLinearLimit();
		ConstraintSettings.Motion.bSoftTwistLimitsEnabled = ConstraintInstance->GetIsSoftTwistLimit();
		ConstraintSettings.Motion.bSoftSwingLimitsEnabled = ConstraintInstance->GetIsSoftSwingLimit();
		ConstraintSettings.Motion.SoftLinearStiffness = 1.0f;	// @todo(ccaulfield): xpbd soft linear constraints
		ConstraintSettings.Motion.SoftLinearDamping = 0.0f;		// @todo(ccaulfield): xpbd soft linear constraints
		ConstraintSettings.Motion.SoftTwistStiffness = DriveAngularStiffnessScale * ConstraintInstance->GetSoftTwistLimitStiffness();
		ConstraintSettings.Motion.SoftTwistDamping = DriveAngularDampingScale * ConstraintInstance->GetSoftTwistLimitDamping();
		ConstraintSettings.Motion.SoftSwingStiffness = DriveAngularStiffnessScale * ConstraintInstance->GetSoftSwingLimitStiffness();
		ConstraintSettings.Motion.SoftSwingDamping = DriveAngularDampingScale * ConstraintInstance->GetSoftSwingLimitDamping();

		ConstraintSettings.Motion.AngularDriveTarget = FQuat(ConstraintInstance->ProfileInstance.AngularDrive.OrientationTarget);
		// NOTE: Hard dependence on EJointAngularConstraintIndex - the following will break if we change the order
		ConstraintSettings.Motion.AngularDriveTargetAngles[(int32)EJointAngularConstraintIndex::Twist] = FMath::DegreesToRadians(ConstraintInstance->ProfileInstance.AngularDrive.OrientationTarget.Roll);
		ConstraintSettings.Motion.AngularDriveTargetAngles[(int32)EJointAngularConstraintIndex::Swing1] = FMath::DegreesToRadians(ConstraintInstance->ProfileInstance.AngularDrive.OrientationTarget.Yaw);
		ConstraintSettings.Motion.AngularDriveTargetAngles[(int32)EJointAngularConstraintIndex::Swing2] = FMath::DegreesToRadians(ConstraintInstance->ProfileInstance.AngularDrive.OrientationTarget.Pitch);

		if (ConstraintInstance->ProfileInstance.AngularDrive.AngularDriveMode == EAngularDriveMode::SLERP)
		{
			ConstraintSettings.Motion.bAngularSLerpDriveEnabled = ConstraintInstance->ProfileInstance.AngularDrive.SlerpDrive.bEnablePositionDrive;
			ConstraintSettings.Motion.bAngularTwistDriveEnabled = false;
			ConstraintSettings.Motion.bAngularSwingDriveEnabled = false;
			
		}
		else
		{
			ConstraintSettings.Motion.bAngularSLerpDriveEnabled = false;
			ConstraintSettings.Motion.bAngularTwistDriveEnabled = ConstraintInstance->ProfileInstance.AngularDrive.TwistDrive.bEnablePositionDrive;
			ConstraintSettings.Motion.bAngularSwingDriveEnabled = ConstraintInstance->ProfileInstance.AngularDrive.SwingDrive.bEnablePositionDrive;
		}

		ConstraintSettings.Motion.AngularDriveStiffness = DriveAngularStiffnessScale * ConstraintInstance->ProfileInstance.AngularDrive.TwistDrive.Stiffness;
		ConstraintSettings.Motion.AngularDriveDamping = DriveAngularDampingScale * ConstraintInstance->ProfileInstance.AngularDrive.TwistDrive.Damping;

		ConstraintSettings.Motion.Sanitize();

		ConstraintHandle = Constraints->AddConstraint({ Actor1->ParticleHandle, Actor2->ParticleHandle }, ConstraintSettings);
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
