// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RigVMCore/RigVMExecuteContext.h"
#include "Units/RigUnit.h"
#include "ParametersExecuteContext.generated.h"

class IAnimNextInterface;

namespace UE::AnimNext
{
	struct FContext;
}

USTRUCT(BlueprintType)
struct FAnimNextParametersExecuteContext : public FRigVMExecuteContext
{
	GENERATED_BODY()

	FAnimNextParametersExecuteContext()
		: FRigVMExecuteContext()
	{
	}

	void SetContextData(TConstArrayView<TArrayView<uint8>> InValues)
	{
		Values = InValues;
	}

	void SetCurrentValueIndex(int32 InIndex)
	{
		check(Values.IsValidIndex(InIndex));
		Index = InIndex;
	}

	TArrayView<uint8> GetData() const
	{
		check(Values.Num() > 0);
		check(Values.IsValidIndex(Index));
		return Values[Index]; 
	}
	
	virtual void Copy(const FRigVMExecuteContext* InOtherContext) override
	{
		Super::Copy(InOtherContext);

		const FAnimNextParametersExecuteContext* OtherContext = (const FAnimNextParametersExecuteContext*)InOtherContext; 
		Values = OtherContext->Values;
	}

private:
	// The parameter values to set
	TConstArrayView<TArrayView<uint8>> Values;

	// Current value that is being set
	int32 Index = 0;
};

USTRUCT(meta=(ExecuteContext="FAnimNextParametersExecuteContext"))
struct FRigUnit_AnimNextParametersBase : public FRigUnit
{
	GENERATED_BODY()
};
