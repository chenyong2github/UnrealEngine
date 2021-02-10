// Copyright Epic Games, Inc. All Rights Reserved.

#include "Core/PBIKConstraint.h"
#include "Core/PBIKSolver.h"
#include "Math/UnrealMathUtility.h"

namespace PBIK
{

	static float ALMOST_ONE = 1.0f - KINDA_SMALL_NUMBER;
	
FJointConstraint::FJointConstraint(FRigidBody* InA, FRigidBody* InB)
{
	A = InA;
	B = InB;

	FVector PinPoint = B->Bone->Position;
	PinPointLocalToA = A->Rotation.Inverse() * (PinPoint - A->Position);
	PinPointLocalToB = B->Rotation.Inverse() * (PinPoint - B->Position);

	XOrig = B->RotationOrig * FVector(1, 0, 0);
	YOrig = B->RotationOrig * FVector(0, 1, 0);
	ZOrig = B->RotationOrig * FVector(0, 0, 1);
}
	
void FJointConstraint::Solve(bool bMoveSubRoots)
{
	// get pos correction to use for rotation
	FVector OffsetA;
	FVector OffsetB;
	FVector Correction = GetPositionCorrection(OffsetA, OffsetB);
	//Correction = Correction * OneOverInvMassSum;

	// apply rotation from correction 
	// at pin point to both bodies
	A->ApplyPushToRotateBody(Correction, OffsetA);//* AInvMass, OffsetA);
	B->ApplyPushToRotateBody(-Correction, OffsetB); //* BInvMass, OffsetB);

	// enforce joint limits
	UpdateJointLimits();

	// calc inv mass of body A
	float AInvMass = 1.0f;
	AInvMass -= A->AttachedEffector ? A->AttachedEffector->StrengthAlpha : 0.0f;
	AInvMass -= !bMoveSubRoots && A->Bone->bIsSubRoot ? 1.0f : 0.0f;
	AInvMass = AInvMass <= 0.0f ? 0.0f : AInvMass;

	// calc inv mass of body B
	float BInvMass = 1.0f;
	BInvMass -= B->AttachedEffector ? B->AttachedEffector->StrengthAlpha : 0.0f;
	BInvMass -= !bMoveSubRoots && B->Bone->bIsSubRoot ? 1.0f : 0.0f;
	BInvMass = BInvMass <= 0.0f ? 0.0f : BInvMass;

	float InvMassSum = AInvMass + BInvMass;
	if (FMath::IsNearlyZero(InvMassSum))
	{
		return; // no correction can be applied on full locked configuration
	}
	float OneOverInvMassSum = 1.0f / InvMassSum;

	// apply positional correction to align pin point on both Bodies
	// NOTE: applying position correction AFTER rotation takes into
	// consideration change in relative pin locations mid-step after
	// bodies have been rotated by ApplyPushToRotateBody
	Correction = GetPositionCorrection(OffsetA, OffsetB);
	Correction = Correction * OneOverInvMassSum;

	A->ApplyPushToPosition(Correction * AInvMass);
	B->ApplyPushToPosition(-Correction * BInvMass);
}

void FJointConstraint::RemoveStretch()
{
	// apply position correction, keeping parent body fixed
	FVector ToA;
	FVector ToB;
	FVector Correction = GetPositionCorrection(ToA, ToB);
	B->Position -= Correction;
}

FVector FJointConstraint::GetPositionCorrection(FVector& OutBodyToA, FVector& OutBodyToB)
{
	OutBodyToA = A->Rotation * PinPointLocalToA;
	OutBodyToB = B->Rotation * PinPointLocalToB;
	FVector PinPointOnA = A->Position + OutBodyToA;
	FVector PinPointOnB = B->Position + OutBodyToB;
	return (PinPointOnB - PinPointOnA);
}

void FJointConstraint::ApplyRotationCorrection(FQuat PureRotA, FQuat PureRotB)
{
	// https://matthias-research.github.io/pages/publications/PBDBodies.pdf/
	// Equation 8 and 9 from "Detailed Rigid Body Simulation with Extended Position Based Dynamics"

	PureRotA.X *= 0.5f;
	PureRotA.Y *= 0.5f;
	PureRotA.Z *= 0.5f;
	PureRotB.X *= 0.5f;
	PureRotB.Y *= 0.5f;
	PureRotB.Z *= 0.5f;
	PureRotA = PureRotA * A->Rotation;
	PureRotB = PureRotB * B->Rotation;

	A->Rotation.X = A->Rotation.X + PureRotA.X;
	A->Rotation.Y = A->Rotation.Y + PureRotA.Y;
	A->Rotation.Z = A->Rotation.Z + PureRotA.Z;
	A->Rotation.W = A->Rotation.W + PureRotA.W;

	B->Rotation.X = B->Rotation.X - PureRotB.X;
	B->Rotation.Y = B->Rotation.Y - PureRotB.Y;
	B->Rotation.Z = B->Rotation.Z - PureRotB.Z;
	B->Rotation.W = B->Rotation.W - PureRotB.W;

	A->Rotation.Normalize();
	B->Rotation.Normalize();
}

void FJointConstraint::UpdateJointLimits()
{
	FBoneSettings& J = B->J;

	// no limits at all
	if (J.X == ELimitType::Free &&
		J.Y == ELimitType::Free &&
		J.Z == ELimitType::Free)
	{
		return;
	}

	// force max angles > min angles
	J.MaxX = J.MaxX < J.MinX ? J.MinX + 1 : J.MaxX;
	J.MaxY = J.MaxY < J.MinY ? J.MinY + 1 : J.MaxY;
	J.MaxZ = J.MaxZ < J.MinZ ? J.MinZ + 1 : J.MaxZ;

	// determine which axes are explicitly or implicitly locked (limited with very small allowable angle)
	bool bLockX = (J.X == ELimitType::Locked) || (J.X == ELimitType::Limited && ((J.MaxX - J.MinX) < 2.0f));
	bool bLockY = (J.Y == ELimitType::Locked) || (J.Y == ELimitType::Limited && ((J.MaxY - J.MinY) < 2.0f));
	bool bLockZ = (J.Z == ELimitType::Locked) || (J.Z == ELimitType::Limited && ((J.MaxZ - J.MinZ) < 2.0f));

	// determine which axes should be treated as a hinge (mutually exclusive)
	bool bXHinge = !bLockX && bLockY && bLockZ;
	bool bYHinge = bLockX && !bLockY && bLockZ;
	bool bZHinge = bLockX && bLockY && !bLockZ;

	// apply hinge corrections
	if (bXHinge)
	{
		UpdateLocalRotateAxes(true, false, false);
		FVector CrossProd = FVector::CrossProduct(XA, XB);
		FQuat PureRotA = FQuat(CrossProd.X, CrossProd.Y, CrossProd.Z, 0.0f);
		ApplyRotationCorrection(PureRotA, PureRotA);
	}
	else if (bYHinge)
	{
		UpdateLocalRotateAxes(false, true, false);
		FVector CrossProd = FVector::CrossProduct(YA, YB);
		FQuat PureRotA = FQuat(CrossProd.X, CrossProd.Y, CrossProd.Z, 0.0f);
		ApplyRotationCorrection(PureRotA, PureRotA);
	}
	else if (bZHinge)
	{
		UpdateLocalRotateAxes(false, false, true);
		FVector CrossProd = FVector::CrossProduct(ZA, ZB);
		FQuat PureRotA = FQuat(CrossProd.X, CrossProd.Y, CrossProd.Z, 0.0f);
		ApplyRotationCorrection(PureRotA, PureRotA);
	}

	if (bLockX || bLockY || bLockZ)
	{
		DecomposeRotationAngles(); // TODO don't strictly need to update everything in here
	}

	if (bLockX)
	{
		RotateWithinLimits(0, 0, AngleX, XA, ZBProjOnX, ZA);
	}

	if (bLockY)
	{
		RotateWithinLimits(0, 0, AngleY, YA, ZBProjOnY, ZA);
	}

	if (bLockZ)
	{
		RotateWithinLimits(0, 0, AngleZ, ZA, YBProjOnZ, YA);
	}

	// enforce min/max angles
	bool bLimitX = J.X == ELimitType::Limited && !bLockX;
	bool bLimitY = J.Y == ELimitType::Limited && !bLockY;
	bool bLimitZ = J.Z == ELimitType::Limited && !bLockZ;
	if (bLimitX || bLimitY || bLimitZ)
	{
		DecomposeRotationAngles();
	}

	if (bLimitX)
	{
		RotateWithinLimits(J.MinX, J.MaxX, AngleX, XA, ZBProjOnX, ZA);
	}

	if (bLimitY)
	{
		RotateWithinLimits(J.MinY, J.MaxY, AngleY, YA, ZBProjOnY, ZA);
	}

	if (bLimitZ)
	{
		RotateWithinLimits(J.MinZ, J.MaxZ, AngleZ, ZA, YBProjOnZ, YA);
	}
}

void FJointConstraint::RotateWithinLimits(
	float MinAngle,
	float MaxAngle,
	float CurrentAngle,
	FVector RotAxis,
	FVector CurVec,
	FVector RefVec)
{
	bool bBeyondMin = CurrentAngle < MinAngle;
	bool bBeyondMax = CurrentAngle > MaxAngle;
	if (bBeyondMin || bBeyondMax)
	{
		float TgtAngle = bBeyondMin ? MinAngle : MaxAngle;
		FQuat TgtRot = FQuat(RotAxis, FMath::DegreesToRadians(TgtAngle));
		FVector TgtVec = TgtRot * RefVec;
		FVector TgtCross = FVector::CrossProduct(TgtVec, CurVec);
		FQuat PureRot = FQuat(TgtCross.X, TgtCross.Y, TgtCross.Z, 0.0f);
		ApplyRotationCorrection(PureRot, PureRot);
	}
}

void FJointConstraint::UpdateLocalRotateAxes(bool bX, bool bY, bool bZ)
{
	FQuat ARot = A->Rotation * A->RotationOrig.Inverse();
	FQuat BRot = B->Rotation * B->RotationOrig.Inverse();

	if (bX)
	{
		XA = ARot * XOrig;
		XB = BRot * XOrig;
	}

	if (bY)
	{
		YA = ARot * YOrig;
		YB = BRot * YOrig;
	}

	if (bZ)
	{
		ZA = ARot * ZOrig;
		ZB = BRot * ZOrig;
	}
}

void FJointConstraint::DecomposeRotationAngles()
{
	FQuat ARot = A->Rotation * A->RotationOrig.Inverse();
	FQuat BRot = B->Rotation * B->RotationOrig.Inverse();

	XA = ARot * XOrig;
	YA = ARot * YOrig;
	ZA = ARot * ZOrig;
	XB = BRot * XOrig;
	YB = BRot * YOrig;
	ZB = BRot * ZOrig;

	ZBProjOnX = FVector::VectorPlaneProject(ZB, XA).GetSafeNormal();
	ZBProjOnY = FVector::VectorPlaneProject(ZB, YA).GetSafeNormal();
	YBProjOnZ = FVector::VectorPlaneProject(YB, ZA).GetSafeNormal();

	AngleX = SignedAngleBetweenNormals(ZA, ZBProjOnX, XA);
	AngleY = SignedAngleBetweenNormals(ZA, ZBProjOnY, YA);
	AngleZ = SignedAngleBetweenNormals(YA, YBProjOnZ, ZA);
}

float FJointConstraint::SignedAngleBetweenNormals(
	const FVector& From, 
	const FVector& To, 
	const FVector& Axis) const
{
	float FromDotTo = FVector::DotProduct(From, To);
	float Angle = FMath::RadiansToDegrees(FMath::Acos(FromDotTo));
	FVector Cross = FVector::CrossProduct(From, To); // TODO may be backwards?
	float Dot = FVector::DotProduct(Cross, Axis);
	return Dot >= 0 ? Angle : -Angle;
}
	
FPinConstraint::FPinConstraint(FRigidBody* InBody, const FVector& InPinPoint)
{
	A = InBody;
	PinPointLocalToA = A->Rotation.Inverse() * (InPinPoint - A->Position);
	GoalPoint = InPinPoint;
}

void FPinConstraint::Solve(bool bMoveSubRoots)
{
	if (!bEnabled || (Alpha <= KINDA_SMALL_NUMBER))
	{
		return;
	}

	if (!bMoveSubRoots && A->Bone->bIsSubRoot)
	{
		return;
	}

	// get positional correction for rotation
	FVector AToPinPoint;
	FVector Correction = GetPositionCorrection(AToPinPoint);

	// rotate body from alignment of pin points
	A->ApplyPushToRotateBody(Correction, AToPinPoint);

	// apply positional correction to Body to align with target (after rotation)
	// (applying directly without considering PositionStiffness because PinConstraints need
	// to precisely pull the attached body to achieve convergence)
	Correction = GetPositionCorrection(AToPinPoint);
	A->Position += Correction; 
}

FVector FPinConstraint::GetPositionCorrection(FVector& OutBodyToPinPoint) const
{
	OutBodyToPinPoint = A->Rotation * PinPointLocalToA;
	FVector PinPoint = A->Position + OutBodyToPinPoint;
	return (GoalPoint - PinPoint) * Alpha;
}

} // namespace

