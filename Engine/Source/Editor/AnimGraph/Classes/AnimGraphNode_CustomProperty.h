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

	// UAnimGraphNode_Base interface
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;
	// Gets the property on InOwnerInstanceClass that corresponds to InInputPin
	void GetInstancePinProperty(const UClass* InOwnerInstanceClass, UEdGraphPin* InInputPin, UProperty*& OutProperty);
	// Gets the unique name for the property linked to a given pin
	FString GetPinTargetVariableName(const UEdGraphPin* InPin) const;
	// Gets Target Class this properties to link
	UClass* GetTargetClass() const;
	// Add Source and Target Properties - Check FAnimNode_CustomProperty
	void AddSourceTargetProperties(const FName& InSourcePropertyName, const FName& InTargetPropertyName);
	// Helper used to get the skeleton class we are targeting
	virtual UClass* GetTargetSkeletonClass() const;

	// ----- UI CALLBACKS ----- //
	// User changed the instance class etc.
	void OnStructuralPropertyChanged(IDetailLayoutBuilder* DetailBuilder);
	// If given property exposed on this node
	virtual ECheckBoxState IsPropertyExposed(FName PropertyName) const;
	// User chose to expose, or unexpose a property
	virtual void OnPropertyExposeCheckboxChanged(ECheckBoxState NewState, FName PropertyName);
	// If all possible properties are exposed on this node
	virtual ECheckBoxState AreAllPropertiesExposed() const;
	// User chose to expose, or unexpose all properties
	virtual void OnPropertyExposeAllCheckboxChanged(ECheckBoxState NewState);
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
	virtual FAnimNode_CustomProperty* GetCustomPropertyNode() PURE_VIRTUAL(UAnimGraphNode_CustomProperty::GetCustomPropertyNode, return nullptr;);
	virtual const FAnimNode_CustomProperty* GetCustomPropertyNode() const PURE_VIRTUAL(UAnimGraphNode_CustomProperty::GetCustomPropertyNode, return nullptr;);

	// Check whether the specified property is structural (i.e. should we rebuild the UI if it changes)
	virtual bool IsStructuralProperty(UProperty* InProperty) const { return false; }

	// Whether this node needs a valid target class up-front
	virtual bool NeedsToSpecifyValidTargetClass() const { return true; }

};
