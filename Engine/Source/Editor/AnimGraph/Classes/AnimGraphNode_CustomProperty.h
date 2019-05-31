// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Misc/Guid.h"
#include "AnimGraphNode_Base.h"
#include "Animation/AnimNode_CustomProperty.h"

#include "AnimGraphNode_CustomProperty.generated.h"

class FCompilerResultsLog;
class IDetailLayoutBuilder;
class IPropertyHandle;

UCLASS(Abstract)
class ANIMGRAPH_API UAnimGraphNode_CustomProperty : public UAnimGraphNode_Base
{
	GENERATED_BODY()

public:

	//~ Begin UEdGraphNode Interface.
	virtual void ValidateAnimNodeDuringCompilation(USkeleton* ForSkeleton, FCompilerResultsLog& MessageLog) override;
	virtual void ReallocatePinsDuringReconstruction(TArray<UEdGraphPin*>& OldPins) override;
	virtual UObject* GetJumpTargetForDoubleClick() const override;
	virtual bool HasExternalDependencies(TArray<class UStruct*>* OptionalOutput /*= NULL*/) const override;
	//~ End UEdGraphNode Interface.

	// Gets the property on InOwnerInstanceClass that corresponds to InInputPin
	void GetInstancePinProperty(const UClass* InOwnerInstanceClass, UEdGraphPin* InInputPin, UProperty*& OutProperty);
	// Gets the unique name for the property linked to a given pin
	FString GetPinTargetVariableName(const UEdGraphPin* InPin) const;
	// Gets Target Class this properties to link
	UClass* GetTargetClass() const;
	// Add Source and Target Properties - Check FAnimNode_CustomProperty
	void AddSourceTargetProperties(const FName& InSourcePropertyName, const FName& InTargetPropertyName);

	// return true if this pin name is property exposed
	// return false if this pin doesn't belong to property
	virtual bool IsValidPropertyPin(const FName& PinName) const
	{
		return (PinName != FName(TEXT("Pose"), FNAME_Find));
	}

	// ----- UI CALLBACKS ----- //
	// If given property exposed on this node
	virtual ECheckBoxState IsPropertyExposed(FName PropertyName) const;
	// User chose to expose, or unexpose a property
	virtual void OnPropertyExposeCheckboxChanged(ECheckBoxState NewState, FName PropertyName);
	// User changed the instance class
	void OnInstanceClassChanged(IDetailLayoutBuilder* DetailBuilder);
protected:

	/** List of property names we know to exist on the target class, so we can detect when
	 *  Properties are added or removed on reconstruction
	 */
	UPROPERTY()
	TArray<FName> KnownExposableProperties;

	/** Names of properties the user has chosen to expose */
	UPROPERTY()
	TArray<FName> ExposedPropertyNames;

	// Searches the instance class for properties that we can expose (public and BP visible)
	virtual void GetExposableProperties(TArray<UProperty*>& OutExposableProperties) const;
	// Gets a property's type as FText (for UI)
	FText GetPropertyTypeText(UProperty* Property);
	// Given a new class, rebuild the known property list (for tracking class changes and moving pins)
	virtual void RebuildExposedProperties();

	// internal node accessor
	virtual FAnimNode_CustomProperty* GetInternalNode() PURE_VIRTUAL(UAnimGraphNode_CustomProperty::GetInternalNode, return nullptr;);
	virtual const FAnimNode_CustomProperty* GetInternalNode() const PURE_VIRTUAL(UAnimGraphNode_CustomProperty::GetInternalNode, return nullptr;);

};
