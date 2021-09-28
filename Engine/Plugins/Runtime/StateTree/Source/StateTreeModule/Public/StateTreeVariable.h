// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/Guid.h"
#include "UObject/ObjectMacros.h"
#include "Templates/SubclassOf.h"
#include "StateTreeTypes.h"
#include "StateTreeVariableDesc.h"
#include "StateTreeVariable.generated.h"


UENUM()
enum class EStateTreeVariableBindingMode : uint8
{
	Any,		// The variable can be bound to any type variable, type and base class are copied from the bound variable.
	Typed,		// The variable can be bound to only the type of variable described in type and base class.
	Definition,	// The variable is used as defining a variable in state tree.
};

/**
 * Describes a reference to a variable or constant in the StateTree.
 * See StateTreeVariableDetails for the UI.
 */
USTRUCT()
struct STATETREEMODULE_API FStateTreeVariable
{
	GENERATED_BODY()

public:
	FStateTreeVariable();
	FStateTreeVariable(const EStateTreeVariableBindingMode InMode, const EStateTreeVariableType InType, TSubclassOf<UObject> InBaseClass = nullptr);
	FStateTreeVariable(const EStateTreeVariableBindingMode InMode, const FName& InName, const EStateTreeVariableType InType, TSubclassOf<UObject> InBaseClass = nullptr);

	// Initialize binding to match variable descriptor.
	void InitializeFromDesc(EStateTreeVariableBindingMode InMode, const FStateTreeVariableDesc& Desc);

#if WITH_EDITOR
	bool IsBound() const { return ID.IsValid(); }

	FStateTreeVariableDesc AsVariableDesc() const;

	float GetFloatValue() const { return *reinterpret_cast<const float*>(Value); }
	int32 GetIntValue() const { return *reinterpret_cast<const int32*>(Value); }
	bool GetBoolValue() const { return *reinterpret_cast<const bool*>(Value); }
	FVector GetVectorValue() const { return *reinterpret_cast<const FVector*>(Value); }
	FWeakObjectPtr GetObjectValue() const { return nullptr; } // For completeness, we don't have a way to set an object value on asset.

	FText GetDescription() const;

	bool ResolveHandle(const struct FStateTreeVariableLayout& Variables, struct FStateTreeConstantStorage& Constants);

	bool HasSameType(const FStateTreeVariable& Other) const;
	bool CopyValueFrom(FStateTreeVariable& Other);
#endif

	UPROPERTY()
	FStateTreeHandle Handle;

	UPROPERTY(EditDefaultsOnly, Category = Value)
	EStateTreeVariableType Type;

#if WITH_EDITORONLY_DATA
	UPROPERTY(EditDefaultsOnly, Category = Value, Transient)	// Do not serialize binding mode
	EStateTreeVariableBindingMode BindingMode;

	UPROPERTY(EditDefaultsOnly, Category = Value)
	TSubclassOf<UObject> BaseClass;

	UPROPERTY(EditDefaultsOnly, Category = Value)
	FGuid ID;

	UPROPERTY(EditDefaultsOnly, Category = Value)
	FName Name;

	static const int ValueStorageSizeBytes = 32;	// LWC_TODO: Perf pessimization. Was 16 - sizeof(FVector3f)

	/* Common buffer to store all the variable types.
	 * Note: Variable types must be POD type and fit into the 16 bytes.
	 * Note: Adding new types require to update FStateTreeVariableDetails too.
	 */
	UPROPERTY(EditDefaultsOnly, Category = Value)
	uint8 Value[ValueStorageSizeBytes];
#endif
};
