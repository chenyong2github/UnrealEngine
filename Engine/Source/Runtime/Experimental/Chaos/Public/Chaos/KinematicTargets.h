// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/ChaosArchive.h"
#include "Chaos/Transform.h"

namespace Chaos
{
	/**
	 * Controls how a kinematic body is integrated each Evolution Advance
	 */
	enum class EKinematicTargetMode
	{
		None,			/** Particle does not move and no data is changed */
		Reset,			/** Particle does not move, velocity and angular velocity are zeroed, then mode is set to "None". */
		Position,		/** Particle is moved to Kinematic Target transform, velocity and angular velocity updated to reflect the change, then mode is set to "Zero". */
		Velocity,		/** Particle is moved based on velocity and angular velocity, mode remains as "Velocity" until changed. */
	};

	/**
	 * Data used to integrate kinematic bodies
	 */
	template<class T, int d>
	class CHAOS_API TKinematicTarget
	{
	public:
		TKinematicTarget()
			: Mode(EKinematicTargetMode::None)
		{
		}

		/** Whether this kinematic target has been set (either velocity or position mode) */
		bool IsSet() const { return (Mode == EKinematicTargetMode::Position) || (Mode == EKinematicTargetMode::Velocity); }

		/** Get the kinematic target mode */
		EKinematicTargetMode GetMode() const { return Mode; }

		/** Get the target transform (asserts if not in Position mode) */
		const TRigidTransform<T, d>& GetTarget() const { check(Mode == EKinematicTargetMode::Position); return Target; }

		/** Get the particle's previous transform for velocity calculations) */
		const TRigidTransform<T, d>& GetPrevious() const { return Previous; }

		/** Clear the kinematic target */
		void Clear() { Target = TRigidTransform<T, d>(); Mode = EKinematicTargetMode::None; }

		/** Use transform target mode and set the transform target */
		void SetTargetMode(const TRigidTransform<T, d>& InTarget, const TRigidTransform<T, d>& InPrevious) { Target = InTarget; Previous = InPrevious;  Mode = EKinematicTargetMode::Position; }

		/** Use velocity target mode */
		void SetVelocityMode() { Mode = EKinematicTargetMode::Velocity; }

		// For internal use only
		void SetMode(EKinematicTargetMode InMode) { Mode = InMode; }

		friend FChaosArchive& operator<<(FChaosArchive& Ar, TKinematicTarget<T, d>& KinematicTarget)
		{
			Ar << KinematicTarget.Target << KinematicTarget.Mode;
			return Ar;
		}

		bool IsEqual(const TKinematicTarget& other) const
		{
			return (
				Mode == other.Mode &&
				Target.GetTranslation() == other.Target.GetTranslation() &&
				Target.GetRotation() == other.Target.GetRotation() &&
				Target.GetScale3D() == other.Target.GetScale3D() &&
				Previous.GetTranslation() == other.Previous.GetTranslation() &&
				Previous.GetRotation() == other.Previous.GetRotation() &&
				Previous.GetScale3D() == other.Previous.GetScale3D()
				);
		}

		template <typename TOther>
		bool IsEqual(const TOther& other) const
		{
			return IsEqual(other.KinematicTarget());
		}

		bool operator==(const TKinematicTarget& other) const
		{
			return IsEqual(other);
		}

	private:
		TRigidTransform<T, d> Previous;
		TRigidTransform<T, d> Target;
		EKinematicTargetMode Mode;
	};
}
