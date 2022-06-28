// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RigVMMemory.h"
#include "RigVMFunction.h"
#include "RigVMTemplate.h"

/**
 * The FRigVMRegistry is used to manage all known function pointers
 * for use in the RigVM. The Register method is called automatically
 * when the static struct is initially constructed for each USTRUCT
 * hosting a RIGVM_METHOD enabled virtual function.
 */
struct RIGVM_API FRigVMRegistry
{
public:

	// Returns the singleton registry
	static FRigVMRegistry& Get();

	// Registers a function given its name.
	// The name will be the name of the struct and virtual method,
	// for example "FMyStruct::MyVirtualMethod"
	void Register(const TCHAR* InName, FRigVMFunctionPtr InFunctionPtr, UScriptStruct* InStruct = nullptr, const TArray<FRigVMFunctionArgument>& InArguments = TArray<FRigVMFunctionArgument>());

	// Initializes the registry by storing the defaults
	void InitializeIfNeeded();
	
	// Refreshes the list and finds the function pointers
	// based on the names.
	void Refresh();

	// Adds a type if it doesn't exist yet and returns its index.
	// This function is not thead-safe
	int32 FindOrAddType(const FRigVMTemplateArgumentType& InType);

	// Returns the type index given a type
	int32 GetTypeIndex(const FRigVMTemplateArgumentType& InType) const;

	// Returns the type index given a cpp type and a type object
	FORCEINLINE int32 GetTypeIndex(const FName& InCPPType, UObject* InCPPTypeObject) const
	{
		return GetTypeIndex(FRigVMTemplateArgumentType(InCPPType, InCPPTypeObject));
	}

	// Returns the type given its index
	const FRigVMTemplateArgumentType& GetType(int32 InTypeIndex) const;

	// Returns the number of types
	FORCEINLINE int32 NumTypes() const { return Types.Num(); }

	// Returns the type given only its cpp type
	const FRigVMTemplateArgumentType& FindTypeFromCPPType(const FString& InCPPType) const;

	// Returns the type index given only its cpp type
	int32 GetTypeIndexFromCPPType(const FString& InCPPType) const;

	// Returns true if the type is an array
	bool IsArrayType(int32 InTypeIndex) const;

	// Returns the dimensions of the array 
	int32 GetArrayDimensionsForType(int32 InTypeIndex) const;

	// Returns true if the type is a wildcard type
	bool IsWildCardType(int32 InTypeIndex) const;

	// Returns true if the types can be matched.
	bool CanMatchTypes(int32 InTypeIndexA, int32 InTypeIndexB, bool bAllowFloatingPointCasts) const;

	// Returns the list of compatible types for a given type
	const TArray<int32>& GetCompatibleTypes(int32 InTypeIndex) const;

	enum ETypeCategory
	{
		ETypeCategory_SingleAnyValue,
		ETypeCategory_ArrayAnyValue,
		ETypeCategory_ArrayArrayAnyValue,
		ETypeCategory_SingleSimpleValue,
		ETypeCategory_ArraySimpleValue,
		ETypeCategory_ArrayArraySimpleValue,
		ETypeCategory_SingleMathStructValue,
		ETypeCategory_ArrayMathStructValue,
		ETypeCategory_ArrayArrayMathStructValue,
		ETypeCategory_SingleScriptStructValue,
		ETypeCategory_ArrayScriptStructValue,
		ETypeCategory_ArrayArrayScriptStructValue,
		ETypeCategory_SingleEnumValue,
		ETypeCategory_ArrayEnumValue,
		ETypeCategory_ArrayArrayEnumValue,
		ETypeCategory_SingleObjectValue,
		ETypeCategory_ArrayObjectValue,
		ETypeCategory_ArrayArrayObjectValue,
		ETypeCategory_Invalid
	};

	// Returns all compatible types given a category
	const TArray<int32>& GetTypesForCategory(ETypeCategory InCategory);

	// Returns the type index of the array matching the given element type index
	int32 GetArrayTypeFromBaseTypeIndex(int32 InTypeIndex) const;

	// Returns the type index of the element matching the given array type index
	int32 GetBaseTypeFromArrayTypeIndex(int32 InTypeIndex) const;

	// Returns the function given its name (or nullptr)
	const FRigVMFunction* FindFunction(const TCHAR* InName) const;

	// Returns the function given its backing up struct and method name
	const FRigVMFunction* FindFunction(UScriptStruct* InStruct, const TCHAR* InName) const;

	// Returns all current rigvm functions
	const TChunkedArray<FRigVMFunction>& GetFunctions() const;

	// Returns a template pointer given its notation (or nullptr)
	const FRigVMTemplate* FindTemplate(const FName& InNotation) const;

	// Returns all current rigvm functions
	const TChunkedArray<FRigVMTemplate>& GetTemplates() const;

	// Defines and retrieves a template given its arguments
	const FRigVMTemplate* GetOrAddTemplateFromArguments(const FName& InName, const TArray<FRigVMTemplateArgument>& InArguments);

	static const TArray<UScriptStruct*>& GetMathTypes();

private:

	static const FName TemplateNameMetaName;

	// disable default constructor
	FRigVMRegistry() {}
	// disable copy constructor
	FRigVMRegistry(const FRigVMRegistry&) = delete;
	// disable assignment operator
	FRigVMRegistry& operator= (const FRigVMRegistry &InOther) = delete;

	struct FTypeInfo
	{
		FTypeInfo()
			: Type()
			, BaseTypeIndex(INDEX_NONE)
			, ArrayTypeIndex(INDEX_NONE)
			, bIsArray(false)
		{}
		
		FRigVMTemplateArgumentType Type;
		int32 BaseTypeIndex;
		int32 ArrayTypeIndex;
		bool bIsArray;
	};

	FORCEINLINE static EObjectFlags DisallowedFlags()
	{
		return RF_BeginDestroyed | RF_FinishDestroyed;
	}

	FORCEINLINE static EObjectFlags NeededFlags()
	{
		return RF_Public;
	}

	static bool IsAllowedType(const FProperty* InProperty, bool bCheckFlags = true);
	static bool IsAllowedType(const UEnum* InEnum);
	static bool IsAllowedType(const UStruct* InStruct);
	static bool IsAllowedType(const UClass* InClass);

	// memory for all (known) types
	// We use TChunkedArray because we need the memory locations to be stable, since we only ever add and never remove.
	TArray<FTypeInfo> Types;
	TMap<FRigVMTemplateArgumentType, int32> TypeToIndex;

	// memory for all functions
	// We use TChunkedArray because we need the memory locations to be stable, since we only ever add and never remove.
	TChunkedArray<FRigVMFunction> Functions;

	// memory for all templates
	TChunkedArray<FRigVMTemplate> Templates;

	// name lookup for functions
	TMap<FName, int32> FunctionNameToIndex;

	// name lookup for templates
	TMap<FName, int32> TemplateNotationToIndex;

	// Maps storing the default types per type category
	TMap<ETypeCategory, TArray<int32>> TypesPerCategory;

	static FRigVMRegistry s_RigVMRegistry;
	friend struct FRigVMStruct;
};
