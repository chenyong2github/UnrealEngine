// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "MovieScene/MovieSceneLiveLinkStructPropertyBindings.h"

#include "LiveLinkMovieScenePrivate.h"

//------------------------------------------------------------------------------
// FLiveLinkStructPropertyBindings implementation.
//------------------------------------------------------------------------------

TMap<FLiveLinkStructPropertyBindings::FPropertyNameKey, FLiveLinkStructPropertyBindings::FPropertyWrapper> FLiveLinkStructPropertyBindings::PropertyCache;

FLiveLinkStructPropertyBindings::FLiveLinkStructPropertyBindings(FName InPropertyName, const FString& InPropertyPath)
	: PropertyPath(InPropertyPath)
	, PropertyName(InPropertyName)
{
}

void FLiveLinkStructPropertyBindings::CacheBinding(const UScriptStruct& InStruct)
{
	FPropertyWrapper Property = FindProperty(InStruct, PropertyName);
	PropertyCache.Add(FPropertyNameKey(InStruct.GetFName(), PropertyName), Property);
}

UProperty* FLiveLinkStructPropertyBindings::GetProperty(const UScriptStruct& InStruct) const
{
	FPropertyWrapper FoundProperty = PropertyCache.FindRef(FPropertyNameKey(InStruct.GetFName(), PropertyName));
	if (UProperty* Property = FoundProperty.GetProperty())
	{
		return Property;
	}

	return FindProperty(InStruct, PropertyName).GetProperty();
}

int64 FLiveLinkStructPropertyBindings::GetCurrentValueForEnum(const UScriptStruct& InStruct, const void* InSourceAddress)
{
	FPropertyWrapper FoundProperty = FindOrAdd(InStruct);

	if (UProperty* Property = FoundProperty.GetProperty())
	{
		if (Property->IsA(UEnumProperty::StaticClass()))
		{
			if (UEnumProperty* EnumProperty = CastChecked<UEnumProperty>(Property))
			{
				UNumericProperty* UnderlyingProperty = EnumProperty->GetUnderlyingProperty();
				const void* ValueAddr = EnumProperty->ContainerPtrToValuePtr<void>(InSourceAddress);
				int64 Result = UnderlyingProperty->GetSignedIntPropertyValue(ValueAddr);
				return Result;
			}
		}
		else
		{
			UE_LOG(LogLiveLinkMovieScene, Error, TEXT("Mismatch in property binding evaluation. %s is not of type: %s"), *Property->GetName(), *UEnumProperty::StaticClass()->GetName());
		}
	}

	return 0;
}

int64 FLiveLinkStructPropertyBindings::GetCurrentValueForEnumAt(int32 InIndex, const UScriptStruct& InStruct, const void* InSourceAddress)
{
	FPropertyWrapper FoundProperty = FindOrAdd(InStruct);

	if (UProperty* Property = FoundProperty.GetProperty())
	{
		UArrayProperty* ArrayProperty = CastChecked<UArrayProperty>(Property);
		check(false);
	}

	return 0;
}

void FLiveLinkStructPropertyBindings::SetCurrentValueForEnum(const UScriptStruct& InStruct, void* InSourceAddress, int64 InValue)
{
	FPropertyWrapper FoundProperty = FindOrAdd(InStruct);

	if (UProperty* Property = FoundProperty.GetProperty())
	{
		if (Property->IsA(UEnumProperty::StaticClass()))
		{
			if (UEnumProperty* EnumProperty = CastChecked<UEnumProperty>(Property))
			{
				UNumericProperty* UnderlyingProperty = EnumProperty->GetUnderlyingProperty();
				void* ValueAddr = EnumProperty->ContainerPtrToValuePtr<void>(InSourceAddress);
				UnderlyingProperty->SetIntPropertyValue(ValueAddr, InValue);
			}
		}
		else
		{
			UE_LOG(LogLiveLinkMovieScene, Error, TEXT("Mismatch in property binding evaluation. %s is not of type: %s"), *Property->GetName(), *UEnumProperty::StaticClass()->GetName());
		}
	}
}

template<> bool FLiveLinkStructPropertyBindings::GetCurrentValue<bool>(const UScriptStruct& InStruct, const void* InSourceAddress)
{
	FPropertyWrapper FoundProperty = FindOrAdd(InStruct);
	if (UProperty* Property = FoundProperty.GetProperty())
	{
		if (Property->IsA(UBoolProperty::StaticClass()))
		{
			if (UBoolProperty* BoolProperty = CastChecked<UBoolProperty>(Property))
			{
				const uint8* ValuePtr = BoolProperty->ContainerPtrToValuePtr<uint8>(InSourceAddress);
				return BoolProperty->GetPropertyValue(ValuePtr);
			}
		}
		else
		{
			UE_LOG(LogLiveLinkMovieScene, Error, TEXT("Mismatch in property binding evaluation. %s is not of type: %s"), *Property->GetName(), *UBoolProperty::StaticClass()->GetName());
		}
	}

	return false;
}

template<> void FLiveLinkStructPropertyBindings::SetCurrentValue<bool>(const UScriptStruct& InStruct, void* InSourceAddress, TCallTraits<bool>::ParamType InValue)
{
	FPropertyWrapper FoundProperty = FindOrAdd(InStruct);
	if (UProperty* Property = FoundProperty.GetProperty())
	{
		if (UBoolProperty* BoolProperty = Cast<UBoolProperty>(Property))
		{
			uint8* ValuePtr = BoolProperty->ContainerPtrToValuePtr<uint8>(InSourceAddress);
			BoolProperty->SetPropertyValue(ValuePtr, InValue);
		}
	}
}

FLiveLinkStructPropertyBindings::FPropertyWrapper FLiveLinkStructPropertyBindings::FindProperty(const UScriptStruct& InStruct, const FName InPropertyName)
{
	FPropertyWrapper NewProperty;
	NewProperty.Property = FindField<UProperty>(&InStruct, InPropertyName);
	return NewProperty;
}
