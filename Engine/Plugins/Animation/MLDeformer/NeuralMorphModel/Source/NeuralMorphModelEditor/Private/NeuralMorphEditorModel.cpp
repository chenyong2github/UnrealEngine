// Copyright Epic Games, Inc. All Rights Reserved.

#include "NeuralMorphEditorModel.h"
#include "IDetailsView.h"
#include "NeuralMorphModel.h"
#include "NeuralMorphNetwork.h"
#include "MLDeformerMorphModelVizSettings.h"
#include "NeuralMorphTrainingModel.h"
#include "MLDeformerEditorToolkit.h"

#define LOCTEXT_NAMESPACE "NeuralMorphEditorModel"

namespace UE::NeuralMorphModel
{
	using namespace UE::MLDeformer;

	FMLDeformerEditorModel* FNeuralMorphEditorModel::MakeInstance()
	{
		return new FNeuralMorphEditorModel();
	}

	void FNeuralMorphEditorModel::OnPropertyChanged(FPropertyChangedEvent& PropertyChangedEvent)
	{
		const FProperty* Property = PropertyChangedEvent.Property;
		if (Property == nullptr)
		{
			return;
		}

		// Process the base class property changes.
		FMLDeformerMorphModelEditorModel::OnPropertyChanged(PropertyChangedEvent);

		// Handle property changes of this model.
		if (Property->GetFName() == GET_MEMBER_NAME_CHECKED(UNeuralMorphModel, Mode))
		{
			if (PropertyChangedEvent.ChangeType == EPropertyChangeType::ValueSet)
			{
				SetResamplingInputOutputsNeeded(true);	// Needed as local mode doesn't support curves, while the global mode does.
				GetEditor()->GetModelDetailsView()->ForceRefresh();
			}
		}
	}

	bool FNeuralMorphEditorModel::IsTrained() const
	{
#if NEURALMORPHMODEL_FORCE_USE_NNI
		return GetMorphModel()->GetNeuralNetwork() != nullptr;
#else
		return (GetMorphModel()->GetNeuralNetwork() != nullptr) || (GetNeuralMorphModel()->GetNeuralMorphNetwork() != nullptr);
#endif
	}

	ETrainingResult FNeuralMorphEditorModel::Train()
	{
		return TrainModel<UNeuralMorphTrainingModel>(this);
	}

	bool FNeuralMorphEditorModel::LoadTrainedNetwork() const
	{
#if NEURALMORPHMODEL_FORCE_USE_NNI
		const bool bSuccess = FMLDeformerMorphModelEditorModel::LoadTrainedNetwork();
		if (bSuccess)
		{
			GetNeuralMorphModel()->SetNeuralMorphNetwork(nullptr);	// Force disable custom inference.
		}
		return bSuccess;
#else
		// Load the specialized neural morph model network.
		// Base the filename on the onnx filename, and replace the file extension.
		FString NetworkFilename = GetTrainedNetworkOnnxFile();
		NetworkFilename.RemoveFromEnd(TEXT("onnx"));
		NetworkFilename += TEXT("nmn");

		// Load the actual network.
		UNeuralMorphNetwork* NeuralNet = NewObject<UNeuralMorphNetwork>(Model);
		if (!NeuralNet->Load(NetworkFilename))
		{
			UE_LOG(LogNeuralMorphModel, Error, TEXT("Failed to load neural morph network from file '%s'!"), *NetworkFilename);
			NeuralNet = nullptr;

			// Restore the deltas to the ones before training started.
			GetMorphModel()->SetMorphTargetDeltas(MorphTargetDeltasBackup);
			return false;
		}

		if (NeuralNet->IsEmpty())
		{
			NeuralNet = nullptr;
		}

		GetNeuralMorphModel()->SetNeuralNetwork(nullptr);			// Disable NNI inference.
		GetNeuralMorphModel()->SetNeuralMorphNetwork(NeuralNet);	// Use our custom inference.
#endif
		return true;
	}
}	// namespace UE::NeuralMorphModel

#undef LOCTEXT_NAMESPACE
