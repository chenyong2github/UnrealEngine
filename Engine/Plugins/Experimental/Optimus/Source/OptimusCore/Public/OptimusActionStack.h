// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"

#include "Containers/Array.h"
#include "Templates/UniquePtr.h"
#include "Templates/UnrealTypeTraits.h"

#include "OptimusActionStack.generated.h"

class IOptimusNodeGraphCollectionOwner;
struct FOptimusAction;

// Base action class
UCLASS()
class OPTIMUSCORE_API UOptimusActionStack :
	public UObject
{
	GENERATED_BODY()
public:
	UOptimusActionStack();

	/// Run a heap-constructed action created with operator new. 
	/// The action stack takes ownership of the pointer. If the function fails the pointer is
	/// no longer valid.
	bool RunAction(FOptimusAction* InAction);

	template<typename T, typename... ArgsType>
	typename TEnableIf<TPointerIsConvertibleFromTo<T, FOptimusAction>::Value, bool>::Type 
	RunAction(ArgsType&& ...Args)
	{
		return RunAction(new T(Forward<ArgsType>(Args)...));
	}

	// UObject override
	
	// The meat and potatoes of the undo/redo mechanism. 
	void PostTransacted(const FTransactionObjectEvent& TransactionEvent) override;

	IOptimusNodeGraphCollectionOwner *GetGraphCollectionRoot() const;

	void SetTransactionScopeFunctions(
		TFunction<int32(UObject* TransactObject, const FString& Title)> InBeginScopeFunc,
		TFunction<void(int32 InTransactionId)> InEndScopeFunc
		);

	bool Redo();
	bool Undo();
private:
	UPROPERTY()
	int32 TransactedActionIndex = 0;

	int32 CurrentActionIndex = 0;

	TArray<TSharedPtr<FOptimusAction>> Actions;

	TFunction<int(UObject* TransactObject, const FString& Title)> BeginScopeFunc;
	TFunction<void(int InTransactionId)> EndScopeFunc;
};
