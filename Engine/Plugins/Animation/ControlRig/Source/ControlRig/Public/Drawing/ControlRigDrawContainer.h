// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ControlRigDrawInstruction.h"
#include "ControlRigDrawContainer.generated.h"

USTRUCT()
struct CONTROLRIG_API FControlRigDrawContainer
{
	GENERATED_BODY()
	virtual ~FControlRigDrawContainer() {}

	int32 Num() const { return Instructions.Num(); }
	int32 GetIndex(const FName& InName) const;
	const FControlRigDrawInstruction& operator[](int32 InIndex) const { return Instructions[InIndex]; }
	FControlRigDrawInstruction& operator[](int32 InIndex) { return Instructions[InIndex]; }
	const FControlRigDrawInstruction& operator[](const FName& InName) const { return Instructions[GetIndex(InName)]; }
	FControlRigDrawInstruction& operator[](const FName& InName) { return Instructions[GetIndex(InName)]; }

	TArray<FControlRigDrawInstruction>::RangedForIteratorType      begin() { return Instructions.begin(); }
	TArray<FControlRigDrawInstruction>::RangedForConstIteratorType begin() const { return Instructions.begin(); }
	TArray<FControlRigDrawInstruction>::RangedForIteratorType      end() { return Instructions.end(); }
	TArray<FControlRigDrawInstruction>::RangedForConstIteratorType end() const { return Instructions.end(); }

	uint32 GetAllocatedSize(void) const { return Instructions.GetAllocatedSize(); }

	void Reset();

	UPROPERTY(EditAnywhere, Category = "DrawContainer")
	TArray<FControlRigDrawInstruction> Instructions;
};
