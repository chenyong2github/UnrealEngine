// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Units/RigUnit.h"
#include "ControlRigDefines.h"
#include "RigUnit_ForLoop.generated.h"

/**
 * Given a count, execute iteratively until the count is up
 */
USTRUCT(meta=(DisplayName="For Loop", Category="Execution", TitleColor="1 0 0", NodeColor="1 1 1", Keywords="Iterate", Icon="EditorStyle|GraphEditor.Macro.Loop_16x"))
struct CONTROLRIG_API FRigUnit_ForLoopCount : public FRigUnitMutable
{
	GENERATED_BODY()

	FRigUnit_ForLoopCount()
	{
		BlockToRun = NAME_None;
		Count = 1;
		Index = 0;
		Ratio = 0.f;
	}

	// FRigVMStruct overrides
	virtual const TArray<FName>& GetControlFlowBlocks_Impl() const override
	{
		static const TArray<FName> Blocks = {ExecuteContextName, ForLoopCompletedPinName};
		return Blocks;
	}
	virtual const bool IsControlFlowBlockSliced(const FName& InBlockName) const { return InBlockName == ExecuteContextName; }
	FORCEINLINE virtual int32 GetNumSlices() const override { return Count; }

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	UPROPERTY(meta = (Singleton))
	FName BlockToRun;

	UPROPERTY(meta = (Singleton, Input))
	int32 Count;

	UPROPERTY(meta = (Singleton, Output))
	int32 Index;

	UPROPERTY(meta = (Singleton, Output))
	float Ratio;

	UPROPERTY(EditAnywhere, Transient, Category = "ForLoop", meta = (Output))
	FControlRigExecuteContext Completed;
};
