// Copyright Epic Games, Inc. All Rights Reserved.

#include "Binding/FloatBinding.h"

#define LOCTEXT_NAMESPACE "UMG"

UFloatBinding::UFloatBinding()
{
}

bool UFloatBinding::IsSupportedDestination(FProperty* Property) const
{
	return IsSupportedSource(Property);
}

bool UFloatBinding::IsSupportedSource(FProperty* Property) const
{
	return IsConcreteTypeCompatibleWithReflectedType<float>(Property) || IsConcreteTypeCompatibleWithReflectedType<double>(Property);
}

float UFloatBinding::GetValue() const
{
	//SCOPE_CYCLE_COUNTER(STAT_UMGBinding);

	if ( UObject* Source = SourceObject.Get() )
	{
		// Since we can bind to either a float or double, we need to perform a narrowing conversion where necessary.
		// If this isn't a property, then we're assuming that a function is used to extract the float value.

		float FloatValue = 0.0f;

		SourcePath.Resolve(Source);
		if (FProperty* Property = SourcePath.GetFProperty())
		{
			double DoubleValue = 0.0;
			if (Property->IsA<FFloatProperty>() && SourcePath.GetValue<float>(Source, FloatValue))
			{
				return FloatValue;
			}
			else if (Property->IsA<FDoubleProperty>() && SourcePath.GetValue<double>(Source, DoubleValue))
			{
				FloatValue = static_cast<float>(DoubleValue);
				return FloatValue;
			}
			else
			{
				checkf(false, TEXT("Unexpected property type: '%s'! Float bindings must use either a float or double property."), *Property->GetCPPType());
			}
		}
		else
		{
			check(SourcePath.GetCachedFunction());
			if (SourcePath.GetValue<float>(Source, FloatValue))
			{
				return FloatValue;
			}
		}
	}

	return 0.0f;
}

#undef LOCTEXT_NAMESPACE
