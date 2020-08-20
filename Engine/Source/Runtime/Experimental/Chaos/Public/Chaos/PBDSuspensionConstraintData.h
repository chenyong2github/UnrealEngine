// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "Chaos/PBDSuspensionConstraintTypes.h"
#include "Chaos/ParticleDirtyFlags.h"
#include "Chaos/Framework/PhysicsProxyBase.h"
#include "Chaos/Framework/PhysicsSolverBase.h"
#include "Chaos/PBDConstraintBaseData.h"

namespace Chaos
{

	enum class ESuspensionConstraintFlags : uint64_t
	{												
		Location = 0,
		Enabled = static_cast<uint64_t>(1) << 1,
		Target = static_cast<uint64_t>(1) << 2,
		HardstopStiffness = static_cast<uint64_t>(1) << 3,
		HardstopVelocityCompensation = static_cast<uint64_t>(1) << 4,
		SpringPreload = static_cast<uint64_t>(1) << 5,
		SpringStiffness = static_cast<uint64_t>(1) << 6,
		SpringDamping = static_cast<uint64_t>(1) << 7,
		MinLength = static_cast<uint64_t>(1) << 8,
		MaxLength = static_cast<uint64_t>(1) << 9,
		Axis = static_cast<uint64_t>(1) << 10,

		DummyFlag
	};

	using FSuspensionConstraintDirtyFlags = TDirtyFlags<ESuspensionConstraintFlags>;

	class CHAOS_API FSuspensionConstraint : public FConstraintBase
	{
	public:
		typedef FPBDSuspensionSettings FData;
		typedef FPBDSuspensionConstraintHandle FHandle;
		friend FData;

		FSuspensionConstraint();
		virtual ~FSuspensionConstraint() override {}

		bool IsDirty() const { return MDirtyFlags.IsDirty(); }
		bool IsDirty(const ESuspensionConstraintFlags CheckBits) const { return MDirtyFlags.IsDirty(CheckBits); }
		void ClearDirtyFlags() { MDirtyFlags.Clear(); }

		const FData& GetSuspensionSettings()const { return SuspensionSettings; }

		CONSTRAINT_JOINT_PROPERPETY_IMPL(bool, Enabled, ESuspensionConstraintFlags::Enabled, SuspensionSettings.Enabled);

		CONSTRAINT_JOINT_PROPERPETY_IMPL(FVec3, Target, ESuspensionConstraintFlags::Target, SuspensionSettings.Target);

		CONSTRAINT_JOINT_PROPERPETY_IMPL(FVec3, Location, ESuspensionConstraintFlags::Location, Location);

		CONSTRAINT_JOINT_PROPERPETY_IMPL(FReal, HardstopStiffness, ESuspensionConstraintFlags::HardstopStiffness, SuspensionSettings.HardstopStiffness);

		CONSTRAINT_JOINT_PROPERPETY_IMPL(FReal, HardstopVelocityCompensation, ESuspensionConstraintFlags::HardstopVelocityCompensation, SuspensionSettings.HardstopVelocityCompensation);

		CONSTRAINT_JOINT_PROPERPETY_IMPL(FReal, SpringPreload, ESuspensionConstraintFlags::SpringPreload, SuspensionSettings.SpringPreload);

		CONSTRAINT_JOINT_PROPERPETY_IMPL(FReal, SpringStiffness, ESuspensionConstraintFlags::SpringStiffness, SuspensionSettings.SpringStiffness);

		CONSTRAINT_JOINT_PROPERPETY_IMPL(FReal, SpringDamping, ESuspensionConstraintFlags::SpringDamping, SuspensionSettings.SpringDamping);

		CONSTRAINT_JOINT_PROPERPETY_IMPL(FReal, MinLength, ESuspensionConstraintFlags::MinLength, SuspensionSettings.MinLength);

		CONSTRAINT_JOINT_PROPERPETY_IMPL(FReal, MaxLength, ESuspensionConstraintFlags::MaxLength, SuspensionSettings.MaxLength);

		CONSTRAINT_JOINT_PROPERPETY_IMPL(FVec3, Axis, ESuspensionConstraintFlags::Axis, SuspensionSettings.Axis);


	protected:
		FSuspensionConstraintDirtyFlags MDirtyFlags;
		FData SuspensionSettings;

		FVec3 Location;	// spring local offset
		FVec3 Target;	// target spring (wheel) end position
	};


} // Chaos
