// Copyright Epic Games, Inc. All Rights Reserved.

#include "PropertyInfoHelpers.h"

#include "UObject/UnrealType.h"

float FPropertyInfoHelpers::FloatComparisonPrecision = 1e-03f;
double FPropertyInfoHelpers::DoubleComparisonPrecision = 1e-03;

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

bool FPropertyInfoHelpers::IsPropertyComponentOrSubobject(const FProperty* Property)
{
	const bool bIsComponentProp = !!(Property->PropertyFlags & (CPF_InstancedReference | CPF_ContainsInstancedReference));
	return bIsComponentProp;
}

void FPropertyInfoHelpers::UpdateDecimalComparisionPrecision(float FloatPrecision, double DoublePrecision)
{
	FloatComparisonPrecision = FloatPrecision;
	DoubleComparisonPrecision = DoublePrecision;
}

bool FPropertyInfoHelpers::AreNumericPropertiesNearlyEqual(const FNumericProperty* NumericProperty, const void* ValuePtrA, const void* ValuePtrB)
{
	check(NumericProperty);
	
	if (const FFloatProperty* FloatProperty = CastField<FFloatProperty>(NumericProperty))
	{
		const float ValueA = FloatProperty->GetFloatingPointPropertyValue(ValuePtrA); 
		const float ValueB = FloatProperty->GetFloatingPointPropertyValue(ValuePtrB);
		return FMath::IsNearlyEqual(ValueA, ValueB, FloatComparisonPrecision);
	}
	
	if (const FDoubleProperty* DoubleProperty = CastField<FDoubleProperty>(NumericProperty))
	{
		const double ValueA = DoubleProperty->GetFloatingPointPropertyValue(ValuePtrA);
		const double ValueB = DoubleProperty->GetFloatingPointPropertyValue(ValuePtrB);
		return FMath::IsNearlyEqual(ValueA, ValueB, DoubleComparisonPrecision);
	}
	
	// Not a float or double? Then some kind of integer (byte, int8, int16, int32, int64, uint8, uint16 ...). Enums are bytes.
	const int64 ValueA = NumericProperty->GetSignedIntPropertyValue(ValuePtrA);
	const int64 ValueB = NumericProperty->GetSignedIntPropertyValue(ValuePtrB);
	return ValueA == ValueB;
}
