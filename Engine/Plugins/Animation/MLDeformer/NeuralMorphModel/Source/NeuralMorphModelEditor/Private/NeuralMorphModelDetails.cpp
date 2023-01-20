// Copyright Epic Games, Inc. All Rights Reserved.

#include "NeuralMorphModelDetails.h"
#include "NeuralMorphModel.h"
#include "NeuralMorphInputInfo.h"
#include "NeuralMorphEditorModel.h"
#include "DetailCategoryBuilder.h"
#include "DetailWidgetRow.h"

#include "Widgets/Layout/SBox.h"
#include "Widgets/Input/SButton.h"
#include "SWarningOrErrorBox.h"


#define LOCTEXT_NAMESPACE "NeuralMorphModelDetails"

namespace UE::NeuralMorphModel
{
	using namespace UE::MLDeformer;

	TSharedRef<IDetailCustomization> FNeuralMorphModelDetails::MakeInstance()
	{
		return MakeShareable(new FNeuralMorphModelDetails());
	}

	void FNeuralMorphModelDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
	{
		// Create all the detail categories and add the properties of the base class.
		FMLDeformerMorphModelDetails::CustomizeDetails(DetailBuilder);

		UNeuralMorphModel* NeuralMorphModel = Cast<UNeuralMorphModel>(Model);
		check(NeuralMorphModel);

		TrainingSettingsCategoryBuilder->AddProperty(GET_MEMBER_NAME_CHECKED(UNeuralMorphModel, Mode), UNeuralMorphModel::StaticClass());

		// Local mode settings.
		TrainingSettingsCategoryBuilder->AddProperty(GET_MEMBER_NAME_CHECKED(UNeuralMorphModel, LocalNumMorphTargetsPerBone), UNeuralMorphModel::StaticClass())
			.Visibility(NeuralMorphModel->Mode == ENeuralMorphMode::Local ? EVisibility::Visible : EVisibility::Collapsed);
		TrainingSettingsCategoryBuilder->AddProperty(GET_MEMBER_NAME_CHECKED(UNeuralMorphModel, LocalNumHiddenLayers), UNeuralMorphModel::StaticClass())
			.Visibility(NeuralMorphModel->Mode == ENeuralMorphMode::Local ? EVisibility::Visible : EVisibility::Collapsed);
		TrainingSettingsCategoryBuilder->AddProperty(GET_MEMBER_NAME_CHECKED(UNeuralMorphModel, LocalNumNeuronsPerLayer), UNeuralMorphModel::StaticClass())
			.Visibility(NeuralMorphModel->Mode == ENeuralMorphMode::Local ? EVisibility::Visible : EVisibility::Collapsed);

		// Global mode settings.
		TrainingSettingsCategoryBuilder->AddProperty(GET_MEMBER_NAME_CHECKED(UNeuralMorphModel, GlobalNumMorphTargets), UNeuralMorphModel::StaticClass())
			.Visibility(NeuralMorphModel->Mode == ENeuralMorphMode::Global ? EVisibility::Visible : EVisibility::Collapsed);
		TrainingSettingsCategoryBuilder->AddProperty(GET_MEMBER_NAME_CHECKED(UNeuralMorphModel, GlobalNumHiddenLayers), UNeuralMorphModel::StaticClass())
			.Visibility(NeuralMorphModel->Mode == ENeuralMorphMode::Global ? EVisibility::Visible : EVisibility::Collapsed);
		TrainingSettingsCategoryBuilder->AddProperty(GET_MEMBER_NAME_CHECKED(UNeuralMorphModel, GlobalNumNeuronsPerLayer), UNeuralMorphModel::StaticClass())
			.Visibility(NeuralMorphModel->Mode == ENeuralMorphMode::Global ? EVisibility::Visible : EVisibility::Collapsed);

		TrainingSettingsCategoryBuilder->AddProperty(GET_MEMBER_NAME_CHECKED(UNeuralMorphModel, NumIterations), UNeuralMorphModel::StaticClass());
		TrainingSettingsCategoryBuilder->AddProperty(GET_MEMBER_NAME_CHECKED(UNeuralMorphModel, BatchSize), UNeuralMorphModel::StaticClass());
		TrainingSettingsCategoryBuilder->AddProperty(GET_MEMBER_NAME_CHECKED(UNeuralMorphModel, LearningRate), UNeuralMorphModel::StaticClass());
		TrainingSettingsCategoryBuilder->AddProperty(GET_MEMBER_NAME_CHECKED(UNeuralMorphModel, LearningRateDecay), UNeuralMorphModel::StaticClass());
		TrainingSettingsCategoryBuilder->AddProperty(GET_MEMBER_NAME_CHECKED(UNeuralMorphModel, RegularizationFactor), UNeuralMorphModel::StaticClass());
	}

	void FNeuralMorphModelDetails::AddTrainingInputFilters()
	{
		FMLDeformerMorphModelDetails::AddTrainingInputFilters();

		UNeuralMorphModel* NeuralMorphModel = Cast<UNeuralMorphModel>(Model);
		check(NeuralMorphModel);

		InputOutputCategoryBuilder->AddProperty(GET_MEMBER_NAME_CHECKED(UNeuralMorphModel, BoneGroups), UNeuralMorphModel::StaticClass())
			.Visibility(NeuralMorphModel->Mode == ENeuralMorphMode::Local ? EVisibility::Visible : EVisibility::Collapsed);

		InputOutputCategoryBuilder->AddProperty(GET_MEMBER_NAME_CHECKED(UNeuralMorphModel, CurveGroups), UNeuralMorphModel::StaticClass())
			.Visibility(NeuralMorphModel->Mode == ENeuralMorphMode::Local ? EVisibility::Visible : EVisibility::Collapsed);
	}

	void FNeuralMorphModelDetails::AddTrainingSettingsErrors()
	{
		FMLDeformerMorphModelDetails::AddTrainingSettingsErrors();

		UNeuralMorphModel* NeuralMorphModel = Cast<UNeuralMorphModel>(Model);
		check(NeuralMorphModel);

		#if !NEURALMORPHMODEL_FORCE_USE_NNI
			const bool bShowWarning = (NeuralMorphModel->GetNeuralNetwork() != nullptr) && (NeuralMorphModel->GetNeuralMorphNetwork() == nullptr);
			FDetailWidgetRow& PerformanceWarningRow = TrainingSettingsCategoryBuilder->AddCustomRow(FText::FromString("NeuralNetPerformanceWarning"))
				.Visibility(bShowWarning ? EVisibility::Visible : EVisibility::Collapsed)
				.WholeRowContent()
				[
					SNew(SBox)
					.Padding(FMargin(0.0f, 4.0f))
					[
						SNew(SWarningOrErrorBox)
						.MessageStyle(EMessageStyle::Warning)
						.Message(LOCTEXT("NeuralNetPerformanceWarning", "The model must be retrained in order to make use of higher performance inference."))
					]
				];
		#else
			const bool bShowWarning = (NeuralMorphModel->GetNeuralNetwork() == nullptr) && (NeuralMorphModel->GetNeuralMorphNetwork() != nullptr);
			FDetailWidgetRow& PerformanceWarningRow = TrainingSettingsCategoryBuilder->AddCustomRow(FText::FromString("NeuralNetWrongInferenceWarning"))
				.Visibility(bShowWarning ? EVisibility::Visible : EVisibility::Collapsed)
				.WholeRowContent()
				[
					SNew(SBox)
					.Padding(FMargin(0.0f, 4.0f))
					[
						SNew(SWarningOrErrorBox)
						.MessageStyle(EMessageStyle::Warning)
						.Message(LOCTEXT("NeuralNetWrongInferenceWarning", "The model was trained using custom inference, but NNI is set as active inference engine. Please retrain the model or switch back to custom inference by recompiling the plugin."))
					]
				];
		#endif
	}
}	// namespace UE::NeuralMorphModel

#undef LOCTEXT_NAMESPACE
