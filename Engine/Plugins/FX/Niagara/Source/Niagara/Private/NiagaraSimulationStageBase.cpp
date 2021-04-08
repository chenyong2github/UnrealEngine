// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraSimulationStageBase.h"
#include "NiagaraEmitter.h"
#include "NiagaraSystem.h"
#include "NiagaraScriptSourceBase.h"

bool UNiagaraSimulationStageBase::AppendCompileHash(FNiagaraCompileHashVisitor* InVisitor) const
{
#if WITH_EDITORONLY_DATA
	const int32 Index = InVisitor->Values.AddDefaulted();
	InVisitor->Values[Index].Object = FString::Printf(TEXT("Class: \"%s\"  Name: \"%s\""), *GetClass()->GetName(), *GetName());
#endif
	InVisitor->UpdatePOD(TEXT("Enabled"), bEnabled ? 1 : 0);
	return true;
}

#if WITH_EDITOR
void UNiagaraSimulationStageBase::SetEnabled(bool bInEnabled)
{
	if (bEnabled != bInEnabled)
	{
		bEnabled = bInEnabled;
		RequestRecompile();
	}
}

void UNiagaraSimulationStageBase::RequestRecompile()
{
	UNiagaraEmitter* Emitter = Cast< UNiagaraEmitter>(GetOuter());
	if (Emitter)
	{
		UNiagaraScriptSourceBase* GraphSource = Emitter->UpdateScriptProps.Script->GetLatestSource();
		if (GraphSource != nullptr)
		{
			GraphSource->MarkNotSynchronized(TEXT("SimulationStage changed."));
		}

		UNiagaraSystem::RequestCompileForEmitter(Emitter);
	}
}

void UNiagaraSimulationStageBase::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	FName PropertyName;
	if (PropertyChangedEvent.Property)
	{
		PropertyName = PropertyChangedEvent.Property->GetFName();
	}

	if (PropertyName == GET_MEMBER_NAME_CHECKED(UNiagaraSimulationStageBase, bEnabled))
	{
		RequestRecompile();
	}
}
#endif

bool UNiagaraSimulationStageGeneric::AppendCompileHash(FNiagaraCompileHashVisitor* InVisitor) const 
{
	Super::AppendCompileHash(InVisitor);

	InVisitor->UpdatePOD(TEXT("Iterations"), Iterations);
	InVisitor->UpdatePOD(TEXT("IterationSource"), (int32)IterationSource);
	InVisitor->UpdatePOD(TEXT("bSpawnOnly"), bSpawnOnly ? 1 : 0);
	InVisitor->UpdatePOD(TEXT("bDisablePartialParticleUpdate"), bDisablePartialParticleUpdate ? 1 : 0);
	InVisitor->UpdateString(TEXT("DataInterface"), DataInterface.BoundVariable.GetName().ToString());
	InVisitor->UpdateString(TEXT("SimulationStageName"), SimulationStageName.ToString());
	return true;
}

#if WITH_EDITOR
void UNiagaraSimulationStageGeneric::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) 
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	FName PropertyName;
	if (PropertyChangedEvent.Property)
	{
		PropertyName = PropertyChangedEvent.Property->GetFName();
	}

	bool bNeedsRecompile = false;
	if (PropertyName == GET_MEMBER_NAME_CHECKED(UNiagaraSimulationStageGeneric, Iterations))
	{
		bNeedsRecompile = true;
	}
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(UNiagaraSimulationStageGeneric, IterationSource))
	{
		bNeedsRecompile = true;
	}
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(UNiagaraSimulationStageGeneric, bSpawnOnly))
	{
		bNeedsRecompile = true;
	}
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(UNiagaraSimulationStageGeneric, bDisablePartialParticleUpdate))
	{
		bNeedsRecompile = true;
	}
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(UNiagaraSimulationStageGeneric, DataInterface))
	{
		bNeedsRecompile = true;
	}
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(UNiagaraSimulationStageGeneric, SimulationStageName))
	{
		bNeedsRecompile = true;
	}

	if (bNeedsRecompile)
	{
		RequestRecompile();
	}
}

FName UNiagaraSimulationStageGeneric::GetStackContextReplacementName() const 
{
	if (IterationSource == ENiagaraIterationSource::Particles)
		return NAME_None;
	else if (IterationSource == ENiagaraIterationSource::DataInterface)
		return DataInterface.BoundVariable.GetName();
	ensureMsgf(false, TEXT("Should not get here! Need to handle unknown case!"));
	return NAME_None;
}
#endif