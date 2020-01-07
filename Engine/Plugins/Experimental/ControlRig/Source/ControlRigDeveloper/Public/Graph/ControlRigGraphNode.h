// Copyright Epic Games, Inc. All Rights Reserved.


#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "EdGraph/EdGraphNodeUtils.h"
#include "K2Node.h"
#include "UObject/SoftObjectPath.h"
#include "EdGraphSchema_K2.h"
#include "ControlRigModel.h"
#include "ControlRigGraphNode.generated.h"

class FBlueprintActionDatabaseRegistrar;
class UEdGraph;
struct FSlateIcon;
class UControlRigBlueprint;

/** Information about a control rig field */
class FControlRigField
{
public:
	FControlRigField(const FEdGraphPinType& InPinType, const FString& InPinPath, const FText& InDisplayNameText, int32 InArrayIndex = INDEX_NONE)
		: InputPin(nullptr)
		, OutputPin(nullptr)
		, PinType(InPinType)
		, PinPath(InPinPath)
		, DisplayNameText(InDisplayNameText)
		, ArrayIndex(InArrayIndex)
	{
	}

	virtual ~FControlRigField() {}

	/** Get the field we refer to */
	virtual FField* GetField() const { return nullptr; }

	/** Get the input pin for this item */
	virtual const FControlRigModelPin* GetPin() const { return nullptr; }
	
	/** Get the output pin for this item */
	virtual UEdGraphPin* GetOutputPin() const { return OutputPin; }

	/** Get the name of this field */
	virtual FString GetPinPath() const { return PinPath; }

	/** Get the name to display for this field */
	virtual FText GetDisplayNameText() const { return DisplayNameText; }

	/** Get the tooltip to display for this field */
	virtual FText GetTooltipText() const { return TooltipText; }

	/** Get the pin type to use for this field */
	virtual FEdGraphPinType GetPinType() const { return PinType; }

	/** Cached input pin */
	UEdGraphPin* InputPin;

	/** Cached output pin */
	UEdGraphPin* OutputPin;

	/** Pin type we use for the field */
	FEdGraphPinType PinType;

	/** Cached name to display */
	FString PinPath;

	/** The name to display for this field  */
	FText DisplayNameText;

	/** The name to display for this field's tooltip  */
	FText TooltipText;

	/** The array index, or INDEX_NONE if this is not an array-indexed property */
	int32 ArrayIndex;

	/** Any sub-fields are represented by children of this field */
	TArray<TSharedRef<FControlRigField>> Children;
};

/** Information about an input/output pin */
class FControlRigPin : public FControlRigField
{
private:
	static FEdGraphPinType GetPinTypeFromPin(const FControlRigModelPin* InPin)
	{
		return InPin->Type;
	}

	static FText GetDisplayNameForPin(const FControlRigModelPin* InPin)
	{
		return FText::FromString(InPin->Name.ToString());
	}

public:
	FControlRigPin(const FControlRigModelPin* InPin, const FString& InPinPath, int32 InArrayIndex = INDEX_NONE)
		: FControlRigField(GetPinTypeFromPin(InPin), *InPinPath, GetDisplayNameForPin(InPin), InArrayIndex)
		, Pin(*InPin)
	{
	}

	virtual const FControlRigModelPin* GetPin() const override { return &Pin; }

private:
	/** The field that we use as input/output */
	FControlRigModelPin Pin;
};

/** Base class for animation ControlRig-related nodes */
UCLASS()
class CONTROLRIGDEVELOPER_API UControlRigGraphNode : public UEdGraphNode
{
	GENERATED_BODY()

	friend class FControlRigGraphNodeDetailsCustomization;
	friend class FControlRigBlueprintCompilerContext;
	friend class UControlRigGraph;
	friend class UControlRigGraphSchema;
	friend class UControlRigBlueprint;
	friend class FControlRigGraphTraverser;
	friend class FControlRigGraphPanelPinFactory;
	friend class FControlRigEditor;
	friend struct FControlRigBlueprintUtils;
	friend class SControlRigGraphPinCurveFloat;

private:
	/** The property we represent. For template nodes this represents the struct/property type name. */
	UPROPERTY()
	FName PropertyName;

	UPROPERTY()
	FString StructPath;

	/** Pin Type for property */
	UPROPERTY()
	FEdGraphPinType PinType;

	/** The type of parameter */
	UPROPERTY()
	int32 ParameterType;

	/** Expanded pins */
	UPROPERTY()
	TArray<FString> ExpandedPins;

	/** Cached dimensions of this node (used for auto-layout) */
	FVector2D Dimensions;

	/** The cached node titles */
	mutable FText NodeTitleFull;
	mutable FText NodeTitle;

	/** Cached info about input/output pins */
	TArray<TSharedRef<FControlRigField>> ExecutionInfos;
	TArray<TSharedRef<FControlRigField>> InputInfos;
	TArray<TSharedRef<FControlRigField>> InputOutputInfos;
	TArray<TSharedRef<FControlRigField>> OutputInfos;

public:
	UControlRigGraphNode();

	// UEdGraphNode Interface.
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual FLinearColor GetNodeTitleColor() const override;
	virtual FLinearColor GetNodeBodyTintColor() const;
	virtual FSlateIcon GetIconAndTint(FLinearColor& OutColor) const override;
	virtual void AllocateDefaultPins() override;
	virtual void ReconstructNode() override;
	virtual void PinConnectionListChanged(UEdGraphPin* Pin) override;
	virtual void GetNodeContextMenuActions(class UToolMenu* Menu, class UGraphNodeContextMenuContext* Context) const override;
	virtual void DestroyNode() override;
	virtual void PinDefaultValueChanged(UEdGraphPin* Pin) override;
	virtual TSharedPtr<INameValidatorInterface> MakeNameValidator() const override;
	virtual FText GetTooltipText() const override;
	virtual bool CanCreateUnderSpecifiedSchema(const UEdGraphSchema* InSchema) const override;
	virtual void AutowireNewNode(UEdGraphPin* FromPin) override;	
	virtual void PrepareForCopying() override;

	virtual bool IsDeprecated() const override;
	virtual FEdGraphNodeDeprecationResponse GetDeprecationResponse(EEdGraphNodeDeprecationType DeprecationType) const override;

	/** Set the cached dimensions of this node */
	void SetDimensions(const FVector2D& InDimensions) { Dimensions = InDimensions; }

	/** Get the cached dimensions of this node */
	const FVector2D& GetDimensions() const { return Dimensions; }

	/** Set the property name we reference */
	void SetPropertyName(const FName& InPropertyName, bool bReplaceInnerProperties=false);

	/** Get the property name we reference */
	FName GetPropertyName() const { return PropertyName; }

	/** Get the execution variable names */
	const TArray<TSharedRef<FControlRigField>>& GetExecutionVariableInfo() const { return ExecutionInfos; }

	/** Get the input variable names */
	const TArray<TSharedRef<FControlRigField>>& GetInputVariableInfo() const { return InputInfos; }

	/** Get the input-output variable names */
	const TArray<TSharedRef<FControlRigField>>& GetInputOutputVariableInfo() const { return InputOutputInfos; }

	/** Get the output variable names */
	const TArray<TSharedRef<FControlRigField>>& GetOutputVariableInfo() const { return OutputInfos; }

	/** Record a pin's expansion state */
	void SetPinExpansion(const FString& InPinPropertyPath, bool bExpanded);

	/** Check a pin's expansion state */
	bool IsPinExpanded(const FString& InPinPropertyPath) const;

	/** Propagate pin defaults to underlying properties if they have changed */
	void CopyPinDefaultsToModel(UEdGraphPin* Pin, bool bUndo = false);

	/** Check whether we are a property accessor */
	bool IsPropertyAccessor() const { return GetUnitScriptStruct() == nullptr; }

	/** Get the blueprint that this node is contained within */
	UControlRigBlueprint* GetBlueprint() const;

	/** Add a new array element to the array referred to by the property path */
	void HandleAddArrayElement(FString InPropertyPath);

	/** Create Variable Pins on the side */
	void CreateVariablePins(bool bAlwaysCreatePins = false);
	
	/** Clear the array referred to by the property path */
	void HandleClearArray(FString InPropertyPath);

	/** Remove the array element referred to by the property path */
	void HandleRemoveArrayElement(FString InPropertyPath);

	/** Insert a new array element after the element referred to by the property path */
	void HandleInsertArrayElement(FString InPropertyPath);

#if WITH_EDITORONLY_DATA
	virtual void PostLoad() override;
	void CacheHierarchyRefConnectionsOnPostLoad();
#endif

protected:
	/** Rebuild the cached info about our inputs/outputs */
	void CacheVariableInfo();

	/** Helper function for AllocateDefaultPins */
	void CreateExecutionPins(bool bAlwaysCreatePins);

	/** Helper function for AllocateDefaultPins */
	void CreateInputPins(bool bAlwaysCreatePins);
	void CreateInputPins_Recursive(const TSharedPtr<FControlRigField>& InputInfo, bool bAlwaysCreatePins);

	/** Helper function for AllocateDefaultPins */
	void CreateInputOutputPins(bool bAlwaysCreatePins);
	void CreateInputOutputPins_Recursive(const TSharedPtr<FControlRigField>& InputOutputInfo, bool bAlwaysCreatePins);

	/** Helper function for AllocateDefaultPins */
	void CreateOutputPins(bool bAlwaysCreatePins);
	void CreateOutputPins_Recursive(const TSharedPtr<FControlRigField>& OutputInfo, bool bAlwaysCreatePins);

	/** Get the generated ControlRig class */
	UClass* GetControlRigGeneratedClass() const;

	/** Get the skeleton generated ControlRig class */
	UClass* GetControlRigSkeletonGeneratedClass() const;

	/** Create a ControlRig field from a field on the ControlRig class, if possible */
	TSharedPtr<FControlRigField> CreateControlRigField(const FControlRigModelPin* InPin, const FString& InPinPath, int32 InArrayIndex = INDEX_NONE) const;

	/** Get all fields that act as execution pins for this node */
	void GetExecutionFields(TArray<TSharedRef<FControlRigField>>& OutFields) const;

	/** Get all fields that act as inputs for this node */
	void GetInputFields(TArray<TSharedRef<FControlRigField>>& OutFields) const;

	/** Get all fields that act as outputs for this node */
	void GetOutputFields(TArray<TSharedRef<FControlRigField>>& OutFields) const;

	/** Get all fields that act as input-outputs for this node */
	void GetInputOutputFields(TArray<TSharedRef<FControlRigField>>& OutFields) const;

	/** Helper function for GetInputFields/GetOutputFields */
	void GetFields(TFunction<bool(const FControlRigModelPin*, const FControlRigModelNode*)> InPinCheckFunction, TArray<TSharedRef<FControlRigField>>& OutFields) const;

	/** Get the struct property for the unit we represent, if any (we could just be a property accessor) */
	FStructProperty* GetUnitProperty() const;

	/** Get the script struct for the unit we represent, if any (we could just be a property accessor) */
	UScriptStruct* GetUnitScriptStruct() const;

	/** Get the property for the unit we represent */
	FProperty* GetProperty() const;

	/** Copies default values from underlying properties into pin defaults, for editing */
	void SetupPinDefaultsFromCDO(UEdGraphPin* Pin);

	/** Recreate pins when we reconstruct this node */
	virtual void ReallocatePinsDuringReconstruction(const TArray<UEdGraphPin*>& OldPins);

	/** Wire-up new pins given old pin wiring */
	virtual void RewireOldPinsToNewPins(TArray<UEdGraphPin*>& InOldPins, TArray<UEdGraphPin*>& InNewPins);

	/** Handle anything post-reconstruction */
	virtual void PostReconstructNode();

	/** Something that could change our title has changed */
	void InvalidateNodeTitle() const;

	/** Destroy all pins in an array */
	void DestroyPinList(TArray<UEdGraphPin*>& InPins);

	/** Sets the body + title color from a color provided by the model */
	void SetColorFromModel(const FLinearColor& InColor);

private:

	bool IsVariable() const;

#if WITH_EDITORONLY_DATA
	TArray<UEdGraphNode*> HierarchyRefOutputConnections;
#endif

	FLinearColor CachedTitleColor;
	FLinearColor CachedNodeColor;
};
