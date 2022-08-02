// Copyright Epic Games, Inc. All Rights Reserved.

#include "PropertyUtil.h"

#include "Serialization/ArchiveSerializedPropertyChain.h"
#include "UObject/Field.h"
#include "UObject/UnrealType.h"

namespace UE::LevelSnapshots::Private
{
	namespace Internal
	{
		static EBreakBehaviour RecursiveFollowPropertyChain(int32 ChainIndex, void* ContainerPtr, const FArchiveSerializedPropertyChain* PropertyChain, const FProperty* LeafProperty, const FHandleValuePtr& Callback)
		{
			const bool bHasReachLeafProperty = !PropertyChain || PropertyChain->GetNumProperties() <= ChainIndex;
			
			const FProperty* CurrentProperty = bHasReachLeafProperty
				? LeafProperty
				: PropertyChain->GetPropertyFromRoot(ChainIndex);
			void* ValuePtr = CurrentProperty->ContainerPtrToValuePtr<void>(ContainerPtr);
			if (const FArrayProperty* ArrayProperty = CastField<FArrayProperty>(CurrentProperty))
			{
				FScriptArrayHelper ArrayHelper(ArrayProperty, ValuePtr);
				
				bool bShouldStop = false;
				for (int32 i = 0; i < ArrayHelper.Num() && !bShouldStop; ++i)
				{
					void* IndexValuePtr = ArrayHelper.GetRawPtr(i);
					void* ElementValuePtr = ArrayProperty->Inner->ContainerPtrToValuePtr<void>(IndexValuePtr);

					const EBreakBehaviour CallbackResult = bHasReachLeafProperty
						? Callback(ElementValuePtr, EPropertyType::NormalProperty)
						: RecursiveFollowPropertyChain(ChainIndex + 1, ElementValuePtr, PropertyChain, LeafProperty, Callback);
					bShouldStop |= CallbackResult == EBreakBehaviour::Break;
				}

				return bShouldStop ? EBreakBehaviour::Break : EBreakBehaviour::Continue;
			}

			if (const FSetProperty* SetProperty = CastField<FSetProperty>(CurrentProperty))
			{
				FScriptSetHelper SetHelper(SetProperty, ValuePtr);
				
				bool bShouldStop = false;
				for (int32 i = 0; i < SetHelper.Num() && !bShouldStop; ++i)
				{
					void* IndexContainerPtr = SetHelper.GetElementPtr(i);
					void* ElementValuePtr = SetProperty->ElementProp->ContainerPtrToValuePtr<void>(IndexContainerPtr);

					const EBreakBehaviour CallbackResult = bHasReachLeafProperty
						? Callback(ElementValuePtr, EPropertyType::NormalProperty)
						: RecursiveFollowPropertyChain(ChainIndex + 1, ElementValuePtr, PropertyChain, LeafProperty, Callback);
					bShouldStop |= CallbackResult == EBreakBehaviour::Break;
				}

				return bShouldStop ? EBreakBehaviour::Break : EBreakBehaviour::Continue;
			}

			if (const FMapProperty* MapProperty = CastField<FMapProperty>(CurrentProperty))
			{
				FScriptMapHelper MapHelper(MapProperty, ValuePtr);

				bool bShouldStop = false;
				for (int32 i = 0; i < MapHelper.Num() && !bShouldStop; ++i)
				{
					// If the map is the leaf, check content of both key and value
					if (bHasReachLeafProperty)
					{
						void* IndexKeyContainerPtr = MapHelper.GetKeyPtr(i);
						const EBreakBehaviour KeyCallbackResult = bHasReachLeafProperty
							? Callback(IndexKeyContainerPtr, EPropertyType::KeyInMap)
							: RecursiveFollowPropertyChain(ChainIndex + 1, IndexKeyContainerPtr, PropertyChain, LeafProperty, Callback);
						bShouldStop |= KeyCallbackResult == EBreakBehaviour::Break;

						if (!bShouldStop) // Fast path
						{
							void* IndexValueContainerPtr = MapHelper.GetValuePtr(i);
							const EBreakBehaviour ValueCallbackResult = bHasReachLeafProperty
								? Callback(IndexValueContainerPtr, EPropertyType::ValueInMap)
								: RecursiveFollowPropertyChain(ChainIndex + 1, IndexValueContainerPtr, PropertyChain, LeafProperty, Callback);
							bShouldStop |= ValueCallbackResult == EBreakBehaviour::Break;
						}
					}
					else // If the map is in the middle of the chain, continue down the correct path
					{
						const FProperty* NextProperty = PropertyChain->GetPropertyFromRoot(ChainIndex + 1);
						if (NextProperty == MapHelper.GetKeyProperty())
						{
							void* IndexKeyContainerPtr = MapHelper.GetPairPtr(i);
							const EBreakBehaviour KeyCallbackResult = RecursiveFollowPropertyChain(ChainIndex + 1, IndexKeyContainerPtr, PropertyChain, LeafProperty, Callback);
							bShouldStop |= KeyCallbackResult == EBreakBehaviour::Break;
						}
						else if (ensure(NextProperty == MapHelper.GetValueProperty()))
						{
							void* IndexValueContainerPtr = MapHelper.GetPairPtr(i);
							const EBreakBehaviour ValueCallbackResult = RecursiveFollowPropertyChain(ChainIndex + 1, IndexValueContainerPtr, PropertyChain, LeafProperty, Callback);
							bShouldStop |= ValueCallbackResult == EBreakBehaviour::Break;
						}
					}
				}

				return bShouldStop ? EBreakBehaviour::Break : EBreakBehaviour::Continue;
			}
			
			if (bHasReachLeafProperty)
			{
				void* LeafValuePtr = LeafProperty->ContainerPtrToValuePtr<void>(ContainerPtr);
				return Callback(LeafValuePtr, EPropertyType::NormalProperty); 
			}
			return RecursiveFollowPropertyChain(ChainIndex + 1, ValuePtr, PropertyChain, LeafProperty, Callback);
		}
	}
	
	void FollowPropertyChain(void* ContainerPtr, const FArchiveSerializedPropertyChain* PropertyChain, const FProperty* LeafProperty, UE::LevelSnapshots::Private::FHandleValuePtr Callback)
	{
		Internal::RecursiveFollowPropertyChain(0, ContainerPtr, PropertyChain, LeafProperty, Callback);
	}

	bool FollowPropertyChainUntilPredicateIsTrue(void* ContainerPtr, const FArchiveSerializedPropertyChain* PropertyChain, const FProperty* LeafProperty, UE::LevelSnapshots::Private::FValuePtrPredicate Callback)
	{
		bool bResult = false;
		FollowPropertyChain(ContainerPtr, PropertyChain, LeafProperty, [Callback = MoveTemp(Callback), &bResult](void* ValuePtr, EPropertyType PropertyType)
		{
			bResult |= Callback(ValuePtr, PropertyType);
			return bResult ? EBreakBehaviour::Break : EBreakBehaviour::Continue;
		});
		return bResult;
	}
}