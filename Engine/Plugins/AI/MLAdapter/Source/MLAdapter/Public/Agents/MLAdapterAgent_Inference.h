// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Agents/MLAdapterAgent.h"
#include "NeuralNetwork.h"
#include "MLAdapterAgent_Inference.generated.h"

/**
 * Inference agents have a neural network that can process senses and output actuations via their Think method. You 
 * can create a blueprint of this class to easily wire-up an agent that functions entirely inside the Unreal Engine.
 * Typically, this class would be used in conjunction with MLAdapterNoRPCManager.
 */
UCLASS(Blueprintable, EditInlineNew)
class MLADAPTER_API UMLAdapterAgent_Inference : public UMLAdapterAgent
{
	GENERATED_BODY()
public:
	virtual void PostInitProperties() override;

	virtual void Think(const float DeltaTime) override;

	UPROPERTY(EditDefaultsOnly, Category = MLAdapter, meta = (AllowedClasses = "NeuralNetwork"))
	FSoftObjectPath NeuralNetworkPath;

	UPROPERTY()
	UNeuralNetwork* Brain;
};
