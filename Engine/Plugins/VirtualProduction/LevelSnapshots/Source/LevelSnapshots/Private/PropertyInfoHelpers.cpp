// Copyright Epic Games, Inc. All Rights Reserved.

#include "PropertyInfoHelpers.h"

#include "UObject/UnrealType.h"

FProperty* FPropertyInfoHelpers::GetParentProperty(const FProperty* Property)
{
	return Property->GetOwner<FProperty>();
}

bool FPropertyInfoHelpers::IsPropertyContainer(const FProperty* Property)
{
	return ensure(Property) && (Property->IsA(FStructProperty::StaticClass()) || Property->IsA(FArrayProperty::StaticClass()) ||
			Property->IsA(FMapProperty::StaticClass()) || Property->IsA(FSetProperty::StaticClass()));
}

bool FPropertyInfoHelpers::IsPropertyCollection(const FProperty* Property)
{
	return ensure(Property) && (Property->IsA(FArrayProperty::StaticClass()) || Property->IsA(FMapProperty::StaticClass()) || Property->IsA(FSetProperty::StaticClass()));
}

bool FPropertyInfoHelpers::IsPropertyInContainer(const FProperty* Property)
{
	if (ensure(Property))
	{
		return IsPropertyInCollection(Property) || IsPropertyInStruct(Property);
	}
	return false;
}

bool FPropertyInfoHelpers::IsPropertyInCollection(const FProperty* Property)
{
	if (ensure(Property))
	{
		const FProperty* ParentProperty = GetParentProperty(Property);
		return ParentProperty && IsPropertyCollection(ParentProperty);
	}
	return false;
}

bool FPropertyInfoHelpers::IsPropertyInStruct(const FProperty* Property)
{
	if (!ensure(Property))
	{
		return false;
	}

	// Parent struct could be FProperty or UScriptStruct

	if (FProperty* ParentProperty = GetParentProperty(Property))
	{
		return ParentProperty->IsA(FStructProperty::StaticClass());
	}

	return IsValid(Property->GetOwner<UScriptStruct>());
}

bool FPropertyInfoHelpers::IsPropertyInMap(const FProperty* Property)
{
	if (!ensure(Property))
	{
		return false;
	}

	FProperty* ParentProperty = GetParentProperty(Property);

	return ParentProperty && ParentProperty->IsA(FMapProperty::StaticClass());
}

bool FPropertyInfoHelpers::IsPropertyComponentFast(const FProperty* Property)
{
	const bool bIsComponentProp = !!(Property->PropertyFlags & (CPF_InstancedReference | CPF_ContainsInstancedReference));
	return bIsComponentProp;
}

bool FPropertyInfoHelpers::IsPropertySubObject(const FProperty* Property)
{
	const bool bIsComponentSubObject = !!(Property->PropertyFlags & (CPF_UObjectWrapper));
	return bIsComponentSubObject;
}

bool FPropertyInfoHelpers::AreNumericPropertiesNearlyEqual(const FNumericProperty* NumericProperty, const void* ValuePtrA, const void* ValuePtrB)
{
	check(NumericProperty);
	
	if (const FFloatProperty* FloatProperty = CastField<FFloatProperty>(NumericProperty))
	{
		const float ValueA = FloatProperty->GetFloatingPointPropertyValue(ValuePtrA); 
		const float ValueB = FloatProperty->GetFloatingPointPropertyValue(ValuePtrB);
		return FMath::IsNearlyEqual(ValueA, ValueB, KINDA_SMALL_NUMBER);
	}
	
	if (const FDoubleProperty* DoubleProperty = CastField<FDoubleProperty>(NumericProperty))
	{
		const double ValueA = DoubleProperty->GetFloatingPointPropertyValue(ValuePtrA);
		const double ValueB = DoubleProperty->GetFloatingPointPropertyValue(ValuePtrB);
		return FMath::IsNearlyEqual(ValueA, ValueB, static_cast<double>(KINDA_SMALL_NUMBER));
	}
	
	// Not a float or double? Then some kind of integer (byte, int8, int16, int32, int64, uint8, uint16 ...). Enums are bytes.
	const int64 ValueA = NumericProperty->GetSignedIntPropertyValue(ValuePtrA);
	const int64 ValueB = NumericProperty->GetSignedIntPropertyValue(ValuePtrB);
	return ValueA == ValueB;
}