// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UMLDeformerModel;

namespace UE::MLDeformer
{
	class FMLDeformerEditorModel;

	DECLARE_DELEGATE_RetVal(FMLDeformerEditorModel*, FOnGetEditorModelInstance);

	class MLDEFORMERFRAMEWORKEDITOR_API FMLDeformerEditorModelRegistry
	{
	public:
		~FMLDeformerEditorModelRegistry();

		void RegisterEditorModel(UClass* ModelType, FOnGetEditorModelInstance Delegate);
		void UnregisterEditorModel(const UClass* ModelType);
		void RemoveEditorModelInstance(FMLDeformerEditorModel* EditorModel);

		FMLDeformerEditorModel* CreateEditorModel(UMLDeformerModel* Model);
		FMLDeformerEditorModel* GetEditorModel(UMLDeformerModel* Model);

		int GetNumRegisteredModels() const { return Map.Num(); }
		int GetNumInstancedModels() const { return InstanceMap.Num(); }
		const TMap<UClass*, FOnGetEditorModelInstance>& GetRegisteredModels() const { return Map; }
		const TMap<UMLDeformerModel*, FMLDeformerEditorModel*>& GetModelInstances() const { return InstanceMap; }

	private:
		TMap<UClass*, FOnGetEditorModelInstance> Map;
		TMap<UMLDeformerModel*, FMLDeformerEditorModel*> InstanceMap;
	};
}	// namespace UE::MLDeformer
