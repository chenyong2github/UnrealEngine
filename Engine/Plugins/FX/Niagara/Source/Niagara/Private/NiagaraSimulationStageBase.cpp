// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraSimulationStageBase.h"
#include "NiagaraEmitter.h"
#include "NiagaraSystem.h"
#include "NiagaraScriptSourceBase.h"


bool UNiagaraSimulationStageGeneric::AppendCompileHash(FNiagaraCompileHashVisitor* InVisitor) const 
{
#if WITH_EDITORONLY_DATA
	int32 Index = InVisitor->Values.AddDefaulted();
	InVisitor->Values[Index].Object = FString::Printf(TEXT("Class: \"%s\"  Name: \"%s\""), *GetClass()->GetName(), *GetName());
#endif

	InVisitor->UpdatePOD(TEXT("Iterations"), Iterations);
	InVisitor->UpdatePOD(TEXT("IterationSource"), (int32)IterationSource);
	InVisitor->UpdatePOD(TEXT("bSpawnOnly"), bSpawnOnly ? 1 : 0);
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

	UNiagaraEmitter* Emitter = Cast< UNiagaraEmitter>(GetOuter());

	bool bNeedsRecompile = false;
	if (PropertyName == GET_MEMBER_NAME_CHECKED(UNiagaraSimulationStageGeneric, Iterations) && Emitter)
	{
		bNeedsRecompile = true;
	}
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(UNiagaraSimulationStageGeneric, IterationSource) && Emitter)
	{
		bNeedsRecompile = true;
	}
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(UNiagaraSimulationStageGeneric, bSpawnOnly) && Emitter)
	{
		bNeedsRecompile = true;
	}
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(UNiagaraSimulationStageGeneric, DataInterface) && Emitter)
	{
		bNeedsRecompile = true;
	}
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(UNiagaraSimulationStageGeneric, SimulationStageName) && Emitter)
	{
		bNeedsRecompile = true;
	}

#if WITH_EDITORONLY_DATA
	if (bNeedsRecompile)
	{
		UNiagaraScriptSourceBase* GraphSource = Emitter->UpdateScriptProps.Script->GetSource();
		if (GraphSource != nullptr)
		{
			GraphSource->MarkNotSynchronized(TEXT("SimulationStageGeneric changed."));
		}

		UNiagaraSystem::RequestCompileForEmitter(Emitter);
	}
#endif
}
#endif