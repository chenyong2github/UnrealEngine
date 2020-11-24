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
		enum class EType : uint8
		{
			Invalid = 0,
			Collision,
			RigidSpring,
			DynamicSpring,
			Position,
			Joint,
			Suspension
		};


		using FGeometryParticleHandle = TGeometryParticleHandle<FReal, 3>;

		FConstraintHandle() : Type(EType::Invalid), ConstraintIndex(INDEX_NONE) { }
		FConstraintHandle(EType InType, int32 InConstraintIndex): Type(InType), ConstraintIndex(InConstraintIndex) {}
		virtual ~FConstraintHandle() {}

		bool IsValid() const
		{
			return (ConstraintIndex != INDEX_NONE && IsEnabled());
		}

		int32 GetConstraintIndex() const
		{
			return ConstraintIndex;
		}

		virtual void SetEnabled(bool InEnabled) = 0;
		virtual bool IsEnabled() const = 0;

		template<typename T>  T* As() { return T::StaticType() == Type ? static_cast<T*>(this) : nullptr; }
		template<typename T>  const T* As() const { return T::StaticType() == Type ? static_cast<const T*>(this) : nullptr; }

	protected:
		friend class FPBDConstraintContainer;

		EType Type;
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
		TContainerConstraintHandle(Base::EType InType, FConstraintContainer* InConstraintContainer, int32 InConstraintIndex)
			: FConstraintHandle(InType, InConstraintIndex), ConstraintContainer(InConstraintContainer) {}

		void RemoveConstraint() { ConstraintContainer->RemoveConstraint(ConstraintIndex); }

		void SetEnabled(bool InEnabled) override { ConstraintContainer->SetConstraintEnabled(ConstraintIndex,InEnabled); }
		bool IsEnabled() const override { return ConstraintContainer->IsConstraintEnabled(ConstraintIndex); }

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
