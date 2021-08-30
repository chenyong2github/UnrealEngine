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
	virtual void SetWorld(UWorld* WorldIn) {}

	virtual void SetTargets(const TArray<TObjectPtr<UUVEditorToolMeshInput>>& TargetsIn)
	{
		Targets = TargetsIn;
	}

	virtual void Shutdown() override {
		UInteractionMechanic::Shutdown();

		for(TObjectPtr<UUVEditorToolMeshInput>& Target : Targets)
		{
			Target = nullptr;
		}
	}
	virtual bool ExecuteAction(UUVToolEmitChangeAPI& EmitChangeAPI) { return true; };
	virtual void UpdateVisualizations() {};

protected:

	UPROPERTY()
	TArray<TObjectPtr<UUVEditorToolMeshInput>> Targets;
};
