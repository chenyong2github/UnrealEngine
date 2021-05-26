// Copyright Epic Games, Inc. All Rights Reserved.

#include "RCPropertyUtilities.h"

#include "CoreMinimal.h"

#include "RCTypeUtilities.h"
#include "UObject/TextProperty.h"
#include "UObject/UnrealType.h"

#if WITH_EDITOR

template <>
bool RemoteControlPropertyUtilities::FromBinary<FProperty>(const FRCPropertyVariant& InSrc, FRCPropertyVariant& OutDst)
{
	const FProperty* Property = OutDst.GetProperty();
	FOREACH_CAST_PROPERTY(Property, FromBinary<CastPropertyType>(InSrc, OutDst))

	return true;
}

template <>
bool RemoteControlPropertyUtilities::ToBinary<FProperty>(const FRCPropertyVariant& InSrc, FRCPropertyVariant& OutDst)
{
	const FProperty* Property = InSrc.GetProperty();
	FOREACH_CAST_PROPERTY(Property, ToBinary<CastPropertyType>(InSrc, OutDst))

	return true;
}

#endif
