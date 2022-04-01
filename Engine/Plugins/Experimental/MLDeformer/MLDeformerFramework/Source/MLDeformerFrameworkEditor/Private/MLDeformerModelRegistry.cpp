// Copyright Epic Games, Inc. All Rights Reserved.

#include "MLDeformerModelRegistry.h"
#include "MLDeformerModel.h"
#include "MLDeformerEditorModel.h"

namespace UE::MLDeformer
{
	FMLDeformerEditorModelRegistry::~FMLDeformerEditorModelRegistry()
	{
		Map.Empty();
		InstanceMap.Empty();
	}

	void FMLDeformerEditorModelRegistry::RegisterEditorModel(UClass* ModelType, FOnGetEditorModelInstance Delegate)
	{
		Map.FindOrAdd(ModelType, Delegate);
	}

	void FMLDeformerEditorModelRegistry::UnregisterEditorModel(const UClass* ModelType)
	{
		if (Map.Contains(ModelType))
		{
			Map.Remove(ModelType);
		}
	}

	FMLDeformerEditorModel* FMLDeformerEditorModelRegistry::CreateEditorModel(UMLDeformerModel* Model)
	{
		FMLDeformerEditorModel* EditorModel = nullptr;
		FOnGetEditorModelInstance* Delegate = Map.Find(Model->GetClass());
		if (Delegate)
		{
			EditorModel = Delegate->Execute();
			InstanceMap.FindOrAdd(Model, EditorModel);
		}

		return EditorModel;
	}

	FMLDeformerEditorModel* FMLDeformerEditorModelRegistry::GetEditorModel(UMLDeformerModel* Model)
	{
		FMLDeformerEditorModel** EditorModel = InstanceMap.Find(Model);
		if (EditorModel)
		{
			return *EditorModel;
		}

		return nullptr;
	}

	void FMLDeformerEditorModelRegistry::RemoveEditorModelInstance(FMLDeformerEditorModel* EditorModel)
	{
		InstanceMap.Remove(EditorModel->GetModel());
	}
}	// namespace UE::MLDeformer
