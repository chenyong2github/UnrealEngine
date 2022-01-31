// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "UObject/NameTypes.h"

// Serialization helper for core variant types only. DO NOT USE!
#define UE_SERIALIZE_VARIANT_FROM_MISMATCHED_TAG(AR_OR_SLOT, ALIAS, TYPE, ALT_TYPE) LWCSerializerPrivate::SerializeFromMismatchedTag<F##ALT_TYPE>(*this, StructTag, AR_OR_SLOT, NAME_##ALIAS, NAME_##TYPE, NAME_##ALT_TYPE)
namespace LWCSerializerPrivate
{

// SerializeFromMismatchedTag helper for core type use only. DO NOT USE!
template<typename FAltType, typename FType, typename FArSlot>
bool SerializeFromMismatchedTag(FType& Target, FName StructTag, FArSlot& ArSlot, FName BaseTag, FName AltTag, FName ThisTag)
{
	if( StructTag == BaseTag || 
		StructTag == ThisTag)
	{
		// Note: relies on Serialize to handle float/double based on archive version.
		return Target.Serialize(ArSlot);
	}
	else if(StructTag == AltTag)
	{
		// Convert from alt type
		FAltType AsAlt;										// TODO: Could we derive this from FType?
		const bool bResult = AsAlt.Serialize(ArSlot);
		Target = static_cast<FType>(AsAlt);					// LWC_TODO: Log precision loss warning for TIsUECoreVariant<FType, float>? Could get spammy.
		return bResult;
	}

	return false;
}

} // namespace LWCSerializerPrivate
