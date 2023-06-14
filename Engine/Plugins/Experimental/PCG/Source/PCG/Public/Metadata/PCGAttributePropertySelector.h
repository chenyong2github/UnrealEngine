// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PCGPoint.h"

#include "Kismet/BlueprintFunctionLibrary.h"

#include "PCGAttributePropertySelector.generated.h"

UENUM()
enum class EPCGAttributePropertySelection
{
	Attribute,
	PointProperty,
	ExtraProperty
};

UENUM()
enum class EPCGExtraProperties : uint8
{
	Index
};

/**
* Blueprint class to allow to select an attribute or a property.
* It will handle the logic and can only be modified using the blueprint library defined below.
* Also has a custom detail view in the PCGEditor plugin.
*/
USTRUCT(BlueprintType)
struct PCG_API FPCGAttributePropertySelector
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category = "Settings")
	EPCGAttributePropertySelection Selection = EPCGAttributePropertySelection::Attribute;

	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category = "Settings", meta = (EditCondition = "Selection == EPCGAttributePropertySelection::Attribute", EditConditionHides))
	FName AttributeName;

	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category = "Settings", meta = (EditCondition = "Selection == EPCGAttributePropertySelection::PointProperty", EditConditionHides))
	EPCGPointProperties PointProperty = EPCGPointProperties::Position;

	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category = "Settings", meta = (EditCondition = "Selection == EPCGAttributePropertySelection::ExtraProperty", EditConditionHides))
	EPCGExtraProperties ExtraProperty = EPCGExtraProperties::Index;

	UPROPERTY()
	TArray<FString> ExtraNames;

	// Force get the "None" attribute instead getting the last attribute
	UPROPERTY()
	bool bForceGetNone = false;

	// Setters, retrurn true if something changed.
	bool SetPointProperty(EPCGPointProperties InPointProperty, bool bResetExtraNames = true);
	bool SetAttributeName(FName InAttributeName, bool bResetExtraNames = true);
	bool SetExtraProperty(EPCGExtraProperties InExtraProperty, bool bResetExtraNames = true);

	FName GetName() const;

	// Returns the text to display in the widget.
	FText GetDisplayText() const;

	// Return true if the underlying name is valid.
	bool IsValid() const;

	// Update the selector with an incoming string.
	bool Update(FString NewValue);
};

/**
* Helper class to allow the BP to call the custom setters and getters on FPCGAttributePropertySelector.
*/
UCLASS()
class PCG_API UPCGAttributePropertySelectorBlueprintHelpers : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "PCG|PCGAttributePropertySelector", meta = (ScriptMethod))
	static bool SetPointProperty(UPARAM(ref) FPCGAttributePropertySelector& Selector, EPCGPointProperties InPointProperty);

	UFUNCTION(BlueprintCallable, Category = "PCG|PCGAttributePropertySelector", meta = (ScriptMethod))
	static bool SetAttributeName(UPARAM(ref) FPCGAttributePropertySelector& Selector, FName InAttributeName);

	UFUNCTION(BlueprintCallable, Category = "PCG|PCGAttributePropertySelector", meta = (ScriptMethod))
	static bool SetExtraProperty(UPARAM(ref) FPCGAttributePropertySelector& Selector, EPCGExtraProperties InExtraProperty);

	UFUNCTION(BlueprintCallable, Category = "PCG|PCGAttributePropertySelector", meta = (ScriptMethod))
	static FName GetName(UPARAM(const, ref) const FPCGAttributePropertySelector& Selector);
};