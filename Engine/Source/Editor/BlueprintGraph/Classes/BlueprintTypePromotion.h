// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EdGraphSchema_K2.h"
#include "BlueprintActionDatabase.h"
#include "BlueprintFunctionNodeSpawner.h"
#include "HAL/IConsoleManager.h"

/** 
* Contains behavior needed to handle type promotion in blueprints. 
* Creates a map of "Operations" to any of their matching UFunctions
* so that we can find the best possible match given several pin types.  
*/
class BLUEPRINTGRAPH_API FTypePromotion : private FNoncopyable
{
public:

	/** Creates a new singleton instance of TypePromotion if there isn't one and returns a reference to it */
	static FTypePromotion& Get();

	/** Deletes the singleton instance of type promotion if there is one */
	static void Shutdown();

	/**
	* Find the function that is the best match given the pins to consider. 
	* Ex: Given "Add" operator and an array of two Vector pins, it will return "Add_VectorVector"
	*/
	static UFunction* FindBestMatchingFunc(FName Operation, const TArray<UEdGraphPin*>& PinsToConsider);

	/** Returns all functions for a specific operation. Will empty the given array and populate it with UFunction pointers */
	static void GetAllFuncsForOp(FName Operation, TArray<UFunction*>& OutFuncs);

	/** Get a set of the supported operator names for type promo. Ex: "Add", "Subtract", "Multiply" */
	static const TSet<FName>& GetAllOpNames();
	
	/** Set of comparison operator names (GreaterThan, LessThan, etc) */
	static const TSet<FName>& GetComparisonOpNames();

	/** Returns true if the given function is a comparison operator */
	static bool IsComparisonFunc(UFunction const* const Func);

	/** Returns true if the given op name is a comparison operator name */
	static bool IsComparisonOpName(const FName OpName);

	static FName GetOpNameFromFunction(UFunction const* const Func);

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

	/** Returns true if the given function can be used for type promotion (it is within the operator table) */
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
	* Given the two pin types cehck which pin type is higher. 
	* Given two structs it will return equal, this does NOT compare PinDefaultSubobjects
	*/
	static ETypeComparisonResult GetHigherType(const FEdGraphPinType& A, const FEdGraphPinType& B);

	/** Returns true if A can be promoted to type B correctly, or if the types are equal */
	static bool IsValidPromotion(const FEdGraphPinType& A, const FEdGraphPinType& B);

	/** Returns true if the given input pin can correctly be converted to the output type as a struct */
	static bool HasStructConversion(const UEdGraphPin* InputPin, const UEdGraphPin* OutputPin);

	/** Returns true if the given function has a registered operator node spawner */
	static bool IsOperatorSpawnerRegistered(UFunction const* const Func);

	/** Function node spawner associated with this operation */
	static UBlueprintFunctionNodeSpawner* GetOperatorSpawner(FName OpName);

	/** keep track of the operator that this function provides so that we dont add multiple 
	to the BP context menu */
	static void RegisterOperatorSpawner(FName OpName, UBlueprintFunctionNodeSpawner* Spawner);

	static void ClearNodeSpawners();

	static const TMap<FName, TArray<FName>>* const GetPrimativePromotionTable();

private:

	explicit FTypePromotion();
	~FTypePromotion();

	/** Callback that will rebuild the op table when hot reload is triggered */
	static void OnModulesChanged(FName ModuleThatChanged, EModuleChangeReason ReasonForChange);

	bool IsFunctionPromotionReady_Internal(const UFunction* const FuncToConsider) const;

	FEdGraphPinType GetPromotedType_Internal(const TArray<UEdGraphPin*>& WildcardPins) const;
	
	UFunction* FindBestMatchingFunc_Internal(FName Operation, const TArray<UEdGraphPin*>& PinsToConsider);

	void GetAllFuncsForOp_Internal(FName Operation, TArray<UFunction*>& OutFuncs);

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

	void AddOpFunction(FName OpName, UFunction* Function);

	static FTypePromotion* Instance;

	/** Delegate to handle that will be used to refresh the op table when a module has changed */
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
	TMap<FName, FFunctionsList> OperatorTable;

	/** Map of operators to their node spawner so that we can clean up the context menu */
	TMap<FName, UBlueprintFunctionNodeSpawner*> OperatorNodeSpawnerMap;
};

namespace TypePromoDebug
{
	/** Enables/Disables type promotion in BP */
	static bool IsTypePromoEnabled()
	{
		static IConsoleVariable* TypePromoCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("BP.TypePromo.IsEnabled"));
		return TypePromoCVar ? TypePromoCVar->GetBool() : false;
	}
}