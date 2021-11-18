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
	if(StructTag == BaseTag)
	{
		// LWC_TODO: Serialize - Convert from float/double based on archive version. Just uses (float) serializer for now.
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
	else if(StructTag == ThisTag)							// LWC_TODO: Not necessary for float variants once we're fixed on doubles, supports e.g. FVector3f->FVector conversions with LWC disabled.
	{
		return Target.Serialize(ArSlot);
	}

	return false;
}

} // namespace LWCSerializerPrivate
