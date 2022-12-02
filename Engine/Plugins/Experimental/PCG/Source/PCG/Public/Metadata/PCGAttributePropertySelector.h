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
};

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

	// Setters, retrurn true if something changed.
	bool SetPointProperty(EPCGPointProperties InPointProperty);
	bool SetAttributeName(FName InAttributeName);

	FName GetName() const;

	// TODO: To be removed when UI widget is done.
	void Update();

#if WITH_EDITOR
	// Return true if the underlying name is valid.
	bool IsValid() const;

	// Returns the text to display in the widget.
	FText GetDisplayText() const;

	// Update the selector with an incoming string.
	bool Update(FString NewValue);
#endif // WITH_EDITOR
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
	static FName GetName(UPARAM(const, ref) const FPCGAttributePropertySelector& Selector);
};