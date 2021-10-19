// Copyright Epic Games, Inc. All Rights Reserved.

#include "MLDeformerPythonTrainingModel.h"
#include "UObject/UObjectHash.h"
#include "MLDeformerAsset.h"
#include "MLDeformerEditorData.h"
#include "MLDeformerFrameCache.h"
#include "MLPytorchDataSetInterface.h"

UMLDeformerPythonTrainingModel::UMLDeformerPythonTrainingModel()
{
}

UMLDeformerPythonTrainingModel::~UMLDeformerPythonTrainingModel()
{
	Clear();
}

// Trigger the frame cache to be deleted.
void UMLDeformerPythonTrainingModel::Clear()
{
	if (DataSetInterface)
	{
		DataSetInterface->Clear();
	}
	EditorData.Reset();
	FrameCache.Reset();
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
	DataSetInterface->SetFrameCache(FrameCache);
}

void UMLDeformerPythonTrainingModel::SetEditorData(TSharedPtr<FMLDeformerEditorData> InEditorData)
{
	EditorData = InEditorData;
}

void UMLDeformerPythonTrainingModel::SetFrameCache(TSharedPtr<FMLDeformerFrameCache> InFrameCache)
{
	FrameCache = InFrameCache;
}
