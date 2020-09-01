// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "OptimusAction.h"

#include "OptimusDataType.h"

#include "OptimusResourceActions.generated.h"


class UOptimusDeformer;
class UOptimusResourceDescription;


USTRUCT()
struct FOptimusResourceAction_AddResource : 
	public FOptimusAction
{
	GENERATED_BODY()

public:
	FOptimusResourceAction_AddResource() = default;

	FOptimusResourceAction_AddResource(
		UOptimusDeformer* InDeformer,
		FOptimusDataTypeRef InDataType,
		FName InName
	);

	UOptimusResourceDescription* GetResource(IOptimusNodeGraphCollectionOwner* InRoot);

protected:
	bool Do(IOptimusNodeGraphCollectionOwner* InRoot) override;
	bool Undo(IOptimusNodeGraphCollectionOwner* InRoot) override;

private:
	// The name of the resource to create.
	FName ResourceName;

	// The data type of the resource
	FOptimusDataTypeRef DataType;
};


USTRUCT()
struct FOptimusResourceAction_RemoveResource :
	public FOptimusAction
{
	GENERATED_BODY()

public:
	FOptimusResourceAction_RemoveResource() = default;

	FOptimusResourceAction_RemoveResource(
		UOptimusResourceDescription* InResource
	);

protected:
	bool Do(IOptimusNodeGraphCollectionOwner* InRoot) override;
	bool Undo(IOptimusNodeGraphCollectionOwner* InRoot) override;

private:
	// The name of the resource to re-create
	FName ResourceName;

	// The data type of the resource
	FOptimusDataTypeRef DataType;

	// The stored resource data.
	TArray<uint8> ResourceData;
};


USTRUCT()
struct FOptimusResourceAction_RenameResource : 
	public FOptimusAction
{
	GENERATED_BODY()

public:
	FOptimusResourceAction_RenameResource() = default;

	FOptimusResourceAction_RenameResource(
	    UOptimusResourceDescription* InResource,
		FName InNewName);

protected:
	bool Do(IOptimusNodeGraphCollectionOwner* InRoot) override;
	bool Undo(IOptimusNodeGraphCollectionOwner* InRoot) override;

private:
	// The new name to give the resource.
	FName NewName;

	// The old name of the resource.
	FName OldName;
};
