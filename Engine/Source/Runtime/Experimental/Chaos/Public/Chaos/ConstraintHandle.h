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

		FConstraintHandle() : ConstraintContainer(nullptr), ConstraintIndex(INDEX_NONE), ConstraintGraphIndex(INDEX_NONE) { }

		FConstraintHandle(FPBDConstraintContainer* InContainer, int32 InConstraintIndex): ConstraintContainer(InContainer), ConstraintIndex(InConstraintIndex), ConstraintGraphIndex(INDEX_NONE) {}

		virtual ~FConstraintHandle() {}

		bool IsValid() const
		{
			// @todo(chaos): why does IsValid() also check IsEnabled()?
			return (ConstraintContainer != nullptr) && (ConstraintIndex != INDEX_NONE) && IsEnabled();
		}

		int32 GetConstraintIndex() const
		{
			return ConstraintIndex;
		}

		int32 GetConstraintGraphIndex() const
		{
			return ConstraintGraphIndex;
		}

		void SetConstraintGraphIndex(int32 InIndex)
		{
			ConstraintGraphIndex = InIndex;
		}

		bool IsInConstraintGraph() const
		{
			return (ConstraintGraphIndex != INDEX_NONE);
		}


		// Implemented in ConstraintContainer.h
		int32 GetContainerId() const;

		// Implemented in ConstraintContainer.h
		void SetEnabled(bool InEnabled);

		// Implemented in ConstraintContainer.h
		bool IsEnabled() const;

		// Implemented in ConstraintContainer.h
		template<typename T>  T* As();
		template<typename T>  const T* As() const;

	protected:
		friend class FPBDConstraintContainer;

		FPBDConstraintContainer* ConstraintContainer;
		int32 ConstraintIndex;
		int32 ConstraintGraphIndex; // @todo(chaos): move constraint graph index to base constraint container
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

		TContainerConstraintHandle()
		{
		}
		
		TContainerConstraintHandle(FConstraintContainer* InConstraintContainer, int32 InConstraintIndex)
			: FConstraintHandle(InConstraintContainer, InConstraintIndex)
		{
		}

		// @todo(chaos): Make this a virtual on FConstraintContainer and move to base class
		void RemoveConstraint()
		{
			ConcreteContainer()->RemoveConstraint(ConstraintIndex);
		}

	protected:
		FConstraintContainer* ConcreteContainer()
		{
			return static_cast<FConstraintContainer*>(ConstraintContainer);
		}

		const FConstraintContainer* ConcreteContainer() const
		{
			return static_cast<const FConstraintContainer*>(ConstraintContainer);
		}

		using Base::ConstraintIndex;
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
