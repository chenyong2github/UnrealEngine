// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/ConstraintHandle.h"
#include "Chaos/Transform.h"

namespace Chaos
{
	// @todo(chaos): get rid of this - we should be able to extend constraint types without having to add to an enum
	enum class EConstraintContainerType : uint8
	{
		Invalid = 0,
		Collision,
		RigidSpring,
		DynamicSpring,
		Position,
		Joint,
		Suspension
	};

	/**
	 * Base class for containers of constraints.
	 * A Constraint Container holds an array of constraints and provides methods to allocate and deallocate constraints
	 *as well as the API required to plug into Constraint Rules.
	 */
	class CHAOS_API FPBDConstraintContainer
	{
	public:
		FPBDConstraintContainer(EConstraintContainerType InType);

		virtual ~FPBDConstraintContainer();

		// The ContainerId is used by the Evolution to map constraints graph items back to constraints
		int32 GetContainerId() const
		{
			return ContainerId;
		}

		void SetContainerId(int32 InContainerId)
		{
			ContainerId = InContainerId;
		}

		EConstraintContainerType GetConstraintType() const
		{
			return ConstraintContainerType;
		}

		virtual void SetConstraintEnabled(int32 ConstraintIndex, bool bEnabled) { }
		virtual bool IsConstraintEnabled(int32 ConstraintIndex) const { return true; }
		virtual void DisconnectConstraints(const TSet<TGeometryParticleHandle<FReal, 3>*>&) {}

	protected:
		int32 GetConstraintIndex(const FConstraintHandle* ConstraintHandle) const;
		void SetConstraintIndex(FConstraintHandle* ConstraintHandle, int32 ConstraintIndex) const;

	private:
		EConstraintContainerType ConstraintContainerType;
		int32 ContainerId;
	};


	//
	// From ConstraintHandle.h
	//

	inline int32 FConstraintHandle::GetContainerId() const
	{
		if (ConstraintContainer != nullptr)
		{
			return ConstraintContainer->GetContainerId();
		}
		return INDEX_NONE;
	}

	inline void FConstraintHandle::SetEnabled(bool bInEnabled)
	{
		if (ConstraintContainer != nullptr)
		{
			ConstraintContainer->SetConstraintEnabled(ConstraintIndex, bInEnabled);
		}
	}

	inline bool FConstraintHandle::IsEnabled() const
	{
		if (ConstraintContainer != nullptr)
		{
			return ConstraintContainer->IsConstraintEnabled(ConstraintIndex);
		}
		return false;
	}

	template<typename T>
	inline T* FConstraintHandle::As()
	{
		return ((ConstraintContainer != nullptr) && (T::StaticType() == ConstraintContainer->GetConstraintType())) ? static_cast<T*>(this) : nullptr;
	}

	template<typename T>
	inline const T* FConstraintHandle::As() const
	{
		return ((ConstraintContainer != nullptr) && (T::StaticType() == ConstraintContainer->GetConstraintType())) ? static_cast<const T*>(this) : nullptr;
	}

}