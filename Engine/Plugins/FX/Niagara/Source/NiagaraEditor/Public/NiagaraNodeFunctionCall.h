// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "NiagaraEditorCommon.h"
#include "NiagaraNodeWithDynamicPins.h"
#include "NiagaraNodeInput.h"
#include "NiagaraNodeFunctionCall.generated.h"

class UNiagaraScript;

USTRUCT()
struct FNiagaraPropagatedVariable
{
	GENERATED_USTRUCT_BODY()

public:

	FNiagaraPropagatedVariable() : FNiagaraPropagatedVariable(FNiagaraVariable()) {}

	FNiagaraPropagatedVariable(FNiagaraVariable SwitchParameter) : SwitchParameter(SwitchParameter), PropagatedName(FString()) {}

	UPROPERTY()
	FNiagaraVariable SwitchParameter;

	/** If set, then this overrides the name of the switch parameter when propagating. */
	UPROPERTY()
	FString PropagatedName;

	FNiagaraVariable ToVariable() const
	{
		FNiagaraVariable Copy = SwitchParameter;
		if (!PropagatedName.IsEmpty())
		{
			Copy.SetName(FName(*PropagatedName));
		}
		return Copy;
	}

	bool operator==(const FNiagaraPropagatedVariable& Other)const
	{
		return SwitchParameter == Other.SwitchParameter;
	}
};

UCLASS(MinimalAPI)
class UNiagaraNodeFunctionCall : public UNiagaraNodeWithDynamicPins
{
	GENERATED_BODY()

public:
	DECLARE_MULTICAST_DELEGATE(FOnInputsChanged);

public:

	UPROPERTY(EditAnywhere, Category = "Function")
	UNiagaraScript* FunctionScript;

	/** 
	 * A path to a script asset which can be used to assign the function script the first time that
	 * default pins are generated. This is used so that the function nodes can be populated in the graph context
	 * menu without having to load all of the actual script assets.
	 */
	UPROPERTY(Transient, meta = (SkipForCompileHash = "true"))
	FName FunctionScriptAssetObjectPath;

	/** Some functions can be provided a signature directly rather than a script. */
	UPROPERTY()
	FNiagaraFunctionSignature Signature;

	UPROPERTY(VisibleAnywhere, Category = "Function")
	TMap<FName, FName> FunctionSpecifiers;

	/** All the input values the function propagates to the next higher caller instead of forcing the user to set them directly. */
	UPROPERTY()
	TArray<FNiagaraPropagatedVariable> PropagatedStaticSwitchParameters;

	bool ScriptIsValid() const;

	//Begin UObject interface
	virtual void PostLoad()override;
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
	//End UObject interface

	//~ Begin UNiagaraNode Interface
	virtual void PostPlacedNewNode() override;
	virtual void Compile(class FHlslNiagaraTranslator* Translator, TArray<int32>& Outputs) override;
	virtual UObject* GetReferencedAsset() const override;
	virtual bool RefreshFromExternalChanges() override;
	virtual ENiagaraNumericOutputTypeSelectionMode GetNumericOutputTypeSelectionMode() const override;
	virtual bool CanAddToGraph(UNiagaraGraph* TargetGraph, FString& OutErrorMsg) const override;
	virtual void SubsumeExternalDependencies(TMap<const UObject*, UObject*>& ExistingConversions) override;
	virtual void GatherExternalDependencyData(ENiagaraScriptUsage InMasterUsage, const FGuid& InMasterUsageId, TArray<FNiagaraCompileHash>& InReferencedCompileHashes, TArray<FString>& InReferencedObjs) const override;
	virtual void UpdateCompileHashForNode(FSHA1& HashState) const override;
	//End UNiagaraNode interface

	//~ Begin EdGraphNode Interface
	virtual void AutowireNewNode(UEdGraphPin* FromPin) override;
	virtual void AllocateDefaultPins() override;
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual FText GetTooltipText() const override;
	virtual FLinearColor GetNodeTitleColor() const override;
	
	/** Returns true if this node is deprecated */
	virtual bool IsDeprecated() const override;
	//~ End EdGraphNode Interface

	bool FindAutoBoundInput(UNiagaraNodeInput* InputNode, UEdGraphPin* PinToAutoBind, FNiagaraVariable& OutFoundVar, ENiagaraInputNodeUsage& OutNodeUsage);

	void BuildParameterMapHistory(FNiagaraParameterMapHistoryBuilder& OutHistory, bool bRecursive = true, bool bFilterForCompilation = true) const override;

	FString GetFunctionName() const { return FunctionDisplayName; }
	UNiagaraGraph* GetCalledGraph() const;
	ENiagaraScriptUsage GetCalledUsage() const;

	/** Walk through the internal script graph for an ParameterMapGet nodes and see if any of them specify a default for VariableName.*/
	UEdGraphPin* FindParameterMapDefaultValuePin(const FName VariableName, ENiagaraScriptUsage InParentUsage) const;

	/** Attempts to find the input pin for a static switch with the given name in the internal script graph. Returns nullptr if no such pin can be found. */
	UEdGraphPin* FindStaticSwitchInputPin(const FName& VariableName) const;

	/** Tries to rename this function call to a new name.  The actual name that gets applied might be different due to conflicts with existing
		nodes with the same name. */
	void SuggestName(FString SuggestedName);

	FOnInputsChanged& OnInputsChanged();

	FNiagaraPropagatedVariable* FindPropagatedVariable(const FNiagaraVariable& Variable);
	void RemovePropagatedVariable(const FNiagaraVariable& Variable);
	void CleanupPropagatedSwitchValues();

	/** Does any automated data manipulated required to update DI function call nodes to the current version. */
	void UpgradeDIFunctionCalls();

	virtual TSharedPtr<SGraphNode> CreateVisualWidget() override;
protected:

	virtual bool IsValidPinToCompile(UEdGraphPin* Pin) const;
	virtual bool GetValidateDataInterfaces() const { return true; };

	virtual bool AllowDynamicPins() const override { return false; }
	virtual bool CanRenamePin(const UEdGraphPin* Pin) const override { return false; }
	virtual bool CanRemovePin(const UEdGraphPin* Pin) const override { return false; }
	virtual bool CanMovePin(const UEdGraphPin* Pin) const override { return false; }

	/** Resets the node name based on the referenced script or signature. Guaranteed unique within a given graph instance.*/
	void ComputeNodeName(FString SuggestedName = FString());

	void SetPinAutoGeneratedDefaultValue(UEdGraphPin& FunctionInputPin, UNiagaraNodeInput& FunctionScriptInputNode);

	bool IsValidPropagatedVariable(const FNiagaraVariable& Variable) const;

	void UpdateNodeErrorMessage();

	/** Adjusted every time that we compile this script. Lets us know that we might differ from any cached versions.*/
	UPROPERTY(meta = (SkipForCompileHash="true"))
	FGuid CachedChangeId;

	UPROPERTY(meta = (SkipForCompileHash = "true"))
	FString FunctionDisplayName;

	FOnInputsChanged OnInputsChangedDelegate;
};

