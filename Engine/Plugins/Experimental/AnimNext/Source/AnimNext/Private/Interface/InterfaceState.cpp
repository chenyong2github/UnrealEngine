// Copyright Epic Games, Inc. All Rights Reserved.

#include "Interface/InterfaceState.h"
#include "Interface/InterfaceContext.h"

namespace UE::AnimNext
{

FParam* FState::FindStateRaw(const FInterfaceKeyWithIdAndStack& InKey, const FContext& InContext, EStatePersistence InPersistence)
{
	FParam* ExistingValue = nullptr;
	TMap<FInterfaceKeyWithIdAndStack, FParam>* ValueMap = nullptr; 

	switch(InPersistence)
	{
	case EStatePersistence::Relevancy:
		ExistingValue = RelevancyValueMap.Find(InKey);
		if(ExistingValue)
		{
			// An access to a state param counts as a 'relevant use' for this update
			static_cast<FRelevancyParam*>(ExistingValue)->UpdateCounter = InContext.UpdateCounter;
		}
		break;
	case EStatePersistence::Permanent:
		ExistingValue = PermanentValueMap.Find(InKey);
		break;
	default:
		check(false);
		break;
	}
	
	return ExistingValue;
}

FParam* FState::AllocateState(const FInterfaceKeyWithIdAndStack& InKey, const FContext& InContext, const FParamTypeHandle& InTypeHandle, EStatePersistence InPersistence)
{
	const TArrayView<uint8> Data(static_cast<uint8*>(FMemory::Malloc(InTypeHandle.GetSize(), InTypeHandle.GetAlignment())), InTypeHandle.GetSize());

	switch(InPersistence)
	{
	case EStatePersistence::Relevancy:
		// TODO: chunked allocator for relevancy-based stuff?
		return &RelevancyValueMap.Add(InKey, FRelevancyParam(InTypeHandle, Data, FParam::EFlags::Mutable, InContext.UpdateCounter));
		break;
	case EStatePersistence::Permanent:
		return &PermanentValueMap.Add(InKey, FParam(InTypeHandle, Data, FParam::EFlags::Mutable));
		break;
	default:
		check(false);
		break;
	}
	
	return nullptr;
}

}
