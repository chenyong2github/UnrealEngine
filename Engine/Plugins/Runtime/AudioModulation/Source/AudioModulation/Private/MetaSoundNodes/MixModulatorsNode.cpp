// Copyright Epic Games, Inc. All Rights Reserved.
#include "IAudioModulation.h"
#include "MetasoundDataFactory.h"
#include "MetasoundExecutableOperator.h"
#include "MetasoundFacade.h"
#include "MetasoundNodeRegistrationMacro.h"
#include "MetasoundPrimitives.h"
#include "SoundModulatorAsset.h"

#define LOCTEXT_NAMESPACE "AudioModulationNodes"

namespace AudioModulation
{
	class FMixModulatorsNodeOperator : public Metasound::TExecutableOperator<FMixModulatorsNodeOperator>
	{
	public:
		static const Metasound::FVertexInterface& GetDefaultInterface()
		{
			static const Metasound::FVertexInterface DefaultInterface(
				Metasound::FInputVertexInterface(
					Metasound::TInputDataVertexModel<FSoundModulatorAsset>("In1", LOCTEXT("MixModulatorsNode_InputModulator1Name", "In 1")),
					Metasound::TInputDataVertexModel<FSoundModulatorAsset>("In2", LOCTEXT("MixModulatorsNode_InputModulator2Name", "In 2")),
					Metasound::TInputDataVertexModel<FSoundModulationParameterAsset>("MixParameter", LOCTEXT("MixModulatorsNode_InputMixParameterName", "Mix Parameter")),
					Metasound::TInputDataVertexModel<bool>("Normalized", LOCTEXT("MixModulatorsNode_InputNormalizedName", "Normalized"), true)
				),
				Metasound::FOutputVertexInterface(
					Metasound::TOutputDataVertexModel<float>("Out", LOCTEXT("MixModulatorsNode_OutputModulatorValue", "Out"))
				)
			);

			return DefaultInterface;
		}

		static const Metasound::FNodeClassMetadata& GetNodeInfo()
		{
			auto CreateNodeClassMetadata = []() -> Metasound::FNodeClassMetadata
			{
				Metasound::FNodeClassMetadata Metadata
				{
					Metasound::FNodeClassName { "Modulation", "MixModulators", "" },
					1, // Major Version
					0, // Minor Version
					LOCTEXT("MixModulatorsNode_Name", "Mix Modulators"),
					LOCTEXT("MixModulatorsNode_Description", "Mixes two modulators using the parameterized mix function. Returns the 'Normalized' value (0-1) if true, or the value in unit space (dB, Frequency, etc.) if set to false."),
					AudioModulation::PluginAuthor,
					AudioModulation::PluginNodeMissingPrompt,
					GetDefaultInterface(),
					{ },
					{ },
					{ }
				};

				return Metadata;
			};

			static const Metasound::FNodeClassMetadata Metadata = CreateNodeClassMetadata();
			return Metadata;
		}

		static TUniquePtr<Metasound::IOperator> CreateOperator(const Metasound::FCreateOperatorParams& InParams, TArray<TUniquePtr<Metasound::IOperatorBuildError>>& OutErrors)
		{
			using namespace Metasound;

			const FInputVertexInterface& InputInterface = InParams.Node.GetVertexInterface().GetInputInterface();
			const FDataReferenceCollection& InputCollection = InParams.InputDataReferences;

			FSoundModulatorAssetReadRef Modulator1ReadRef = InputCollection.GetDataReadReferenceOrConstruct<FSoundModulatorAsset>("In1");
			FSoundModulatorAssetReadRef Modulator2ReadRef = InputCollection.GetDataReadReferenceOrConstruct<FSoundModulatorAsset>("In2");
			FSoundModulationParameterAssetReadRef ParameterReadRef = InputCollection.GetDataReadReferenceOrConstruct<FSoundModulationParameterAsset>("MixParameter");
			FBoolReadRef NormalizedReadRef = InputCollection.GetDataReadReferenceOrConstructWithVertexDefault<bool>(InputInterface, "Normalized", InParams.OperatorSettings);

			return MakeUnique<FMixModulatorsNodeOperator>(InParams.OperatorSettings, Modulator1ReadRef, Modulator2ReadRef, ParameterReadRef, NormalizedReadRef);
		}

		FMixModulatorsNodeOperator(
			const Metasound::FOperatorSettings& InSettings,
			const FSoundModulatorAssetReadRef& InModulator1,
			const FSoundModulatorAssetReadRef& InModulator2,
			const FSoundModulationParameterAssetReadRef& InParameter,
			const Metasound::FBoolReadRef& InNormalized)
			: Modulator1(InModulator1)
			, Modulator2(InModulator2)
			, Normalized(InNormalized)
			, Parameter(InParameter)
			, OutValue(Metasound::TDataWriteReferenceFactory<float>::CreateAny(InSettings))
		{
		}

		virtual ~FMixModulatorsNodeOperator() = default;

		virtual Metasound::FDataReferenceCollection GetInputs() const override
		{
			using namespace Metasound;

			FDataReferenceCollection Inputs;
			Inputs.AddDataReadReference("In1", Modulator1);
			Inputs.AddDataReadReference("In2", Modulator2);
			Inputs.AddDataReadReference("MixParameter", Parameter);
			Inputs.AddDataReadReference("Normalized", Normalized);

			return Inputs;
		}

		virtual Metasound::FDataReferenceCollection GetOutputs() const override
		{
			using namespace Metasound;

			FDataReferenceCollection Outputs;
			Outputs.AddDataReadReference("Out", FFloatReadRef(OutValue));

			return Outputs;
		}

		void Execute()
		{
			float Value1 = 1.0f;
			const FSoundModulatorAsset& ModulatorAsset1 = *Modulator1;
			if (ModulatorAsset1.IsValid())
			{
				Value1 = ModulatorAsset1->GetValue();
			}

			float Value2 = 1.0f;
			const FSoundModulatorAsset& ModulatorAsset2 = *Modulator2;
			if (ModulatorAsset2.IsValid())
			{
				Value2 = ModulatorAsset2->GetValue();
			}

			const FSoundModulationParameterAsset& ParameterAsset = *Parameter;
			if (ParameterAsset.IsValid())
			{
				ParameterAsset->GetParameter().MixFunction(Value1, Value2);
			}
			else
			{
				Audio::FModulationParameter::GetDefaultMixFunction()(Value1, Value2);
			}

			*OutValue = Value1;
		}

	private:
		FSoundModulatorAssetReadRef Modulator1;
		FSoundModulatorAssetReadRef Modulator2;
		Metasound::FBoolReadRef Normalized;
		FSoundModulationParameterAssetReadRef Parameter;

		Metasound::TDataWriteReference<float> OutValue;
	};

	class FMixModulatorsNode : public Metasound::FNodeFacade
	{
	public:
		FMixModulatorsNode(const Metasound::FNodeInitData& InInitData)
			: Metasound::FNodeFacade(InInitData.InstanceName, InInitData.InstanceID, Metasound::TFacadeOperatorClass<FMixModulatorsNodeOperator>())
		{
		}

		virtual ~FMixModulatorsNode() = default;
	};

	METASOUND_REGISTER_NODE(FMixModulatorsNode)
} // namespace AudioModulation

#undef LOCTEXT_NAMESPACE
