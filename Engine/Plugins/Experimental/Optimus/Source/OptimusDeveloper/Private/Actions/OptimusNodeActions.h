// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "OptimusAction.h"
#include "OptimusNodePin.h"

#include "OptimusNodeActions.generated.h"

class UOptimusNode;
class UOptimusNodePin;

USTRUCT()
struct FOptimusNodeAction_RenameNode : 
	public FOptimusAction
{
	GENERATED_BODY()

public:
	FOptimusNodeAction_RenameNode() = default;

	FOptimusNodeAction_RenameNode(
		UOptimusNode* InNode,
		FString InNewName
	);

protected:
	bool Do(IOptimusPathResolver* InRoot) override;
	bool Undo(IOptimusPathResolver* InRoot) override;

private:
	// The path of the node to be renamed.
	FString NodePath;

	// The node's new name
	FText NewName;

	// The node's old name
	FText OldName;
};


USTRUCT()
struct FOptimusNodeAction_MoveNode :
	public FOptimusAction
{
	GENERATED_BODY()

public:
	FOptimusNodeAction_MoveNode() = default;

	FOptimusNodeAction_MoveNode(
		UOptimusNode* InNode,
		const FVector2D &InPosition
	);

protected:
	bool Do(IOptimusPathResolver* InRoot) override;
	bool Undo(IOptimusPathResolver* InRoot) override;

private:
	// The path of the node to be moved.
	FString NodePath;

	// The node's new position
	FVector2D NewPosition;

	// The node's old position
	FVector2D OldPosition;
};


USTRUCT()
struct FOptimusNodeAction_SetPinValue :
	public FOptimusAction
{
	GENERATED_BODY()

public:
	FOptimusNodeAction_SetPinValue() = default;

	FOptimusNodeAction_SetPinValue(
		UOptimusNodePin *InPin,
		const FString &InNewValue
		);

protected:
	bool Do(IOptimusPathResolver* InRoot) override;
	bool Undo(IOptimusPathResolver* InRoot) override;
	
private:
	// The path of the pin to set the value on
	FString PinPath;

	// The new value to set
	FString NewValue;

	// The old value
	FString OldValue;
};


USTRUCT()
struct FOptimusNodeAction_SetPinName :
	public FOptimusAction
{
	GENERATED_BODY()

public:
	FOptimusNodeAction_SetPinName() = default;

	FOptimusNodeAction_SetPinName(
		UOptimusNodePin *InPin,
		FName InPinName
		);

protected:
	bool Do(IOptimusPathResolver* InRoot) override;
	bool Undo(IOptimusPathResolver* InRoot) override;
	
private:
	bool SetPinName(
		IOptimusPathResolver* InRoot,
		FName InName) const;
	
	// The path of the pin to set the value on
	FString PinPath;

	// The new name to set
	FName NewPinName;

	// The old name
	FName OldPinName;
};


USTRUCT()
struct FOptimusNodeAction_SetPinType :
	public FOptimusAction
{
	GENERATED_BODY()

public:
	FOptimusNodeAction_SetPinType() = default;

	FOptimusNodeAction_SetPinType(
		UOptimusNodePin *InPin,
		FOptimusDataTypeRef InDataType
		);

protected:
	bool Do(IOptimusPathResolver* InRoot) override;
	bool Undo(IOptimusPathResolver* InRoot) override;
	
private:
	bool SetPinType(
		IOptimusPathResolver* InRoot,
		FName InDataType) const;
	
	// The path of the pin to set the value on
	FString PinPath;

	// The new type to set
	FName NewDataTypeName;

	// The old data type
	FName OldDataTypeName;
};


USTRUCT()
struct FOptimusNodeAction_SetPinDataDomain :
	public FOptimusAction
{
	GENERATED_BODY()

public:
	FOptimusNodeAction_SetPinDataDomain() = default;

	FOptimusNodeAction_SetPinDataDomain(
		UOptimusNodePin *InPin,
		const TArray<FName>& InContextNames
		);

protected:
	bool Do(IOptimusPathResolver* InRoot) override;
	bool Undo(IOptimusPathResolver* InRoot) override;
	
private:
	bool SetPinDataDomain(
		IOptimusPathResolver* InRoot,
		const TArray<FName>& InContextNames
		) const;
	
	// The path of the pin to set the value on
	FString PinPath;

	// The resource contexts to set
	TArray<FName> NewContextNames;

	// The old resource contexts
	TArray<FName> OldContextNames;
};



USTRUCT()
struct FOptimusNodeAction_AddRemovePin :
	public FOptimusAction
{
	GENERATED_BODY()

	FOptimusNodeAction_AddRemovePin() = default;

	FOptimusNodeAction_AddRemovePin(
		UOptimusNode* InNode,
		FName InName,
		EOptimusNodePinDirection InDirection,
		FOptimusNodePinStorageConfig InStorageConfig,
		FOptimusDataTypeRef InDataType,
		UOptimusNodePin* InBeforePin = nullptr
	);

	FOptimusNodeAction_AddRemovePin(
		UOptimusNodePin *InPin
		);

protected:
	bool AddPin(IOptimusPathResolver* InRoot);
	bool RemovePin(IOptimusPathResolver* InRoot) const;
	
	// The path of the node to have the pin added/removed from.
	FString NodePath;

	// Name of the new pin. After Do is called, this will be changed to the actual pin name
	// that got constructed.
	FName PinName = NAME_None;

	// The pin direction (input or output)
	EOptimusNodePinDirection Direction = EOptimusNodePinDirection::Unknown;

	// The storage configuration (value vs resource, etc.)
	FOptimusNodePinStorageConfig StorageConfig;

	// The data type of the pin to create
	FName DataType = NAME_None;

	// (Optional) The pin that will be be located right after this new pin.  
	FString BeforePinPath;

	// The path of the newly created pin
	FString PinPath;

	// Expanded state of the pin being removed. 
	bool bExpanded = false;
};

template<>
struct TStructOpsTypeTraits<FOptimusNodeAction_AddRemovePin> :
	TStructOpsTypeTraitsBase2<FOptimusNodeAction_AddRemovePin>
{
	enum
	{
		WithPureVirtual = true,
	};
};


USTRUCT()
struct FOptimusNodeAction_AddPin :
	public FOptimusNodeAction_AddRemovePin
{
	GENERATED_BODY()

	FOptimusNodeAction_AddPin() = default;

	FOptimusNodeAction_AddPin(
		UOptimusNode* InNode,
		FName InName,
		EOptimusNodePinDirection InDirection,
		FOptimusNodePinStorageConfig InStorageConfig,
		FOptimusDataTypeRef InDataType,
		UOptimusNodePin* InBeforePin = nullptr
	) : FOptimusNodeAction_AddRemovePin(InNode, InName, InDirection, InStorageConfig, InDataType, InBeforePin)
	{
	}

	// Called to retrieve the pin that was created by Do after it has been called.
	UOptimusNodePin* GetPin(IOptimusPathResolver* InRoot) const;

protected:
	bool Do(IOptimusPathResolver* InRoot) override { return AddPin(InRoot); }
	bool Undo(IOptimusPathResolver* InRoot) override { return RemovePin(InRoot); }
};


USTRUCT()
struct FOptimusNodeAction_RemovePin :
	public FOptimusNodeAction_AddRemovePin
{
	GENERATED_BODY()
	
	FOptimusNodeAction_RemovePin() = default;

	FOptimusNodeAction_RemovePin(
		UOptimusNodePin *InPinToRemove
		) : FOptimusNodeAction_AddRemovePin(InPinToRemove)
	{
	}

protected:
	bool Do(IOptimusPathResolver* InRoot) override { return RemovePin(InRoot); }
	bool Undo(IOptimusPathResolver* InRoot) override { return AddPin(InRoot); }
};
