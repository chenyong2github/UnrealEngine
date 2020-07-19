// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"

#include "Containers/Array.h"
#include "Templates/UniquePtr.h"
#include "Templates/UnrealTypeTraits.h"

#include "OptimusAction.generated.h"

class IOptimusNodeGraphCollectionOwner;

// Base action class. This is a UStruct so that we can use UE's RAII to check for type
// similarity.
USTRUCT()
struct FOptimusAction
{
	GENERATED_BODY()

	FOptimusAction(const FString& InTitle = {});
	virtual ~FOptimusAction();

	const FString& GetTitle() const 
	{
		return Title;
	}

	template <typename FmtType, typename... ArgTypes>
	void SetTitlef(const FmtType& Fmt, ArgTypes&& ...Args)
	{
		Title = FString::Printf(Fmt, Forward<ArgTypes>(Args)...);
	}

protected:
	friend class UOptimusActionStack;
	friend struct FOptimusCompoundAction;

	/// Performs the action as set by the action's constructor.
	virtual bool Do(IOptimusNodeGraphCollectionOwner* InRoot) PURE_VIRTUAL(FOptimusAction::Do, return false; );

	/// Reverts the action performed by the action's Do function.
	virtual bool Undo(IOptimusNodeGraphCollectionOwner* InRoot) PURE_VIRTUAL(FOptimusAction::Undo, return false; );

private:
	/// The title of the action. Should be set by the constructor of the derived objects.
	FString Title;
};

USTRUCT()
struct FOptimusCompoundAction :
	public FOptimusAction
{
	GENERATED_BODY()

	FOptimusCompoundAction() = default;

	// Copy nothing.
	FOptimusCompoundAction(const FOptimusCompoundAction &) {}
	FOptimusCompoundAction &operator=(const FOptimusCompoundAction &) { return *this; }

	FOptimusCompoundAction(FOptimusCompoundAction&&) = default;
	FOptimusCompoundAction& operator=(FOptimusCompoundAction&&) = default;

	template <typename FmtType, typename... ArgTypes>
	FOptimusCompoundAction(FmtType&& Fmt, ArgTypes&& ...Args) :
		FOptimusAction(FString::Printf(Forward<FmtType>(Fmt), Forward<ArgTypes>(Args)...))
	{ }

	template<typename T, typename... ArgTypes>
	typename TEnableIf<TPointerIsConvertibleFromTo<T, FOptimusAction>::Value, void>::Type 
	AddSubAction(ArgTypes&& ...Args)
	{
		SubActions.Add(TUniquePtr<FOptimusAction>(new T(Forward<ArgTypes>(Args)...)));
	}

protected:
	bool Do(IOptimusNodeGraphCollectionOwner* InRoot) override;
	bool Undo(IOptimusNodeGraphCollectionOwner* InRoot) override;

private:
	TArray<TUniquePtr<FOptimusAction>> SubActions;
};
