// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "K2Node.h"
#include "IClassVariableCreator.h"
#include "EdGraph/EdGraphPin.h"

#include "K2Node_PropertyAccess.generated.h"

UCLASS(MinimalAPI)
class UK2Node_PropertyAccess : public UK2Node, public IClassVariableCreator
{
public:
	GENERATED_BODY()

	/** Set the path and attempt to resolve the leaf property. */
	void SetPath(const TArray<FString>& InPath);
	void SetPath(TArray<FString>&& InPath);

	/** Clear the path */
	void ClearPath();

	/** Get the path */
	const TArray<FString>& GetPath() const { return Path; }

	/** Get the path as text */
	const FText& GetTextPath() const { return TextPath; }

	/** Get the resolved leaf property, if any. */
	const FProperty* GetResolvedProperty() const { return ResolvedProperty.Get(); }

	/** Get the resolved leaf property's array index, if any. */
	int32 GetResolvedArrayIndex() const { return ResolvedArrayIndex; }

	/** Get the resolved pin type, if any. */
	const FEdGraphPinType& GetResolvedPinType() const { return ResolvedPinType; }

	/** Get the output pin */
	UEdGraphPin* GetOutputPin() const { return FindPinChecked(TEXT("Value"), EGPD_Output); }

private:
	/** Path that this access exposes */
	UPROPERTY()
	TArray<FString> Path;

	/** Path as text, for display */
	UPROPERTY()
	FText TextPath;

	/** Resolved pin type */
	UPROPERTY()
	FEdGraphPinType ResolvedPinType;

	/** Generated property created during compilation */
	UPROPERTY()
	FName GeneratedPropertyName = NAME_None;

	/** Resolved leaf property for the path, NULL if path cant be resolved or is empty. */
	TFieldPath<FProperty> ResolvedProperty;

	/** Resolved array index, if the leaf property is an array. If this is INDEX_NONE then the property refers to the entire array */
	int32 ResolvedArrayIndex = INDEX_NONE;

	// IClassVariableCreator interface
	virtual void CreateClassVariablesFromBlueprint(IAnimBlueprintVariableCreationContext& InCreationContext) override;

	// UEdGraphNode interface
	virtual void AllocateDefaultPins() override;
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual void AddSearchMetaDataInfo(TArray<FSearchTagDataPair>& OutTaggedMetaData) const override;
	virtual void PinConnectionListChanged(UEdGraphPin* Pin) override;
	virtual bool IsCompatibleWithGraph(UEdGraph const* TargetGraph) const override;
	
	// UK2Node interface
	virtual void ExpandNode(class FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph) override;
	virtual void ReallocatePinsDuringReconstruction(TArray<UEdGraphPin*>& OldPins) override;
	virtual bool IsNodePure() const override { return true; }
	virtual void GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const override;
	virtual FText GetMenuCategory() const override;

	/** Helper function for pin allocation */
	void AllocatePins(UEdGraphPin* InOldOutputPin = nullptr);

	/** Attempt to resolve the path to a leaf property */
	void ResolveLeafProperty();
};