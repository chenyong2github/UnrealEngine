// Copyright Epic Games, Inc. All Rights Reserved.

#include "MLDeformerPythonTrainingModel.h"
#include "UObject/UObjectHash.h"
#include "MLDeformerAsset.h"
#include "MLDeformerEditorData.h"
#include "MLPytorchDataSetInterface.h"

UMLDeformerPythonTrainingModel::UMLDeformerPythonTrainingModel()
{
}

UMLDeformerPythonTrainingModel* UMLDeformerPythonTrainingModel::Get()
{
	TArray<UClass*> PythonTrainingModels;
	GetDerivedClasses(UMLDeformerPythonTrainingModel::StaticClass(), PythonTrainingModels);
	const int32 NumClasses = PythonTrainingModels.Num();
	if (NumClasses > 0)
	{
		return Cast<UMLDeformerPythonTrainingModel>(PythonTrainingModels[NumClasses - 1]->GetDefaultObject());
	}
	return nullptr;
}

UMLDeformerAsset* UMLDeformerPythonTrainingModel::GetDeformerAsset()
{
	return EditorData->GetDeformerAsset();
}

void UMLDeformerPythonTrainingModel::CreateDataSetInterface()
{
	if (DataSetInterface == nullptr)
	{
		DataSetInterface = NewObject<UMLPytorchDataSetInterface>();
	}
	DataSetInterface->SetEditorData(EditorData);
}

void UMLDeformerPythonTrainingModel::SetEditorData(TSharedPtr<FMLDeformerEditorData> InEditorData)
{
	EditorData = InEditorData;
}
