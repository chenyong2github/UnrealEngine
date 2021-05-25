// Copyright Epic Games, Inc. All Rights Reserved.

#include "RCPropertyUtilities.h"

#include "CoreMinimal.h"

#include "RCTypeUtilities.h"
#include "UObject/TextProperty.h"
#include "UObject/UnrealType.h"

#if WITH_EDITOR

template <>
bool RemoteControlPropertyUtilities::FromBytes<FStructProperty>(const FRCPropertyVariant& InSrcData, FRCPropertyVariant& InDst)
{
	InDst.GetProperty()->CopyCompleteValue((uint8*)InDst.GetPropertyData(), (uint8*)InSrcData.GetPropertyData());
	
	return true;
}

template <>
bool RemoteControlPropertyUtilities::FromBytes<FArrayProperty>(const FRCPropertyVariant& InSrcData, FRCPropertyVariant& InDst)
{
	const FArrayProperty* Property = InDst.GetProperty<FArrayProperty>();

	FScriptArrayHelper SrcArrayHelper(Property, Property->ContainerPtrToValuePtr<void>(InSrcData.GetPropertyData()));
	FScriptArrayHelper DstArrayHelper(Property, Property->ContainerPtrToValuePtr<void>(InDst.GetPropertyData()));

	const int32 Num = SrcArrayHelper.Num();	
	const FProperty* InnerProperty = Property->Inner;
	if(InnerProperty->HasAnyPropertyFlags(CPF_IsPlainOldData))
	{
		DstArrayHelper.EmptyAndAddValues(Num);
	}
	else
	{
		DstArrayHelper.EmptyAndAddUninitializedValues(Num);
	}

	for(int32 Idx = 0; Idx < Num; ++Idx)
	{
		FRCPropertyVariant Dst{InnerProperty, DstArrayHelper.GetRawPtr(Idx)};
		FromBytes<FProperty>({InnerProperty, SrcArrayHelper.GetRawPtr(Idx)}, Dst);
	}

	return true;
}

template <>
bool RemoteControlPropertyUtilities::FromBytes<FSetProperty>(const FRCPropertyVariant& InSrcData, FRCPropertyVariant& InDst)
{
	const FSetProperty* Property = InDst.GetProperty<FSetProperty>();
	
	FScriptSetHelper SrcSetHelper(Property, Property->ContainerPtrToValuePtr<void>(InSrcData.GetPropertyData()));
	FScriptSetHelper DstSetHelper(Property, Property->ContainerPtrToValuePtr<void>(InDst.GetPropertyData()));

	const FProperty* InnerProperty = Property->ElementProp;
	
	DstSetHelper.EmptyElements(SrcSetHelper.Num());

	for(int32 Idx = 0; Idx < SrcSetHelper.Num(); ++Idx)
	{
		DstSetHelper.AddUninitializedValue();
		FRCPropertyVariant Dst{InnerProperty, DstSetHelper.GetElementPtr(Idx)};
		FromBytes<FProperty>({InnerProperty, SrcSetHelper.GetElementPtr(Idx)}, Dst);
	}

	return true;
}

template <>
bool RemoteControlPropertyUtilities::FromBytes<FMapProperty>(const FRCPropertyVariant& InSrcData, FRCPropertyVariant& InDst)
{
	const FMapProperty* Property = InDst.GetProperty<FMapProperty>();
	
	FScriptMapHelper SrcMapHelper(Property, Property->ContainerPtrToValuePtr<void>(InSrcData.GetPropertyData()));
	FScriptMapHelper DstMapHelper(Property, Property->ContainerPtrToValuePtr<void>(InDst.GetPropertyData()));

	const FProperty* KeyProperty = Property->KeyProp;
	const FProperty* ValueProperty = Property->ValueProp;

	DstMapHelper.EmptyValues(SrcMapHelper.Num());

	for(int32 Idx = 0; Idx < SrcMapHelper.Num(); ++Idx)
	{
		DstMapHelper.AddUninitializedValue();

		FRCPropertyVariant KeyDst{KeyProperty, (void*)DstMapHelper.GetKeyPtr(Idx)};
		FromBytes<FProperty>({KeyProperty, SrcMapHelper.GetKeyPtr(Idx)}, KeyDst);

		FRCPropertyVariant ValueDst{ValueProperty, (void*)DstMapHelper.GetValuePtr(Idx)};
		FromBytes<FProperty>({ValueProperty, SrcMapHelper.GetValuePtr(Idx)}, ValueDst);
	}

	return true;
}

template <>
bool RemoteControlPropertyUtilities::ToBytes<FStructProperty>(const FRCPropertyVariant& InSrc, FRCPropertyVariant& OutDst)
{
	InSrc.GetProperty()->CopyCompleteValue((uint8*)OutDst.GetPropertyData(), (uint8*)InSrc.GetPropertyData());

	return true;
}

template <>
bool RemoteControlPropertyUtilities::ToBytes<FArrayProperty>(const FRCPropertyVariant& InSrc, FRCPropertyVariant& OutDst)
{
	const FArrayProperty* Property = InSrc.GetProperty<FArrayProperty>();

	// Dst wasn't yet set to anything
	if(OutDst.Num() == 0)
	{
		OutDst.Init(1);
		Property->InitializeValueInternal(OutDst.GetPropertyData());	
	}
	
	FScriptArrayHelper SrcArrayHelper(Property, Property->ContainerPtrToValuePtr<void>(InSrc.GetPropertyData()));
	FScriptArrayHelper DstArrayHelper(Property, Property->ContainerPtrToValuePtr<void>(OutDst.GetPropertyData()));

	const int32 Num = SrcArrayHelper.Num();
	const FProperty* InnerProperty = Property->Inner;
	if(InnerProperty->HasAnyPropertyFlags(CPF_IsPlainOldData))
	{
		DstArrayHelper.EmptyAndAddValues(Num);
	}
	else
	{
		DstArrayHelper.EmptyAndAddUninitializedValues(Num);
	}

	for(int32 Idx = 0; Idx < Num; ++Idx)
	{
		FRCPropertyVariant Dst{InnerProperty, (uint8*)DstArrayHelper.GetRawPtr(Idx)};
		ToBytes<FProperty>({InnerProperty, SrcArrayHelper.GetRawPtr(Idx)}, Dst);
	}		

	return true;
}

template <>
bool RemoteControlPropertyUtilities::ToBytes<FSetProperty>(const FRCPropertyVariant& InSrc, FRCPropertyVariant& OutDst)
{
	return false;
}

template <>
bool RemoteControlPropertyUtilities::ToBytes<FMapProperty>(const FRCPropertyVariant& InSrc, FRCPropertyVariant& OutDst)
{
	return false;
}

#endif
template <>
bool RemoteControlPropertyUtilities::FromBytes<FProperty>(const FRCPropertyVariant& InSrcData, FRCPropertyVariant& InDst)
{
	const FProperty* Property = InDst.GetProperty();
	FOREACH_CAST_PROPERTY(Property, FromBytes<CastPropertyType>(InSrcData, InDst))

	return true;
}

template <>
bool RemoteControlPropertyUtilities::ToBytes<FProperty>(const FRCPropertyVariant& InSrc, FRCPropertyVariant& OutDst)
{
	const FProperty* Property = InSrc.GetProperty();
	FOREACH_CAST_PROPERTY(Property, ToBytes<CastPropertyType>(InSrc, OutDst))

	return true;
}
