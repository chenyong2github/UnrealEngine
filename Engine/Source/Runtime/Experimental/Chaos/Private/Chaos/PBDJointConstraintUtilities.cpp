// Copyright Epic Games, Inc. All Rights Reserved.
#include "Chaos/PBDJointConstraintUtilities.h"
#include "Chaos/DenseMatrix.h"
#include "Chaos/ParticleHandle.h"
#include "Chaos/Utilities.h"

//#pragma optimize("", off)

namespace Chaos
{

	void FPBDJointUtilities::DecomposeSwingTwistLocal(const FRotation3& R0, const FRotation3& R1, FRotation3& R01Swing, FRotation3& R01Twist)
	{
		const FRotation3 R01 = R0.Inverse() * R1;
		R01.ToSwingTwistX(R01Swing, R01Twist);
	}

	// @todo(ccaulfield): separate linear soft and stiff
	FReal FPBDJointUtilities::GetLinearStiffness(
		const FPBDJointSolverSettings& SolverSettings,
		const FPBDJointSettings& JointSettings)
	{
		return (SolverSettings.Stiffness > (FReal)0) ? SolverSettings.Stiffness : JointSettings.Stiffness;
	}

	FReal FPBDJointUtilities::GetSoftLinearStiffness(
		const FPBDJointSolverSettings& SolverSettings,
		const FPBDJointSettings& JointSettings)
	{
		return (SolverSettings.SoftLinearStiffness > (FReal)0) ? SolverSettings.SoftLinearStiffness : JointSettings.SoftLinearStiffness;
	}

	FReal FPBDJointUtilities::GetSoftLinearDamping(
		const FPBDJointSolverSettings& SolverSettings,
		const FPBDJointSettings& JointSettings)
	{
		return (SolverSettings.SoftLinearDamping > (FReal)0) ? SolverSettings.SoftLinearDamping : JointSettings.SoftLinearDamping;
	}

	FReal FPBDJointUtilities::GetTwistStiffness(
		const FPBDJointSolverSettings& SolverSettings,
		const FPBDJointSettings& JointSettings)
	{
		return (SolverSettings.Stiffness > (FReal)0) ? SolverSettings.Stiffness : JointSettings.Stiffness;
	}

	FReal FPBDJointUtilities::GetSoftTwistStiffness(
		const FPBDJointSolverSettings& SolverSettings,
		const FPBDJointSettings& JointSettings)
	{
		return (SolverSettings.SoftTwistStiffness > 0)? SolverSettings.SoftTwistStiffness : JointSettings.SoftTwistStiffness;
	}

	FReal FPBDJointUtilities::GetSoftTwistDamping(
		const FPBDJointSolverSettings& SolverSettings,
		const FPBDJointSettings& JointSettings)
	{
		return (SolverSettings.SoftTwistDamping > 0) ? SolverSettings.SoftTwistDamping : JointSettings.SoftTwistDamping;
	}

	FReal FPBDJointUtilities::GetSwingStiffness(
		const FPBDJointSolverSettings& SolverSettings,
		const FPBDJointSettings& JointSettings)
	{
		return (SolverSettings.Stiffness > (FReal)0) ? SolverSettings.Stiffness : JointSettings.Stiffness;
	}

	FReal FPBDJointUtilities::GetSoftSwingStiffness(
		const FPBDJointSolverSettings& SolverSettings,
		const FPBDJointSettings& JointSettings)
	{
		return (SolverSettings.SoftSwingStiffness > 0) ? SolverSettings.SoftSwingStiffness : JointSettings.SoftSwingStiffness;
	}

	FReal FPBDJointUtilities::GetSoftSwingDamping(
		const FPBDJointSolverSettings& SolverSettings,
		const FPBDJointSettings& JointSettings)
	{
		return (SolverSettings.SoftSwingDamping > 0) ? SolverSettings.SoftSwingDamping : JointSettings.SoftSwingDamping;
	}

	FReal FPBDJointUtilities::GetLinearDriveStiffness(
		const FPBDJointSolverSettings& SolverSettings,
		const FPBDJointSettings& JointSettings)
	{
		return (SolverSettings.LinearDriveStiffness > 0.0f) ? SolverSettings.LinearDriveStiffness : JointSettings.LinearDriveStiffness;
	}

	FReal FPBDJointUtilities::GetLinearDriveDamping(
		const FPBDJointSolverSettings& SolverSettings,
		const FPBDJointSettings& JointSettings)
	{
		return (SolverSettings.LinearDriveDamping > 0.0f) ? SolverSettings.LinearDriveDamping : JointSettings.LinearDriveDamping;
	}

	FReal FPBDJointUtilities::GetAngularTwistDriveStiffness(
		const FPBDJointSolverSettings& SolverSettings,
		const FPBDJointSettings& JointSettings)
	{
		if (JointSettings.bAngularTwistPositionDriveEnabled)
		{
			return (SolverSettings.AngularDriveStiffness > 0.0f) ? SolverSettings.AngularDriveStiffness : JointSettings.AngularDriveStiffness;
		}
		return 0.0f;
	}

	FReal FPBDJointUtilities::GetAngularTwistDriveDamping(
		const FPBDJointSolverSettings& SolverSettings,
		const FPBDJointSettings& JointSettings)
	{
		if (JointSettings.bAngularTwistVelocityDriveEnabled)
		{
			return (SolverSettings.AngularDriveDamping > 0.0f) ? SolverSettings.AngularDriveDamping : JointSettings.AngularDriveDamping;
		}
		return 0.0f;
	}

	FReal FPBDJointUtilities::GetAngularSwingDriveStiffness(
		const FPBDJointSolverSettings& SolverSettings,
		const FPBDJointSettings& JointSettings)
	{
		if (JointSettings.bAngularSwingPositionDriveEnabled)
		{
			return (SolverSettings.AngularDriveStiffness > 0.0f) ? SolverSettings.AngularDriveStiffness : JointSettings.AngularDriveStiffness;
		}
		return 0.0f;
	}

	FReal FPBDJointUtilities::GetAngularSwingDriveDamping(
		const FPBDJointSolverSettings& SolverSettings,
		const FPBDJointSettings& JointSettings)
	{
		if (JointSettings.bAngularSwingVelocityDriveEnabled)
		{
			return (SolverSettings.AngularDriveDamping > 0.0f) ? SolverSettings.AngularDriveDamping : JointSettings.AngularDriveDamping;
		}
		return 0.0f;
	}

	FReal FPBDJointUtilities::GetAngularSLerpDriveStiffness(
		const FPBDJointSolverSettings& SolverSettings,
		const FPBDJointSettings& JointSettings)
	{
		if (JointSettings.bAngularSLerpPositionDriveEnabled)
		{
			return (SolverSettings.AngularDriveStiffness > 0.0f) ? SolverSettings.AngularDriveStiffness : JointSettings.AngularDriveStiffness;
		}
		return 0.0f;
	}

	FReal FPBDJointUtilities::GetAngularSLerpDriveDamping(
		const FPBDJointSolverSettings& SolverSettings,
		const FPBDJointSettings& JointSettings)
	{
		if (JointSettings.bAngularSLerpVelocityDriveEnabled)
		{
			return (SolverSettings.AngularDriveDamping > 0.0f) ? SolverSettings.AngularDriveDamping : JointSettings.AngularDriveDamping;
		}
		return 0.0f;
	}

	FReal FPBDJointUtilities::GetLinearProjection(
		const FPBDJointSolverSettings& SolverSettings,
		const FPBDJointSettings& JointSettings)
	{
		return (SolverSettings.LinearProjection > 0.0f) ? SolverSettings.LinearProjection : JointSettings.LinearProjection;
	}

	FReal FPBDJointUtilities::GetAngularProjection(
		const FPBDJointSolverSettings& SolverSettings,
		const FPBDJointSettings& JointSettings)
	{
		return (SolverSettings.AngularProjection > 0.0f) ? SolverSettings.AngularProjection : JointSettings.AngularProjection;
	}

	bool FPBDJointUtilities::GetLinearSoftAccelerationMode(
		const FPBDJointSolverSettings& SolverSettings,
		const FPBDJointSettings& JointSettings)
	{
		return JointSettings.LinearSoftForceMode == EJointForceMode::Acceleration;
	}

	bool FPBDJointUtilities::GetAngularSoftAccelerationMode(
		const FPBDJointSolverSettings& SolverSettings,
		const FPBDJointSettings& JointSettings)
	{
		return JointSettings.AngularSoftForceMode == EJointForceMode::Acceleration;
	}

	bool FPBDJointUtilities::GetDriveAccelerationMode(
		const FPBDJointSolverSettings& SolverSettings,
		const FPBDJointSettings& JointSettings)
	{
		return JointSettings.AngularDriveForceMode == EJointForceMode::Acceleration;
	}


	FVec3 FPBDJointUtilities::ConditionInertia(const FVec3& InI, const FReal MaxRatio)
	{
		FReal IMin = InI.Min();
		FReal IMax = InI.Max();
		if ((MaxRatio > 0) && (IMin > 0))
		{
			FReal Ratio = IMax / IMin;
			if (Ratio > MaxRatio)
			{
				FReal MinIMin = IMax / MaxRatio;
				return FVec3(
					FMath::Lerp(MinIMin, IMax, (InI.X - IMin) / (IMax - IMin)),
					FMath::Lerp(MinIMin, IMax, (InI.Y - IMin) / (IMax - IMin)),
					FMath::Lerp(MinIMin, IMax, (InI.Z - IMin) / (IMax - IMin)));
			}
		}
		return InI;
	}

	
	FVec3 FPBDJointUtilities::ConditionParentInertia(const FVec3& IParent, const FVec3& IChild, const FReal MinRatio)
	{
		if (MinRatio > 0)
		{
			FReal IParentMax = IParent.Max();
			FReal IChildMax = IChild.Max();
			if ((IParentMax > 0) && (IChildMax > 0))
			{
				FReal Ratio = IParentMax / IChildMax;
				if (Ratio < MinRatio)
				{
					FReal Multiplier = MinRatio / Ratio;
					return IParent * Multiplier;
				}
			}
		}
		return IParent;
	}

	
	FReal FPBDJointUtilities::ConditionParentMass(const FReal MParent, const FReal MChild, const FReal MinRatio)
	{
		if ((MinRatio > 0) && (MParent > 0) && (MChild > 0))
		{
			FReal Ratio = MParent / MChild;
			if (Ratio < MinRatio)
			{
				FReal Multiplier = MinRatio / Ratio;
				return MParent * Multiplier;
			}
		}
		return MParent;
	}

	
	void FPBDJointUtilities::GetConditionedInverseMass(
		const FReal InMParent, 
		const FVec3 InIParent, 
		const FReal InMChild, 
		const FVec3 InIChild, 
		FReal& OutInvMParent, 
		FReal& OutInvMChild, 
		FVec3& OutInvIParent,
		FVec3& OutInvIChild,
		const FReal MinParentMassRatio, 
		const FReal MaxInertiaRatio)
	{
		FReal MParent = ConditionParentMass(InMParent, InMChild, MinParentMassRatio);
		FReal MChild = InMChild;

		FVec3 IParent = ConditionInertia(InIParent, MaxInertiaRatio);
		FVec3 IChild = ConditionInertia(InIChild, MaxInertiaRatio);
		IParent = ConditionParentInertia(IParent, IChild, MinParentMassRatio);

		OutInvMParent = 0;
		OutInvIParent = FVec3(0, 0, 0);
		if (MParent > 0)
		{
			OutInvMParent = (FReal)1 / MParent;
			OutInvIParent = FVec3((FReal)1 / IParent.X, (FReal)1 / IParent.Y, (FReal)1 / IParent.Z);
		}

		OutInvMChild = 0;
		OutInvIChild = FVec3(0, 0, 0);
		if (MChild > 0)
		{
			OutInvMChild = (FReal)1 / MChild;
			OutInvIChild = FVec3((FReal)1 / IChild.X, (FReal)1 / IChild.Y, (FReal)1 / IChild.Z);
		}
	}

	
	void FPBDJointUtilities::GetConditionedInverseMass(
		const FReal InM0,
		const FVec3 InI0,
		FReal& OutInvM0, 
		FVec3& OutInvI0,
		const FReal MaxInertiaRatio)
	{
		OutInvM0 = 0;
		OutInvI0 = FVec3(0, 0, 0);
		if (InM0 > 0)
		{
			FVec3 I0 = ConditionInertia(InI0, MaxInertiaRatio);
			OutInvM0 = (FReal)1 / InM0;
			OutInvI0 = FVec3((FReal)1 / I0.X, (FReal)1 / I0.Y, (FReal)1 / I0.Z);
		}
	}


	FVec3 FPBDJointUtilities::GetSphereLimitedPositionError(const FVec3& CX, const FReal Radius)
	{
		FReal CXLen = CX.Size();
		if (CXLen < Radius)
		{
			return FVec3(0, 0, 0);
		}
		else if (CXLen > SMALL_NUMBER)
		{
			FVec3 Dir = CX / CXLen;
			return CX - Radius * Dir;
		}
		return CX;
	}


	FVec3 FPBDJointUtilities::GetCylinderLimitedPositionError(const FVec3& InCX, const FVec3& Axis, const FReal Limit, const EJointMotionType AxisMotion)
	{
		FVec3 CXAxis = FVec3::DotProduct(InCX, Axis) * Axis;
		FVec3 CXPlane = InCX - CXAxis;
		FReal CXPlaneLen = CXPlane.Size();
		if (AxisMotion == EJointMotionType::Free)
		{
			CXAxis = FVec3(0, 0, 0);
		}
		if (CXPlaneLen < Limit)
		{
			CXPlane = FVec3(0, 0, 0);
		}
		else if (CXPlaneLen > KINDA_SMALL_NUMBER)
		{
			FVec3 Dir = CXPlane / CXPlaneLen;
			CXPlane = CXPlane - Limit * Dir;
		}
		return CXAxis + CXPlane;
	}


	FVec3 FPBDJointUtilities::GetLineLimitedPositionError(const FVec3& CX, const FVec3& Axis, const FReal Limit, const EJointMotionType AxisMotion)
	{
		FReal CXDist = FVec3::DotProduct(CX, Axis);
		if ((AxisMotion == EJointMotionType::Free) || (FMath::Abs(CXDist) < Limit))
		{
			return CX - CXDist * Axis;
		}
		else if (CXDist >= Limit)
		{
			return CX - Limit * Axis;
		}
		else
		{
			return CX + Limit * Axis;
		}
	}


	FVec3 FPBDJointUtilities::GetLimitedPositionError(const FPBDJointSettings& JointSettings, const FRotation3& R0, const FVec3& InCX)
	{
		// This function is only used for projection and is only relevant for hard limits.
		// Treat soft-limits as free for error calculation.
		const TVector<EJointMotionType, 3>& Motion =
		{
			((JointSettings.LinearMotionTypes[0] == EJointMotionType::Limited) && JointSettings.bSoftLinearLimitsEnabled) ? EJointMotionType::Free : JointSettings.LinearMotionTypes[0],
			((JointSettings.LinearMotionTypes[1] == EJointMotionType::Limited) && JointSettings.bSoftLinearLimitsEnabled) ? EJointMotionType::Free : JointSettings.LinearMotionTypes[1],
			((JointSettings.LinearMotionTypes[2] == EJointMotionType::Limited) && JointSettings.bSoftLinearLimitsEnabled) ? EJointMotionType::Free : JointSettings.LinearMotionTypes[2],
		};

		if ((Motion[0] == EJointMotionType::Locked) && (Motion[1] == EJointMotionType::Locked) && (Motion[2] == EJointMotionType::Locked))
		{
			return InCX;
		}
		else if ((Motion[0] == EJointMotionType::Limited) && (Motion[1] == EJointMotionType::Limited) && (Motion[2] == EJointMotionType::Limited))
		{
			// Spherical distance constraints
			return GetSphereLimitedPositionError(InCX, JointSettings.LinearLimit);
		}
		else if ((Motion[1] == EJointMotionType::Limited) && (Motion[2] == EJointMotionType::Limited))
		{
			// Circular Limit (X Axis)
			FVec3 Axis = R0 * FVec3(1, 0, 0);
			return GetCylinderLimitedPositionError(InCX, Axis, JointSettings.LinearLimit, Motion[0]);
		}
		else if ((Motion[0] == EJointMotionType::Limited) && (Motion[2] == EJointMotionType::Limited))
		{
			// Circular Limit (Y Axis)
			FVec3 Axis = R0 * FVec3(0, 1, 0);
			return GetCylinderLimitedPositionError(InCX, Axis, JointSettings.LinearLimit, Motion[1]);
		}
		else if ((Motion[0] == EJointMotionType::Limited) && (Motion[1] == EJointMotionType::Limited))
		{
			// Circular Limit (Z Axis)
			FVec3 Axis = R0 * FVec3(0, 0, 1);
			return GetCylinderLimitedPositionError(InCX, Axis, JointSettings.LinearLimit, Motion[2]);
		}
		else
		{
			// Line/Square/Cube Limits (no way to author square or cube limits, but would work if we wanted it)
			FVec3 CX = InCX;
			if (Motion[0] != EJointMotionType::Locked)
			{
				FVec3 Axis = R0 * FVec3(1, 0, 0);
				CX = GetLineLimitedPositionError(CX, Axis, JointSettings.LinearLimit, Motion[0]);
			}
			if (Motion[1] != EJointMotionType::Locked)
			{
				FVec3 Axis = R0 * FVec3(0, 1, 0);
				CX = GetLineLimitedPositionError(CX, Axis, JointSettings.LinearLimit, Motion[1]);
			}
			if (Motion[2] != EJointMotionType::Locked)
			{
				FVec3 Axis = R0 * FVec3(0, 0, 1);
				CX = GetLineLimitedPositionError(CX, Axis, JointSettings.LinearLimit, Motion[2]);
			}
			return CX;
		}
	}
}
