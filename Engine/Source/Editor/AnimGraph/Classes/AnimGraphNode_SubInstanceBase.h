// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Misc/Guid.h"
#include "AnimGraphNode_CustomProperty.h"

#include "AnimGraphNode_SubInstanceBase.generated.h"

class FCompilerResultsLog;
class IDetailLayoutBuilder;
class IPropertyHandle;
class SToolTip;
struct FAnimNode_SubInstance;

UCLASS(MinimalAPI, Abstract)
class UAnimGraphNode_SubInstanceBase : public UAnimGraphNode_CustomProperty
{
	GENERATED_BODY()

public:

	//~ Begin UEdGraphNode Interface.
	virtual FLinearColor GetNodeTitleColor() const override;
	virtual FText GetTooltipText() const override;
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual void ValidateAnimNodeDuringCompilation(USkeleton* ForSkeleton, FCompilerResultsLog& MessageLog) override;
	virtual void ReallocatePinsDuringReconstruction(TArray<UEdGraphPin*>& OldPins) override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;
	//~ End UEdGraphNode Interface.

	// UAnimGraphNode_Base interface
	virtual FPoseLinkMappingRecord GetLinkIDLocation(const UScriptStruct* NodeType, UEdGraphPin* SourcePin) override;

	// UAnimGraphNode_CustomProperty interface
	virtual bool IsStructuralProperty(UProperty* InProperty) const override;

	// Node accessor
	virtual FAnimNode_SubInstance* GetSubInstanceNode() PURE_VIRTUAL(UAnimGraphNode_SubInstanceBase::GetSubInstanceNode, return nullptr;);
	virtual const FAnimNode_SubInstance* GetSubInstanceNode() const PURE_VIRTUAL(UAnimGraphNode_SubInstanceBase::GetSubInstanceNode, return nullptr;);

protected:
	// Finds out whether there is a loop in the graph formed by sub instances from this node
	bool HasInstanceLoop();
	
	/** Generates widgets for exposing/hiding Pins for this node using the rpovided detail builder */
	void GenerateExposedPinsDetails(IDetailLayoutBuilder &DetailBuilder);

	// Finds out whether there is a loop in the graph formed by sub instances from CurrNode, used by HasInstanceLoop. VisitedNodes and NodeStack are required
	// to track the graph links
	// VisitedNodes - Node we have searched the links of, so we don't do it twice
	// NodeStack - The currently considered chain of nodes. If a loop is detected this will contain the chain that causes the loop
	static bool HasInstanceLoop_Recursive(UAnimGraphNode_SubInstanceBase* CurrNode, TArray<FGuid>& VisitedNodes, TArray<FGuid>& NodeStack);

	// ----- UI CALLBACKS ----- //

	// Gets path to the currently selected instance class' blueprint
	virtual FString GetCurrentInstanceBlueprintPath() const;
	// Filter callback for blueprints (only accept matching skeletons/interfaces)
	virtual bool OnShouldFilterInstanceBlueprint(const FAssetData& AssetData) const;
	// Instance blueprint was changed by user
	void OnSetInstanceBlueprint(const FAssetData& AssetData, IDetailLayoutBuilder* InDetailBuilder);
	// ----- END UI CALLBACKS ----- //
};
