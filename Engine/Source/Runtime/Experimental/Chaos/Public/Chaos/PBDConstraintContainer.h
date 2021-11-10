// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/ConstraintHandle.h"
#include "Chaos/Transform.h"
#include "Chaos/Evolution/SolverBody.h"

namespace Chaos
{
	/**
	 * Base class for containers of constraints.
	 * A Constraint Container holds an array of constraints and provides methods to allocate and deallocate constraints
	 *as well as the API required to plug into Constraint Rules.
	 */
	class CHAOS_API FPBDConstraintContainer
	{
	public:
		FPBDConstraintContainer(FConstraintHandleTypeID InConstraintHandleType);

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

		const FConstraintHandleTypeID& GetConstraintHandleType() const
		{
			return ConstraintHandleType;
		}

		virtual void DisconnectConstraints(const TSet<TGeometryParticleHandle<FReal, 3>*>&) {}

	protected:
		FConstraintHandleTypeID ConstraintHandleType;
		int32 ContainerId;
	};

	class CHAOS_API FPBDIndexedConstraintContainer : public FPBDConstraintContainer
	{
	public:
		FPBDIndexedConstraintContainer(FConstraintHandleTypeID InType)
			: FPBDConstraintContainer(InType)
		{
		}

		virtual void SetConstraintEnabled(int32 ConstraintIndex, bool bEnabled) { }
		virtual bool IsConstraintEnabled(int32 ConstraintIndex) const { return true; }

	protected:
		int32 GetConstraintIndex(const FIndexedConstraintHandle* ConstraintHandle) const;
		void SetConstraintIndex(FIndexedConstraintHandle* ConstraintHandle, int32 ConstraintIndex) const;
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

	inline const FConstraintHandleTypeID& FConstraintHandle::GetType() const
	{
		if (ConstraintContainer != nullptr)
		{
			return ConstraintContainer->GetConstraintHandleType();
		}
		return FConstraintHandle::InvalidType();
	}

	template<typename T>
	inline T* FConstraintHandle::As()
	{
		// @todo(chaos): we need a safe cast that allows for casting to intermediate base classes (e.g., FIndexedConstraintHandle)
		return ((ConstraintContainer != nullptr) && ConstraintContainer->GetConstraintHandleType().IsA(T::StaticType())) ? static_cast<T*>(this) : nullptr;
	}

	template<typename T>
	inline const T* FConstraintHandle::As() const
	{
		return ((ConstraintContainer != nullptr) && ConstraintContainer->GetConstraintHandleType().IsA(T::StaticType())) ? static_cast<const T*>(this) : nullptr;
	}

}