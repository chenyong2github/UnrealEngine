// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

namespace UE
{
	namespace Anim
	{
		/** Set of type-traits, used by the Animation Attributes system to verify and implement certain behavior */
		template <class AttributeType>
		struct TAttributeTypeTraitsBase
		{
			enum
			{
				/** Determines whether or not the type should be blended, and is supported to do so. True by default */
				IsBlendable = true,
				/** Determines whether or not the type has an associated user-defined implementation of IAttributeBlendOperator. False by default */
				WithCustomBlendOperator = false,
				/** Determines whether or not the type should be step-interpolated rather than linearly. False by default */
				StepInterpolate = false
			};
		};

		template <class AttributeType>
		struct TAttributeTypeTraits : public TAttributeTypeTraitsBase<AttributeType>
		{
		};
		
		/** Implemented functionality for retrieving a types trait value for platforms without constexpr support */
#if !PLATFORM_COMPILER_HAS_IF_CONSTEXPR
		/**
		 * Selection of IsBlendableType call.
		 */
		template<class AttributeType>
		FORCEINLINE typename TEnableIf<!TAttributeTypeTraits<AttributeType>::IsBlendable, bool>::Type IsBlendableType()
		{
			return false;
		}

		template<class AttributeType>
		FORCEINLINE typename TEnableIf<TAttributeTypeTraits<AttributeType>::IsBlendable, bool>::Type IsBlendableType()
		{
			return true;
		}

		/**
		 * Selection of IsStepInterpolatedType call.
		 */
		template<class AttributeType>
		FORCEINLINE typename TEnableIf<!TAttributeTypeTraits<AttributeType>::StepInterpolate, bool>::Type IsStepInterpolatedType()
		{
			return false;
		}

		template<class AttributeType>
		FORCEINLINE typename TEnableIf<TAttributeTypeTraits<AttributeType>::StepInterpolate, bool>::Type IsStepInterpolatedType()
		{
			return true;
		}

		/**
		 * Selection of CustomBlendedType call.
		 */
		template<class AttributeType>
		FORCEINLINE typename TEnableIf<!TAttributeTypeTraits<AttributeType>::WithCustomBlendOperator, bool>::Type CustomBlendedType()
		{
			return false;
		}

		template<class AttributeType>
		FORCEINLINE typename TEnableIf<TAttributeTypeTraits<AttributeType>::WithCustomBlendOperator, bool>::Type CustomBlendedType()
		{
			return true;
		}
#endif
	}
}
