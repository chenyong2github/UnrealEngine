// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "OptimusDataType.h"

#include "Containers/Array.h"
#include "Containers/Map.h"
#include "Misc/Optional.h"

class FFieldClass;
class UScriptStruct;
struct FShaderValueTypeHandle;

class FOptimusDataTypeRegistry
{
public:
	~FOptimusDataTypeRegistry();

	/** Get the singleton registry object */
	OPTIMUSDEVELOPER_API static FOptimusDataTypeRegistry &Get();

	// Register a POD type that has corresponding types on both the UE and HLSL side.
	OPTIMUSDEVELOPER_API bool RegisterType(
		const FFieldClass &InFieldType,
	    FShaderValueTypeHandle InShaderValueType,
	    FName InPinCategory,
	    TOptional<FLinearColor> InPinColor,
	    EOptimusDataTypeUsageFlags InUsageFlags
	);

	// Register a complex type that has corresponding types on both the UE and HLSL side.
	OPTIMUSDEVELOPER_API bool RegisterType(
	    UScriptStruct *InStructType,
	    FShaderValueTypeHandle InShaderValueType,
		TOptional<FLinearColor> InPinColor,
		bool bInShowElements,
	    EOptimusDataTypeUsageFlags InUsageFlags
		);

	// Register a complex type that has only has correspondence on the UE side.
	OPTIMUSDEVELOPER_API bool RegisterType(
	    UClass* InClassType,
	    TOptional<FLinearColor> InPinColor,
	    EOptimusDataTypeUsageFlags InUsageFlags
		);

	// Register a type that only has correspondence on the HLSL side. 
	// Presence of the EOptimusDataTypeFlags::UseInVariable results in an error.
	OPTIMUSDEVELOPER_API bool RegisterType(
		FName InTypeName,
	    FShaderValueTypeHandle InShaderValueType,
	    FName InPinCategory,
	    UObject* InPinSubCategory,
		FLinearColor InPinColor,
	    EOptimusDataTypeUsageFlags InUsageFlags
		);

	/** Returns all registered types */
	OPTIMUSDEVELOPER_API TArray<FOptimusDataTypeHandle> GetAllTypes() const;

	/** Find the registered type associated with the given property's type. Returns an invalid
	  * handle if no registered type is associated.
	*/
	OPTIMUSDEVELOPER_API FOptimusDataTypeHandle FindType(const FProperty &InProperty) const;

	/** Find the registered type associated with the given field class. Returns an invalid
	  * handle if no registered type is associated.
	*/
	OPTIMUSDEVELOPER_API FOptimusDataTypeHandle FindType(const FFieldClass& InFieldType) const;

	/** Find the registered type with the given name. Returns an invalid handle if no registered 
	  * type with that name exists.
	*/
	OPTIMUSDEVELOPER_API FOptimusDataTypeHandle FindType(FName InTypeName) const;

protected:
	friend class FOptimusDeveloperModule;

	/** Call during module init to register all known built-in types */
	static void RegisterBuiltinTypes();

	/** Call during module shutdown to release memory */
	static void UnregisterAllTypes();

private:
	FOptimusDataTypeRegistry() = default;

	bool RegisterType(FName InTypeName, TFunction<void(FOptimusDataType &)> InFillFunc);

	TMap<FName /* TypeName */, FOptimusDataTypeHandle> RegisteredTypes;
	TArray<FName> RegistrationOrder;
};
