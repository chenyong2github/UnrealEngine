// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once
#include "EdGraphSchema_Niagara.h"
#include "NiagaraNodeUsageSelector.h"
#include "NiagaraNodeStaticSwitch.generated.h"

UENUM()
enum class ENiagaraStaticSwitchType : uint8
{
	Bool,
	Integer,
	Enum
};

USTRUCT()
struct FStaticSwitchTypeData
{
	GENERATED_USTRUCT_BODY()

	/** This determines how the switch value is interpreted */
	UPROPERTY()
	ENiagaraStaticSwitchType SwitchType;

	/** If the type is int, this sets the upper range of input values the switch supports (so pins = MaxIntCount + 1) */
	UPROPERTY()
	int32 MaxIntCount;

	/** If the type is enum, this is the enum being switched on, otherwise it holds no sensible value */
	UPROPERTY()
	UEnum* Enum;

	/** If set, then this switch is not exposed but will rather be evaluated by the given compile-time constant */
	UPROPERTY()
	FName SwitchConstant;

	FStaticSwitchTypeData() : SwitchType(ENiagaraStaticSwitchType::Bool), MaxIntCount(1), Enum(nullptr)
	{ }
};

UCLASS(MinimalAPI)
class UNiagaraNodeStaticSwitch : public UNiagaraNodeUsageSelector
{
	GENERATED_UCLASS_BODY()

public:

	UPROPERTY()
	FName InputParameterName;
	
	UPROPERTY()
	FStaticSwitchTypeData SwitchTypeData;

	FNiagaraTypeDefinition GetInputType() const;

	void ChangeSwitchParameterName(const FName& NewName);
	void OnSwitchParameterTypeChanged(const FNiagaraTypeDefinition& OldType);

	void SetSwitchValue(int Value);
	void SetSwitchValue(const FCompileConstantResolver& ConstantResolver);
	void ClearSwitchValue();
	/** If true then the value of this static switch is not set by the user but directly by the compiler via one of the engine constants (e.g. Emitter.Determinism). */
	bool IsSetByCompiler() const;

	void UpdateCompilerConstantValue(class FHlslNiagaraTranslator* Translator);

	//~ Begin EdGraphNode Interface
	virtual void AllocateDefaultPins() override;
	virtual FText GetTooltipText() const override;
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual FLinearColor GetNodeTitleColor() const override;
	virtual bool IsCompilerRelevant() const override { return false; }
	//~ End EdGraphNode Interface

	//~ Begin UNiagaraNode Interface
	virtual void Compile(class FHlslNiagaraTranslator* Translator, TArray<int32>& Outputs) override;
	virtual bool SubstituteCompiledPin(FHlslNiagaraTranslator* Translator, UEdGraphPin** LocallyOwnedPin) override;
	virtual UEdGraphPin* GetTracedOutputPin(UEdGraphPin* LocallyOwnedOutputPin) const override;
	virtual UEdGraphPin* GetTracedOutputPin(UEdGraphPin* LocallyOwnedOutputPin, bool bRecursive) const;
	virtual void BuildParameterMapHistory(FNiagaraParameterMapHistoryBuilder& OutHistory, bool bRecursive = true, bool bFilterForCompilation = true) const override;
	//~ End UNiagaraNode Interface

	virtual void DestroyNode() override;

protected:
	//~ Begin UNiagaraNodeUsageSelector Interface
	virtual void InsertInputPinsFor(const FNiagaraVariable& Var) override;
	virtual bool AllowNiagaraTypeForAddPin(const FNiagaraTypeDefinition& InType) override;
	//~ End UNiagaraNodeUsageSelector Interface

private:
	/** This finds the first valid input pin index for the current switch value, returns false if no value can be found */
	bool GetVarIndex(class FHlslNiagaraTranslator* Translator, int32 InputPinCount, int32& VarIndexOut) const;

	bool GetVarIndex(class FHlslNiagaraTranslator* Translator, int32 InputPinCount, int32 Value, int32& VarIndexOut) const;

	void RemoveUnusedGraphParameter(const FNiagaraVariable& OldParameter);

	/** If true then the current SwitchValue should be used for compilation, otherwise the node is not yet connected to a constant value */
	bool IsValueSet = false;

	/** The current value that the node is evaluated with. For bool 0 is false, for enums the value is the index of the enum value. */
	int32 SwitchValue = 0;
};
