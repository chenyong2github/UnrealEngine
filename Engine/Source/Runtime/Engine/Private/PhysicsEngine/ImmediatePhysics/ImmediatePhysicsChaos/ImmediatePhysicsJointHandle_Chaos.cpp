// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Physics/ImmediatePhysics/ImmediatePhysicsChaos/ImmediatePhysicsJointHandle_Chaos.h"
#include "Physics/ImmediatePhysics/ImmediatePhysicsChaos/ImmediatePhysicsActorHandle_Chaos.h"

#include "Chaos/Particle/ParticleUtilities.h"
#include "Chaos/PBDJointConstraints.h"

#include "PhysicsEngine/ConstraintInstance.h"

static_assert((int32)Chaos::EJointMotionType::Free == (int32)EAngularConstraintMotion::ACM_Free, "Chaos::EJointMotionType and EAngularConstraintMotion mismatch");
static_assert((int32)Chaos::EJointMotionType::Limited == (int32)EAngularConstraintMotion::ACM_Limited, "Chaos::EJointMotionType and EAngularConstraintMotion mismatch");
static_assert((int32)Chaos::EJointMotionType::Locked == (int32)EAngularConstraintMotion::ACM_Locked, "Chaos::EJointMotionType and EAngularConstraintMotion mismatch");

namespace ImmediatePhysics_Chaos
{
	const float ChaosImmediate_StiffnessDt = 0.03f;

	float ChaosImmediate_JointStiffness = 1.0f;
	FAutoConsoleVariableRef CVarJointStiffness(TEXT("p.Chaos.ImmPhys.JointStiffness"), ChaosImmediate_JointStiffness, TEXT("Joint solver stiffness."));

	bool ChaosImmediate_StiffnessUseMass = true;
	FAutoConsoleVariableRef CVarStiffnessUseMass(TEXT("p.Chaos.ImmPhys.StiffnessUseMass"), ChaosImmediate_StiffnessUseMass, TEXT("Whether to use mass to scale stiffness in the conversion."));

	float ChaosImmediate_JointMinProjection = 0.1f;
	float ChaosImmediate_JointMaxProjection = 1.0f;
	FAutoConsoleVariableRef CVarJointMinProjection(TEXT("p.Chaos.ImmPhys.JointMinProjection"), ChaosImmediate_JointMinProjection, TEXT("Joint min projection (for joints with projection disabled)."));
	FAutoConsoleVariableRef CVarJointMaxProjection(TEXT("p.Chaos.ImmPhys.JointMaxProjection"), ChaosImmediate_JointMaxProjection, TEXT("Joint max projection (for joints with projection enabled)."));

	float ChaosImmediate_DriveStiffnessSourceMin = 100.0f;
	float ChaosImmediate_DriveStiffnessSourceMax = 2000.0f;
	float ChaosImmediate_DriveStiffnessTargetMin = 0.1f;
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
	// Ranges and effective mass use are vcar controlled.
	float ConvertDriveAngularStiffness(const float InStiffness, const Chaos::TVector<float, 3>& InvI0, const Chaos::TVector<float, 3>& InvI1)
	{
		if (ChaosImmediate_DriveStiffnessSourceMax > ChaosImmediate_DriveStiffnessSourceMin)
		{
			const float InvI = ChaosImmediate_StiffnessUseMass ? (InvI0.Min() + InvI1.Min()) : 1.0f;
			const float F = (InStiffness - ChaosImmediate_DriveStiffnessSourceMin) * InvI  / (ChaosImmediate_DriveStiffnessSourceMax - ChaosImmediate_DriveStiffnessSourceMin);
			return FMath::Lerp(ChaosImmediate_DriveStiffnessTargetMin, ChaosImmediate_DriveStiffnessTargetMax, FMath::Clamp(F, 0.0f, 1.0f));
		}
		return 0;
	}

	float ConvertSoftLinearStiffness(const float InStiffness, const float InvM0, const float InvM1)
	{
		if (ChaosImmediate_SoftLinearStiffnessSourceMax > ChaosImmediate_SoftLinearStiffnessSourceMin)
		{
			const float InvM = ChaosImmediate_StiffnessUseMass ? (InvM0 + InvM1) : 1.0f;
			const float F = (InStiffness - ChaosImmediate_SoftLinearStiffnessSourceMin) * InvM / (ChaosImmediate_SoftLinearStiffnessSourceMax - ChaosImmediate_SoftLinearStiffnessSourceMin);
			return FMath::Lerp(ChaosImmediate_SoftLinearStiffnessTargetMin, ChaosImmediate_SoftLinearStiffnessTargetMax, FMath::Clamp(F, 0.0f, 1.0f));
		}
		return 0;
	}

	float ConvertSoftAngularStiffness(const float InStiffness, const Chaos::TVector<float, 3>& InvI0, const Chaos::TVector<float, 3>& InvI1)
	{
		if (ChaosImmediate_SoftAngularStiffnessSourceMax > ChaosImmediate_SoftAngularStiffnessSourceMin)
		{
			const float InvI = ChaosImmediate_StiffnessUseMass ? (InvI0.Min() + InvI1.Min()) : 1.0f;
			const float F = (InStiffness - ChaosImmediate_SoftAngularStiffnessSourceMin) * InvI / (ChaosImmediate_SoftAngularStiffnessSourceMax - ChaosImmediate_SoftAngularStiffnessSourceMin);
			return FMath::Lerp(ChaosImmediate_SoftAngularStiffnessTargetMin, ChaosImmediate_SoftAngularStiffnessTargetMax, FMath::Clamp(F, 0.0f, 1.0f));
		}
		return 0;
	}


	FJointHandle::FJointHandle(FChaosConstraintContainer* InConstraints, FConstraintInstance* ConstraintInstance, FActorHandle* Actor1, FActorHandle* Actor2)
		: ActorHandles({ Actor1, Actor2 })
		, Constraints(InConstraints)
	{
		using namespace Chaos;

		// BodyInstance/PhysX has the constraint locations in actor-space, but we need them in Center-of-Mass space
		// @todo(ccaulfield): support CoM in Chaos particles
		FTransform ConstraintFrame1 = ConstraintInstance->GetRefFrame(EConstraintFrame::Frame1);
		FTransform ConstraintFrame2 = ConstraintInstance->GetRefFrame(EConstraintFrame::Frame2);

		FPBDJointSettings ConstraintSettings;
		ConstraintSettings.ConstraintFrames = 
		{ 
			FParticleUtilities::ActorLocalToParticleLocal(TGenericParticleHandle<FReal, 3>(Actor1->GetParticle()), ConstraintFrame1),
			FParticleUtilities::ActorLocalToParticleLocal(TGenericParticleHandle<FReal, 3>(Actor2->GetParticle()), ConstraintFrame2),
		};
		ConstraintSettings.Motion.Stiffness = ChaosImmediate_JointStiffness;
		ConstraintSettings.Motion.LinearMotionTypes = 
		{
			static_cast<EJointMotionType>(ConstraintInstance->GetLinearXMotion()),
			static_cast<EJointMotionType>(ConstraintInstance->GetLinearYMotion()),
			static_cast<EJointMotionType>(ConstraintInstance->GetLinearZMotion()),
		};
		ConstraintSettings.Motion.LinearLimit = ConstraintInstance->GetLinearLimit();
		ConstraintSettings.Motion.AngularMotionTypes =
		{
			static_cast<EJointMotionType>(ConstraintInstance->GetAngularTwistMotion()),
			static_cast<EJointMotionType>(ConstraintInstance->GetAngularSwing1Motion()),
			static_cast<EJointMotionType>(ConstraintInstance->GetAngularSwing2Motion())
		};
		ConstraintSettings.Motion.AngularLimits =
		{
			FMath::DegreesToRadians(ConstraintInstance->GetAngularTwistLimit()),
			FMath::DegreesToRadians(ConstraintInstance->GetAngularSwing1Limit()),
			FMath::DegreesToRadians(ConstraintInstance->GetAngularSwing2Limit())
		};
		ConstraintSettings.Motion.LinearProjection = ConstraintInstance->IsProjectionEnabled() ? ChaosImmediate_JointMaxProjection : ChaosImmediate_JointMinProjection;
		ConstraintSettings.Motion.AngularProjection = ConstraintInstance->IsProjectionEnabled() ? ChaosImmediate_JointMaxProjection : ChaosImmediate_JointMinProjection;
		ConstraintSettings.Motion.bSoftLinearLimitsEnabled = ConstraintInstance->GetIsSoftLinearLimit();
		ConstraintSettings.Motion.bSoftTwistLimitsEnabled = ConstraintInstance->GetIsSoftTwistLimit();
		ConstraintSettings.Motion.bSoftSwingLimitsEnabled = ConstraintInstance->GetIsSoftSwingLimit();
		ConstraintSettings.Motion.SoftLinearStiffness = ConvertSoftLinearStiffness(ConstraintInstance->GetSoftLinearLimitStiffness(), Actor1->GetInverseMass(), Actor2->GetInverseMass());
		ConstraintSettings.Motion.SoftTwistStiffness = ConvertSoftAngularStiffness(ConstraintInstance->GetSoftTwistLimitStiffness(), Actor1->GetInverseInertia(), Actor2->GetInverseInertia());
		ConstraintSettings.Motion.SoftSwingStiffness = ConvertSoftAngularStiffness(ConstraintInstance->GetSoftSwingLimitStiffness(), Actor1->GetInverseInertia(), Actor2->GetInverseInertia());
		 
		// @todo(ccaulfield): Remove one of these
		ConstraintSettings.Motion.AngularDriveTarget = FQuat(ConstraintInstance->ProfileInstance.AngularDrive.OrientationTarget);
		ConstraintSettings.Motion.AngularDriveTargetAngles = TVector<float, 3>(
			FMath::DegreesToRadians(ConstraintInstance->ProfileInstance.AngularDrive.OrientationTarget.Roll), 
			FMath::DegreesToRadians(ConstraintInstance->ProfileInstance.AngularDrive.OrientationTarget.Pitch),
			FMath::DegreesToRadians(ConstraintInstance->ProfileInstance.AngularDrive.OrientationTarget.Yaw));

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

		ConstraintSettings.Motion.AngularDriveStiffness = ConvertDriveAngularStiffness(ConstraintInstance->ProfileInstance.AngularDrive.TwistDrive.Stiffness, Actor1->GetInverseInertia(), Actor2->GetInverseInertia());

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
