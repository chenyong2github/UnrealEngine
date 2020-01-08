// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Misc/Guid.h"
#include "AnimGraphNode_LinkedAnimGraphBase.h"
#include "Animation/AnimNode_LinkedAnimLayer.h"

#include "AnimGraphNode_LinkedAnimLayer.generated.h"

class FCompilerResultsLog;
class IDetailLayoutBuilder;
class IPropertyHandle;
class SToolTip;

UCLASS(MinimalAPI, Experimental)
class UAnimGraphNode_LinkedAnimLayer : public UAnimGraphNode_LinkedAnimGraphBase
{
	GENERATED_BODY()

public:

	UPROPERTY(EditAnywhere, Category = Settings)
	FAnimNode_LinkedAnimLayer Node;

	/** Guid of the named layer graph we refer to */
	UPROPERTY()
	FGuid InterfaceGuid;

	// Begin UObject interface
	virtual void Serialize(FArchive& Ar) override;

	//~ Begin UEdGraphNode Interface.
	virtual FText GetTooltipText() const override;
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual void ValidateAnimNodeDuringCompilation(USkeleton* ForSkeleton, FCompilerResultsLog& MessageLog) override;
	virtual UObject* GetJumpTargetForDoubleClick() const override;
	virtual void JumpToDefinition() const override;
	virtual bool HasExternalDependencies(TArray<class UStruct*>* OptionalOutput /*= NULL*/) const override;
	virtual void ReconstructNode() override;
	//~ End UEdGraphNode Interface.

	// UAnimGraphNode_Base interface
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;

	// Optionally updates layer GUID if it is invalid
	void UpdateGuidForLayer();

protected:
	// ----- UI CALLBACKS ----- //
	// Handlers for layer combo
	void GetLayerNames(TArray<TSharedPtr<FString>>& OutStrings, TArray<TSharedPtr<SToolTip>>& OutToolTips, TArray<bool>& OutRestrictedItems);
	FString GetLayerName() const;
	void OnLayerChanged(IDetailLayoutBuilder* DetailBuilder);
	bool HasAvailableLayers() const;
	bool HasValidNonSelfLayer() const;
	void HandleSetObjectBeingDebugged(UObject* InDebugObj);
	void HandleInstanceChanged();
	// ----- END UI CALLBACKS ----- //

	// Begin UAnimGraphNode_CustomProperty
	virtual FAnimNode_CustomProperty* GetCustomPropertyNode() override { return &Node;  }
	virtual const FAnimNode_CustomProperty* GetCustomPropertyNode() const override { return &Node; }
	void GetExposableProperties( TArray<FProperty*>& OutExposableProperties) const override;
	virtual bool NeedsToSpecifyValidTargetClass() const override { return false; }
	virtual UClass* GetTargetSkeletonClass() const override;

	// Begin UAnimGraphNode_LinkedAnimGraphBase
	virtual FAnimNode_LinkedAnimLayer* GetLinkedAnimGraphNode() override { return &Node;  }
	virtual const FAnimNode_LinkedAnimLayer* GetLinkedAnimGraphNode() const override { return &Node; }
	virtual bool OnShouldFilterInstanceBlueprint(const FAssetData& AssetData) const override;
	virtual FString GetCurrentInstanceBlueprintPath() const override;
	virtual bool IsStructuralProperty(FProperty* InProperty) const override;

	// Helper function to get the interface currently in use by the selected layer
	TSubclassOf<UInterface> GetInterfaceForLayer() const;

	// Helper function to get the interface graph GUID currently in use by the selected layer
	FGuid GetGuidForLayer() const;

	// Get the preview node, if any, when instanced in an animation blueprint and debugged
	FAnimNode_LinkedAnimLayer* GetPreviewNode() const;

	// Handle used to hook into object being debugged changing
	FDelegateHandle SetObjectBeingDebuggedHandle;
};

UE_DEPRECATED(4.24, "UAnimGraphNode_Layer has been renamed to UAnimGraphNode_LinkedAnimLayer")
typedef UAnimGraphNode_LinkedAnimLayer UAnimGraphNode_Layer;