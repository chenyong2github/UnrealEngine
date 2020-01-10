// Copyright Epic Games, Inc. All Rights Reserved.


#pragma once

#include "CoreMinimal.h"
#include "Textures/SlateIcon.h"
#include "K2Node.h"
#include "K2Node_EditorPropertyAccess.generated.h"

class UEdGraph;
class UK2Node_CallFunction;
class FBlueprintActionDatabaseRegistrar;

UCLASS(Abstract)
class UK2Node_EditorPropertyAccessBase : public UK2Node
{
	GENERATED_BODY()

public:
	//~ Begin UEdGraphNode Interface.
	virtual void AllocateDefaultPins() override;
	virtual FSlateIcon GetIconAndTint(FLinearColor& OutColor) const override;
	virtual void PostReconstructNode() override;
	//~ End UEdGraphNode Interface.

	//~ Begin UK2Node Interface
	virtual void GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const override;
	virtual FText GetMenuCategory() const override;
	virtual bool CanPasteHere(const UEdGraph* TargetGraph) const override;
	virtual bool IsActionFilteredOut(const class FBlueprintActionFilter& Filter) override;
	virtual bool IsConnectionDisallowed(const UEdGraphPin* MyPin, const UEdGraphPin* OtherPin, FString& OutReason) const override;
	virtual void EarlyValidation(class FCompilerResultsLog& MessageLog) const override;
	virtual void NotifyPinConnectionListChanged(UEdGraphPin* Pin) override;
	virtual void ExpandNode(class FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph) override;
	//~ End UK2Node Interface

	/** Get the then output pin */
	UEdGraphPin* GetThenPin() const;
	/** Get the object input pin */
	UEdGraphPin* GetObjectPin() const;
	/** Get the property name input pin */
	UEdGraphPin* GetPropertyNamePin() const;
	/** Get the property value output pin */
	virtual UEdGraphPin* GetPropertyValuePin() const PURE_VIRTUAL(UK2Node_EditorPropertyAccessBase::GetPropertyValuePin, return nullptr;);
	/** Get the result output pin */
	UEdGraphPin* GetResultPin() const;

protected:
	/** Allocate the property value pin for this node */
	virtual void AllocatePropertyValuePin() PURE_VIRTUAL(UK2Node_EditorPropertyAccessBase::AllocatePropertyValuePin, );

	/** Get the function name from UKismetSystemLibrary that the CallFunction node should use for the underlying access function */
	virtual FName GetUnderlyingFunctionName() const PURE_VIRTUAL(UK2Node_EditorPropertyAccessBase::GetUnderlyingFunctionName, return FName(););

	/** Updates the type of the property value pin based on its connection type */
	void RefreshPropertyValuePin();

	/**
	 * Takes the specified "MutatablePin" and sets its 'PinToolTip' field (according to the specified description)
	 * 
	 * @param   MutatablePin	The pin you want to set tool-tip text on
	 * @param   PinDescription	A string describing the pin's purpose
	 */
	void SetPinToolTip(UEdGraphPin& MutatablePin, const FText& PinDescription) const;
};

UCLASS()
class UK2Node_GetEditorProperty : public UK2Node_EditorPropertyAccessBase
{
	GENERATED_BODY()

public:
	//~ Begin UEdGraphNode Interface.
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual FText GetTooltipText() const override;
	//~ End UEdGraphNode Interface.

	//~ Begin UK2Node_EditorPropertyAccessBase Interface.
	virtual void AllocatePropertyValuePin() override;
	virtual UEdGraphPin* GetPropertyValuePin() const override;
	virtual FName GetUnderlyingFunctionName() const override;
	//~ End UK2Node_EditorPropertyAccessBase Interface.
};

UCLASS()
class UK2Node_SetEditorProperty : public UK2Node_EditorPropertyAccessBase
{
	GENERATED_BODY()

public:
	//~ Begin UEdGraphNode Interface.
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual FText GetTooltipText() const override;
	//~ End UEdGraphNode Interface.

	//~ Begin UK2Node_EditorPropertyAccessBase Interface.
	virtual void AllocatePropertyValuePin() override;
	virtual UEdGraphPin* GetPropertyValuePin() const override;
	virtual FName GetUnderlyingFunctionName() const override;
	//~ End UK2Node_EditorPropertyAccessBase Interface.
};
