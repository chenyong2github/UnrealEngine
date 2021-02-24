// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NiagaraDebuggerCommon.h"
#include "NiagaraOutliner.generated.h"

/** Filters used in the Niagara Outliner. */
USTRUCT()
struct FNiagaraOutlinerFilters
{
	GENERATED_BODY()
		
	UPROPERTY(EditAnywhere, Category = "Filters", meta = (InlineEditConditionToggle))
	uint32 bFilterBySystemExecutionState : 1;

	UPROPERTY(EditAnywhere, Category = "Filters", meta = (InlineEditConditionToggle))
	uint32 bFilterByEmitterExecutionState : 1;
	
	UPROPERTY(EditAnywhere, Category = "Filters", meta = (InlineEditConditionToggle))
	uint32 bFilterByEmitterSimTarget : 1;

	/** Only show systems with the following execution state. */
	UPROPERTY(EditAnywhere, Config, Category="Filters", meta = (EditCondition = "bFilterBySystemExecutionState"))
	ENiagaraExecutionState SystemExecutionState;

	/** Only show emitters with the following execution state. */
	UPROPERTY(EditAnywhere, Config, Category="Filters", meta = (EditCondition = "bFilterByEmitterExecutionState"))
	ENiagaraExecutionState EmitterExecutionState;

	/** Only show emitters with this SimTarget. */
	UPROPERTY(EditAnywhere, Config, Category = "Filters", meta = (EditCondition = "bFilterByEmitterSimTarget"))
	ENiagaraSimTarget EmitterSimTarget;

	//TODO: Capture system culled / scalability state to filter by culled and possibly reasons for cull.	
// 	UPROPERTY(EditAnywhere, Config, Category = "Filters")
// 	uint32 bCulledOnly : 1;
};

UCLASS(config = EditorPerProjectUserSettings, defaultconfig)
class UNiagaraOutliner : public UObject
{
	GENERATED_UCLASS_BODY()

public:
	DECLARE_MULTICAST_DELEGATE(FOnChanged);
	
	FOnChanged OnSettingsChangedDelegate;
	FOnChanged OnDataChangedDelegate;

	//UObject Interface
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)override;
	//UObject Interface END

	void UpdateData(const FNiagaraOutlinerData& NewData);

	const FNiagaraOutlinerWorldData* FindWorldData(const FString& WorldName);
	const FNiagaraOutlinerSystemData* FindSystemData(const FString& WorldName, const FString& SystemName);
	const FNiagaraOutlinerSystemInstanceData* FindComponentData(const FString& WorldName, const FString& SystemName, const FString& ComponentName);
	const FNiagaraOutlinerEmitterInstanceData* FindEmitterData(const FString& WorldName, const FString& SystemName, const FString& ComponentName, const FString& EmitterName);

	const void GetAllSystemNames(TArray<FString>& OutSystems);
	const void GetAllComponentNames(TArray<FString>& OutSystems);
	const void GetAllEmitterNames(TArray<FString>& OutSystems);

	UPROPERTY(EditAnywhere, Category="Settings")
	FNiagaraOutlinerSettings Settings;

	UPROPERTY(EditAnywhere, Category = "Filters")
	FNiagaraOutlinerFilters Filters;

	UPROPERTY(VisibleAnywhere, Category="Outliner", Transient)
	FNiagaraOutlinerData Data;
};