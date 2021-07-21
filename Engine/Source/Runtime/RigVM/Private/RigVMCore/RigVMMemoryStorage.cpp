// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigVMCore/RigVMMemoryStorage.h"

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

URigVMMemoryStorage::FPropertyDescription::FPropertyDescription(const FProperty* InProperty, const FString& InDefaultValue, const FName& InName)
	: Name(InName)
	, Property(InProperty)
	, PinType()
	, DefaultValue(InDefaultValue)
{
}

URigVMMemoryStorage::FPropertyDescription::FPropertyDescription(const FName& InName, const FEdGraphPinType& InPinType, const FString& InDefaultValue)
	: Name(InName)
	, Property(nullptr)
	, PinType(InPinType)
	, DefaultValue(InDefaultValue)
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UClass* URigVMMemoryStorage::CreateStorageClass(UObject* InOuter, const TArray<FPropertyDescription>& InProperties)
{
	return nullptr;
}

URigVMMemoryStorage* URigVMMemoryStorage::CreateStorage(UObject* InOuter,
	const TArray<FPropertyDescription>& InProperties)
{
	return nullptr;
}