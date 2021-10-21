// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Core.h"
#include "Chaos/Declares.h"
#include "Chaos/Vector.h"

namespace Chaos
{
	class FPBDConstraintContainer;
	class FPBDIndexedConstraintContainer;

	/**
	 * @brief A type id for constraint handles to support safe up/down casting (including intermediate classes in the hierrachy)
	 *
	 * Every constraint handle must provide a StaticType() member which gives the constraint type
	 * name and base class chain.
	 * 
	 * Every constraint container must provide a GetConstraintHandleType() method to get the constraint 
	 * type for handles that reference the container.
	*/
	class FConstraintHandleTypeID
	{
	public:
		FConstraintHandleTypeID(const FName& InName, const FConstraintHandleTypeID* InBaseType = nullptr)
			: TypeName(InName)
			, BaseType(InBaseType)
		{
		}

		/**
		 * @brief An invalid constraint handle type for initialization and invalidation
		*/
		static const FConstraintHandleTypeID InvalidTypeID()
		{
			return FConstraintHandleTypeID(NAME_None);
		}

		/**
		 * @brief Whether this type can be cast to the specified type
		*/
		bool IsA(const FConstraintHandleTypeID& TypeID) const
		{
			if (TypeID.TypeName == TypeName)
			{
				return true;
			}
			if (TypeID.BaseType != nullptr)
			{
				return IsA(*TypeID.BaseType);
			}
			return false;
		}

	private:
		FName TypeName;
		const FConstraintHandleTypeID* BaseType;
	};


	/**
	 * @brief Base class for constraint handles.
	 * 
	 * Constraints are referenced by handle in the constraint graph. 
	 * Constraint handles allow us to support different allocation and storage policies for constraints.
	 * E.g., heap-allocated constraints, array-based constraints etc.
	 * 
	 * @see FIndexedConstraintHandle, FIntrusiveConstraintHandle
	*/
	class CHAOS_API FConstraintHandle
	{
	public:
		using FGeometryParticleHandle = TGeometryParticleHandle<FReal, 3>;

		FConstraintHandle() 
			: ConstraintContainer(nullptr)
			, ConstraintGraphIndex(INDEX_NONE) 
		{
		}

		FConstraintHandle(FPBDConstraintContainer* InContainer)
			: ConstraintContainer(InContainer)
			, ConstraintGraphIndex(INDEX_NONE)
		{
		}

		virtual ~FConstraintHandle()
		{
		}

		virtual bool IsValid() const
		{
			// @todo(chaos): why does IsValid() also check IsEnabled()?
			return (ConstraintContainer != nullptr) && IsEnabled();
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

		virtual void SetEnabled(bool InEnabled) = 0;

		virtual bool IsEnabled() const = 0;

		virtual bool IsSleeping() const { return false; }
		virtual void SetIsSleeping(const bool bInIsSleeping) {}

		// Implemented in ConstraintContainer.h
		int32 GetContainerId() const;

		// Implemented in ConstraintContainer.h
		template<typename T>  T* As();
		template<typename T>  const T* As() const;

		static const FConstraintHandleTypeID& StaticType()
		{
			static FConstraintHandleTypeID STypeID(TEXT("FConstraintHandle"), nullptr);
			return STypeID;
		}

	protected:
		friend class FPBDConstraintContainer;

		FPBDConstraintContainer* ConstraintContainer;
		
		// @todo(chaos): move constraint graph index to base constraint container
		int32 ConstraintGraphIndex;
	};


	/**
	 * @brief Base class for constraints that are allocated at permanent memory addresses and inherit the handle.
	 *
	 * Intended for use by constraint types that are allocated on the heap or in a block allocator and therefore have a persistent
	 * address (as opposed to array-based containers where the array could relocate). The constraint class should inherit
	 * this handle class. This effectively eliminates the handle, reducing cache misses and allocations.
	*/
	class CHAOS_API FIntrusiveConstraintHandle : public FConstraintHandle
	{
	public:
		FIntrusiveConstraintHandle()
			: FConstraintHandle()
		{
		}

		void SetContainer(FPBDConstraintContainer* InContainer)
		{
			ConstraintContainer = InContainer;
		}

		static const FConstraintHandleTypeID& StaticType()
		{
			static FConstraintHandleTypeID STypeID(TEXT("FIntrusiveConstraintHandle"), &FConstraintHandle::StaticType());
			return STypeID;
		}
	};


	/**
	 * @brief Base class for constraints that are allocated at permanent memory addresses and inherit the handle.
	 * 
	 * @see FIntrusiveConstraintHandle
	 * 
	 * @tparam T_CONSTRAINT The constraint type
	*/
	template<typename T_CONSTRAINT>
	class CHAOS_API TIntrusiveConstraintHandle : public FIntrusiveConstraintHandle
	{
	public:
		using FConstraint = T_CONSTRAINT;

		TIntrusiveConstraintHandle()
			: FIntrusiveConstraintHandle()
		{
		}

		void SetContainer(FPBDConstraintContainer* InContainer)
		{
			ConstraintContainer = InContainer;
		}

		FConstraint* GetConstraint()
		{
			return static_cast<FConstraint*>(this);
		}

		const FConstraint* GetConstraint() const
		{
			return static_cast<const FConstraint*>(this);
		}
	};

	/**
	 * Base class for handles to constraints in an index-based container
	 */
	class CHAOS_API FIndexedConstraintHandle : public FConstraintHandle
	{
	public:
		using FGeometryParticleHandle = TGeometryParticleHandle<FReal, 3>;

		FIndexedConstraintHandle() 
			: FConstraintHandle()
			, ConstraintIndex(INDEX_NONE)
		{
		}

		FIndexedConstraintHandle(FPBDConstraintContainer* InContainer, int32 InConstraintIndex)
			: FConstraintHandle(InContainer)
			, ConstraintIndex(InConstraintIndex)
		{
		}

		virtual ~FIndexedConstraintHandle()
		{
		}

		virtual bool IsValid() const override
		{
			return (ConstraintIndex != INDEX_NONE) && FConstraintHandle::IsValid();
		}

		int32 GetConstraintIndex() const
		{
			return ConstraintIndex;
		}

		static const FConstraintHandleTypeID& StaticType()
		{
			static FConstraintHandleTypeID STypeID(TEXT("FIndexedConstraintHandle"), &FConstraintHandle::StaticType());
			return STypeID;
		}

	protected:
		friend class FPBDIndexedConstraintContainer;

		int32 ConstraintIndex;
	};


	/**
	 * Utility base class for ConstraintHandles. Provides basic functionality common to most constraint containers.
	 */
	template<typename T_CONTAINER>
	class CHAOS_API TIndexedContainerConstraintHandle : public FIndexedConstraintHandle
	{
	public:
		using Base = FIndexedConstraintHandle;
		using FGeometryParticleHandle = typename Base::FGeometryParticleHandle;
		using FConstraintContainer = T_CONTAINER;

		TIndexedContainerConstraintHandle()
			: FIndexedConstraintHandle()
		{
		}
		
		TIndexedContainerConstraintHandle(FConstraintContainer* InConstraintContainer, int32 InConstraintIndex)
			: FIndexedConstraintHandle(InConstraintContainer, InConstraintIndex)
		{
		}

		inline virtual void SetEnabled(bool bInEnabled) override
		{
			if (ConcreteContainer() != nullptr)
			{
				ConcreteContainer()->SetConstraintEnabled(ConstraintIndex, bInEnabled);
			}
		}

		inline virtual bool IsEnabled() const override
		{
			if (ConcreteContainer() != nullptr)
			{
				return ConcreteContainer()->IsConstraintEnabled(ConstraintIndex);
			}
			return false;
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

		void FreeHandle(FConstraintContainerHandle* Handle) { delete Handle; }
	};
}
