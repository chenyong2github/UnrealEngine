// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PropertyHelpers.h"

class FProperty;
class UStruct;
class FUsdAttribute;
class AActor;
struct FUsdImportContext;

#if USE_USD_SDK

namespace UE
{
	class FUsdAttribute;
	class FUsdPrim;
}

typedef TFunction<void(void*, const UE::FUsdAttribute&, FProperty*, int32)> FStructSetterFunction;

class FUSDPropertySetter
{
public:
	FUSDPropertySetter(FUsdImportContext& InImportContext);
	
	/**
	 * Applies properties found on a UsdPrim (and possibly its children) to a spawned actor
	 */
	void ApplyPropertiesToActor(AActor* SpawnedActor, const UE::FUsdPrim& Prim, const FString& StartingPropertyPath);

	/**
	 * Registers a setter for a struct type to set the struct in bulk instad of by individual inner property
	 */
	void RegisterStructSetter(FName StructName, FStructSetterFunction Function);
private:
	/**
	 * Finds properties and addresses for those properties and applies them from values in USD attributes
	 */
	void ApplyPropertiesFromUsdAttributes(const UE::FUsdPrim& Prim, AActor* SpawnedActor, const FString& StartingPropertyPath);

	/**
	 * Sets a property value from a USD Attribute
	 */
	void SetFromUSDValue(PropertyHelpers::FPropertyAddress& PropertyAddress, const UE::FUsdPrim& Prim, const UE::FUsdAttribute& Attribute, int32 ArrayIndex);

	/**
	 * Finds Key/Value pairs for TMap properties;
	 */
	bool FindMapKeyAndValues(const UE::FUsdPrim& Prim, UE::FUsdAttribute& OutKey, TArray<UE::FUsdAttribute>& OutValues);

	/**
	 * Verifies the result of trying to set a given usd attribute with a given usd property.  Will produce an error if the types are incompatible
	 */
	bool VerifyResult(bool bResult, const UE::FUsdAttribute& Attribute, FProperty* Property);

	/**
	 * Combines two property paths into a single "." delimited property path
	 */
	FString CombinePropertyPaths(const FString& Path1, const FString& Path2);
private:
	/** Registered struct types that have setters to set the values in bulk without walking the properties */
	TMap<FName, FStructSetterFunction> StructToSetterMap;

	FUsdImportContext& ImportContext;
};
#endif // #if USE_USD_SDK
