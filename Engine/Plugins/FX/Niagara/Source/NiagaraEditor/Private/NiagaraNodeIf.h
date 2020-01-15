// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "NiagaraNodeWithDynamicPins.h"
#include "EdGraphSchema_Niagara.h"
#include "NiagaraNodeIf.generated.h"

USTRUCT()
struct FPinGuidsForPath
{
	GENERATED_USTRUCT_BODY()

	FPinGuidsForPath()
		: OutputPinGuid(FGuid())
		, InputAPinGuid(FGuid())
		, InputBPinGuid(FGuid())
	{
	}

	bool IsValid() const
	{
		return OutputPinGuid.IsValid() && InputAPinGuid.IsValid() && InputBPinGuid.IsValid();
	}

	UPROPERTY()
	FGuid OutputPinGuid;

	UPROPERTY()
	FGuid InputAPinGuid;

	UPROPERTY()
	FGuid InputBPinGuid;
};

UCLASS(MinimalAPI)
class UNiagaraNodeIf : public UNiagaraNodeWithDynamicPins
{
	GENERATED_UCLASS_BODY()

public:

	/** Outputs of this branch. */
	UPROPERTY()
	TArray<FNiagaraVariable> OutputVars;

	UPROPERTY()
	TArray<FPinGuidsForPath> PathAssociatedPinGuids;

	UPROPERTY()
	FGuid ConditionPinGuid;

	//~ Begin UObject Interface
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void PostLoad() override;
	//~ End UObject Interface

	//~ Begin EdGraphNode Interface
	virtual void AllocateDefaultPins() override;
	virtual FText GetTooltipText() const override;
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	//~ End EdGraphNode Interface

	//~ Begin UNiagaraNode Interface
	virtual void Compile(class FHlslNiagaraTranslator* Translator, TArray<int32>& Outputs) override;
	virtual bool RefreshFromExternalChanges() override;
	virtual ENiagaraNumericOutputTypeSelectionMode GetNumericOutputTypeSelectionMode() const;
	virtual void ResolveNumerics(const UEdGraphSchema_Niagara* Schema, bool bSetInline, TMap<TPair<FGuid, UEdGraphNode*>, FNiagaraTypeDefinition>* PinCache);
	//~ End UNiagaraNode Interface

protected:

	/** Helper function to create a variable to add to the OutputVars and FGuid to add to PathAssociatedPinGuids. */
	FGuid AddOutput(FNiagaraTypeDefinition Type, const FName& Name);

	/** Helper to get a pin in the pins list by GUID */
	const UEdGraphPin* GetPinByGuid(const FGuid& InGuid);

	//~ Begin EdGraphNode Interface
	virtual void OnPinRemoved(UEdGraphPin* PinToRemove) override;
	//~ End EdGraphNode Interface
	
	//~ Begin UNiagaraNodeWithDynamicPins Interface
	virtual void OnNewTypedPinAdded(UEdGraphPin* NewPin) override;
	virtual void OnPinRenamed(UEdGraphPin* RenamedPin, const FString& OldName) override;
	virtual bool CanRenamePin(const UEdGraphPin* Pin) const override;
	virtual bool CanRemovePin(const UEdGraphPin* Pin) const override;
	virtual bool CanMovePin(const UEdGraphPin* Pin) const override { return false; }
	virtual bool AllowNiagaraTypeForAddPin(const FNiagaraTypeDefinition& InType) override;
	//~ End UNiagaraNodeWithDynamicPins Interface

private:
	static const FString InputAPinSuffix;
	static const FString InputBPinSuffix;
	static const FName ConditionPinName;
};
