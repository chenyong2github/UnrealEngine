// Copyright Epic Games, Inc. All Rights Reserved.
#include "NeuralMorphModel.h"
#include "NeuralMorphModelVizSettings.h"
#include "NeuralMorphModelInstance.h"
#include "NeuralMorphInputInfo.h"
#include "MLDeformerAsset.h"
#include "MLDeformerComponent.h"
#include "UObject/UObjectGlobals.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(NeuralMorphModel)

#define LOCTEXT_NAMESPACE "NeuralMorphModel"

// Implement our module.
namespace UE::NeuralMorphModel
{
	class NEURALMORPHMODEL_API FNeuralMorphModelModule
		: public IModuleInterface
	{
		void StartupModule() override
		{
			#if NEURALMORPHMODEL_FORCE_USE_NNI
				UE_LOG(LogNeuralMorphModel, Warning, TEXT("Running neural morph model with NNI. The faster custom inference code path will be disabled."));
			#endif
		}
	};
}
IMPLEMENT_MODULE(UE::NeuralMorphModel::FNeuralMorphModelModule, NeuralMorphModel)

// Our log category for this model.
NEURALMORPHMODEL_API DEFINE_LOG_CATEGORY(LogNeuralMorphModel)

//////////////////////////////////////////////////////////////////////////////

UNeuralMorphModel::UNeuralMorphModel(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Create the visualization settings for this model.
	// Never directly create one of the frameworks base classes such as the FMLDeformerMorphModelVizSettings as
	// that can cause issues with detail customizations.
#if WITH_EDITORONLY_DATA
	SetVizSettings(ObjectInitializer.CreateEditorOnlyDefaultSubobject<UNeuralMorphModelVizSettings>(this, TEXT("VizSettings")));
#endif
}

UMLDeformerModelInstance* UNeuralMorphModel::CreateModelInstance(UMLDeformerComponent* Component)
{
	return NewObject<UNeuralMorphModelInstance>(Component);
}

UMLDeformerInputInfo* UNeuralMorphModel::CreateInputInfo()
{
	return NewObject<UNeuralMorphInputInfo>(this);
}

void UNeuralMorphModel::Serialize(FArchive& Archive)
{
#if !NEURALMORPHMODEL_FORCE_USE_NNI
	if (Archive.IsSaving() || Archive.IsCooking())
	{
		UNeuralNetwork* NNINeuralNetwork = GetNeuralNetwork();

		// Show a warning when we are not using custom inference yet on this model.
		if (NeuralMorphNetwork == nullptr && NNINeuralNetwork != nullptr)
		{
			UE_LOG(LogNeuralMorphModel, Display, TEXT("Neural Morph Model in MLD asset '%s' should be retrained to get higher performance by taking advantage of custom inference."), *GetDeformerAsset()->GetName());
		}

		// If we have a custom inference network, make sure we don't save out the NNI network.
		if (NeuralMorphNetwork != nullptr && NNINeuralNetwork != nullptr)
		{
			SetNeuralNetwork(nullptr);
		}
	}
#endif

	// Convert the UMLDeformerInputInfo object into a UNeuralMorphInputInfo object.
	UMLDeformerInputInfo* CurInputInfo = GetInputInfo();
	if (CurInputInfo)
	{
		if (!CurInputInfo->IsA<UNeuralMorphInputInfo>())
		{
			UNeuralMorphInputInfo* NeuralMorphInputInfo = Cast<UNeuralMorphInputInfo>(CreateInputInfo());
			NeuralMorphInputInfo->CopyMembersFrom(CurInputInfo);
			SetInputInfo(NeuralMorphInputInfo);
		}		
	}

	Super::Serialize(Archive);
}

void UNeuralMorphModel::PostLoad()
{
	Super::PostLoad();

#if !NEURALMORPHMODEL_FORCE_USE_NNI
	// Show a warning when we are not using custom inference yet on this model.
	if (NeuralMorphNetwork == nullptr && GetNeuralNetwork() != nullptr)
	{
		UE_LOG(LogNeuralMorphModel, Display, TEXT("Neural Morph Model in MLD asset '%s' should be retrained to get higher performance by taking advantage of custom inference."), *GetDeformerAsset()->GetName());
	}
#endif
}

#undef LOCTEXT_NAMESPACE
