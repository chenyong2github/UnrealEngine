// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AnimNodeFunctionRef.generated.h"

/**
 * Cached function name/ptr that is resolved at init time
 */
USTRUCT()
struct ENGINE_API FAnimNodeFunctionRef
{
	GENERATED_BODY()

public:
	// Cache the function ptr from the name
	void Initialize(const UClass* InClass);
	
	// Call the function
	void Call(UObject* InObject, void* InParameters = nullptr) const;

	// Set the function via name
	void SetFromFunctionName(FName InName) { FunctionName = InName; }

	// Set the function via a function
	void SetFromFunction(UFunction* InFunction) { FunctionName = InFunction ? InFunction->GetFName() : NAME_None; }
	
	// Get the function name
	FName GetFunctionName() const { return FunctionName; }
	
	// Get the function we reference
	UFunction* GetFunction() const { return Function; }
	
	// Check if we reference a valid function
	bool IsValid() const { return Function != nullptr; }

private:
	// The name of the function to call
	UPROPERTY()
	FName FunctionName = NAME_None;

	// The function to call, recovered by looking for a function of name FunctionName
	UPROPERTY(Transient)
	UFunction* Function = nullptr;
};