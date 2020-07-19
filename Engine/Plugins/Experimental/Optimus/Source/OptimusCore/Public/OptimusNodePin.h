// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"

#include "OptimusNodePin.generated.h"


class UOptimusNode;


UENUM()
enum class EOptimusNodePinDirection : uint8
{
	Unknown,
	Input, 
	Output
};


UCLASS(BlueprintType)
class OPTIMUSCORE_API UOptimusNodePin : public UObject
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
	UOptimusNode* GetNode();
	const UOptimusNode* GetNode() const;

	/// Returns the array of pin names from the root pin to this pin. Can be used to to
	/// easily traverse the pin hierarchy.
	TArray<FName> GetPinNamePath() const;

	/// Returns a unique name for this pin within the namespace of the owning node.
	/// E.g: Direction.X
	FName GetUniqueName() const;

	/// Returns the path of the pin from the graph collection owner root.
	/// E.g: SetupGraph/LinearBlendSkinning1.Direction.X
	FString GetPinPath() const;

	/// Returns a pin path from a string. Returns an empty array if string is invalid or empty.
	static TArray<FName> GetPinNamePathFromString(const FString& PinPathString);

	/// Returns the C++ data type string for this 
	FString GetTypeString() const { return TypeString; }

	UObject* GetTypeObject() const;

	const TArray<UOptimusNodePin*> &GetSubPins() const { return SubPins; }

protected:
	friend class UOptimusNode;

	// Initialize the pin data from the given direction and property.
	void InitializeFromProperty(
		EOptimusNodePinDirection InDirection, 
		const FProperty* InProperty
		);

	void AddSubPin(
		UOptimusNodePin* InSubPin);

private:
	UPROPERTY()
	EOptimusNodePinDirection Direction = EOptimusNodePinDirection::Unknown;

	UPROPERTY()
	FString TypeString;

	UPROPERTY(transient)
	mutable UObject* TypeObject = nullptr;

	/// @brief The path to the data type definition so that we can resolve it lazily later.
	UPROPERTY()
	FString TypeObjectPath;

	UPROPERTY()
	TArray<UOptimusNodePin*> SubPins;

	/// @brief The invalid pin. Used as a sentinel.
	static UOptimusNodePin* InvalidPin;
};
