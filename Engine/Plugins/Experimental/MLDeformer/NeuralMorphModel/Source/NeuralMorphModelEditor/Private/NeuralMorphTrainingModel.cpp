// Copyright Epic Games, Inc. All Rights Reserved.

#include "NeuralMorphTrainingModel.h"
#include "NeuralMorphModel.h"
#include "NeuralMorphEditorModel.h"
#include "MLDeformerEditorModel.h"
#include "Misc/ScopedSlowTask.h"
#include "Math/NumericLimits.h"

#define LOCTEXT_NAMESPACE "NeuralMorphTrainingModel"

UNeuralMorphModel* UNeuralMorphTrainingModel::GetNeuralMorphModel() const
{
	return Cast<UNeuralMorphModel>(GetModel());
}

UE::NeuralMorphModel::FNeuralMorphEditorModel* UNeuralMorphTrainingModel::GetNeuralMorphEditorModel() const
{
	return static_cast<UE::NeuralMorphModel::FNeuralMorphEditorModel*>(EditorModel);
}

#undef LOCTEXT_NAMESPACE
