// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

#include "Chaos/ParticleHandleFwd.h"
#include "Chaos/PBDJointConstraintTypes.h"


namespace Chaos
{
	template<typename T, int d>
	class TPBDJointUtilities
	{
	public:
		/**
		 * Increase the lower inertia components to ensure that the maximum ratio between any pair of elements is MaxRatio.
		 *
		 * @param InI The input inertia.
		 * @return An altered inertia so that the minimum element is at least MaxElement/MaxRatio.
		 */
		static CHAOS_API TVector<T, d> ConditionInertia(
			const TVector<T, d>& InI, 
			const T MaxRatio);

		/**
		 * Increase the IParent inertia so that its largest component is at least MinRatio times the largest IChild component.
		 * This is used to condition joint chains for more robust solving with low iteration counts or larger time steps.
		 *
		 * @param IParent The input inertia.
		 * @param IChild The input inertia.
		 * @param OutIParent The output inertia.
		 * @param MinRatio Parent inertia will be at least this multiple of child inertia
		 * @return The max/min ratio of InI elements.
		 */
		static CHAOS_API TVector<T, d> ConditionParentInertia(
			const TVector<T, d>& IParent, 
			const TVector<T, d>& IChild, 
			const T MinRatio);

		static CHAOS_API T ConditionParentMass(
			const T MParent, 
			const T MChild, 
			const T MinRatio);

		static CHAOS_API void GetConditionedInverseMass(
			const TPBDRigidParticleHandle<T, d>* PParent, 
			const TPBDRigidParticleHandle<T, d>* PChild, 
			T& OutInvMParent, 
			T& OutInvMChild, 
			PMatrix<T, d, d>& OutInvIParent, 
			PMatrix<T, d, d>& OutInvIChild, 
			const T MinParentMassRatio, 
			const T MaxInertiaRatio);

		static CHAOS_API void GetConditionedInverseMass(
			const TPBDRigidParticleHandle<T, d>* P0, 
			T& OutInvM0, 
			PMatrix<T, d, d>& OutInvI0, 
			const T MaxInertiaRatio);

		static CHAOS_API void CalculateSwingConstraintSpace(
			const TPBDJointSolverSettings<T, d>& SolverSettings,
			const TPBDJointSettings<T, d>& JointSettings,
			const int32 Index0,
			const int32 Index1,
			TVector<T, d>& P0,
			TRotation<T, d>& Q0,
			TVector<T, d>& P1,
			TRotation<T, d>& Q1,
			TVector<T, d>& OutX0,
			PMatrix<T, d, d>& OutR0,
			TVector<T, d>& OutX1,
			PMatrix<T, d, d>& OutR1,
			TVector<T, d>& OutCR);

		static CHAOS_API void CalculateConeConstraintSpace(
			const TPBDJointSolverSettings<T, d>& SolverSettings,
			const TPBDJointSettings<T, d>& JointSettings,
			const int32 Index0,
			const int32 Index1,
			TVector<T, d>& P0,
			TRotation<T, d>& Q0,
			TVector<T, d>& P1,
			TRotation<T, d>& Q1,
			TVector<T, d>& OutX0,
			PMatrix<T, d, d>& OutR0,
			TVector<T, d>& OutX1,
			PMatrix<T, d, d>& OutR1,
			TVector<T, d>& OutCR);

		static CHAOS_API void ApplyJointPositionConstraint(
			const T Dt,
			const TPBDJointSolverSettings<T, d>& SolverSettings,
			const TPBDJointSettings<T, d>& JointSettings,
			const int32 Index0,
			const int32 Index1,
			TVector<T, d>& P0,
			TRotation<T, d>& Q0,
			TVector<T, d>& P1,
			TRotation<T, d>& Q1,
			float InvM0,
			const PMatrix<T, d, d>& InvIL0,
			float InvM1,
			const PMatrix<T, d, d>& InvIL1);

		static CHAOS_API void ApplyJointTwistConstraint(
			const T Dt,
			const TPBDJointSolverSettings<T, d>& SolverSettings,
			const TPBDJointSettings<T, d>& JointSettings,
			const int32 Index0,
			const int32 Index1,
			TVector<T, d>& P0,
			TRotation<T, d>& Q0,
			TVector<T, d>& P1,
			TRotation<T, d>& Q1,
			float InvM0,
			const PMatrix<T, d, d>& InvIL0,
			float InvM1,
			const PMatrix<T, d, d>& InvIL1);

		static CHAOS_API void ApplyJointConeConstraint(
			const T Dt,
			const TPBDJointSolverSettings<T, d>& SolverSettings,
			const TPBDJointSettings<T, d>& JointSettings,
			const int32 Index0,
			const int32 Index1,
			TVector<T, d>& P0,
			TRotation<T, d>& Q0,
			TVector<T, d>& P1,
			TRotation<T, d>& Q1,
			float InvM0,
			const PMatrix<T, d, d>& InvIL0,
			float InvM1,
			const PMatrix<T, d, d>& InvIL1);

		static CHAOS_API void ApplyJointSwingConstraint(
			const T Dt,
			const TPBDJointSolverSettings<T, d>& SolverSettings,
			const TPBDJointSettings<T, d>& JointSettings,
			const int32 Index0,
			const int32 Index1,
			const EJointAngularConstraintIndex SwingConstraint,
			TVector<T, d>& P0,
			TRotation<T, d>& Q0,
			TVector<T, d>& P1,
			TRotation<T, d>& Q1,
			float InvM0,
			const PMatrix<T, d, d>& InvIL0,
			float InvM1,
			const PMatrix<T, d, d>& InvIL1);

		static CHAOS_API void ApplyJointTwistDrive(
			const T Dt,
			const TPBDJointSolverSettings<T, d>& SolverSettings,
			const TPBDJointSettings<T, d>& JointSettings,
			const int32 Index0,
			const int32 Index1,
			TVector<T, d>& P0,
			TRotation<T, d>& Q0,
			TVector<T, d>& P1,
			TRotation<T, d>& Q1,
			float InvM0,
			const PMatrix<T, d, d>& InvIL0,
			float InvM1,
			const PMatrix<T, d, d>& InvIL1);

		static CHAOS_API void ApplyJointConeDrive(
			const T Dt,
			const TPBDJointSolverSettings<T, d>& SolverSettings,
			const TPBDJointSettings<T, d>& JointSettings,
			const int32 Index0,
			const int32 Index1,
			TVector<T, d>& P0,
			TRotation<T, d>& Q0,
			TVector<T, d>& P1,
			TRotation<T, d>& Q1,
			float InvM0,
			const PMatrix<T, d, d>& InvIL0,
			float InvM1,
			const PMatrix<T, d, d>& InvIL1);

		static CHAOS_API void ApplyJointSLerpDrive(
			const T Dt,
			const TPBDJointSolverSettings<T, d>& SolverSettings,
			const TPBDJointSettings<T, d>& JointSettings,
			const int32 Index0,
			const int32 Index1,
			TVector<T, d>& P0,
			TRotation<T, d>& Q0,
			TVector<T, d>& P1,
			TRotation<T, d>& Q1,
			float InvM0,
			const PMatrix<T, d, d>& InvIL0,
			float InvM1,
			const PMatrix<T, d, d>& InvIL1);
	};
}