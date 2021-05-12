// Copyright Epic Games, Inc. All Rights Reserved.

#include "ViewModels/Stack/NiagaraStackSimulationStagesGroup.h"
#include "ViewModels/NiagaraEmitterViewModel.h"
#include "ViewModels/NiagaraScriptViewModel.h"
#include "NiagaraScriptGraphViewModel.h"
#include "NiagaraEmitter.h"
#include "NiagaraSimulationStageBase.h"
#include "NiagaraGraph.h"
#include "NiagaraScriptSource.h"
#include "ViewModels/Stack/INiagaraStackItemGroupAddUtilities.h"
#include "ViewModels/Stack/NiagaraStackGraphUtilities.h"

#include "ScopedTransaction.h"

#define LOCTEXT_NAMESPACE "NiagaraStackSimulationStagesGroup"

class FSimulationStagesGroupAddAction : public INiagaraStackItemGroupAddAction
{
public:
	FSimulationStagesGroupAddAction(UClass* InSimulationStageClass)
		: SimulationStageClass(InSimulationStageClass)
	{
	}

	virtual TArray<FString> GetCategories() const override
	{
		return {};
	}

	virtual FText GetDisplayName() const override
	{
		return SimulationStageClass->GetDisplayNameText();
	}

	virtual FText GetDescription() const override
	{
		return FText::FromString(SimulationStageClass->GetDescription());
	}

	virtual FText GetKeywords() const override
	{
		return FText();
	}

	UClass* GetSimulationStageClass() const
	{
		return SimulationStageClass;
	}

private:
	UClass* SimulationStageClass;
};

class FSimulationStagesGroupAddUtilities : public TNiagaraStackItemGroupAddUtilities<UNiagaraSimulationStageBase*>
{
public:
	FSimulationStagesGroupAddUtilities(TSharedRef<FNiagaraEmitterViewModel> InEmitterViewModel, FOnItemAdded InOnItemAdded)
		: TNiagaraStackItemGroupAddUtilities(LOCTEXT("SimulationStagesGroupAddItemName", "Simulation Stage"), EAddMode::AddFromAction, true, InOnItemAdded)
		, EmitterViewModel(InEmitterViewModel)
	{
	}

	virtual void AddItemDirectly() override	{ unimplemented(); }

	virtual void GenerateAddActions(TArray<TSharedRef<INiagaraStackItemGroupAddAction>>& OutAddActions, const FNiagaraStackItemGroupAddOptions& AddProperties) const override 
	{
		TArray<UClass*> SimulationStageClasses;
		GetDerivedClasses(UNiagaraSimulationStageBase::StaticClass(), SimulationStageClasses);
		for (UClass* SimulationStageClass : SimulationStageClasses)
		{
			OutAddActions.Add(MakeShared<FSimulationStagesGroupAddAction>(SimulationStageClass));
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
		TSharedRef<FSimulationStagesGroupAddAction> SimulationStageAddAction = StaticCastSharedRef<FSimulationStagesGroupAddAction>(AddAction);

		// The stack should not have been created if any of these are null, so bail out if it happens somehow rather than try to handle all of these cases.
		checkf(Emitter != nullptr && Source != nullptr && Graph != nullptr, TEXT("Stack created for invalid emitter or graph."));

		FScopedTransaction ScopedTransaction(LOCTEXT("AddNewSimulationStagesTransaction", "Add new shader stage"));

		Emitter->Modify();
		UNiagaraSimulationStageBase* SimulationStage = NewObject<UNiagaraSimulationStageBase>(Emitter, SimulationStageAddAction->GetSimulationStageClass(), NAME_None, RF_Transactional);
		SimulationStage->Script = NewObject<UNiagaraScript>(SimulationStage, MakeUniqueObjectName(SimulationStage, UNiagaraScript::StaticClass(), "SimulationStage"), EObjectFlags::RF_Transactional);
		SimulationStage->Script->SetUsage(ENiagaraScriptUsage::ParticleSimulationStageScript);
		SimulationStage->Script->SetUsageId(SimulationStage->GetMergeId());
		SimulationStage->Script->SetLatestSource(Source);
		Emitter->AddSimulationStage(SimulationStage);
		FNiagaraStackGraphUtilities::ResetGraphForOutput(*Graph, ENiagaraScriptUsage::ParticleSimulationStageScript, SimulationStage->Script->GetUsageId());

		// Set the emitter here so that the internal state of the view model is updated.
		// TODO: Move the logic for managing additional scripts into the emitter view model or script view model.
		TWeakPtr<FNiagaraEmitterInstance, ESPMode::ThreadSafe> Simulation = EmitterViewModelPinned->GetSimulation();
		EmitterViewModelPinned->Reset();
		EmitterViewModelPinned->Initialize(Emitter, Simulation);

		OnItemAdded.ExecuteIfBound(SimulationStage);
	}

private:
	TWeakPtr<FNiagaraEmitterViewModel> EmitterViewModel;
};

void UNiagaraStackSimulationStagesGroup::Initialize(FRequiredEntryData InRequiredEntryData)
{
	FText DisplayName = LOCTEXT("SimulationStageGroupName", "Add Simulation Stage");
	FText ToolTip = LOCTEXT("SimulationStageGroupTooltip", "Shader stages for this emitter.");
	AddUtilities = MakeShared<FSimulationStagesGroupAddUtilities>(InRequiredEntryData.EmitterViewModel.ToSharedRef(), 
		TNiagaraStackItemGroupAddUtilities<UNiagaraSimulationStageBase*>::FOnItemAdded::CreateUObject(this, &UNiagaraStackSimulationStagesGroup::ItemAddedFromUtilties));
	Super::Initialize(InRequiredEntryData, DisplayName, ToolTip, AddUtilities.Get());
}

void UNiagaraStackSimulationStagesGroup::SetOnItemAdded(FOnItemAdded InOnItemAdded)
{
	ItemAddedDelegate = InOnItemAdded;
}

void UNiagaraStackSimulationStagesGroup::ItemAddedFromUtilties(UNiagaraSimulationStageBase* AddedSimulationStage)
{
	ItemAddedDelegate.ExecuteIfBound();
}

#undef LOCTEXT_NAMESPACE
