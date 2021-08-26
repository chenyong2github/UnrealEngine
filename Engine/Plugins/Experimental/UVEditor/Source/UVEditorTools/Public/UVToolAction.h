// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "InteractionMechanic.h"

#include "UVToolAction.generated.h"

class UUVEditorToolMeshInput;
class UUVToolEmitChangeAPI;


// TODO: Should this be spread out across multiple files?

UCLASS()
class UVEDITORTOOLS_API UUVToolAction : public UInteractionMechanic
{
	GENERATED_BODY()
public:
	virtual void Initialize(UWorld* WorldIn, const TArray<TObjectPtr<UUVEditorToolMeshInput>>& TargetsIn)
	{
		World = WorldIn;
		Targets = TargetsIn;
	}		
	virtual void Shutdown() override {
		UInteractionMechanic::Shutdown();

		World = nullptr;

		for(TObjectPtr<UUVEditorToolMeshInput>& Target : Targets)
		{
			Target = nullptr;
		}
	}
	virtual bool ExecuteAction(UUVToolEmitChangeAPI& EmitChangeAPI) { return true; };
	virtual void UpdateVisualizations() {};

protected:

	UPROPERTY()
	TWeakObjectPtr<UWorld> World;

	UPROPERTY()
	TArray<TObjectPtr<UUVEditorToolMeshInput>> Targets;
};
