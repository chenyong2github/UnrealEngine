// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "ViewModels/Stack/NiagaraStackShaderStagesGroup.h"
#include "ViewModels/NiagaraEmitterViewModel.h"
#include "NiagaraScriptViewModel.h"
#include "NiagaraScriptGraphViewModel.h"
#include "NiagaraEmitter.h"
#include "NiagaraShaderStageBase.h"
#include "NiagaraGraph.h"
#include "NiagaraScriptSource.h"
#include "ViewModels/Stack/INiagaraStackItemGroupAddUtilities.h"
#include "ViewModels/Stack/NiagaraStackGraphUtilities.h"

#include "ScopedTransaction.h"

#define LOCTEXT_NAMESPACE "NiagaraStackShaderStagesGroup"

class FShaderStagesGroupAddAction : public INiagaraStackItemGroupAddAction
{
public:
	FShaderStagesGroupAddAction(UClass* InShaderStageClass)
		: ShaderStageClass(InShaderStageClass)
	{
	}

	virtual FText GetCategory() const override
	{
		return LOCTEXT("AddShaderStageCategory", "Add Shader Stage");
	}

	virtual FText GetDisplayName() const override
	{
		return ShaderStageClass->GetDisplayNameText();
	}

	virtual FText GetDescription() const override
	{
		return FText::FromString(ShaderStageClass->GetDescription());
	}

	virtual FText GetKeywords() const override
	{
		return FText();
	}

	UClass* GetShaderStageClass() const
	{
		return ShaderStageClass;
	}

private:
	UClass* ShaderStageClass;
};

class FShaderStagesGroupAddUtilities : public TNiagaraStackItemGroupAddUtilities<UNiagaraShaderStageBase*>
{
public:
	FShaderStagesGroupAddUtilities(TSharedRef<FNiagaraEmitterViewModel> InEmitterViewModel, FOnItemAdded InOnItemAdded)
		: TNiagaraStackItemGroupAddUtilities(LOCTEXT("ShaderStagesGroupAddItemName", "Shader Stage"), EAddMode::AddFromAction, true, InOnItemAdded)
		, EmitterViewModel(InEmitterViewModel)
	{
	}

	virtual void AddItemDirectly() override	{ unimplemented(); }

	virtual void GenerateAddActions(TArray<TSharedRef<INiagaraStackItemGroupAddAction>>& OutAddActions, const FNiagaraStackItemGroupAddOptions& AddProperties) const override 
	{
		TArray<UClass*> ShaderStageClasses;
		GetDerivedClasses(UNiagaraShaderStageBase::StaticClass(), ShaderStageClasses);
		for (UClass* ShaderStageClass : ShaderStageClasses)
		{
			OutAddActions.Add(MakeShared<FShaderStagesGroupAddAction>(ShaderStageClass));
		}
	}

	virtual void ExecuteAddAction(TSharedRef<INiagaraStackItemGroupAddAction> AddAction, int32 TargetIndex) override 
	{ 
		TSharedPtr<FNiagaraEmitterViewModel> EmitterViewModelPinned = EmitterViewModel.Pin();
		if (EmitterViewModelPinned.IsValid() == false)
		{
			return;
		}

		UNiagaraEmitter* Emitter = EmitterViewModelPinned->GetEmitter();
		UNiagaraScriptSource* Source = EmitterViewModelPinned->GetSharedScriptViewModel()->GetGraphViewModel()->GetScriptSource();
		UNiagaraGraph* Graph = EmitterViewModelPinned->GetSharedScriptViewModel()->GetGraphViewModel()->GetGraph();
		TSharedRef<FShaderStagesGroupAddAction> ShaderStageAddAction = StaticCastSharedRef<FShaderStagesGroupAddAction>(AddAction);

		// The stack should not have been created if any of these are null, so bail out if it happens somehow rather than try to handle all of these cases.
		checkf(Emitter != nullptr && Source != nullptr && Graph != nullptr, TEXT("Stack created for invalid emitter or graph."));

		FScopedTransaction ScopedTransaction(LOCTEXT("AddNewShaderStagesTransaction", "Add new shader stage"));

		Emitter->Modify();
		UNiagaraShaderStageBase* ShaderStage = NewObject<UNiagaraShaderStageBase>(Emitter, ShaderStageAddAction->GetShaderStageClass(), NAME_None, RF_Transactional);
		ShaderStage->Script = NewObject<UNiagaraScript>(ShaderStage, MakeUniqueObjectName(ShaderStage, UNiagaraScript::StaticClass(), "ShaderStage"), EObjectFlags::RF_Transactional);
		ShaderStage->Script->SetUsage(ENiagaraScriptUsage::ParticleShaderStageScript);
		ShaderStage->Script->SetUsageId(ShaderStage->GetMergeId());
		ShaderStage->Script->SetSource(Source);
		Emitter->AddShaderStage(ShaderStage);
		FNiagaraStackGraphUtilities::ResetGraphForOutput(*Graph, ENiagaraScriptUsage::ParticleShaderStageScript, ShaderStage->Script->GetUsageId());

		// Set the emitter here so that the internal state of the view model is updated.
		// TODO: Move the logic for managing additional scripts into the emitter view model or script view model.
		TWeakPtr<FNiagaraEmitterInstance, ESPMode::ThreadSafe> Simulation = EmitterViewModelPinned->GetSimulation();
		EmitterViewModelPinned->Reset();
		EmitterViewModelPinned->Initialize(Emitter, Simulation);

		OnItemAdded.ExecuteIfBound(ShaderStage);
	}

private:
	TWeakPtr<FNiagaraEmitterViewModel> EmitterViewModel;
};

void UNiagaraStackShaderStagesGroup::Initialize(FRequiredEntryData InRequiredEntryData)
{
	FText DisplayName = LOCTEXT("ShaderStageGroupName", "Add Shader Stage");
	FText ToolTip = LOCTEXT("ShaderStageGroupTooltip", "Shader stages for this emitter.");
	AddUtilities = MakeShared<FShaderStagesGroupAddUtilities>(InRequiredEntryData.EmitterViewModel.ToSharedRef(), 
		TNiagaraStackItemGroupAddUtilities<UNiagaraShaderStageBase*>::FOnItemAdded::CreateUObject(this, &UNiagaraStackShaderStagesGroup::ItemAddedFromUtilties));
	Super::Initialize(InRequiredEntryData, DisplayName, ToolTip, AddUtilities.Get());
}

void UNiagaraStackShaderStagesGroup::SetOnItemAdded(FOnItemAdded InOnItemAdded)
{
	ItemAddedDelegate = InOnItemAdded;
}

void UNiagaraStackShaderStagesGroup::ItemAddedFromUtilties(UNiagaraShaderStageBase* AddedShaderStage)
{
	ItemAddedDelegate.ExecuteIfBound();
}

#undef LOCTEXT_NAMESPACE
