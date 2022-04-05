// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MLDeformerTrainingModel.h"
#include "VertexDeltaTrainingModel.generated.h"

namespace UE::VertexDeltaModel
{
	class FVertexDeltaEditorModel;
}

class UVertexDeltaModel;

UCLASS(Blueprintable)
class VERTEXDELTAMODELEDITOR_API UVertexDeltaTrainingModel
	: public UMLDeformerTrainingModel
{
	GENERATED_BODY()

public:
	/** Main training function, with implementation in python. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Python")
	int32 Train() const;

	UVertexDeltaModel* GetVertexDeltaModel() const;
	UE::VertexDeltaModel::FVertexDeltaEditorModel* GetVertexDeltaEditorModel() const;
};
