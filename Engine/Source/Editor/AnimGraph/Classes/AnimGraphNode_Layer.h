// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Misc/Guid.h"
#include "AnimGraphNode_SubInstanceBase.h"
#include "Animation/AnimNode_Layer.h"

#include "AnimGraphNode_Layer.generated.h"

class FCompilerResultsLog;
class IDetailLayoutBuilder;
class IPropertyHandle;
class SToolTip;

UCLASS(MinimalAPI, Experimental)
class UAnimGraphNode_Layer : public UAnimGraphNode_SubInstanceBase
{
	GENERATED_BODY()

public:

	UPROPERTY(EditAnywhere, Category = Settings)
	FAnimNode_Layer Node;

	//~ Begin UEdGraphNode Interface.
	virtual FText GetTooltipText() const override;
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual void ValidateAnimNodeDuringCompilation(USkeleton* ForSkeleton, FCompilerResultsLog& MessageLog) override;
	virtual UObject* GetJumpTargetForDoubleClick() const override;
	virtual void JumpToDefinition() const override;
	virtual bool HasExternalDependencies(TArray<class UStruct*>* OptionalOutput /*= NULL*/) const override;
	//~ End UEdGraphNode Interface.

	// UAnimGraphNode_Base interface
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;

protected:
	// ----- UI CALLBACKS ----- //
	// Handlers for layer combo
	void GetLayerNames(TArray<TSharedPtr<FString>>& OutStrings, TArray<TSharedPtr<SToolTip>>& OutToolTips, TArray<bool>& OutRestrictedItems);
	FString GetLayerName() const;
	void OnLayerChanged(IDetailLayoutBuilder* DetailBuilder);
	bool HasAvailableLayers() const;
	bool HasValidNonSelfLayer() const;
	// ----- END UI CALLBACKS ----- //

	// Begin UAnimGraphNode_CustomProperty
	virtual FAnimNode_CustomProperty* GetCustomPropertyNode() override { return &Node;  }
	virtual const FAnimNode_CustomProperty* GetCustomPropertyNode() const override { return &Node; }
	void GetExposableProperties( TArray<UProperty*>& OutExposableProperties) const override;
	virtual bool NeedsToSpecifyValidTargetClass() const override { return false; }
	virtual UClass* GetTargetSkeletonClass() const override;

	// Begin UAnimGraphNode_SubInstanceBase
	virtual FAnimNode_SubInstance* GetSubInstanceNode() override { return &Node;  }
	virtual const FAnimNode_SubInstance* GetSubInstanceNode() const override { return &Node; }
	virtual bool OnShouldFilterInstanceBlueprint(const FAssetData& AssetData) const override;
	virtual FString GetCurrentInstanceBlueprintPath() const override;
	virtual bool IsStructuralProperty(UProperty* InProperty) const override;

	// Helper function to get the interface currently in use by the selected layer
	TSubclassOf<UInterface> GetInterfaceForLayer() const;
};
