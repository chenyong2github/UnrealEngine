// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Core.h"
#include "Chaos/Declares.h"
#include "Chaos/Vector.h"

namespace Chaos
{
	class FPBDConstraintContainer;

	/**
	 * Base class for constraint handles.
	 */
	class CHAOS_API FConstraintHandle
	{
	public:
		using FGeometryParticleHandle = TGeometryParticleHandle<FReal, 3>;

		FConstraintHandle() : ConstraintIndex(INDEX_NONE) { }
		FConstraintHandle(int32 InConstraintIndex): ConstraintIndex(InConstraintIndex) {}
		~FConstraintHandle() {}

		bool IsValid() const
		{
			return (ConstraintIndex != INDEX_NONE );
		}

		int32 GetConstraintIndex() const
		{
			return ConstraintIndex;
		}

		// @todo(ccaulfield): Add checked down - casting to specific constraint - type handle
		template<typename T>  T* As() { return static_cast<T*>(this); }

	protected:
		friend class FPBDConstraintContainer;

		int32 ConstraintIndex;
	};


	/**
	 * Utility base class for ConstraintHandles. Provides basic functionality common to most constraint containers.
	 */
	template<typename T_CONTAINER>
	class CHAOS_API TContainerConstraintHandle : public FConstraintHandle
	{
	public:
		using Base = FConstraintHandle;
		using FGeometryParticleHandle = typename Base::FGeometryParticleHandle;
		using FConstraintContainer = T_CONTAINER;

		TContainerConstraintHandle() : ConstraintContainer(nullptr) {}
		TContainerConstraintHandle(FConstraintContainer* InConstraintContainer, int32 InConstraintIndex) 
			: FConstraintHandle(InConstraintIndex), ConstraintContainer(InConstraintContainer) {}

		void RemoveConstraint() { ConstraintContainer->RemoveConstraint(ConstraintIndex); }

	protected:
		using Base::ConstraintIndex;
		FConstraintContainer* ConstraintContainer;
	};


	/**
	 * An allocator for constraint handles.
	 *
	 * @todo(ccaulfield): block allocator for handles, or support custom allocators in constraint containers.
	 */
	template<class T_CONTAINER>
	class CHAOS_API TConstraintHandleAllocator
	{
	public:
		using FConstraintContainer = T_CONTAINER;
		using FConstraintContainerHandle = typename FConstraintContainer::FConstraintContainerHandle;

		FConstraintContainerHandle* AllocHandle(FConstraintContainer* ConstraintContainer, int32 ConstraintIndex) { return new FConstraintContainerHandle(ConstraintContainer, ConstraintIndex); }
		template<class TYPE>
		FConstraintContainerHandle* AllocHandle(FConstraintContainer* ConstraintContainer, int32 ConstraintIndex) { return new FConstraintContainerHandle(ConstraintContainer, ConstraintIndex, TYPE::StaticType()); }
		void FreeHandle(FConstraintContainerHandle* Handle) { delete Handle; }
	};
}
