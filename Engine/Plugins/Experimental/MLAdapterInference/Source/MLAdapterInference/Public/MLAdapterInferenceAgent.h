// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Agents/MLAdapterAgent.h"
#include "NeuralNetwork.h"
#include "MLAdapterInferenceAgent.generated.h"

UCLASS(Blueprintable, EditInlineNew)
class MLADAPTERINFERENCE_API UMLAdapterInferenceAgent : public UMLAdapterAgent
{
	GENERATED_BODY()
public:
	virtual void Think(const float DeltaTime) override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InferenceAgent)
	UNeuralNetwork* Brain;
};
