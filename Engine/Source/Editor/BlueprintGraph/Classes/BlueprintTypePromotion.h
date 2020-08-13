// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EdGraphSchema_K2.h"
#include "BlueprintActionDatabase.h"
#include "BlueprintFunctionNodeSpawner.h"
#include "HAL/IConsoleManager.h"

/** Contains behavior needed to handle type promotion in blueprints */
class FTypePromotion : private FNoncopyable
{
public:

	static FTypePromotion& Get();

	/** 
	* If given more than two input pins, this will just use the highest type
	* and return the function for that operation. It's update to nodes like the 
	* UK2Node_CommutativeAssociativeBinaryOperator to patch the input pins
	* together on expansion
	* 
	* @return	A function that matches a promoted type. Nullptr if no match is found
	*/
	static UFunction* GetOperatorFunction(const FString& Operation, const TArray<UEdGraphPin*>& WildcardPins);

	static UFunction* GetOperatorFunction(const FString& Operation, const TArray<UEdGraphPin*>& InputPins, const UEdGraphPin* OutputPin);

	/** 
	* Find the function that has this input and the lowest matching other input.
	* Ex: Given "Add" and "Vector" this function would return the "Add_VectorFloat" function
	*/
	static UFunction* FindLowestMatchingFunc(const FString& Operation, const FEdGraphPinType& InputType, TArray<UFunction*>& OutPossibleFunctions);

	/** Returns all functions for a specific operation. Will empty the given array and populate it with UFunction pointers */
	static void GetAllFuncsForOp(const FString& Operation, TArray<UFunction*>& OutFuncs);

	/** Get an array of the supported operator names for type promo. Ex: "Add", "Subtract", "Multiply" */
	static const TArray<FString>& GetOpNames();

	static bool GetOpNameFromFunction(UFunction const* const Func, FString& OutName);

	/** Returns true if the given function is a candidate to handle type promotion */
	static bool IsPromotableFunction(const UFunction* Function);

	/**
	* Determine what type a given set of wildcard pins would result in
	*
	* @return	Pin type that is the "highest" of all the given pins
	*/
	static FEdGraphPinType GetPromotedType(const TArray<UEdGraphPin*>& WildcardPins);

	/**
	* Attempts to promote type A to type B. Will only work if TypeB is higher than type B
	*
	* @return	True if the promotion was successful
	*/
	static bool PromotePin(FEdGraphPinType& InTypeA, const FEdGraphPinType& TypeB);

	static bool IsFunctionPromotionReady(const UFunction* const FuncToConsider);

	/** Represents the possible results when comparing two types for promotion */
	enum class ETypeComparisonResult : uint8
	{
		TypeAHigher,
		TypeBHigher,
		TypesEqual,
		InvalidComparison
	};

	/**
	* 
	* 
	* 
	* @return 
	*/
	static ETypeComparisonResult GetHigherType(const FEdGraphPinType& A, const FEdGraphPinType& B);

	/** Returns true if A can be promoted to type B correctly, or if the types are equal */
	static bool IsValidPromotion(const FEdGraphPinType& A, const FEdGraphPinType& B);

	static bool HasStructConversion(const UEdGraphPin* InputPin, const UEdGraphPin* OutputPin);

	static bool IsOperatorSpawnerRegistered(UFunction const* const Func);

	static UBlueprintFunctionNodeSpawner* GetOperatorSpawner(const FString& OpName);

	/** keep track of the operator that this function provides so that we dont add multiple 
	to the BP context menu */
	static void RegisterOperatorSpawner(const FString& OpName, UBlueprintFunctionNodeSpawner* Spawner);

	static void ClearNodeSpawners();

private:

	explicit FTypePromotion();
	~FTypePromotion();

	/** Callback that will rebuild the op table when hot reload is triggered */
	static void OnModulesChanged(FName ModuleThatChanged, EModuleChangeReason ReasonForChange);

	UFunction* GetOperatorFunction_Internal(const FString& Operation, const TArray<UEdGraphPin*>& WildcardPins) const;

	UFunction* GetOperatorFunction_Internal(const FString& Operation, const TArray<UEdGraphPin*>& InputPins, const UEdGraphPin* OutputPin) const;

	bool IsFunctionPromotionReady_Internal(const UFunction* const FuncToConsider) const;

	FEdGraphPinType GetPromotedType_Internal(const TArray<UEdGraphPin*>& WildcardPins) const;

	UFunction* FindLowestMatchingFunc_Internal(const FString& Operation, const FEdGraphPinType& InputType, TArray<UFunction*>& OutPossibleFunctions);

	void GetAllFuncsForOp_Internal(const FString& Operation, TArray<UFunction*>& OutFuncs);

	bool PromotePin_Internal(FEdGraphPinType& InTypeA, const FEdGraphPinType& TypeB);

	/**
	* Determines which pin type is "higher" 
	* 
	* @see FTypePromotion::PromotionTable
	*/
	ETypeComparisonResult GetHigherType_Internal(const FEdGraphPinType& A, const FEdGraphPinType& B) const;

	/** Creates a lookup table of types and operations to their appropriate UFunction */
	void CreateOpTable();

	/** Creates the table of what types can be promoted to others */
	void CreatePromotionTable();

	void AddOpFunction(const FString& OpName, UFunction* Function);

	static FTypePromotion* Instance;

	/** Delegate that gets called when a module is reloaded so we can rebuild the op table */
	FDelegateHandle OnModulesChangedDelegateHandle;

	/** A map of 'Type' to its 'available promotions'. See ctor for creation */
	TMap<FName, TArray<FName>> PromotionTable;

	/**
	 * A single operator can have multiple functions associated with it; usually
	 * for handling different types (int*int, vs. int*vector), hence this array.
	 * This is the same implementation style as the Math Expression node.
	 */
	typedef TArray<UFunction*> FFunctionsList;

	/**
	 * A lookup table, mapping operator strings (like "Add", "Multiply", etc.) to a list
	 * of associated functions.
	 */
	TMap<FString, FFunctionsList> OperatorTable;

	/** Map of operators to their node spawner so that we can clean up the context menu */
	TMap<FString, UBlueprintFunctionNodeSpawner*> OperatorNodeSpawnerMap;
};

namespace TypePromoDebug
{
	/** Enables/Disables type promotion in BP */
	static bool bIsTypePromoEnabled = false;
	static FAutoConsoleVariableRef CVarIsTypePromoEnabled(
		TEXT("BP.TypePromo.IsEnabled"), bIsTypePromoEnabled,
		TEXT("If true then type promotion inside of blueprints will be enabled"),
		FConsoleVariableDelegate::CreateLambda([](IConsoleVariable* InVariable)
			{
				// Clear the node spawner so that we create the new BP actions correctly
				FTypePromotion::ClearNodeSpawners();

				// Refresh all the actions so that the context menu goes back to the normal options
				if (FBlueprintActionDatabase* Actions = FBlueprintActionDatabase::TryGet())
				{
					Actions->RefreshAll();
				}
			}),
		ECVF_Default);
}