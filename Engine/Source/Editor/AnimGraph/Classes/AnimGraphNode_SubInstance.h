// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Misc/Guid.h"
#include "AnimGraphNode_CustomProperty.h"
#include "Animation/AnimNode_SubInstance.h"

#include "AnimGraphNode_SubInstance.generated.h"

class FCompilerResultsLog;
class IDetailLayoutBuilder;
class IPropertyHandle;

UCLASS(MinimalAPI)
class UAnimGraphNode_SubInstance : public UAnimGraphNode_CustomProperty
{
	GENERATED_BODY()

public:

	UPROPERTY(EditAnywhere, Category = Settings)
	FAnimNode_SubInstance Node;

	//~ Begin UEdGraphNode Interface.
	virtual FLinearColor GetNodeTitleColor() const override;
	virtual FText GetTooltipText() const override;
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual void ValidateAnimNodeDuringCompilation(USkeleton* ForSkeleton, FCompilerResultsLog& MessageLog) override;
	virtual void ReallocatePinsDuringReconstruction(TArray<UEdGraphPin*>& OldPins) override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	//~ End UEdGraphNode Interface.

	// Detail panel customizations
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;

	virtual bool IsValidPropertyPin(const FName& PinName) const override
	{
		return UAnimGraphNode_CustomProperty::IsValidPropertyPin(PinName) && (PinName != FName(TEXT("InPose"), FNAME_Find));
	}

private:

	// Finds out whether there is a loop in the graph formed by sub instances from this node
	bool HasInstanceLoop();

	// Finds out whether there is a loop in the graph formed by sub instances from CurrNode, used by HasInstanceLoop. VisitedNodes and NodeStack are required
	// to track the graph links
	// VisitedNodes - Node we have searched the links of, so we don't do it twice
	// NodeStack - The currently considered chain of nodes. If a loop is detected this will contain the chain that causes the loop
	static bool HasInstanceLoop_Recursive(UAnimGraphNode_SubInstance* CurrNode, TArray<FGuid>& VisitedNodes, TArray<FGuid>& NodeStack);

	// ----- UI CALLBACKS ----- //
	// If given property exposed on this node
	ECheckBoxState IsPropertyExposed(FName PropertyName) const;
	// User chose to expose, or unexpose a property
	void OnPropertyExposeCheckboxChanged(ECheckBoxState NewState, FName PropertyName);

	// Gets path to the currently selected instance class' blueprint
	FString GetCurrentInstanceBlueprintPath() const;
	// Filter callback for blueprints (only accept matching skeletons)
	bool OnShouldFilterInstanceBlueprint(const FAssetData& AssetData) const;
	// Instance blueprint was changed by user
	void OnSetInstanceBlueprint(const FAssetData& AssetData, TSharedRef<IPropertyHandle> InstanceClassPropHandle);
	// ----- END UI CALLBACKS ----- //

	// Begin UAnimGraphNode_CustomProperty
	virtual FAnimNode_CustomProperty* GetInternalNode() override { return &Node;  }
	virtual const FAnimNode_CustomProperty* GetInternalNode() const override { return &Node; }
};
