// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RigVMFunction.h"
#include "UObject/Object.h"
#include "RigVMTypeUtils.h"
#include "RigVMTemplate.generated.h"

USTRUCT()
struct RIGVM_API FRigVMTemplateArgumentType
{
	GENERATED_BODY()

	UPROPERTY()
	FName CPPType;
	
	UPROPERTY()
	TObjectPtr<UObject> CPPTypeObject; 

	FRigVMTemplateArgumentType()
		: CPPType(NAME_None)
		, CPPTypeObject(nullptr)
	{
		CPPType = RigVMTypeUtils::GetWildCardCPPTypeName();
		CPPTypeObject = RigVMTypeUtils::GetWildCardCPPTypeObject();
	}

	FRigVMTemplateArgumentType(const FName& InCPPType, UObject* InCPPTypeObject = nullptr)
		: CPPType(InCPPType)
		, CPPTypeObject(InCPPTypeObject)
	{
		check(!CPPType.IsNone());
	}

	static FRigVMTemplateArgumentType Array()
	{
		return FRigVMTemplateArgumentType(RigVMTypeUtils::GetWildCardArrayCPPTypeName(), RigVMTypeUtils::GetWildCardCPPTypeObject());
	}

	bool operator == (const FRigVMTemplateArgumentType& InOther) const
	{
		return CPPType == InOther.CPPType;
	}

	bool operator != (const FRigVMTemplateArgumentType& InOther) const
	{
		return CPPType != InOther.CPPType;
	}

	friend FORCEINLINE uint32 GetTypeHash(const FRigVMTemplateArgumentType& InType)
	{
		return GetTypeHash(InType.CPPType);
	}

	FName GetCPPTypeObjectPath() const
	{
		if(CPPTypeObject)
		{
			return *CPPTypeObject->GetPathName();
		}
		return NAME_None;
	}

	bool IsWildCard() const
	{
		return CPPTypeObject == RigVMTypeUtils::GetWildCardCPPTypeObject() ||
			CPPType == RigVMTypeUtils::GetWildCardCPPTypeName() ||
			CPPType == RigVMTypeUtils::GetWildCardArrayCPPTypeName();
	}

	bool IsArray() const
	{
		return RigVMTypeUtils::IsArrayType(CPPType.ToString());
	}

	FString GetBaseCPPType() const
	{
		if(IsArray())
		{
			return RigVMTypeUtils::BaseTypeFromArrayType(CPPType.ToString());
		}
		return CPPType.ToString();
	}

	void ConvertToArray() 
	{
		CPPType = *RigVMTypeUtils::ArrayTypeFromBaseType(CPPType.ToString());
	}

	void ConvertToBaseElement() 
	{
		CPPType = *RigVMTypeUtils::BaseTypeFromArrayType(CPPType.ToString());
	}
};

/**
 * The template argument represents a single parameter
 * in a function call and all of its possible types
 */
USTRUCT()
struct RIGVM_API FRigVMTemplateArgument
{
	GENERATED_BODY()
	
	enum EArrayType
	{
		EArrayType_SingleValue,
		EArrayType_ArrayValue,
		EArrayType_ArrayArrayValue,
		EArrayType_Mixed,
		EArrayType_Invalid
	};

	// default constructor
	FRigVMTemplateArgument();

	FRigVMTemplateArgument(const FName& InName, ERigVMPinDirection InDirection, int32 InType);
	FRigVMTemplateArgument(const FName& InName, ERigVMPinDirection InDirection, const TArray<int32>& InTypeIndices);

	// returns the name of the argument
	const FName& GetName() const { return Name; }

	// returns the direction of the argument
	ERigVMPinDirection GetDirection() const { return Direction; }

	// returns true if this argument supports a given type across a set of permutations
	bool SupportsTypeIndex(int32 InTypeIndex, int32* OutTypeIndex = nullptr) const;

	// returns the flat list of types (including duplicates) of this argument
	const TArray<int32>& GetTypeIndices() const;

	// returns an array of all of the supported types
	TArray<int32> GetSupportedTypeIndices(const TArray<int32>& InPermutationIndices = TArray<int32>()) const;

#if WITH_EDITOR
	// returns an array of all supported types as strings. this is used for automated testing only.
	TArray<FString> GetSupportedTypeStrings(const TArray<int32>& InPermutationIndices = TArray<int32>()) const;
#endif
	
	// returns true if an argument is singleton (same type for all variants)
	bool IsSingleton(const TArray<int32>& InPermutationIndices = TArray<int32>()) const;

	// returns true if the argument uses an array container
	EArrayType GetArrayType() const;
	
protected:

	UPROPERTY()
	int32 Index;

	UPROPERTY()
	FName Name;

	UPROPERTY()
	ERigVMPinDirection Direction;

	UPROPERTY()
	TArray<int32> TypeIndices;

	TMap<int32, TArray<int32>> TypeToPermutations;

	// constructor from a property
	FRigVMTemplateArgument(FProperty* InProperty);

	friend struct FRigVMTemplate;
	friend class URigVMController;
	friend struct FRigVMRegistry;
};

/**
 * The template is used to group multiple rigvm functions
 * that share the same notation. Templates can then be used
 * to build polymorphic nodes (RigVMTemplateNode) that can
 * take on any of the permutations supported by the template.
 */
USTRUCT()
struct RIGVM_API FRigVMTemplate
{
	GENERATED_BODY()
public:

	typedef TMap<FName, int32> FTypeMap;
	typedef TPair<FName, int32> FTypePair;

	// Default constructor
	FRigVMTemplate();

	// returns true if this is a valid template
	bool IsValid() const;

	// Returns the notation of this template
	const FName& GetNotation() const;

	// Returns the name of the template
	FName GetName() const;

	// returns true if this template is compatible with another one
	bool IsCompatible(const FRigVMTemplate& InOther) const;

	// returns true if this template can merge another one
	bool Merge(const FRigVMTemplate& InOther);

	// returns the number of args of this template
	int32 NumArguments() const { return Arguments.Num(); }

	// returns an argument for a given index
	const FRigVMTemplateArgument* GetArgument(int32 InIndex) const { return &Arguments[InIndex]; }

		// returns an argument given a name (or nullptr)
	const FRigVMTemplateArgument* FindArgument(const FName& InArgumentName) const;

	// returns true if a given arg supports a type
	bool ArgumentSupportsTypeIndex(const FName& InArgumentName, int32 InTypeIndex, int32* OutTypeIndex = nullptr) const;

	// returns the number of permutations supported by this template
	int32 NumPermutations() const { return Permutations.Num(); }

	// returns a permutation given an index
	const FRigVMFunction* GetPermutation(int32 InIndex) const;

	// returns true if a given function is a permutation of this template
	bool ContainsPermutation(const FRigVMFunction* InPermutation) const;

	// returns the index of the permutation within the template of a given function (or INDEX_NONE)
	int32 FindPermutation(const FRigVMFunction* InPermutation) const;

	// returns true if the template was able to resolve to single permutation
	bool FullyResolve(FTypeMap& InOutTypes, int32& OutPermutationIndex) const;

	// returns true if the template was able to resolve to at least one permutation
	bool Resolve(FTypeMap& InOutTypes, TArray<int32> & OutPermutationIndices, bool bAllowFloatingPointCasts) const;

	// returns true if the template can resolve an argument to a new type
	bool ResolveArgument(const FName& InArgumentName, const int32 InTypeIndex, FTypeMap& InOutTypes) const;

	// returns true if a given argument is valid for a template
	static bool IsValidArgumentForTemplate(const FRigVMTemplateArgument& InArgument);

	// returns the prefix for an argument in the notation
	static const FString& GetArgumentNotationPrefix(const FRigVMTemplateArgument& InArgument);

	// returns the notation of an argument
	static FString GetArgumentNotation(const FRigVMTemplateArgument& InArgument);

	// returns an array of structs in the inheritance order of a given struct
	static TArray<UStruct*> GetSuperStructs(UStruct* InStruct, bool bIncludeLeaf = true);

#if WITH_EDITOR

	// Returns the color based on the permutation's metadata
	FLinearColor GetColor(const TArray<int32>& InPermutationIndices = TArray<int32>()) const;

	// Returns the tooltip based on the permutation's metadata
	FText GetTooltipText(const TArray<int32>& InPermutationIndices = TArray<int32>()) const;

	// Returns the display name text for an argument 
	FText GetDisplayNameForArgument(const FName& InArgumentName, const TArray<int32>& InPermutationIndices = TArray<int32>()) const;

	// Returns meta data on the property of the permutations 
	FString GetArgumentMetaData(const FName& InArgumentName, const FName& InMetaDataKey, const TArray<int32>& InPermutationIndices = TArray<int32>()) const;

	FString GetCategory() const;
	FString GetKeywords() const;

#endif

private:

	// Constructor from a struct, a template name and a function index
	FRigVMTemplate(UScriptStruct* InStruct, const FString& InTemplateName, int32 InFunctionIndex);

	// Constructor from a template name, arguments and a function index
	FRigVMTemplate(const FName& InTemplateName, const TArray<FRigVMTemplateArgument>& InArguments, int32 InFunctionIndex);

	UPROPERTY()
	int32 Index;

	UPROPERTY()
	FName Notation;

	UPROPERTY()
	TArray<FRigVMTemplateArgument> Arguments;

	UPROPERTY()
	TArray<int32> Permutations;

	friend struct FRigVMRegistry;
	friend class URigVMController;
	friend class URigVMLibraryNode;
};

