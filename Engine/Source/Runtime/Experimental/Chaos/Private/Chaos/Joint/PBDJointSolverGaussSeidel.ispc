// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#define EXPLICIT_VECTOR4 1

#include "Math/Vector.isph"
#include "Math/Quat.isph"

struct FRigidTransform
{
	FVector4 Rotation;
	FVector Translation;
	FVector Scale3D;
};

static const uniform struct FMatrix Matrix33Zero = {{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1}};

static const uniform int32 MaxConstrainedBodies = 2;

struct FJointSolverGaussSeidel
{
	// Local-space constraint settings
	FRigidTransform XLs[MaxConstrainedBodies];	// Local-space joint connector transforms
	FVector InvILs[MaxConstrainedBodies];		// Local-space inverse inertias
	float InvMs[MaxConstrainedBodies];			// Inverse masses

	// World-space constraint state
	FVector Xs[MaxConstrainedBodies];			// World-space joint connector positions
	FVector4 Rs[MaxConstrainedBodies];			// World-space joint connector rotations

	// World-space body state
	FVector Ps[MaxConstrainedBodies];			// World-space particle CoM positions
	FVector4 Qs[MaxConstrainedBodies];			// World-space particle CoM rotations
	FVector Vs[MaxConstrainedBodies];			// World-space particle CoM velocities
	FVector Ws[MaxConstrainedBodies];			// World-space particle CoM angular velocities

	// Accumulated world-body state deltas
	FVector DPs[MaxConstrainedBodies];			// World-space particle CoM positions
	FVector DRs[MaxConstrainedBodies];			// World-space particle CoM rotations (in axis-angle form)
	FVector DVs[MaxConstrainedBodies];			// World-space particle CoM velocities
	FVector DWs[MaxConstrainedBodies];			// World-space particle CoM angular velocities

	// XPBD Previous iteration world-space body state
	FVector PrevPs[MaxConstrainedBodies];		// World-space particle CoM positions
	FVector4 PrevQs[MaxConstrainedBodies];		// World-space particle CoM rotations
	FVector PrevXs[MaxConstrainedBodies];		// World-space joint connector positions

	// XPBD constraint multipliers (net applied constraint-space deltas)
	float LinearSoftLambda;
	float LinearDriveLambda;
	float TwistSoftLambda;
	float SwingSoftLambda;
	float TwistDriveLambda;
	float SwingDriveLambda;

	// Post-angular constraint position fixup (to reduce iterations required for stiff angular constraints)
	float AngularPositionCorrection;
};

static inline uniform FMatrix SetMatrix33(
	const uniform float x00, const uniform float x10, const uniform float x20, 
	const uniform float x01, const uniform float x11, const uniform float x21, 
	const uniform float x02, const uniform float x12, const uniform float x22)
{
	return SetMatrix(SetVector4(x00, x01, x02, 0), SetVector4(x10, x11, x12, 0), SetVector4(x20, x21, x22, 0), Float0001);
}

static inline uniform FMatrix SetMatrix33(
	const uniform float x00, const uniform float x10, const uniform float x20, 
	const uniform float x11, const uniform float x21, const uniform float x22)
{
	return SetMatrix(SetVector4(x00, x10, x20, 0), SetVector4(x10, x11, x21, 0), SetVector4(x20, x21, x22, 0), Float0001);
}

static inline uniform FMatrix SetMatrix33(
	const uniform float x00, const uniform float x11, const uniform float x22)
{
	return SetMatrix(SetVector4(x00, 0, 0, 0), SetVector4(0, x11, 0, 0), SetVector4(0, 0, x22, 0), Float0001);
}

static inline uniform FMatrix SetMatrix33(const uniform FVector4& x0, const uniform FVector4& x1, const uniform FVector4& x2)
{
	return SetMatrix(x0, x1, x2, Float0001);
}

static inline uniform FMatrix MultiplyAB(const uniform FMatrix& L, const uniform FMatrix& R)
{
	const uniform FVector4 L0 = *((uniform FVector4 *uniform)&L.M[0]);
	const uniform FVector4 L1 = *((uniform FVector4 *uniform)&L.M[4]);
	const uniform FVector4 L2 = *((uniform FVector4 *uniform)&L.M[8]);

	const uniform FVector4 x0 = SetVector4(R.M[0]) * L0 + SetVector4(R.M[1]) * L1 + SetVector4(R.M[2]) * L2;
	const uniform FVector4 x1 = SetVector4(R.M[4]) * L0 + SetVector4(R.M[5]) * L1 + SetVector4(R.M[6]) * L2;
	const uniform FVector4 x2 = SetVector4(R.M[8]) * L0 + SetVector4(R.M[9]) * L1 + SetVector4(R.M[10]) * L2;

	return SetMatrix33(x0, x1, x2);
}

static inline uniform FMatrix MultiplyABt(const uniform FMatrix& L, const uniform FMatrix& R)
{
	const uniform FVector4 L0 = *((uniform FVector4 *uniform)&L.M[0]);
	const uniform FVector4 L1 = *((uniform FVector4 *uniform)&L.M[4]);
	const uniform FVector4 L2 = *((uniform FVector4 *uniform)&L.M[8]);

	const uniform FVector4 x0 = SetVector4(R.M[0]) * L0 + SetVector4(R.M[4]) * L1 + SetVector4(R.M[8]) * L2;
	const uniform FVector4 x1 = SetVector4(R.M[1]) * L0 + SetVector4(R.M[5]) * L1 + SetVector4(R.M[9]) * L2;
	const uniform FVector4 x2 = SetVector4(R.M[2]) * L0 + SetVector4(R.M[6]) * L1 + SetVector4(R.M[10]) * L2;

	return SetMatrix33(x0, x1, x2);
}

static inline uniform FVector Multiply(const uniform FMatrix& LIn, const uniform FVector& R)
{
	const uniform FVector4 L0 = *((uniform FVector4 *uniform)&LIn.M[0]);
	const uniform FVector4 L1 = *((uniform FVector4 *uniform)&LIn.M[4]);
	const uniform FVector4 L2 = *((uniform FVector4 *uniform)&LIn.M[8]);

	const uniform FVector4 Result = L0 * SetVector4(R.V[0]) + L1 * SetVector4(R.V[1]) + L2 * SetVector4(R.V[2]);

	return SetVector(Result.V[0], Result.V[1], Result.V[2]);
}

static inline uniform FMatrix AddAB(const uniform FMatrix& L, const uniform FMatrix& R)
{
	const uniform FVector4 *uniform A	= (const uniform FVector4 *uniform) &L;
	const uniform FVector4 *uniform B	= (const uniform FVector4 *uniform) &R;

	return SetMatrix33(A[0] + B[0], A[1] + B[1], A[2] + B[2]);
}

export uniform int32 SizeofFJointSolverGaussSeidel()
{
	return sizeof(uniform FJointSolverGaussSeidel);
}

static inline void UpdateDerivedStateInner(uniform FJointSolverGaussSeidel * uniform M)
{
	M->Xs[0] = M->Ps[0] + VectorQuaternionRotateVector(M->Qs[0], M->XLs[0].Translation);
	M->Xs[1] = M->Ps[1] + VectorQuaternionRotateVector(M->Qs[1], M->XLs[1].Translation);
	M->Rs[0] = VectorQuaternionMultiply2(M->Qs[0], M->XLs[0].Rotation);
	M->Rs[1] = VectorQuaternionMultiply2(M->Qs[1], M->XLs[1].Rotation);
}

export void UpdateDerivedState(uniform FJointSolverGaussSeidel * uniform M)
{
	UpdateDerivedStateInner(M);
}

static inline void ApplyPositionDeltaInner(
	uniform FJointSolverGaussSeidel * uniform M,
	const uniform float Stiffness,
	const uniform FVector& DP0,
	const uniform FVector& DP1)
{
	M->Ps[0] = M->Ps[0] + (Stiffness * DP0);
	M->Ps[1] = M->Ps[1] + (Stiffness * DP1);
}

export void ApplyPositionDelta(
	uniform FJointSolverGaussSeidel * uniform M,
	const uniform float Stiffness,
	const uniform FVector& DP0,
	const uniform FVector& DP1)
{
	ApplyPositionDeltaInner(M, Stiffness, DP0, DP1);
}

static inline void ApplyRotationDeltaInner(
	uniform FJointSolverGaussSeidel * uniform M,
	const uniform float Stiffness,
	const uniform FVector& DR0,
	const uniform FVector& DR1)
{
	const uniform FVector4 Qs0 = M->Qs[0];
	const uniform FVector4 Qs1 = M->Qs[1];
	const uniform FVector4 DR0Stiffness = SetVector4(DR0 * Stiffness, 0);
	const uniform FVector4 DR1Stiffness = SetVector4(DR1 * Stiffness, 0);

	const uniform FVector4 DQ0 = VectorQuaternionMultiply2(DR0Stiffness, Qs0) * 0.5f;
	const uniform FVector4 DQ1 = VectorQuaternionMultiply2(DR1Stiffness, Qs1) * 0.5f;

	const uniform FVector4 Qs0Norm = VectorNormalizeQuaternion(Qs0 + DQ0);
	const uniform FVector4 Qs1Norm = VectorNormalizeQuaternion(Qs1 + DQ1);

	const uniform FVector4 Qs1ShortestArcWith = VectorQuaternionEnforceShortestArcWith(Qs1Norm, Qs0Norm);

	M->Qs[0] = Qs0Norm;
	M->Qs[1] = Qs1ShortestArcWith;
}

export void ApplyRotationDelta(
	uniform FJointSolverGaussSeidel * uniform M,
	const uniform float Stiffness,
	const uniform FVector& DR0,
	const uniform FVector& DR1)
{
	ApplyRotationDeltaInner(M, Stiffness, DR0, DR1);
}

static inline uniform FMatrix MakeQuatRotationTranslationMatrix(const uniform FVector4& Q)
{
	// Assumes origin is 0,0,0
	const uniform FVector4 xyz2 = Q + Q; // x2, y2, z2
	const uniform FVector4 x = xyz2 * Q.V[0]; // xx, xy, xz
	const uniform FVector4 y = xyz2 * Q.V[1]; // yx, yy, yz
	const uniform FVector4 w = xyz2 * Q.V[3]; // wx, wy, wz
	const uniform float zz = Q.V[2] * xyz2.V[2];

	const uniform FVector4 yx = SetVector4(y.V[2], x.V[2], x.V[1], 0); // yz, xz, xy
	const uniform FVector4 Adds = yx + w; // M[6], M[8], M[1]
	const uniform FVector4 Minuses = yx - w; // M[9], M[2], M[4]
	
	const uniform FVector4 OneMinus0 = SetVector4(y.V[1], x.V[0], x.V[0], 0); // yy, xx, xx
	const uniform FVector4 Oneminus1 = SetVector4(zz, zz, y.V[1], 0); // zz, zz, yy
	const uniform FVector4 OneMinuses = FloatOne - (OneMinus0 + Oneminus1); // M[0], M[5], M[10]
	
	return SetMatrix33(
		SetVector4(OneMinuses.V[0], Adds.V[2], Minuses.V[1], 0),
		SetVector4(Minuses.V[2], OneMinuses.V[1], Adds.V[0], 0),
		SetVector4(Adds.V[1], Minuses.V[0], OneMinuses.V[2], 0)
	);
}

static inline uniform FMatrix ComputeWorldSpaceInertia(const uniform FVector4 &Q, const uniform FVector &I)
{
	const uniform FMatrix QM = MakeQuatRotationTranslationMatrix(Q);
	return MultiplyAB(QM, MultiplyABt(SetMatrix33(I.V[0], I.V[1], I.V[2]), QM));
}

export void ApplyRotationConstraint(
	uniform FJointSolverGaussSeidel * uniform M,
	const uniform float Stiffness,
	const uniform FVector& Axis0,
	const uniform FVector& Axis1,
	const uniform float Angle)
{
	// World-space inverse mass
	const uniform FMatrix InvI0 = ComputeWorldSpaceInertia(M->Qs[0], M->InvILs[0]);
	const uniform FMatrix InvI1 = ComputeWorldSpaceInertia(M->Qs[1], M->InvILs[1]);
	const uniform FVector IA0 = Multiply(InvI0, Axis0);
	const uniform FVector IA1 = Multiply(InvI1, Axis1);

	// Joint-space inverse mass
	const uniform float II0 = VectorDot(Axis0, IA0);
	const uniform float II1 = VectorDot(Axis1, IA1);
	const uniform float AngleM = Angle / (II0 + II1);

	const uniform FVector DR0 = IA0 * (AngleM);
	const uniform FVector DR1 = IA1 * -(AngleM);

	ApplyRotationDeltaInner(M, Stiffness, DR0, DR1);

	if (M->AngularPositionCorrection > 0)
	{
		const uniform FVector PrevX0 = M->Xs[0];
		const uniform FVector PrevX1 = M->Xs[1];
		UpdateDerivedStateInner(M);

		const uniform FVector DX = (M->Xs[1] - M->Xs[0]) - (PrevX1 - PrevX0);
		const uniform FVector DP0 = DX * (M->InvMs[0] / (M->InvMs[0] + M->InvMs[1]));
		const uniform FVector DP1 = DX * (-M->InvMs[1] / (M->InvMs[0] + M->InvMs[1]));
		ApplyPositionDeltaInner(M, Stiffness * M->AngularPositionCorrection, DP0, DP1);
	}

	UpdateDerivedStateInner(M);
}

static inline uniform FMatrix ComputeJointFactorMatrix(const uniform FVector& V, const uniform FMatrix& M, const uniform float Im)
{
	// Rigid objects rotational contribution to the impulse.
	// Vx*M*VxT+Im
	const uniform FVector V221 = SetVector(-V.V[2], V.V[2], -V.V[1]);
	const uniform FVector V222 = SetVector(-V.V[2], -V.V[2], -V.V[2]);
	const uniform FVector M544 = SetVector(M.M[5], M.M[4], M.M[4]);
	const uniform FVector V111 = SetVector(V.V[1], V.V[1], V.V[1]);
	const uniform FVector M988 = SetVector(M.M[9], M.M[8], M.M[8]);
	const uniform FVector V100 = SetVector(V.V[1], -V.V[0], V.V[0]);
	const uniform FVector M995 = SetVector(M.M[9], M.M[9], M.M[5]);
	const uniform FVector M10109 = SetVector(M.M[10], M.M[10], M.M[9]);
	const uniform FVector IM = SetVector(Im, 0, 0);

	const uniform FVector R1 = (V221 * (V222 * M544 + V111 * M988)) + (V100 * (V222 * M995 + V111 * M10109)) + IM;

	const uniform FVector V211 = SetVector(V.V[2], -V.V[1], -V.V[1]);
	const uniform FVector V221_2 = SetVector(V.V[2], V.V[2], -V.V[1]);
	const uniform FVector M000 = SetVector(M.M[0], M.M[0], M.M[0]);
	const uniform FVector V000 = SetVector(-V.V[0], -V.V[0], V.V[0]);
	const uniform FVector M884 = SetVector(M.M[8], M.M[8], M.M[4]);
	const uniform FVector V000_2 = SetVector(-V.V[0], V.V[0], V.V[0]);
	const uniform FVector M844 = SetVector(M.M[8], M.M[4], M.M[4]);
	const uniform FVector M1095 = SetVector(M.M[10], M.M[9], M.M[5]);
	const uniform FVector IM_2 = SetVector(Im, 0, Im);

	const uniform FVector R2 = (V211 * (V221_2 * M000 + V000 * M884)) + (V000_2 * (V221_2 * M844 + V000 * M1095)) + IM_2;

	return SetMatrix33(R1.V[0], R1.V[1], R1.V[2], R2.V[0], R2.V[1], R2.V[2]);
}

export void ApplyPositionConstraint(
	uniform FJointSolverGaussSeidel * uniform M,
	const uniform FVector &CX,
	const uniform float LinearStiffness)
{
	const uniform FMatrix InvI0 = ComputeWorldSpaceInertia(M->Qs[0], M->InvILs[0]);
	const uniform FMatrix InvI1 = ComputeWorldSpaceInertia(M->Qs[1], M->InvILs[1]);

	// Calculate constraint correction
	uniform FMatrix M0 = Matrix33Zero;
	uniform FMatrix M1 = M0;
	if (M->InvMs[0] > 0)
	{
		M0 = ComputeJointFactorMatrix(M->Xs[0] - M->Ps[0], InvI0, M->InvMs[0]);
	}
	if (M->InvMs[1] > 0)
	{
		M1 = ComputeJointFactorMatrix(M->Xs[1] - M->Ps[1], InvI1, M->InvMs[1]);
	}
	const uniform FMatrix MI = MatrixInverse(AddAB(M0, M1));
	const uniform FVector DX = Multiply(MI, CX);

	// Apply constraint correction
	const uniform FVector DP0 = DX * M->InvMs[0];
	const uniform FVector DP1 = DX * -M->InvMs[1];
	const uniform FVector DR0 = Multiply(InvI0, VectorCross(M->Xs[0] - M->Ps[0], DX));
	const uniform FVector DR1 = Multiply(InvI1, VectorCross(M->Xs[1] - M->Ps[1], VectorNegate(DX)));

	ApplyPositionDeltaInner(M, LinearStiffness, DP0, DP1);
	ApplyRotationDeltaInner(M, LinearStiffness, DR0, DR1);
	UpdateDerivedStateInner(M);
}

static inline uniform FVector CalculateAngularVelocity(const uniform FVector4 &R0, const uniform FVector4 &R1, const uniform float Dt)
{
	const uniform FVector4 R1Shortest = VectorQuaternionEnforceShortestArcWith(R1, R0);

	// W = 2 * dQ/dT * Qinv
	const uniform FVector4 DRDt = (R1Shortest - R0) * (1.0f / Dt);
	const uniform FVector4 RInv = QuatInverse(R0);
	const uniform FVector4 W = VectorQuaternionMultiply2(DRDt, RInv) * 2.0f;

	return SetVector(W);
}

static inline void ApplyPositionDeltaInner(
	uniform FJointSolverGaussSeidel * uniform M,
	const uniform int32 BodyIndex,
	const uniform float Stiffness,
	const uniform FVector& DP)
{
	M->Ps[BodyIndex] = M->Ps[BodyIndex] + Stiffness * DP;
}

static inline void ApplyRotationDeltaInner(
	uniform FJointSolverGaussSeidel * uniform M,
	const uniform int32 BodyIndex,
	const uniform float Stiffness,
	const uniform FVector& DR)
{
	const uniform FVector4 Qs = M->Qs[BodyIndex];
	const uniform FVector4 DRStiffness = SetVector4(DR * Stiffness, 0);
	const uniform FVector4 DQ = VectorQuaternionMultiply2(DRStiffness, Qs) * 0.5f;
	M->Qs[BodyIndex] = VectorNormalizeQuaternion(Qs + DQ);
}

static inline void ApplyVelocityDeltaInner(
	uniform FJointSolverGaussSeidel * uniform M,
	const uniform int32 BodyIndex,
	const uniform float Stiffness,
	const uniform FVector& DV,
	const uniform FVector& DW)
{
	M->Vs[BodyIndex] = M->Vs[BodyIndex] + Stiffness * DV;
	M->Ws[BodyIndex] = M->Ws[BodyIndex] + Stiffness * DW;
}

static inline void ApplyVelocityDeltaInner(
	uniform FJointSolverGaussSeidel * uniform M,
	const uniform float Stiffness,
	const uniform FVector& DV0,
	const uniform FVector& DW0,
	const uniform FVector& DV1,
	const uniform FVector& DW1)
{
	M->Vs[0] = M->Vs[0] + Stiffness * DV0;
	M->Vs[1] = M->Vs[1] + Stiffness * DV1;
	M->Ws[0] = M->Ws[0] + Stiffness * DW0;
	M->Ws[1] = M->Ws[1] + Stiffness * DW1;
}

export void ApplyVelocityDelta(
	uniform FJointSolverGaussSeidel * uniform M,
	const uniform float Stiffness,
	const uniform FVector& DV0,
	const uniform FVector& DW0,
	const uniform FVector& DV1,
	const uniform FVector& DW1)
{
	ApplyVelocityDeltaInner(M, Stiffness, DV0, DW0, DV1, DW1);
}

static inline void UpdateDerivedStateInner(uniform FJointSolverGaussSeidel * uniform M, const uniform int32 BodyIndex)
{
	M->Xs[BodyIndex] = M->Ps[BodyIndex] + VectorQuaternionRotateVector(M->Qs[BodyIndex], M->XLs[BodyIndex].Translation);
	M->Rs[BodyIndex] = VectorQuaternionMultiply2(M->Qs[BodyIndex], M->XLs[BodyIndex].Rotation);
}

export void ApplyPositionProjection(
	uniform FJointSolverGaussSeidel * uniform M,
	const uniform FVector &CX,
	const uniform float CXLen,
	const uniform float ParentMassScale,
	const uniform float Stiffness)
{
	const uniform FVector CXDir = CX / CXLen;
	const uniform FVector V0 = M->Vs[0] + VectorCross(M->Ws[0], M->Xs[0] - M->Ps[0]);
	const uniform FVector V1 = M->Vs[1] + VectorCross(M->Ws[1], M->Xs[1] - M->Ps[1]);
	const uniform FVector CV = VectorDot(V1 - V0, CXDir) * CXDir;

	const uniform float IM0 = ParentMassScale * M->InvMs[0];
	const uniform float IM1 = M->InvMs[1];
	const uniform FVector IIL0 = ParentMassScale * M->InvILs[0];
	const uniform FVector& IIL1 = M->InvILs[1];
	const uniform FMatrix II0 = ComputeWorldSpaceInertia(M->Qs[0], IIL0);
	const uniform FMatrix II1 = ComputeWorldSpaceInertia(M->Qs[1], IIL1);
	uniform FMatrix J0 = Matrix33Zero;
	if(IM0 > 0)
	{
		J0 = ComputeJointFactorMatrix(M->Xs[0] - M->Ps[0], II0, IM0);
	}
	const uniform FMatrix J1 = ComputeJointFactorMatrix(M->Xs[1] - M->Ps[1], II1, IM1);
	const uniform FMatrix IJ = MatrixInverse(AddAB(J0, J1));

	const uniform FVector DX = Multiply(IJ, CX);
	const uniform FVector DV = Multiply(IJ, CV);

	const uniform FVector DP0 = IM0 * DX;
	const uniform FVector DP1 = -IM1 * DX;
	const uniform FVector DR0 = Multiply(II0, VectorCross(M->Xs[0] - M->Ps[0], DX));
	const uniform FVector DR1 = Multiply(II1, VectorCross(M->Xs[1] - M->Ps[1], VectorNegate(DX)));

	const uniform FVector DV0 = IM0 * DV;
	const uniform FVector DV1 = -IM1 * DV;
	const uniform FVector DW0 = Multiply(II0, VectorCross(M->Xs[0] - M->Ps[0], DV));
	const uniform FVector DW1 = Multiply(II1, VectorCross(M->Xs[1] - M->Ps[1], VectorNegate(DV)));

	ApplyPositionDeltaInner(M, Stiffness, DP0, DP1);
	ApplyRotationDeltaInner(M, Stiffness, DR0, DR1);
	ApplyVelocityDeltaInner(M, Stiffness, DV0, DW0, DV1, DW1);
	UpdateDerivedStateInner(M);
}

// See "XPBD: Position-Based Simulation of Compliant Constrained Dynamics"
export void ApplyRotationConstraintSoft(
	uniform FJointSolverGaussSeidel * uniform M,
	const uniform float Dt,
	const uniform float Stiffness,
	const uniform float Damping,
	const uniform bool bAccelerationMode,
	const uniform FVector& Axis0,
	const uniform FVector& Axis1,
	const uniform float Angle,
	uniform float& Lambda)
{
	// World-space inverse mass
	const uniform FMatrix InvI0 = ComputeWorldSpaceInertia(M->Qs[0], M->InvILs[0]);
	const uniform FMatrix InvI1 = ComputeWorldSpaceInertia(M->Qs[1], M->InvILs[1]);
	const uniform FVector IA0 = Multiply(InvI0, Axis0);
	const uniform FVector IA1 = Multiply(InvI1, Axis1);

	// Joint-space inverse mass
	const uniform float II0 = VectorDot(Axis0, IA0);
	const uniform float II1 = VectorDot(Axis1, IA1);
	const uniform float II = (II0 + II1);

	// Damping angular velocity
	uniform float AngVelDt = 0;
	if (Damping > KINDA_SMALL_NUMBER)
	{
		const uniform FVector W0 = CalculateAngularVelocity(M->PrevQs[0], M->Qs[0], 1.0f);
		const uniform FVector W1 = CalculateAngularVelocity(M->PrevQs[1], M->Qs[1], 1.0f);
		AngVelDt = VectorDot(Axis0, W0) - VectorDot(Axis1, W1);
	}

	const uniform float SpringMassScale = (bAccelerationMode) ? 1.0f / II : 1.0f;
	const uniform float S = SpringMassScale * Stiffness * Dt * Dt;
	const uniform float D = SpringMassScale * Damping * Dt;
	const uniform float Multiplier = 1.0f / ((S + D) * II + 1.0f);
	const uniform float DLambda = Multiplier * (S * Angle - D * AngVelDt - Lambda);

	//const FVec3 DR0 = IA0 * DLambda;
	//const FVec3 DR1 = IA1 * -DLambda;
	const uniform FVector DR0 = Axis0 * (DLambda * II0);
	const uniform FVector DR1 = Axis1 * -(DLambda * II1);

	Lambda += DLambda;
	ApplyRotationDeltaInner(M, 1.0f, DR0, DR1);

	// Correct the positional error that was introduced by the rotation correction. This is
	// correct when the position dofs are all locked, but not otherwise (fixable).
	// This significantly improves angular stiffness at lower iterations, although the same effect
	// is achieved by increasing iterations.
	// @todo(ccaulfield): this position correction needs to have components in direction of inactive position constraints removed
	if (M->AngularPositionCorrection > 0)
	{
		const uniform FVector PrevX0 = M->Xs[0];
		const uniform FVector PrevX1 = M->Xs[1];
		UpdateDerivedStateInner(M);

		const uniform FVector DX = (M->Xs[1] - M->Xs[0]) - (PrevX1 - PrevX0);
		const uniform FVector DP0 = DX * (M->InvMs[0] / (M->InvMs[0] + M->InvMs[1]));
		const uniform FVector DP1 = DX * (-M->InvMs[1] / (M->InvMs[0] + M->InvMs[1]));
		ApplyPositionDeltaInner(M, M->AngularPositionCorrection, DP0, DP1);
	}

	UpdateDerivedStateInner(M);
}
