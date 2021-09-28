// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#if WITH_EDITOR
#include "Misc/Guid.h"
#endif
#include "StateTreeTypes.h"
#include "StateTreeVariableDesc.h"
#include "StateTreeVariable.h"
#include "StateTreeVariableLayout.generated.h"

/**
 * Defines layout of variable descriptors.
 * Can be used to define a variable layout in the UI too, see StateTreeVariableLayoutDetails.
 */
USTRUCT()
struct STATETREEMODULE_API FStateTreeVariableLayout
{
	GENERATED_BODY()

public:

	uint16 GetMemoryUsage() const
	{
		if (Variables.Num() > 0)
		{
			return Variables.Last().Offset + FStateTreeVariableHelpers::GetVariableMemoryUsage(Variables.Last().Type);
		}
		return 0;
	}

	uint16 CalculateOffsetsAndMemoryUsage()
	{
		uint16 MemoryUsage = 0;

		for (FStateTreeVariableDesc& Var : Variables)
		{
			Var.Offset = MemoryUsage;
			MemoryUsage += FStateTreeVariableHelpers::GetVariableMemoryUsage(Var.Type);
		}

		return MemoryUsage;
	}

	void Reset()
	{
		Variables.Reset();
	}

#if WITH_EDITOR
	FStateTreeHandle GetVariableHandle(const FGuid& VariableID) const
	{
		const FStateTreeVariableDesc* Var = Variables.FindByPredicate([&VariableID](const FStateTreeVariableDesc& Var) -> bool { return Var.ID == VariableID; });
		return Var != nullptr ? FStateTreeHandle(Var->Offset) : FStateTreeHandle::Invalid;
	}

	FStateTreeVariableDesc& DefineVariable(const FStateTreeVariableDesc& Desc)
	{
		const FGuid ID = Desc.ID;
		int32 Index = Variables.IndexOfByPredicate([ID](const FStateTreeVariableDesc& Desc) -> bool { return Desc.ID == ID; });
		if (Index == INDEX_NONE)
		{
			Index = Variables.Add(Desc);
		}
		return Variables[Index];
	}

	FStateTreeVariableDesc& DefineVariable(const FName& ScopeName, const FStateTreeVariableDesc& Desc)
	{
		FStateTreeVariableDesc& NewVar = DefineVariable(Desc);
		NewVar.Name = FName(ScopeName.ToString() + TEXT(".") + Desc.Name.ToString());
		return NewVar;
	}

	FStateTreeVariableDesc& DefineVariable(const FStateTreeVariable& Var)
	{
		return DefineVariable(Var.AsVariableDesc());
	}

	FStateTreeVariableDesc& DefineVariable(const FName& ScopeName, const FStateTreeVariable& Var)
	{
		return DefineVariable(ScopeName, Var.AsVariableDesc());
	}

#endif

	UPROPERTY(EditDefaultsOnly, Category = Layout)
	TArray<FStateTreeVariableDesc> Variables;
};
