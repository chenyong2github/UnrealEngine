// Copyright Epic Games, Inc. All Rights Reserved.

#include "Animation/AttributeTypes.h"
#include "Animation/BuiltInAttributeTypes.h"
#include "Misc/DelayedAutoRegister.h"

namespace UE
{
	namespace Anim
	{		
		TArray<TWeakObjectPtr<const UScriptStruct>> AttributeTypes::RegisteredTypes;
		TArray<TUniquePtr<IAttributeBlendOperator>> AttributeTypes::Operators;
		TArray<TWeakObjectPtr<const UScriptStruct>> AttributeTypes::InterpolatableTypes;
		std::atomic<bool> AttributeTypes::bInitialized = false;		
		
		void AttributeTypes::LazyInitialize()
		{
			bool bWasUninitialized = false;
			if (bInitialized.compare_exchange_strong(bWasUninitialized, true))
			{
				Initialize();
			}
		}

		void AttributeTypes::Initialize()
		{
			RegisterType<FFloatAnimationAttribute>();
			RegisterType<FIntegerAnimationAttribute>();
			RegisterType<FStringAnimationAttribute>();
			RegisterType<FTransformAnimationAttribute>();
			RegisterType<FVectorAnimationAttribute>();
			RegisterType<FQuaternionAnimationAttribute>();
		}
		
		static FDelayedAutoRegisterHelper DelayedAttributeTypesInitializationHelper(EDelayedRegisterRunPhase::ObjectSystemReady, []()
		{
			UE::Anim::AttributeTypes::LazyInitialize();
		});
	}
}

