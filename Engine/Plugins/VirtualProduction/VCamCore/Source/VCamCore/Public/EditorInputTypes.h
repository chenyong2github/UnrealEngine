// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"
#include "Input/Events.h"
#include "VCamModifier.h"


#include "EditorInputTypes.generated.h"

class UVCamModifier;

DECLARE_DYNAMIC_DELEGATE_TwoParams(FKeyInputDelegate, float, DeltaTime, const FKeyEvent&, KeyEvent);
DECLARE_DYNAMIC_DELEGATE_TwoParams(FAnalogInputDelegate, float, DeltaTime, const FAnalogInputEvent&, AnalogEvent);
DECLARE_DYNAMIC_DELEGATE_TwoParams(FPointerInputDelegate, float, DeltaTime, const FPointerEvent&, MouseEvent);
DECLARE_DYNAMIC_DELEGATE_TwoParams(FMotionInputEvent, float, DeltaTime, const FMotionEvent&, MotionEvent);


// Store a delegate and track delta times since the delegate was last accessed
template<typename T>
struct TTimestampedDelegateStore
{
private:
	double LastEvaluationTime = -1.0;

	T Delegate;
public:
	TTimestampedDelegateStore() = default;
	TTimestampedDelegateStore(T& InDelegate) : Delegate(InDelegate) {};
	
	const T& GetDelegate(double EvaluationTime, float& DeltaTime)
	{
		// Default the value
		DeltaTime = 0.0f;

		// Only set Delta time if we've been evaluated at least once
		if (LastEvaluationTime > 0.0)
		{
			DeltaTime = EvaluationTime - LastEvaluationTime;
		}

		LastEvaluationTime = EvaluationTime;

		return Delegate;
	}
};

// Maps a set of FKeys to Timestamped Delegate Stores with some additional convenience functions
template<typename T>
struct TInputDelegateStore
{
	using DelegateArrayType = TArray<TTimestampedDelegateStore<T>>;
	
	TMap<FKey, DelegateArrayType> DelegateMap;

	void AddDelegate(const FKey InKey, T& InDelegate)
	{
		DelegateArrayType& DelegateArray = DelegateMap.FindOrAdd(InKey);
		DelegateArray.Emplace(InDelegate);
	}

	void RemoveDelegate(const FKey InKey, T& InDelegate)
	{
		if (DelegateArrayType* DelegateArray = DelegateMap.Find(InKey))
		{
			DelegateArray->Remove(InDelegate);
		}
	}

	// Find the delegates for the specific key
	DelegateArrayType* FindDelegateArray(const FKey InKey)
	{
		return DelegateMap.Find(InKey);
	}
};

// Links a Modifier with a Name for use in a Modifier Stack
USTRUCT()
struct FModifierStackEntry
{
	GENERATED_BODY()

	// Identifier for this modifier in the stack
    UPROPERTY(EditAnywhere, Category="Modifier")
    FName Name;

	// Controls whether the modifier actually gets applied
	UPROPERTY(EditAnywhere, Category="Modifier")
	bool bEnabled = true;

	// The current generated modifier instance
	UPROPERTY(EditAnywhere, Instanced, Category="Modifier")
    UVCamModifier* GeneratedModifier = nullptr;
	
	FModifierStackEntry() = default;

	// If ModifierClass is provided then you must also supply a valid outer for the generated modifier
	FModifierStackEntry(const FName& InName, const TSubclassOf<UVCamModifier> InModifierClass, UObject* InOuter)
		: Name(InName)
	{
		if (InModifierClass)
		{
			GeneratedModifier = NewObject<UVCamModifier>(InOuter, InModifierClass.Get());
		}
	};

	bool operator==(const FModifierStackEntry& Other) const
	{
		return this->Name.IsEqual(Other.Name) && this->bEnabled == Other.bEnabled && this->GeneratedModifier == Other.GeneratedModifier;
	};
};