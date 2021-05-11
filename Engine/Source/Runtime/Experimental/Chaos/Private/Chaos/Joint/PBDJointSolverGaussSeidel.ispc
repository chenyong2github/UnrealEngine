// Copyright Epic Games, Inc. All Rights Reserved.

#define EXPLICIT_VECTOR4 1

#include "Math/Vector.isph"
#include "Math/Quat.isph"
#include "Chaos/Matrix33.isph"

struct FRigidTransform
{
	FVector4 Rotation;
	FVector Translation;
	FVector Scale3D;
};

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
	FMatrix33 InvIs[MaxConstrainedBodies];		// World-space inverse inertias

	// XPBD Previous iteration world-space body state
	FVector InitPs[MaxConstrainedBodies];		// World-space particle positions
	FVector4 InitQs[MaxConstrainedBodies];		// World-space particle rotations
	FVector InitXs[MaxConstrainedBodies];		// World-space joint connector positions
	FVector4 InitRs[MaxConstrainedBodies];		// World-space joint connector rotations

	// Accumulated Impulse and AngularImpulse (Impulse * Dt since they are mass multiplied position corrections)
	FVector NetLinearImpulse;
	FVector NetAngularImpulse;

	// XPBD Accumulators (net impulse for each soft constraint/drive)
	float LinearSoftLambda;
	float TwistSoftLambda;
	float SwingSoftLambda;
	FVector LinearDriveLambdas;
	FVector RotationDriveLambdas;
	
	// Solver stiffness - increased over iterations for stability
	// @todo(chaos): remove Stiffness from SolverSettings (because it is not a solver constant)
	float SolverStiffness;

	// Constraint padding which can act something like a velocity constraint (for restitution)
	FVector LinearConstraintPadding;
	FVector AngularConstraintPadding;

	// Tolerances below which we stop solving
	float PositionTolerance;					// Distance error below which we consider a constraint or drive solved
	float AngleTolerance;						// Angle error below which we consider a constraint or drive solved

	FVector LastPs[MaxConstrainedBodies];		// Positions at the beginning of the iteration
	FVector4 LastQs[MaxConstrainedBodies];		// Rotations at the beginning of the iteration
	int NumActiveConstraints;					// The number of active constraints and drives in the last iteration (-1 initial value)
	bool bIsActive;								// Whether any constraints actually moved any bodies in last iteration
};

export uniform int32 SizeofFJointSolverGaussSeidel()
{
	return sizeof(uniform FJointSolverGaussSeidel);
}

static inline uniform FMatrix33 ComputeWorldSpaceInertia(const uniform FVector4 &Q, const uniform FVector &I)
{
	const uniform FMatrix33 QM = MakeQuatRotationTranslationMatrix(Q);
	const uniform FMatrix33 L = SetMatrix33(I.V[0], I.V[1], I.V[2]);
	return MultiplyAB(QM, MultiplyABt(L, QM));
}

static inline uniform FMatrix33 ComputeJointFactorMatrix(const uniform FVector& V, const uniform FMatrix33& M, const uniform float Im)
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

static inline uniform FVector CalculateAngularVelocity(const uniform FVector4 &R0, const uniform FVector4 &R1, const uniform float Dt)
{
	const uniform FVector4 R1Shortest = VectorQuaternionEnforceShortestArcWith(R1, R0);

	// W = 2 * dQ/dT * Qinv
	const uniform FVector4 DRDt = (R1Shortest - R0) * (1.0f / Dt);
	const uniform FVector4 RInv = QuatInverse(R0);
	const uniform FVector4 W = VectorQuaternionMultiply2(DRDt, RInv) * 2.0f;

	return SetVector(W);
}

static inline uniform FVector CalculateVelocity(const uniform FVector& P0, const uniform FVector& P1, const uniform float Dt)
{
	return (P1 - P0) / Dt;
}

static inline void ApplyPositionDelta(
	uniform FJointSolverGaussSeidel * uniform M,
	const uniform int Index,
	const uniform FVector& DP)
{
	M->Ps[Index] = M->Ps[Index] + DP;
}

static inline void ApplyPositionDelta(
	uniform FJointSolverGaussSeidel * uniform M,
	const uniform FVector& DP0,
	const uniform FVector& DP1)
{
	M->Ps[0] = M->Ps[0] + DP0;
	M->Ps[1] = M->Ps[1] + DP1;
}

static inline void ApplyRotationDelta(
	uniform FJointSolverGaussSeidel * uniform M,
	const uniform int Index,
	const uniform FVector& InDR)
{
	const uniform FVector4 Qs = M->Qs[Index];
	const uniform FVector4 DR = SetVector4(InDR, 0);

	const uniform FVector4 DQ = VectorQuaternionMultiply2(DR, Qs) * 0.5f;

	const uniform FVector4 QsNorm = VectorNormalizeQuaternion(Qs + DQ);

	M->Qs[Index] = QsNorm;

	const uniform FVector4 Qs1Shortest = VectorQuaternionEnforceShortestArcWith(M->Qs[1], M->Qs[0]);
	M->Qs[1] = Qs1Shortest;

	M->Xs[Index] = M->Ps[Index] + VectorQuaternionRotateVector(M->Qs[Index], M->XLs[Index].Translation);
	M->Rs[Index] = VectorQuaternionMultiply2(M->Qs[Index], M->XLs[Index].Rotation);

	const uniform FVector4 Rs1Shortest = VectorQuaternionEnforceShortestArcWith(M->Rs[1], M->Rs[0]);
	M->Rs[1] = Rs1Shortest;

	M->InvIs[Index] = ComputeWorldSpaceInertia(M->Qs[Index], M->InvILs[Index]);
}

static inline void ApplyRotationDelta(
	uniform FJointSolverGaussSeidel * uniform M,
	const uniform FVector& InDR0,
	const uniform FVector& InDR1)
{
	if (M->InvMs[0] > 0.0f)
	{
		const uniform FVector4 Qs0 = M->Qs[0];
		const uniform FVector4 DR0 = SetVector4(InDR0, 0);
		const uniform FVector4 DQ0 = VectorQuaternionMultiply2(DR0, Qs0) * 0.5f;
		const uniform FVector4 Q0 = VectorNormalizeQuaternion(Qs0 + DQ0);
		M->Qs[0] = Q0;
		M->Xs[0] = M->Ps[0] + VectorQuaternionRotateVector(Q0, M->XLs[0].Translation);
		M->Rs[0] = VectorQuaternionMultiply2(Q0, M->XLs[0].Rotation);
		M->InvIs[0] = ComputeWorldSpaceInertia(Q0, M->InvILs[0]);
	}

	if (M->InvMs[1] > 0.0f)
	{
		const uniform FVector4 Qs1 = M->Qs[1];
		const uniform FVector4 DR1 = SetVector4(InDR1, 0);
		const uniform FVector4 DQ1 = VectorQuaternionMultiply2(DR1, Qs1) * 0.5f;
		const uniform FVector4 Q1 = VectorNormalizeQuaternion(Qs1 + DQ1);
		M->Qs[1] = Q1;
		M->Xs[1] = M->Ps[1] + VectorQuaternionRotateVector(Q1, M->XLs[1].Translation);
		M->Rs[1] = VectorQuaternionMultiply2(Q1, M->XLs[1].Rotation);
		M->InvIs[1] = ComputeWorldSpaceInertia(Q1, M->InvILs[1]);
	}

	M->Qs[1] = VectorQuaternionEnforceShortestArcWith(M->Qs[1], M->Qs[0]);
	M->Rs[1] = VectorQuaternionEnforceShortestArcWith(M->Rs[1], M->Rs[0]);
}

export void ApplyRotationDelta2(
	uniform FJointSolverGaussSeidel * uniform M,
	const uniform FVector& DR0,
	const uniform FVector& DR1)
{
	ApplyRotationDelta(M, DR0, DR1);
}

static inline void ApplyDelta(
	uniform FJointSolverGaussSeidel * uniform M,
	const uniform int Index,
	const uniform FVector& DP,
	const uniform FVector& DR)
{
	ApplyPositionDelta(M, Index, DP);
	ApplyRotationDelta(M, Index, DR);
}

static inline void ApplyVelocityDelta(
	uniform FJointSolverGaussSeidel * uniform M,
	const uniform FVector& DV0,
	const uniform FVector& DW0,
	const uniform FVector& DV1,
	const uniform FVector& DW1)
{
	M->Vs[0] = M->Vs[0] + DV0;
	M->Vs[1] = M->Vs[1] + DV1;
	M->Ws[0] = M->Ws[0] + DW0;
	M->Ws[1] = M->Ws[1] + DW1;
}


static inline void ApplyVelocityDelta(
	uniform FJointSolverGaussSeidel * uniform M,
	const uniform int Index,
	const uniform FVector& DV,
	const uniform FVector& DW)
{
	M->Vs[Index] = M->Vs[Index] + DV;
	M->Ws[Index] = M->Ws[Index] + DW;
}

export void ApplyPositionConstraintSoft(
	uniform FJointSolverGaussSeidel * uniform M,
	const uniform float Dt,
	const uniform float JointStiffness,
	const uniform float JointDamping,
	const uniform bool bAccelerationMode,
	const uniform FVector& Axis,
	const uniform float Delta,
	const uniform float TargetVel,
	uniform float& Lambda)
{
	// Joint-space inverse mass
	const uniform FVector AngularAxis0 = VectorCross(M->Xs[0] - M->Ps[0], Axis);
	const uniform FVector AngularAxis1 = VectorCross(M->Xs[1] - M->Ps[1], Axis);
	const uniform FVector IA0 = Multiply(M->InvIs[0], AngularAxis0);
	const uniform FVector IA1 = Multiply(M->InvIs[1], AngularAxis1);
	const uniform float II0 = VectorDot(AngularAxis0, IA0);
	const uniform float II1 = VectorDot(AngularAxis1, IA1);
	const uniform float II = (M->InvMs[0] + II0 + M->InvMs[1] + II1);

	uniform float VelDt = 0;
	if (JointDamping > KINDA_SMALL_NUMBER)
	{
		const uniform FVector V0Dt = CalculateVelocity(M->InitXs[0], M->Xs[0], 1.0f);
		const uniform FVector V1Dt = CalculateVelocity(M->InitXs[1], M->Xs[1], 1.0f);
		VelDt = TargetVel * Dt + VectorDot(V0Dt - V1Dt, Axis);
	}

	const uniform float SpringMassScale = (bAccelerationMode) ? 1.0f / (M->InvMs[0] + M->InvMs[1]) : 1.0f;
	const uniform float S = SpringMassScale * JointStiffness * Dt * Dt;
	const uniform float D = SpringMassScale * JointDamping * Dt;
	const uniform float Multiplier = 1.0f / ((S + D) * II + 1.0f);
	const uniform float DLambda = M->SolverStiffness * Multiplier * (S * Delta - D * VelDt - Lambda);
	
	const uniform FVector DP0 = (M->InvMs[0] * DLambda) * Axis;
	const uniform FVector DP1 = (-M->InvMs[1] * DLambda) * Axis;
	const uniform FVector DR0 = DLambda * Multiply(M->InvIs[0], AngularAxis0);
	const uniform FVector DR1 = -DLambda * Multiply(M->InvIs[1], AngularAxis1);

	ApplyPositionDelta(M, DP0, DP1);
	ApplyRotationDelta(M, DR0, DR1);

	Lambda += DLambda;
	M->NetLinearImpulse = M->NetLinearImpulse + DLambda * Axis;
}

// See "XPBD: Position-Based Simulation of Compliant Constrained Dynamics"
export void ApplyRotationConstraintSoftKD(
	uniform FJointSolverGaussSeidel * uniform M,
	const uniform int KIndex,
	const uniform int DIndex,
	const uniform float Dt,
	const uniform float JointStiffness,
	const uniform float JointDamping,
	const uniform bool bAccelerationMode,
	const uniform FVector& Axis,
	const uniform float Angle,
	const uniform float AngVelTarget,
	uniform float& Lambda)
{
	// World-space inverse mass
	const uniform FVector IA1 = Multiply(M->InvIs[DIndex], Axis);

	// Joint-space inverse mass
	uniform float II1 = VectorDot(Axis, IA1);
	const uniform float II = II1;

	// Damping angular velocity
	uniform float AngVelDt = 0;
	if (JointDamping > KINDA_SMALL_NUMBER)
	{
		const uniform FVector W0Dt = CalculateAngularVelocity(M->InitRs[KIndex], M->Rs[KIndex], 1.0f);
		const uniform FVector W1Dt = CalculateAngularVelocity(M->InitRs[DIndex], M->Rs[DIndex], 1.0f);
		AngVelDt = AngVelTarget * Dt + VectorDot(Axis, W0Dt - W1Dt);
	}

	const uniform float SpringMassScale = (bAccelerationMode) ? 1.0f / II : 1.0f;
	const uniform float S = SpringMassScale * JointStiffness * Dt * Dt;
	const uniform float D = SpringMassScale * JointDamping * Dt;
	const uniform float Multiplier = 1.0f / ((S + D) * II + 1.0f);
	const uniform float DLambda = M->SolverStiffness * Multiplier * (S * Angle - D * AngVelDt - Lambda);

	//const FVec3 DR1 = IA1 * -DLambda;
	const uniform FVector DR1 = Axis * -(DLambda * II1);

	ApplyRotationDelta(M, DIndex, DR1);

	Lambda += DLambda;
	M->NetAngularImpulse = M->NetAngularImpulse + (KIndex == 0 ? 1 : -1) * DLambda * Axis;
}

export void ApplyRotationConstraintSoftDD(
	uniform FJointSolverGaussSeidel * uniform M,
	const uniform float Dt,
	const uniform float JointStiffness,
	const uniform float JointDamping,
	const uniform bool bAccelerationMode,
	const uniform FVector& Axis,
	const uniform float Angle,
	const uniform float AngVelTarget,
	uniform float& Lambda)
{
	// World-space inverse mass
	const uniform FVector IA0 = Multiply(M->InvIs[0], Axis);
	const uniform FVector IA1 = Multiply(M->InvIs[1], Axis);

	// Joint-space inverse mass
	uniform float II0 = VectorDot(Axis, IA0);
	uniform float II1 = VectorDot(Axis, IA1);
	const uniform float II = (II0 + II1);

	// Damping angular velocity
	uniform float AngVelDt = 0;
	if (JointDamping > KINDA_SMALL_NUMBER)
	{
		const uniform FVector W0Dt = CalculateAngularVelocity(M->InitRs[0], M->Rs[0], 1.0f);
		const uniform FVector W1Dt = CalculateAngularVelocity(M->InitRs[1], M->Rs[1], 1.0f);
		AngVelDt = AngVelTarget * Dt + VectorDot(Axis, W0Dt - W1Dt);
	}

	const uniform float SpringMassScale = (bAccelerationMode) ? 1.0f / II : 1.0f;
	const uniform float S = SpringMassScale * JointStiffness * Dt * Dt;
	const uniform float D = SpringMassScale * JointDamping * Dt;
	const uniform float Multiplier = 1.0f / ((S + D) * II + 1.0f);
	const uniform float DLambda = M->SolverStiffness * Multiplier * (S * Angle - D * AngVelDt - Lambda);

	//const FVec3 DR0 = IA0 * DLambda;
	//const FVec3 DR1 = IA1 * -DLambda;
	const uniform FVector DR0 = Axis * (DLambda * II0);
	const uniform FVector DR1 = Axis * -(DLambda * II1);

	ApplyRotationDelta(M, DR0, DR1);

	Lambda += DLambda;
	M->NetAngularImpulse = M->NetAngularImpulse + DLambda * Axis;
}


export void ApplyPointPositionConstraintKD(
	uniform FJointSolverGaussSeidel * uniform M,
	const uniform int KIndex,
	const uniform int DIndex,
	const uniform FVector &CX,
	const uniform float InStiffness)
{

	// Calculate constraint correction
	const uniform FMatrix33 M1 = ComputeJointFactorMatrix(M->Xs[DIndex] - M->Ps[DIndex], M->InvIs[DIndex], M->InvMs[DIndex]);
	const uniform FMatrix33 MI = MatrixInverse(M1);
	const uniform FVector DX = InStiffness * Multiply(MI, CX);

	// Apply constraint correction
	const uniform FVector DP1 = (DX * -M->InvMs[DIndex]);
	const uniform FVector DR1 = Multiply(M->InvIs[DIndex], VectorCross(M->Xs[DIndex] - M->Ps[DIndex], VectorNegate(DX)));

	ApplyDelta(M, DIndex, DP1, DR1);

	M->NetLinearImpulse = M->NetLinearImpulse + ((KIndex == 0) ? DX : VectorNegate(DX));
}

export void ApplyPointPositionConstraintDD(
	uniform FJointSolverGaussSeidel * uniform M,
	const uniform FVector &CX,
	const uniform float InStiffness)
{
	// Calculate constraint correction
	uniform FMatrix33 M0 = ComputeJointFactorMatrix(M->Xs[0] - M->Ps[0], M->InvIs[0], M->InvMs[0]);
	uniform FMatrix33 M1 = ComputeJointFactorMatrix(M->Xs[1] - M->Ps[1], M->InvIs[1], M->InvMs[1]);
	const uniform FMatrix33 MI = MatrixInverse(AddAB(M0, M1));
	const uniform FVector DX = InStiffness * Multiply(MI, CX);

	// Apply constraint correction
	const uniform FVector DP0 = (DX * M->InvMs[0]);
	const uniform FVector DP1 = (DX * -M->InvMs[1]);
	const uniform FVector DR0 = Multiply(M->InvIs[0], VectorCross(M->Xs[0] - M->Ps[0], DX));
	const uniform FVector DR1 = Multiply(M->InvIs[1], VectorCross(M->Xs[1] - M->Ps[1], VectorNegate(DX)));

	ApplyPositionDelta(M, DP0, DP1);
	ApplyRotationDelta(M, DR0, DR1);

	M->NetLinearImpulse = M->NetLinearImpulse + DX;
}

//
//////////////////////////////////////////////////////
//
// Batched Mode Stuff
//

struct FJointSolverJointState
{
	// Local-space constraint settings
	FRigidTransform XLs[MaxConstrainedBodies];
	FVector InvILs[MaxConstrainedBodies];
	float InvMs[MaxConstrainedBodies];

	// World-space constraint state
	FVector Xs[MaxConstrainedBodies];
	FVector4 Rs[MaxConstrainedBodies];

	// World-space body state
	FVector Ps[MaxConstrainedBodies];
	FVector4 Qs[MaxConstrainedBodies];
	FMatrix33 InvIs[MaxConstrainedBodies];

	// XPBD initial world-space body state (start of each tick, not each sub-tick iteration)
	FVector PrevPs[MaxConstrainedBodies];
	FVector4 PrevQs[MaxConstrainedBodies];
	FVector PrevXs[MaxConstrainedBodies];

	FVector DPs[MaxConstrainedBodies];
	FVector DRs[MaxConstrainedBodies];

	float PositionTolerance;
	float AngleTolerance;
};

export uniform int32 SizeofFJointSolverJointState()
{
	return sizeof(uniform FJointSolverJointState);
}

struct FJointSolverConstraintRowState
{
	// Per-Iteration Transient
	FVector DPs[MaxConstrainedBodies];
	FVector DRs[MaxConstrainedBodies];
	FVector Axis;
	float Error;

	// Per-Tick Transient
	float Lambda;
};

export uniform int32 SizeofFJointSolverConstraintRowState()
{
	return sizeof(uniform FJointSolverConstraintRowState);
}

struct FJointSolverConstraintRowData
{
	int32 UpdateType;			// Method to use to calculate axis, errors, etc.
	int32 JointIndex;			// Index into the outer container's joint array
	int32 ConstraintIndex;		// Context dependent: X,Y,Z or Twist,Swing1,Swing2 for linear and angular rows
	int32 NumRows;				// Number of rows (including this one) to solve simultaneously
	float Stiffness;			// PBD or XPBD stiffness
	float Damping;				// XPBD damping
	float Limit;
	bool bIsAccelerationMode;
	bool bIsSoft;
};

export uniform int32 SizeofFJointSolverConstraintRowData()
{
	return sizeof(uniform FJointSolverConstraintRowData);
}

static inline void BatchUpdateDerivedStateImpl(uniform FJointSolverJointState Joints[], uniform int JointIndexBegin, uniform int JointIndexEnd)
{
	for(uniform int JointIndex = JointIndexBegin; JointIndex < JointIndexEnd; JointIndex++)
	{
		if (Joints[JointIndex].InvMs[0] > 0.0f)
		{
			Joints[JointIndex].Xs[0] = Joints[JointIndex].Ps[0] + VectorQuaternionRotateVector(Joints[JointIndex].Qs[0], Joints[JointIndex].XLs[0].Translation);
			Joints[JointIndex].Rs[0] = VectorQuaternionMultiply2(Joints[JointIndex].Qs[0], Joints[JointIndex].XLs[0].Rotation);
			Joints[JointIndex].InvIs[0] = ComputeWorldSpaceInertia(Joints[JointIndex].Qs[0], Joints[JointIndex].InvILs[0]);
			Joints[JointIndex].DPs[0] = ZeroVector;
			Joints[JointIndex].DRs[0] = ZeroVector;
		}

		if (Joints[JointIndex].InvMs[1] > 0.0f)
		{
			Joints[JointIndex].Xs[1] = Joints[JointIndex].Ps[1] + VectorQuaternionRotateVector(Joints[JointIndex].Qs[1], Joints[JointIndex].XLs[1].Translation);
			Joints[JointIndex].Rs[1] = VectorQuaternionMultiply2(Joints[JointIndex].Qs[1], Joints[JointIndex].XLs[1].Rotation);
			Joints[JointIndex].InvIs[1] = ComputeWorldSpaceInertia(Joints[JointIndex].Qs[1], Joints[JointIndex].InvILs[1]);
			Joints[JointIndex].DPs[1] = ZeroVector;
			Joints[JointIndex].DRs[1] = ZeroVector;
		}

		Joints[JointIndex].Rs[1] = VectorQuaternionEnforceShortestArcWith(Joints[JointIndex].Rs[1], Joints[JointIndex].Rs[0]);
	}
}

export void BatchUpdateDerivedState(uniform FJointSolverJointState Joints[], uniform int JointIndexBegin, uniform int JointIndexEnd)
{
	BatchUpdateDerivedStateImpl(Joints, JointIndexBegin, JointIndexEnd);
}

static inline void BatchApplyPositionConstraint1(
	const uniform float Dt,
	uniform FJointSolverJointState& JointState,
	uniform FJointSolverConstraintRowData& RowData,
	uniform FJointSolverConstraintRowState& RowState)
{
	uniform float II = 0.0f;
	uniform FVector AngularAxis0 = ZeroVector;
	uniform FVector AngularAxis1 = ZeroVector;

	if (JointState.InvMs[0] > 0.0f)
	{
		AngularAxis0 = VectorCross(JointState.Xs[0] - JointState.Ps[0], RowState.Axis);
		const uniform FVector IA0 = Multiply(JointState.InvIs[0], AngularAxis0);
		const uniform float II0 = VectorDot(AngularAxis0, IA0);
		II += JointState.InvMs[0] + II0;
	}
	if (JointState.InvMs[1] > 0.0f)
	{
		AngularAxis1 = VectorCross(JointState.Xs[1] - JointState.Ps[1], RowState.Axis);
		const uniform FVector IA1 = Multiply(JointState.InvIs[1], AngularAxis1);
		const uniform float II1 = VectorDot(AngularAxis1, IA1);
		II += JointState.InvMs[1] + II1;
	}

	if (RowData.bIsSoft)
	{
		uniform float VelDt = 0;
		if (RowData.Damping > KINDA_SMALL_NUMBER)
		{
			const uniform FVector V0 = CalculateVelocity(JointState.PrevXs[0], JointState.Xs[0], 1.0f);
			const uniform FVector V1 = CalculateVelocity(JointState.PrevXs[1], JointState.Xs[1], 1.0f);
			VelDt = VectorDot(V0 - V1, RowState.Axis);
		}

		const uniform float SpringMassScale = (RowData.bIsAccelerationMode) ? 1.0f / (JointState.InvMs[0] + JointState.InvMs[1]) : 1.0f;
		const uniform float S = SpringMassScale * RowData.Stiffness * Dt * Dt;
		const uniform float D = SpringMassScale * RowData.Damping * Dt;
		const uniform float Multiplier = 1.0f / ((S + D) * II + 1.0f);
		const uniform float DLambda = Multiplier * (S * RowState.Error - D * VelDt - RowState.Lambda);

		RowState.DPs[0] = (JointState.InvMs[0] * DLambda) * RowState.Axis;
		RowState.DPs[1] = (-JointState.InvMs[1] * DLambda) * RowState.Axis;
		RowState.DRs[0] = DLambda * Multiply(JointState.InvIs[0], AngularAxis0);
		RowState.DRs[1] = -DLambda * Multiply(JointState.InvIs[1], AngularAxis1);
		RowState.Lambda += DLambda;
	}
	else
	{
		const uniform FVector DX = RowState.Axis * (RowData.Stiffness * RowState.Error / II);

		// Apply constraint correction
		RowState.DPs[0] = JointState.InvMs[0] * DX;
		RowState.DPs[1] = JointState.InvMs[1] * VectorNegate(DX);
		RowState.DRs[0] = Multiply(JointState.InvIs[0], VectorCross(JointState.Xs[0] - JointState.Ps[0], DX));
		RowState.DRs[1] = Multiply(JointState.InvIs[1], VectorCross(JointState.Xs[1] - JointState.Ps[1], VectorNegate(DX)));
	}
}

static inline void BatchApplyDelta(
	uniform FJointSolverJointState* uniform JointStates, 
	uniform int JointIndex)
{
	if (JointStates[JointIndex].InvMs[0] > 0.0f)
	{
		const uniform FVector4 Qs0 = JointStates[JointIndex].Qs[0];
		const uniform FVector4 DR0 = SetVector4(JointStates[JointIndex].DRs[0], 0);
		const uniform FVector4 DQ0 = VectorQuaternionMultiply2(DR0, Qs0) * 0.5f;
		const uniform FVector4 Q0 = VectorNormalizeQuaternion(Qs0 + DQ0);
		JointStates[JointIndex].Ps[0] = JointStates[JointIndex].Ps[0] + JointStates[JointIndex].DPs[0];
		JointStates[JointIndex].Qs[0] = Q0;
	}

	if (JointStates[JointIndex].InvMs[1] > 0.0f)
	{
		const uniform FVector4 Qs1 = JointStates[JointIndex].Qs[1];
		const uniform FVector4 DR1 = SetVector4(JointStates[JointIndex].DRs[1], 0);
		const uniform FVector4 DQ1 = VectorQuaternionMultiply2(DR1, Qs1) * 0.5f;
		const uniform FVector4 Q1 = VectorNormalizeQuaternion(Qs1 + DQ1);
		JointStates[JointIndex].Ps[1] = JointStates[JointIndex].Ps[1] + JointStates[JointIndex].DPs[1];
		JointStates[JointIndex].Qs[1] = Q1;
	}

	JointStates[JointIndex].Qs[1] = VectorQuaternionEnforceShortestArcWith(JointStates[JointIndex].Qs[1], JointStates[JointIndex].Qs[0]);
}

export uniform int BatchApplyPositionConstraints(
	uniform float Dt, 
	uniform FJointSolverJointState* uniform JointStates, 
	uniform FJointSolverConstraintRowData* uniform RowDatas, 
	uniform FJointSolverConstraintRowState* uniform RowStates, 
	uniform int JointIndexBegin, 
	uniform int JointIndexEnd, 
	uniform int RowIndexBegin, 
	uniform int RowIndexEnd)
{
	uniform int32 NumActiveConstraints = 0;

	// Calculate the delta position and rotation to fix the constraint errors
	// NOTE: Rows are in Joint order, so we can loop over them directly rather than looping over joints and then their rows.
	for (uniform int32 RowIndex = RowIndexBegin; RowIndex < RowIndexEnd; /* no op */)
	{
		const uniform int32 RowJointIndex = RowDatas[RowIndex].JointIndex;
		//if (RowDatas[RowIndex].NumRows == 3)
		//{
		//	uniform FVector Error = FVec3(RowStates[RowIndex].Error, RowStates[RowIndex + 1].Error, RowStates[RowIndex + 2].Error);
		//	if (Error.Size() > JointStates[RowJointIndex].PositionTolerance)
		//	{
		//		ApplyPositionConstraint3(Dt, JointStates[RowJointIndex], RowDatas[RowIndex], RowStates[RowIndex], RowStates[RowIndex + 1], RowStates[RowIndex + 2]);
		//		JointStates[RowJointIndex].DPs[0] = JointStates[RowJointIndex].DPs[0] + RowStates[RowIndex].DPs[0];
		//		JointStates[RowJointIndex].DPs[1] = JointStates[RowJointIndex].DPs[1] + RowStates[RowIndex].DPs[1];
		//		JointStates[RowJointIndex].DRs[0] = JointStates[RowJointIndex].DRs[0] + RowStates[RowIndex].DRs[0];
		//		JointStates[RowJointIndex].DRs[1] = JointStates[RowJointIndex].DRs[1] + RowStates[RowIndex].DRs[1];
		//		NumActiveConstraints += 3;
		//	}
		//	RowIndex += 3;
		//}
		//else if (RowDatas[RowIndex].NumRows == 1)
		{
			if (abs(RowStates[RowIndex].Error) > JointStates[RowJointIndex].PositionTolerance)
			{
				BatchApplyPositionConstraint1(Dt, JointStates[RowJointIndex], RowDatas[RowIndex], RowStates[RowIndex]);
				JointStates[RowJointIndex].DPs[0] = JointStates[RowJointIndex].DPs[0] + RowStates[RowIndex].DPs[0];
				JointStates[RowJointIndex].DPs[1] = JointStates[RowJointIndex].DPs[1] + RowStates[RowIndex].DPs[1];
				JointStates[RowJointIndex].DRs[0] = JointStates[RowJointIndex].DRs[0] + RowStates[RowIndex].DRs[0];
				JointStates[RowJointIndex].DRs[1] = JointStates[RowJointIndex].DRs[1] + RowStates[RowIndex].DRs[1];
				++NumActiveConstraints;
			}
			++RowIndex;
		}
	}

	// Accumulate and apply the deltas for each joint
	for (uniform int32 JointIndex = JointIndexBegin; JointIndex < JointIndexEnd; ++JointIndex)
	{
		BatchApplyDelta(JointStates, JointIndex);
	}

	return NumActiveConstraints;
}

static inline void BatchApplyRotationConstraint(
	const uniform float Dt,
	uniform FJointSolverJointState& JointState,
	uniform FJointSolverConstraintRowData& RowData,
	uniform FJointSolverConstraintRowState& RowState)
{
	// Joint-space inverse mass
	uniform FVector IA0 = ZeroVector;
	uniform FVector IA1 = ZeroVector;
	uniform float II0 = 0.0f;
	uniform float II1 = 0.0f;
	if (JointState.InvMs[0] > 0)
	{
		IA0 = Multiply(JointState.InvIs[0], RowState.Axis);
		II0 = VectorDot(RowState.Axis, IA0);
	}
	if (JointState.InvMs[1] > 0)
	{
		IA1 = Multiply(JointState.InvIs[1], RowState.Axis);
		II1 = VectorDot(RowState.Axis, IA1);
	}
	const uniform float II = (II0 + II1);

	if (RowData.bIsSoft)
	{
		// Damping angular velocity
		uniform float AngVelDt = 0;
		if (RowData.Damping > 0.0f)
		{
			const uniform FVector W0 = CalculateAngularVelocity(JointState.PrevQs[0], JointState.Qs[0], 1.0f);
			const uniform FVector W1 = CalculateAngularVelocity(JointState.PrevQs[1], JointState.Qs[1], 1.0f);
			AngVelDt = VectorDot(RowState.Axis, W0) - VectorDot(RowState.Axis, W1);
		}

		const uniform float SpringMassScale = (RowData.bIsAccelerationMode) ? 1.0f / II : 1.0f;
		const uniform float S = SpringMassScale * RowData.Stiffness * Dt * Dt;
		const uniform float D = SpringMassScale * RowData.Damping * Dt;
		const uniform float Multiplier = 1.0f / ((S + D) * II + 1.0f);
		const uniform float DLambda = Multiplier * (S * RowState.Error - D * AngVelDt - RowState.Lambda);

		//const FVector DR0 = IA0 * DLambda;
		//const FVector DR1 = IA1 * -DLambda;
		RowState.DRs[0] = RowState.Axis * (DLambda * II0);
		RowState.DRs[1] = RowState.Axis * -(DLambda * II1);
		RowState.Lambda += DLambda;
	}
	else
	{
		RowState.DRs[0] = IA0 * (RowData.Stiffness * RowState.Error / II);
		RowState.DRs[1] = IA1 * -(RowData.Stiffness * RowState.Error / II);
	}
}

static inline void BatchApplyRotationDelta(
	uniform FJointSolverJointState* uniform JointStates, 
	uniform int JointIndex)
{
	if (JointStates[JointIndex].InvMs[0] > 0.0f)
	{
		const uniform FVector4 Qs0 = JointStates[JointIndex].Qs[0];
		const uniform FVector4 DR0 = SetVector4(JointStates[JointIndex].DRs[0], 0);
		const uniform FVector4 DQ0 = VectorQuaternionMultiply2(DR0, Qs0) * 0.5f;
		const uniform FVector4 Q0 = VectorNormalizeQuaternion(Qs0 + DQ0);
		JointStates[JointIndex].Qs[0] = Q0;
	}

	if (JointStates[JointIndex].InvMs[1] > 0.0f)
	{
		const uniform FVector4 Qs1 = JointStates[JointIndex].Qs[1];
		const uniform FVector4 DR1 = SetVector4(JointStates[JointIndex].DRs[1], 0);
		const uniform FVector4 DQ1 = VectorQuaternionMultiply2(DR1, Qs1) * 0.5f;
		const uniform FVector4 Q1 = VectorNormalizeQuaternion(Qs1 + DQ1);
		JointStates[JointIndex].Qs[1] = Q1;
	}

	JointStates[JointIndex].Qs[1] = VectorQuaternionEnforceShortestArcWith(JointStates[JointIndex].Qs[1], JointStates[JointIndex].Qs[0]);
}

export uniform int BatchApplyRotationConstraints(
	uniform float Dt, 
	uniform FJointSolverJointState* uniform JointStates, 
	uniform FJointSolverConstraintRowData* uniform RowDatas, 
	uniform FJointSolverConstraintRowState* uniform RowStates, 
	uniform int JointIndexBegin, 
	uniform int JointIndexEnd, 
	uniform int RowIndexBegin, 
	uniform int RowIndexEnd)
{
	uniform int NumActiveConstraints = 0;

	// Calculate the delta position and rotation to fix the constraint errors
	// NOTE: Rows are in Joint order, so we can loop over them directly rather than looping over joints and then their rows.
	for (uniform int RowIndex = RowIndexBegin; RowIndex < RowIndexEnd; ++RowIndex)
	{
		const uniform int RowJointIndex = RowDatas[RowIndex].JointIndex;

		if (abs(RowStates[RowIndex].Error) > JointStates[RowJointIndex].AngleTolerance)
		{
			BatchApplyRotationConstraint(Dt, JointStates[RowJointIndex], RowDatas[RowIndex], RowStates[RowIndex]);

			JointStates[RowJointIndex].DRs[0] = JointStates[RowJointIndex].DRs[0] + RowStates[RowIndex].DRs[0];
			JointStates[RowJointIndex].DRs[1] = JointStates[RowJointIndex].DRs[1] + RowStates[RowIndex].DRs[1];
			++NumActiveConstraints;
		}
	}

	// Accumulate and apply the deltas for each joint
	for (uniform int32 JointIndex = JointIndexBegin; JointIndex < JointIndexEnd; ++JointIndex)
	{
		BatchApplyRotationDelta(JointStates, JointIndex);
	}

	return NumActiveConstraints;
}
