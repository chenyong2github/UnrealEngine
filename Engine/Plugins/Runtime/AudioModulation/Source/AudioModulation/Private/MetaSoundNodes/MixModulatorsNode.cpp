// Copyright Epic Games, Inc. All Rights Reserved.

#include "AudioModulation.h"
#include "IAudioModulation.h"
#include "MetasoundDataFactory.h"
#include "MetasoundExecutableOperator.h"
#include "MetasoundFacade.h"
#include "MetasoundNodeRegistrationMacro.h"
#include "MetasoundPrimitives.h"
#include "MetasoundSourceInterface.h"
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

			return MakeUnique<FMixModulatorsNodeOperator>(InParams, Modulator1ReadRef, Modulator2ReadRef, ParameterReadRef, NormalizedReadRef);
		}

		FMixModulatorsNodeOperator(
			const Metasound::FCreateOperatorParams& InParams,
			const FSoundModulatorAssetReadRef& InModulator1,
			const FSoundModulatorAssetReadRef& InModulator2,
			const FSoundModulationParameterAssetReadRef& InParameter,
			const Metasound::FBoolReadRef& InNormalized)
			: DeviceId(InParams.Environment.GetValue<Audio::FDeviceId>(Metasound::Frontend::SourceInterface::Environment::DeviceID))
			, Modulator1(InModulator1)
			, Modulator2(InModulator2)
			, Normalized(InNormalized)
			, Parameter(InParameter)
			, OutValue(Metasound::TDataWriteReferenceFactory<float>::CreateAny(InParams.OperatorSettings))
		{
			Execute();
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
			auto UpdateValue = [](const FSoundModulatorAssetReadRef& ReadRef, Audio::FDeviceId InDeviceId, Audio::FModulatorHandle& OutHandle)
			{
				float Value = 1.0f;
				const FSoundModulatorAsset& ModulatorAsset = *ReadRef;

				// Checks if handle needs refresh.  This can happen if the incoming modulator is changed or nulled.
				if (ModulatorAsset.GetModulatorId() != OutHandle.GetModulatorId())
				{
					if (IAudioModulationManager* InModulation = AudioModulation::GetDeviceModulationManager(InDeviceId))
					{
						OutHandle = ModulatorAsset->CreateModulatorHandle(*InModulation);
					}
				}

				if (OutHandle.IsValid())
				{
					OutHandle.GetValueThreadSafe(Value);
				}

				return Value;
			};

			float Value1 = UpdateValue(Modulator1, DeviceId, ModHandle1);
			const float Value2 = UpdateValue(Modulator2, DeviceId, ModHandle2);

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
		const Audio::FDeviceId DeviceId;

		Audio::FModulatorHandle ModHandle1;
		Audio::FModulatorHandle ModHandle2;

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
