// Copyright Epic Games, Inc. All Rights Reserved.

#include "PropertyUtil.h"

#include "UObject/Field.h"
#include "UObject/UnrealType.h"
#include "Serialization/ArchiveSerializedPropertyChain.h"

namespace
{
	using namespace SnapshotUtil::Property;
	
	EBreakBehaviour RecursiveFollowPropertyChain(int32 ChainIndex, void* ContainerPtr, const FArchiveSerializedPropertyChain* PropertyChain, const FProperty* LeafProperty, const FHandleValuePtr& Callback)
	{
		const bool bHasReachLeafProperty = !PropertyChain || PropertyChain->GetNumProperties() <= ChainIndex; 
		if (bHasReachLeafProperty)
		{
			void* LeafValuePtr = LeafProperty->ContainerPtrToValuePtr<void>(ContainerPtr);
			return Callback(LeafValuePtr);
		}
		
		const FProperty* CurrentProperty = PropertyChain->GetPropertyFromRoot(ChainIndex);
		void* ValuePtr = CurrentProperty->ContainerPtrToValuePtr<void>(ContainerPtr);
		
		if (const FArrayProperty* ArrayProperty = CastField<FArrayProperty>(CurrentProperty))
		{
			FScriptArrayHelper ArrayHelper(ArrayProperty, ValuePtr);
			
			bool bShouldStop = false;
			for (int32 i = 0; i < ArrayHelper.Num() && !bShouldStop; ++i)
			{
				void* IndexValuePtr = ArrayHelper.GetRawPtr(i);
				void* ElementValuePtr = ArrayProperty->Inner->ContainerPtrToValuePtr<void>(IndexValuePtr);
				
				const EBreakBehaviour CallbackResult = RecursiveFollowPropertyChain(ChainIndex + 1, ElementValuePtr, PropertyChain, LeafProperty, Callback);
				bShouldStop |= CallbackResult == EBreakBehaviour::Break;
			}

			return EBreakBehaviour::Continue;
		}
		else if (const FSetProperty* SetProperty = CastField<FSetProperty>(CurrentProperty))
		{
			FScriptSetHelper SetHelper(SetProperty, ValuePtr);
			
			bool bShouldStop = false;
			for (int32 i = 0; i < SetHelper.Num() && !bShouldStop; ++i)
			{
				void* IndexValuePtr = SetHelper.GetElementPtr(i);
				void* ElementValuePtr = SetProperty->ElementProp->ContainerPtrToValuePtr<void>(IndexValuePtr);
				
				const EBreakBehaviour CallbackResult = RecursiveFollowPropertyChain(ChainIndex + 1, ElementValuePtr, PropertyChain, LeafProperty, Callback);
				bShouldStop |= CallbackResult == EBreakBehaviour::Break;
			}

			return EBreakBehaviour::Continue;
		}
		else if (const FMapProperty* MapProperty = CastField<FMapProperty>(CurrentProperty))
		{
			// Currently unsupported
			return EBreakBehaviour::Break;
		}
		else
		{
			return RecursiveFollowPropertyChain(ChainIndex + 1, ValuePtr, PropertyChain, LeafProperty, Callback);
		}
	}
}

void SnapshotUtil::Property::FollowPropertyChain(void* ContainerPtr, const FArchiveSerializedPropertyChain* PropertyChain, const FProperty* LeafProperty, FHandleValuePtr Callback)
{
	RecursiveFollowPropertyChain(0, ContainerPtr, PropertyChain, LeafProperty, Callback);
}

bool SnapshotUtil::Property::FollowPropertyChainUntilPredicateIsTrue(void* ContainerPtr, const FArchiveSerializedPropertyChain* PropertyChain, const FProperty* LeafProperty, FValuePtrPredicate Callback)
{
	bool bResult = false;
	FollowPropertyChain(ContainerPtr, PropertyChain, LeafProperty, [Callback = MoveTemp(Callback), &bResult](void* ValuePtr)
	{
		bResult |= Callback(ValuePtr);
		return bResult ? EBreakBehaviour::Break : EBreakBehaviour::Continue;
	});
	return bResult;
}