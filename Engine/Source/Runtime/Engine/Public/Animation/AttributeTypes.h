// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Animation/AttributesRuntime.h"
#include "Animation/CustomAttributes.h"
#include "Animation/IAttributeBlendOperator.h"
#include "Animation/AttributeBlendOperator.h"
#include "Animation/AttributeTraits.h"
#include "CoreMinimal.h"
#include "UObject/WeakObjectPtr.h"

#include "CoreTypes.h"
#include "Templates/UnrealTemplate.h"

enum EAdditiveAnimationType;

namespace UE
{
	namespace Anim
	{
		/** Concept used to verify a user-defined attribute type with its TAttributeTypeTraits::Blendable value set to true */
		struct CBlendableAttribute
		{
			template <typename T>
			auto Requires(T& Val) -> decltype(
				Val.Multiply(.5f),
				Val.Accumulate(Val, 1.f, (EAdditiveAnimationType)0),
				Val.MakeAdditive(Val),
				Val.Interpolate(Val, 0.5f)
			);
		};

		struct ENGINE_API AttributeTypes
		{
		protected:
			static void Initialize();

			static TArray<TWeakObjectPtr<const UScriptStruct>> RegisteredTypes;
			static TArray<TUniquePtr<IAttributeBlendOperator>> Operators;
			static TArray<TWeakObjectPtr<const UScriptStruct>> InterpolatableTypes;
		public:

			/** Used for registering an attribute type for which TAttributeTypeTraits::WithCustomBlendOperator is set to true, use RegisterType() otherwise */
			template<typename AttributeType, typename OperatorType, typename... OperatorArgs>
			static void RegisterTypeWithOperator(OperatorArgs&&... args)
			{
				AttributeTypes::Initialize();

				static_assert(TAttributeTypeTraits<AttributeType>::WithCustomBlendOperator, "Attribute type does not require a custom blend operation");
				UScriptStruct* ScriptStruct = AttributeType::StaticStruct();

				AttributeTypes::RegisteredTypes.Add(ScriptStruct);
				AttributeTypes::Operators.Add(MakeUnique<OperatorType>(Forward<OperatorArgs>(args)...));

#if PLATFORM_COMPILER_HAS_IF_CONSTEXPR
				if constexpr (!UE::Anim::TAttributeTypeTraits<AttributeType>::StepInterpolate)
#else
				if (!UE::Anim::IsStepInterpolatedType<AttributeType>())
#endif // PLATFORM_COMPILER_HAS_IF_CONSTEXPR	
				{
					AttributeTypes::InterpolatableTypes.Add(ScriptStruct);
				}
			}

			/** Used for registering an attribute type for which TAttributeTypeTraits::WithCustomBlendOperator is set to false, use RegisterTypeWithOperator() otherwise */
#if PLATFORM_COMPILER_HAS_IF_CONSTEXPR
			template<typename AttributeType>
			static void RegisterType()
			{
				static_assert(!TAttributeTypeTraits<AttributeType>::WithCustomBlendOperator, "Attribute type requires a custom blend operation");

				AttributeTypes::Initialize();
				UScriptStruct* ScriptStruct = AttributeType::StaticStruct();
				AttributeTypes::RegisteredTypes.Add(ScriptStruct);
				
				if constexpr (UE::Anim::TAttributeTypeTraits<AttributeType>::IsBlendable)	
				{
					static_assert(TModels<CBlendableAttribute, AttributeType>::Value, "Missing function implementations required for Attribute blending");
					
					if constexpr (!UE::Anim::TAttributeTypeTraits<AttributeType>::StepInterpolate)
					{
						AttributeTypes::InterpolatableTypes.Add(ScriptStruct);
					}
				}

				AttributeTypes::Operators.Add(MakeUnique<TAttributeBlendOperator<AttributeType>>());
			}
#else
			template<typename AttributeType>
			static typename TEnableIf<TAttributeTypeTraits<AttributeType>::IsBlendable, void>::Type RegisterType()
			{
				static_assert(!TAttributeTypeTraits<AttributeType>::WithCustomBlendOperator, "Attribute type requires a custom blend operation");
				static_assert(TModels<CBlendableAttribute, AttributeType>::Value, "Missing arithmetic operators required for Attribute blending");

				AttributeTypes::Initialize();
				UScriptStruct* ScriptStruct = AttributeType::StaticStruct();
				AttributeTypes::RegisteredTypes.Add(ScriptStruct);

				if (!UE::Anim::IsStepInterpolatedType<AttributeType>())
				{
					
					AttributeTypes::InterpolatableTypes.Add(ScriptStruct);
				}

				AttributeTypes::Operators.Add(MakeUnique<TAttributeBlendOperator<AttributeType>>());
			}

			template<typename AttributeType>
			static typename TEnableIf<!TAttributeTypeTraits<AttributeType>::IsBlendable, void>::Type RegisterType()
			{
				static_assert(!TAttributeTypeTraits<AttributeType>::WithCustomBlendOperator, "Attribute type requires a custom blend operation");

				AttributeTypes::Initialize();
				UScriptStruct* ScriptStruct = AttributeType::StaticStruct();
				AttributeTypes::RegisteredTypes.Add(ScriptStruct);

				AttributeTypes::Operators.Add(MakeUnique<TAttributeBlendOperator<AttributeType>>());
			}
#endif			
			/** Unregisters a specific attribute type and deletes its associated blend operator */
			template<typename AttributeType>
			static void UnregisterType()
			{
				UScriptStruct* ScriptStruct = AttributeType::StaticStruct();
				const int32 Index = AttributeTypes::RegisteredTypes.IndexOfByKey(ScriptStruct);
				if (Index != INDEX_NONE)
				{
					AttributeTypes::RegisteredTypes.RemoveAtSwap(Index);
					AttributeTypes::Operators.RemoveAtSwap(Index);
				}
			}

			/** Returns the blend operator for the provided type, asserts when the type is not registered */
			static const IAttributeBlendOperator* GetTypeOperator(TWeakObjectPtr<const UScriptStruct> WeakStruct)
			{
				AttributeTypes::Initialize();
				const int32 Index = AttributeTypes::RegisteredTypes.IndexOfByKey(WeakStruct);
				checkf(Index != INDEX_NONE, TEXT("Missing operator for custom attribute, type was not registered previously"));
				return AttributeTypes::Operators[Index].Get();
			}

			/** Returns whether or not the provided type can be interpolated, defaults to false when the type is not registered */
			static bool CanInterpolateType(TWeakObjectPtr<const UScriptStruct> WeakStruct)
			{
				AttributeTypes::Initialize();
				return AttributeTypes::InterpolatableTypes.Contains(WeakStruct);
			}

			/** Returns whether or not the type is registered */
			static bool IsTypeRegistered(const UScriptStruct* ScriptStruct)
			{
				AttributeTypes::Initialize();
				return AttributeTypes::RegisteredTypes.Contains(ScriptStruct);
			}
		};
	}
}
