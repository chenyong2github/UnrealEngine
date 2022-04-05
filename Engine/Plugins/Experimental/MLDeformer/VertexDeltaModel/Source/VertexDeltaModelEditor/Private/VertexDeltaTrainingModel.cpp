// Copyright Epic Games, Inc. All Rights Reserved.

#include "VertexDeltaTrainingModel.h"
#include "VertexDeltaModel.h"
#include "VertexDeltaEditorModel.h"
#include "MLDeformerEditorModel.h"
#include "Misc/ScopedSlowTask.h"
#include "Math/NumericLimits.h"

#define LOCTEXT_NAMESPACE "VertexDeltaTrainingModel"

UVertexDeltaModel* UVertexDeltaTrainingModel::GetVertexDeltaModel() const
{
	return Cast<UVertexDeltaModel>(GetModel());
}

UE::VertexDeltaModel::FVertexDeltaEditorModel* UVertexDeltaTrainingModel::GetVertexDeltaEditorModel() const
{
	return static_cast<UE::VertexDeltaModel::FVertexDeltaEditorModel*>(EditorModel);
}

#undef LOCTEXT_NAMESPACE
