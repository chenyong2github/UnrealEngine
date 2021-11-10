// Copyright Epic Games, Inc. All Rights Reserved.
#include "MetasoundDataFactory.h"
#include "MetasoundExecutableOperator.h"
#include "MetasoundFacade.h"
#include "MetasoundNodeRegistrationMacro.h"
#include "MetasoundPrimitives.h"
#include "SoundModulatorAsset.h"

#define LOCTEXT_NAMESPACE "AudioModulationNodes"

namespace AudioModulation
{
	class FGetModulatorValueNodeOperator : public Metasound::TExecutableOperator<FGetModulatorValueNodeOperator>
	{
	public:
		static const Metasound::FVertexInterface& GetDefaultInterface()
		{
			static const Metasound::FVertexInterface DefaultInterface(
				Metasound::FInputVertexInterface(
					Metasound::TInputDataVertexModel<FSoundModulatorAsset>("Modulator", LOCTEXT("MetasoundValueNode_InputModulatorName", "Modulator")),
					Metasound::TInputDataVertexModel<bool>("Normalized", LOCTEXT("MixModulatorsNode_InputNormalizedName", "Normalized"), true)
				),
				Metasound::FOutputVertexInterface(
					Metasound::TOutputDataVertexModel<float>("Out", LOCTEXT("MetasoundValueNode_OutputModulatorValue", "Out"))
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
					Metasound::FNodeClassName { "Modulation", "GetModulatorValue", "" },
					1, // Major Version
					0, // Minor Version
					LOCTEXT("GetModulatorValueNode_Name", "Get Modulator Value"),
					LOCTEXT("GetModulatorValueNode_Description", "Returns the current value of the given modulator. Converts value to unit space if 'Normalized' is false."),
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

			FSoundModulatorAssetReadRef ModulatorReadRef = InputCollection.GetDataReadReferenceOrConstruct<FSoundModulatorAsset>("Modulator");
			FBoolReadRef NormalizedReadRef = InputCollection.GetDataReadReferenceOrConstructWithVertexDefault<bool>(InputInterface, "Normalized", InParams.OperatorSettings);

			return MakeUnique<FGetModulatorValueNodeOperator>(InParams.OperatorSettings, ModulatorReadRef, NormalizedReadRef);
		}

		FGetModulatorValueNodeOperator(const Metasound::FOperatorSettings& InSettings, const FSoundModulatorAssetReadRef& InModulator, const Metasound::FBoolReadRef& InNormalized)
			: Modulator(InModulator)
			, Normalized(InNormalized)
			, OutValue(Metasound::TDataWriteReferenceFactory<float>::CreateAny(InSettings))
		{
		}

		virtual ~FGetModulatorValueNodeOperator() = default;

		virtual Metasound::FDataReferenceCollection GetInputs() const override
		{
			using namespace Metasound;

			FDataReferenceCollection Inputs;
			Inputs.AddDataReadReference("Modulator", Modulator);
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
			const FSoundModulatorAsset& ModulatorAsset = *Modulator;
			if (ModulatorAsset.IsValid())
			{
				float Value = ModulatorAsset->GetValue();

				if (!*Normalized)
				{
					const Audio::FModulationParameter& Parameter = ModulatorAsset->GetParameter();
					if (Parameter.bRequiresConversion)
					{
						Parameter.UnitFunction(Value);
					}
				}

				*OutValue = Value;
			}
			else
			{
				*OutValue = 1.0f;
			}
		}

	private:
		FSoundModulatorAssetReadRef Modulator;
		Metasound::FBoolReadRef Normalized;
		Metasound::TDataWriteReference<float> OutValue;
	};

	class FGetModulatorValueNode : public Metasound::FNodeFacade
	{
	public:
		FGetModulatorValueNode(const Metasound::FNodeInitData& InInitData)
			: Metasound::FNodeFacade(InInitData.InstanceName, InInitData.InstanceID, Metasound::TFacadeOperatorClass<FGetModulatorValueNodeOperator>())
		{
		}

		virtual ~FGetModulatorValueNode() = default;
	};

	METASOUND_REGISTER_NODE(FGetModulatorValueNode)
} // namespace AudioModulation

#undef LOCTEXT_NAMESPACE
