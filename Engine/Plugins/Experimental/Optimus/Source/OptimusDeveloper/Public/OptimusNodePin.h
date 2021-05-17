// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "OptimusDataType.h"

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"

#include "OptimusNodePin.generated.h"

class UOptimusActionStack;
class UOptimusNode;
enum class EOptimusGraphNotifyType;


UENUM()
enum class EOptimusNodePinDirection : uint8
{
	Unknown,
	Input, 
	Output
};

/** Specifies the storage type of the pin data */
UENUM()
enum class EOptimusNodePinStorageType : uint8
{
	Value,				/** Plain value of some type */
	Resource			/** Resource binding of some type */
};


UCLASS(BlueprintType)
class OPTIMUSDEVELOPER_API UOptimusNodePin : public UObject
{
	GENERATED_BODY()

public:
	UOptimusNodePin() = default;

	/// Returns whether this pin is an input or output connection.
	/// @return The direction of this pin.
	EOptimusNodePinDirection GetDirection() const { return Direction; }

	/// Returns the parent pin of this pin, or nullptr if it is the top-most pin.
	UOptimusNodePin* GetParentPin();
	const UOptimusNodePin* GetParentPin() const;

	/// Returns the root pin of this pin hierarchy.
	UOptimusNodePin* GetRootPin();
	const UOptimusNodePin* GetRootPin() const;

	/// Returns the owning node of this pin and all its ancestors and children.
	UOptimusNode* GetNode() const;

	/// Returns the array of pin names from the root pin to this pin. Can be used to to
	/// easily traverse the pin hierarchy.
	TArray<FName> GetPinNamePath() const;

	/// Returns a unique name for this pin within the namespace of the owning node.
	/// E.g: Direction.X
	FName GetUniqueName() const;

	/** Returns a user-friendly display name for this pin */
	FText GetDisplayName() const;

	/// Returns the path of the pin from the graph collection owner root.
	/// E.g: SetupGraph/LinearBlendSkinning1.Direction.X
	FString GetPinPath() const;

	/// Returns a pin path from a string. Returns an empty array if string is invalid or empty.
	static TArray<FName> GetPinNamePathFromString(const FString& PinPathString);

	/** Return the registered Optimus data type associated with this pin */
	FOptimusDataTypeHandle GetDataType() const { return DataType.Resolve(); }

	/** Returns the storage type for this pin, either a value or a bound resource */
	EOptimusNodePinStorageType GetStorageType() const { return StorageType; }

	/** Returns the FProperty object for this pin. This can be used to directly address the
	  * node data represented by this pin. Not all pins have an underlying resource so this can
	  * return nullptr.
	  */
	FProperty *GetPropertyFromPin() const;

	/// Returns the current value of this pin, including sub-values if necessary, as a string.
	FString GetValueAsString() const;

	/// Sets the value of this pin from a value string in an undoable fashion.
	bool SetValueFromString(const FString& InStringValue);

	/// Sets the value of this pin from a value string with no undo (although if a transaction
	/// bracket is open, it will receive the modification).
	bool SetValueFromStringDirect(const FString &InStringValue);

	/// Returns the sub-pins of this pin. For example for a pin representing the FVector type, 
	/// this will return pins for the X, Y, and Z components of it (as float values).
	const TArray<UOptimusNodePin*> &GetSubPins() const { return SubPins; }

	/// Ask this pin if it allows a connection from the other pin. 
	/// @param InOtherPin The other pin to connect to/from
	/// @param OutReason An optional string that will contain the reason why the connection
	/// cannot be made if this function returns false.
	/// @return True if the connection can be made, false otherwise.
	bool CanCannect(const UOptimusNodePin *InOtherPin, FString *OutReason = nullptr) const;

	/** Set the expansion state of this pin. This is purely driven by the UI and is not
	    an undoable operation. No notifications are sent if the state changes. */
	void SetIsExpanded(bool bInIsExpanded);

	/** Returns the stored expansion state */
	bool GetIsExpanded() const;

protected:
	friend class UOptimusNode;

	// Initialize the pin data from the given direction and property.
	void Initialize(
		EOptimusNodePinDirection InDirection, 
		EOptimusNodePinStorageType InStorageType,
		FOptimusDataTypeRef InDataTypeRef
		);

	void AddSubPin(
		UOptimusNodePin* InSubPin);

private:
	uint8 *GetPropertyValuePtr() const;

	void Notify(EOptimusGraphNotifyType InNotifyType);

	UOptimusActionStack* GetActionStack() const;

	UPROPERTY()
	EOptimusNodePinDirection Direction = EOptimusNodePinDirection::Unknown;

	UPROPERTY()
	EOptimusNodePinStorageType StorageType = EOptimusNodePinStorageType::Value;

	UPROPERTY()
	FOptimusDataTypeRef DataType;

	UPROPERTY()
	TArray<UOptimusNodePin*> SubPins;
};
