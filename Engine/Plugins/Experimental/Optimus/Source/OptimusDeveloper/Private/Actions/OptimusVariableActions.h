// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "OptimusAction.h"

#include "OptimusDataType.h"

#include "OptimusVariableActions.generated.h"


class UOptimusDeformer;
class UOptimusVariableDescription;


USTRUCT()
struct FOptimusVariableAction_AddVariable : 
	public FOptimusAction
{
	GENERATED_BODY()

public:
	FOptimusVariableAction_AddVariable() = default;

	FOptimusVariableAction_AddVariable(
	    UOptimusDeformer* InDeformer,
	    FOptimusDataTypeRef InDataType,
	    FName InName
		);

	UOptimusVariableDescription* GetVariable(IOptimusPathResolver* InRoot) const;

protected:
	bool Do(IOptimusPathResolver* InRoot) override;
	bool Undo(IOptimusPathResolver* InRoot) override;

private:
	// The name of the variable to create.
	FName VariableName;

	// The data type of the variable
	FOptimusDataTypeRef DataType;
};


USTRUCT()
struct FOptimusVariableAction_RemoveVariable : 
	public FOptimusAction
{
	GENERATED_BODY()

public:
	FOptimusVariableAction_RemoveVariable() = default;

	FOptimusVariableAction_RemoveVariable(
	    UOptimusVariableDescription* InVariable);

protected:
	bool Do(IOptimusPathResolver* InRoot) override;
	bool Undo(IOptimusPathResolver* InRoot) override;

private:
	// The name of the variable to remove and re-create on undo.
	FName VariableName;

	// The data type of the variable
	FOptimusDataTypeRef DataType;

	// The stored variable data.
	TArray<uint8> VariableData;
};


USTRUCT()
struct FOptimusVariableAction_RenameVariable : 
	public FOptimusAction
{
	GENERATED_BODY()

public:
	FOptimusVariableAction_RenameVariable() = default;

	FOptimusVariableAction_RenameVariable(
	    UOptimusVariableDescription* InVariable,
	    FName InNewName);

protected:
	bool Do(IOptimusPathResolver* InRoot) override;
	bool Undo(IOptimusPathResolver* InRoot) override;

private:
	// The new name to give the variable.
	FName NewName;

	// The old name of the variable.
	FName OldName;
};
