// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Templates/UniquePtr.h"

class IPCGAttributeAccessor;
class IPCGAttributeAccessorKeyIterator;
class IPCGAttributeAccessorKeys;
class FProperty;
class UClass;
class UPCGData;
class UStruct;
struct FPCGAttributePropertySelector;

namespace PCGAttributeAccessorHelpers
{
	TUniquePtr<IPCGAttributeAccessor> CreatePropertyAccessor(const FProperty* InProperty);
	TUniquePtr<IPCGAttributeAccessor> CreatePropertyAccessor(const FName InPropertyName, const UClass* InClass);
	TUniquePtr<IPCGAttributeAccessor> CreatePropertyAccessor(const FName InPropertyName, const UStruct* InStruct);

	/** 
	* Creates a const accessor to the property or attribute pointed at by the InSelector.
	* Note that InData must not be null if the selector points to an attribute,
	* but in the case of properties, it either has to be the appropriate type or null.
	*/
	TUniquePtr<const IPCGAttributeAccessor> CreateConstAccessor(const UPCGData* InData, const FPCGAttributePropertySelector& InSelector);
	
	/** 
	* Creates a non-const accessor to the property or attribute pointed at by the InSelector.
	* Note that InData must not be null if the selector points to an attribute,
	* but in the case of properties, it either has to be the appropriate type or null.
	*/
	TUniquePtr<IPCGAttributeAccessor> CreateAccessor(UPCGData* InData, const FPCGAttributePropertySelector& InSelector);

	TUniquePtr<const IPCGAttributeAccessorKeys> CreateConstKeys(const UPCGData* InData, const FPCGAttributePropertySelector& InSelector);
	TUniquePtr<IPCGAttributeAccessorKeys> CreateKeys(UPCGData* InData, const FPCGAttributePropertySelector& InSelector);
}