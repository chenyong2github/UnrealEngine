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
	using PropertyCreateFuncT = TFunction<FProperty *(UStruct *InScope, FName InName)>;

	/** Defines a pointer to a function that takes a const uint8 pointer that points to the
	  * property value, and adds entries to the OutShaderValue and fills them with a value
	  * converted that matches what a shader parameter structure expects (e.g. bool is
	  * converted to a 32-bit integer). The pointer returned is a pointer to the value after
	  * the converted from value.
	  */
	using PropertyValueConvertFuncT = TFunction<bool(TArrayView<const uint8> InRawValue, TArray<uint8>& OutShaderValue)>;

	~FOptimusDataTypeRegistry();

	/** Get the singleton registry object */
	OPTIMUSCORE_API static FOptimusDataTypeRegistry &Get();

	// Register a POD type that has corresponding types on both the UE and HLSL side.
	OPTIMUSCORE_API bool RegisterType(
		const FFieldClass &InFieldType,
		const FText& InDisplayName,
	    FShaderValueTypeHandle InShaderValueType,
		PropertyCreateFuncT InPropertyCreateFunc,
		PropertyValueConvertFuncT InPropertyValueConvertFunc,
	    FName InPinCategory,
	    TOptional<FLinearColor> InPinColor,
	    EOptimusDataTypeUsageFlags InUsageFlags
	);

	// Register a complex type that has corresponding types on both the UE and HLSL side.
	OPTIMUSCORE_API bool RegisterType(
	    UScriptStruct *InStructType,
	    FShaderValueTypeHandle InShaderValueType,
		TOptional<FLinearColor> InPinColor,
		bool bInShowElements,
	    EOptimusDataTypeUsageFlags InUsageFlags
		);

	// Register a complex type that has only has correspondence on the UE side.
	OPTIMUSCORE_API bool RegisterType(
	    UClass* InClassType,
	    TOptional<FLinearColor> InPinColor,
	    EOptimusDataTypeUsageFlags InUsageFlags
		);

	// Register a type that only has correspondence on the HLSL side. 
	// Presence of the EOptimusDataTypeFlags::UseInVariable results in an error.
	OPTIMUSCORE_API bool RegisterType(
		FName InTypeName,
		const FText& InDisplayName,
	    FShaderValueTypeHandle InShaderValueType,
	    FName InPinCategory,
	    UObject* InPinSubCategory,
		FLinearColor InPinColor,
	    EOptimusDataTypeUsageFlags InUsageFlags
		);

	/** Returns all registered types */
	OPTIMUSCORE_API TArray<FOptimusDataTypeHandle> GetAllTypes() const;

	/** Find the registered type associated with the given property's type. Returns an invalid
	  * handle if no registered type is associated.
	*/
	OPTIMUSCORE_API FOptimusDataTypeHandle FindType(const FProperty &InProperty) const;

	/** Find the registered type associated with the given field class. Returns an invalid
	  * handle if no registered type is associated.
	*/
	OPTIMUSCORE_API FOptimusDataTypeHandle FindType(const FFieldClass& InFieldType) const;

	/** Find the registered type with the given name. Returns an invalid handle if no registered 
	  * type with that name exists.
	*/
	OPTIMUSCORE_API FOptimusDataTypeHandle FindType(FName InTypeName) const;

	/** Find a registered type from a FShaderValueTypeHandle. If multiple types are using the
	  * same shader value type, then the first one found in the registration order will be
	  * returned.
	  */
	// FIXME: We should allow for some kind of type hinting from the HLSL side (e.g. vector4 a color or a vector of four independent scalars).
	OPTIMUSCORE_API FOptimusDataTypeHandle FindType(FShaderValueTypeHandle InValueType) const;

protected:
	friend class FOptimusCoreModule;
	friend struct FOptimusDataType;

	/** Call during module init to register all known built-in types */
	static void RegisterBuiltinTypes();

	/** Call during module shutdown to release memory */
	static void UnregisterAllTypes();

	/** A helper function to return a property create function. The function can be unbound
	 *  and that should be checked prior to calling
	 */
	PropertyCreateFuncT FindPropertyCreateFunc(FName InTypeName) const;

	/** A helper function to return a property create function. The function can be unbound
	*  and that should be checked prior to calling
	*/
	PropertyValueConvertFuncT FindPropertyValueConvertFunc(FName InTypeName) const;

private:
	FOptimusDataTypeRegistry() = default;

	bool RegisterType(
		FName InTypeName,
		TFunction<void(FOptimusDataType &)> InFillFunc,
	    PropertyCreateFuncT InPropertyCreateFunc = {},
	    PropertyValueConvertFuncT InPropertyValueConvertFunc = {}
		);

	struct FTypeInfo
	{
		FOptimusDataTypeHandle Handle;
		PropertyCreateFuncT PropertyCreateFunc;
		PropertyValueConvertFuncT PropertyValueConvertFunc;
	};

	TMap<FName /* TypeName */, FTypeInfo> RegisteredTypes;
	TArray<FName> RegistrationOrder;
};
