// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Declares.h"
#include "Chaos/Vector.h"

namespace Chaos
{
	template<class T, int d>
	class TPBDConstraintContainer;

	/**
	 * Base class for constraint handles.
	 * @todo(ccaulfield): Add checked down-casting to specific constraint-type handle
	 */
	template<class T, int d>
	class CHAOS_API TConstraintHandle
	{
	public:
		using FReal = T;
		static const int Dimensions = d;
		using FGeometryParticleHandle = TGeometryParticleHandle<FReal, Dimensions>;

		TConstraintHandle() : ConstraintIndex(INDEX_NONE) {}
		TConstraintHandle(int32 InConstraintIndex) : ConstraintIndex(InConstraintIndex) {}
		~TConstraintHandle() {}

		bool IsValid() const
		{
			return (ConstraintIndex != INDEX_NONE);
		}

		int32 GetConstraintIndex() const
		{
			return ConstraintIndex;
		}

	protected:
		friend class TPBDConstraintContainer<T, d>;

		int32 ConstraintIndex;
	};


	/**
	 * Utility base class for ConstraintHandles. Provides basic functionality common to most constraint containers.
	 */
	template<typename T_CONTAINER>
	class CHAOS_API TContainerConstraintHandle : public TConstraintHandle<typename T_CONTAINER::FReal, T_CONTAINER::Dimensions>
	{
	public:
		using Base = TConstraintHandle<typename T_CONTAINER::FReal, T_CONTAINER::Dimensions>;
		using FReal = typename Base::FReal;
		static const int Dimensions = Base::Dimensions;
		using FGeometryParticleHandle = typename Base::FGeometryParticleHandle;
		using FConstraintContainer = T_CONTAINER;

		TContainerConstraintHandle() : ConstraintContainer(nullptr) {}
		TContainerConstraintHandle(FConstraintContainer* InConstraintContainer, int32 InConstraintIndex) : TConstraintHandle<FReal, Dimensions>(InConstraintIndex), ConstraintContainer(InConstraintContainer) {}

		void RemoveConstraint() { ConstraintContainer->RemoveConstraint(ConstraintIndex); }

		TVector<FGeometryParticleHandle*, 2> GetConstrainedParticles() const { return ConstraintContainer->GetConstrainedParticles(ConstraintIndex); }

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
		using FConstraintHandle = typename FConstraintContainer::FConstraintHandle;

		FConstraintHandle* AllocHandle(FConstraintContainer* ConstraintContainer, int32 ConstraintIndex) { return new FConstraintHandle(ConstraintContainer, ConstraintIndex); }
		void FreeHandle(FConstraintHandle* Handle) { delete Handle; }
	};
}
