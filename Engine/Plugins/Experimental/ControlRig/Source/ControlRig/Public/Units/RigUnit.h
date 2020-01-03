// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ControlRigDefines.h"
#include "RigVMCore/RigVMRegistry.h"
#include "RigUnit.generated.h"

struct FRigUnitContext;

/** 
 * Current state of execution type
 */
UENUM()
enum class EUnitExecutionType : uint8
{
	Always,
	InEditingTime, // in control rig editor
	Disable, // disable completely - good for debugging
	Initialize, // only during init
	Max UMETA(Hidden),
};

/** Base class for all rig units */
USTRUCT(BlueprintType, meta=(Abstract, NodeColor = "0.1 0.1 0.1"))
struct CONTROLRIG_API FRigUnit
{
	GENERATED_BODY()

	FRigUnit()
		:ExecutionType(EUnitExecutionType::Always)
	{}

	/** Virtual destructor */
	virtual ~FRigUnit() {}

	/** Returns the label of this unit */
	virtual FString GetUnitLabel() const { return FString(); }

	/** Execute logic for this rig unit */
	virtual void Execute(const FRigUnitContext& Context) {}

	/* 
	 * This is property name given by ControlRig as transient when initialized, so only available in run-time
	 */
	UPROPERTY(BlueprintReadOnly, transient, Category=FRigUnit, meta = (Constant))
	FName RigUnitName;
	
	/* 
	 * This is struct name given by ControlRig as transient when initialized, so only available in run-time
	 */
	UPROPERTY(BlueprintReadOnly, transient, Category=FRigUnit, meta = (Constant))
	FName RigUnitStructName;

	UPROPERTY(EditAnywhere, Category = FRigUnit, meta = (Constant))
	EUnitExecutionType ExecutionType;
};

/** Base class for all rig units that can change data */
USTRUCT(BlueprintType, meta = (Abstract))
struct CONTROLRIG_API FRigUnitMutable : public FRigUnit
{
	GENERATED_BODY()

	FRigUnitMutable()
	: FRigUnit()
	{}

	/*
	 * This property is used to chain multiple mutable units together
	 */
	UPROPERTY(DisplayName = "Execute", Transient, meta = (Input, Output))
	FControlRigExecuteContext ExecuteContext;
};

// this will have to change in the future and move to editor, I assume the errors will be saved in the rig unit and it will print fromthe editor module
namespace UnitLogHelpers
{
	CONTROLRIG_API void PrintMissingHierarchy(const FName& InputName);
	CONTROLRIG_API void PrintUnimplemented(const FName& InputName);
}
 