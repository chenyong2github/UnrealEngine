// Copyright Epic Games, Inc. All Rights Reserved.

#include "Animation/AttributeTypes.h"
#include "Animation/BuiltInAttributeTypes.h"

namespace UE
{
	namespace Anim
	{		
		TArray<TWeakObjectPtr<const UScriptStruct>> AttributeTypes::RegisteredTypes;
		TArray<TUniquePtr<IAttributeBlendOperator>> AttributeTypes::Operators;
		TArray<TWeakObjectPtr<const UScriptStruct>> AttributeTypes::InterpolatableTypes;

		void AttributeTypes::Initialize()
		{
			static bool bInitialized = false;
			if (!bInitialized)
			{
				bInitialized = true;

				RegisterType<FFloatAnimationAttribute>();
				RegisterType<FIntegerAnimationAttribute>();
				RegisterType<FStringAnimationAttribute>();
				RegisterType<FTransformAnimationAttribute>();
			}
		}		
	}
}

