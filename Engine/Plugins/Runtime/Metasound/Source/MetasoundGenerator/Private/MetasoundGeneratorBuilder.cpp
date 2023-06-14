// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundGeneratorBuilder.h"

#include "AudioParameter.h"
#include "MetasoundFrontendDataTypeRegistry.h"
#include "MetasoundOperatorBuilder.h"
#include "MetasoundOperatorInterface.h"
#include "MetasoundOutputNode.h"
#include "MetasoundParameterTransmitter.h"
#include "MetasoundRouter.h"
#include "MetasoundTrace.h"
#include "MetasoundTrigger.h"
#include "MetasoundVertexData.h"
#include "Analysis/MetasoundFrontendAnalyzerFactory.h"
#include "Analysis/MetasoundFrontendAnalyzerRegistry.h"
#include "Analysis/MetasoundFrontendVertexAnalyzer.h"
#include "Interfaces/MetasoundFrontendSourceInterface.h"

namespace Metasound
{
	FAsyncMetaSoundBuilder::FAsyncMetaSoundBuilder(FMetasoundGenerator* InGenerator, FMetasoundGeneratorInitParams&& InInitParams, bool bInTriggerGenerator)
		: Generator(InGenerator)
		, InitParams(MoveTemp(InInitParams))
		, bTriggerGenerator(bInTriggerGenerator)
	{
	}

	void FAsyncMetaSoundBuilder::DoWork()
	{
		using namespace Audio;
		using namespace Frontend;
		using namespace MetasoundGeneratorPrivate;

		METASOUND_TRACE_CPUPROFILER_EVENT_SCOPE_TEXT(*FString::Printf(TEXT("AsyncMetaSoundBuilder::DoWork %s"), *InitParams.MetaSoundName));

		// Create an instance of the new graph
		FBuildResults BuildResults;
		FOperatorAndInputs GraphOperatorAndInputs = GeneratorBuilder::BuildGraphOperator(InitParams, BuildResults);
		GeneratorBuilder::LogBuildErrors(InitParams.MetaSoundName, BuildResults);

		if (GraphOperatorAndInputs.Operator.IsValid())
		{
			// Create graph analyzer
			TUniquePtr<FGraphAnalyzer> GraphAnalyzer;
			if (InitParams.BuilderSettings.bPopulateInternalDataReferences)
			{
				GraphAnalyzer = GeneratorBuilder::BuildGraphAnalyzer(MoveTemp(BuildResults.InternalDataReferences), InitParams.Environment, InitParams.OperatorSettings);
			}
	
			// Collect data for generator
			FMetasoundGeneratorData GeneratorData = GeneratorBuilder::BuildGeneratorData(InitParams, MoveTemp(GraphOperatorAndInputs), MoveTemp(GraphAnalyzer));

			Generator->SetPendingGraph(MoveTemp(GeneratorData), bTriggerGenerator);
		}
		else 
		{
			UE_LOG(LogMetaSound, Error, TEXT("Failed to build Metasound operator from graph in MetasoundSource [%s]"), *InitParams.MetaSoundName);
			// Set null generator data to inform that generator failed to build. 
			// Otherwise, generator will continually wait for a new generator.
			Generator->SetPendingGraphBuildFailed();
		}

		InitParams.Release();
	}

	namespace GeneratorBuilder
	{
		TArray<FAudioBufferReadRef> FindOutputAudioBuffers(const TArray<FVertexName>& InAudioVertexNames, const FVertexInterfaceData& InVertexData, const FOperatorSettings& InOperatorSettings, const FString& InMetaSoundName)
		{
			TArray<FAudioBufferReadRef> OutputBuffers;

			const FOutputVertexInterfaceData& OutputVertexData = InVertexData.GetOutputs();

			// Get output audio buffers.
			for (const FVertexName& AudioOutputName : InAudioVertexNames)
			{
				if (!OutputVertexData.IsVertexBound(AudioOutputName))
				{
					UE_LOG(LogMetaSound, Warning, TEXT("MetasoundSource [%s] does not contain audio output [%s] in output"), *InMetaSoundName, *AudioOutputName.ToString());
				}
				OutputBuffers.Add(OutputVertexData.GetOrConstructDataReadReference<FAudioBuffer>(AudioOutputName, InOperatorSettings));
			}

			return OutputBuffers;
		}

		void LogBuildErrors(const FString& InMetaSoundName, const FBuildResults& InBuildResults)
		{
			// Log build errors
			for (const IOperatorBuilder::FBuildErrorPtr& Error : InBuildResults.Errors)
			{
				if (Error.IsValid())
				{
					UE_LOG(LogMetaSound, Warning, TEXT("MetasoundSource [%s] build error [%s] \"%s\""), *InMetaSoundName, *(Error->GetErrorType().ToString()), *(Error->GetErrorDescription().ToString()));
				}
			}
		}

		TUniquePtr<Frontend::FGraphAnalyzer> BuildGraphAnalyzer(TMap<FGuid, FDataReferenceCollection>&& InInternalDataReferences, const FMetasoundEnvironment& InEnvironment, const FOperatorSettings& InOperatorSettings)
		{
			METASOUND_TRACE_CPUPROFILER_EVENT_SCOPE_TEXT(TEXT("GeneratorBuilder::BuildGraphAnalyzer"));
			using namespace Frontend;

			const uint64 InstanceID = InEnvironment.GetValue<uint64>(SourceInterface::Environment::TransmitterID);
			return MakeUnique<FGraphAnalyzer>(InOperatorSettings, InstanceID, MoveTemp(InInternalDataReferences));
		}

		FOperatorAndInputs BuildGraphOperator(FMetasoundGeneratorInitParams& InInitParams, FBuildResults& OutBuildResults)
		{
			METASOUND_TRACE_CPUPROFILER_EVENT_SCOPE_TEXT(TEXT("GeneratorBuilder::BuildGraphOperator"));
			using namespace Frontend;

			// Choose which type of data reference access to create depending upon the access of the vertex.
			auto VertexAccessTypeToDataReferenceAccessType = [](EVertexAccessType InVertexAccessType) -> EDataReferenceAccessType
			{
				switch(InVertexAccessType)
				{
					case EVertexAccessType::Value:
						return EDataReferenceAccessType::Value;

					case EVertexAccessType::Reference:
					default:
						return EDataReferenceAccessType::Write;
				}
			};

			const FInputVertexInterface& InputInterface = InInitParams.Graph->GetVertexInterface().GetInputInterface();
			FInputVertexInterfaceData InputData(InputInterface);

			// Set input data based on the input parameters and the input interface
			IDataTypeRegistry& DataRegistry = IDataTypeRegistry::Get();
			for (FAudioParameter& Parameter : InInitParams.DefaultParameters)
			{
				const FName ParamName = Parameter.ParamName;
				if (const FInputDataVertex* InputVertex = InputInterface.Find(ParamName))
				{
					FLiteral Literal = Frontend::ConvertParameterToLiteral(MoveTemp(Parameter));

					TOptional<FAnyDataReference> DataReference = DataRegistry.CreateDataReference(InputVertex->DataTypeName, VertexAccessTypeToDataReferenceAccessType(InputVertex->AccessType), Literal, InInitParams.OperatorSettings);

					if (DataReference)
					{
						InputData.BindVertex(ParamName, *DataReference);
					}
					else if(MetaSoundParameterEnableWarningOnIgnoredParameterCVar)
					{
						UE_LOG(LogMetaSound, Warning, TEXT("Failed to create initial input data reference from parameter %s of type %s on graph in MetaSoundSource [%s]"), *ParamName.ToString(), *InputVertex->DataTypeName.ToString(), *InInitParams.MetaSoundName);
					}
				}
				else if(MetaSoundParameterEnableWarningOnIgnoredParameterCVar)
				{
					UE_LOG(LogMetaSound, Warning, TEXT("Failed to set initial input parameter %s on graph in MetaSoundSource [%s]"), *ParamName.ToString(), *InInitParams.MetaSoundName);
				}
			}

			// Set any remaining inputs to their default values.
			for (const MetasoundVertexDataPrivate::FInputBinding& Binding : InputData)
			{
				// Only create data reference if something does not already exist. 
				if (!Binding.IsBound())
				{
					const FInputDataVertex& InputVertex = Binding.GetVertex();
					EDataReferenceAccessType AccessType = VertexAccessTypeToDataReferenceAccessType(InputVertex.AccessType);
					TOptional<FAnyDataReference> DataReference = DataRegistry.CreateDataReference(InputVertex.DataTypeName, VertexAccessTypeToDataReferenceAccessType(InputVertex.AccessType), InputVertex.GetDefaultLiteral(), InInitParams.OperatorSettings);

					if (DataReference)
					{
						InputData.BindVertex(InputVertex.VertexName, *DataReference);
					}
				}
			}

			// Reset as elements in array have been moved.
			InInitParams.DefaultParameters.Reset();

			// Create an instance of the new graph
			FBuildGraphOperatorParams BuildParams { *InInitParams.Graph, InInitParams.OperatorSettings, InputData, InInitParams.Environment };
			FOperatorAndInputs OpAndInputs;
			FOperatorBuilder Builder(InInitParams.BuilderSettings);

			if (InInitParams.DynamicOperatorTransactor.IsValid())
			{
				OpAndInputs.Operator = Builder.BuildDynamicGraphOperator(BuildParams, *InInitParams.DynamicOperatorTransactor, OutBuildResults);
			}
			else
			{
				OpAndInputs.Operator = Builder.BuildGraphOperator(BuildParams, OutBuildResults);
			}
			OpAndInputs.Inputs = InputData;

			return OpAndInputs;
		}

		MetasoundGeneratorPrivate::FMetasoundGeneratorData BuildGeneratorData(const FMetasoundGeneratorInitParams& InInitParams, FOperatorAndInputs&& InGraphOperatorAndInputs, TUniquePtr<Frontend::FGraphAnalyzer> InAnalyzer)
		{
			using namespace Audio;
			using namespace Frontend;
			using namespace MetasoundGeneratorPrivate;

			METASOUND_TRACE_CPUPROFILER_EVENT_SCOPE_TEXT(TEXT("GeneratorBuilder::BuildGeneratorData"));

			checkf(InGraphOperatorAndInputs.Operator.IsValid(), TEXT("Graph operator must be a valid object"))

			// Gather relevant input and output references
			FVertexInterfaceData VertexData(InInitParams.Graph->GetVertexInterface());
			InGraphOperatorAndInputs.Operator->BindInputs(VertexData.GetInputs());
			InGraphOperatorAndInputs.Operator->BindOutputs(VertexData.GetOutputs());

			// Replace input data with writable inputs
			VertexData.GetInputs() = InGraphOperatorAndInputs.Inputs;

			// Get inputs
			FTriggerWriteRef PlayTrigger = VertexData.GetInputs().GetOrConstructDataWriteReference<FTrigger>(SourceInterface::Inputs::OnPlay, InInitParams.OperatorSettings, false);

			// Get outputs
			TArray<FAudioBufferReadRef> OutputBuffers = FindOutputAudioBuffers(InInitParams.AudioOutputNames, VertexData, InInitParams.OperatorSettings, InInitParams.MetaSoundName);
			FTriggerReadRef FinishTrigger = TDataReadReferenceFactory<FTrigger>::CreateExplicitArgs(InInitParams.OperatorSettings, false);

			if (InInitParams.Graph->GetVertexInterface().GetOutputInterface().Contains(SourceOneShotInterface::Outputs::OnFinished))
			{
				FinishTrigger = VertexData.GetOutputs().GetOrConstructDataReadReference<FTrigger>(SourceOneShotInterface::Outputs::OnFinished, InInitParams.OperatorSettings, false);
			}

			// Create the parameter setter map so parameter packs can be cracked
			// open and distributed as appropriate...
			const IDataTypeRegistry& DataTypeRegistry = IDataTypeRegistry::Get();
			FParameterSetterSortedMap ParameterSetters;
			TMap<FName, FParameterPackSetter> ParameterPackSetters;
			FInputVertexInterfaceData& GraphInputs = VertexData.GetInputs();
			for (const MetasoundVertexDataPrivate::FInputBinding& Binding : GraphInputs)
			{
				// Only assign inputs that are writable. 
				if (EDataReferenceAccessType::Write == Binding.GetAccessType())
				{
					if (const FAnyDataReference* DataRef = Binding.GetDataReference())
					{
						const FInputDataVertex& InputVertex = Binding.GetVertex();
						const Frontend::IParameterAssignmentFunction& PackSetter = DataTypeRegistry.GetRawAssignmentFunction(InputVertex.DataTypeName);
						if (PackSetter)
						{
							FParameterPackSetter ParameterPackSetter(InputVertex.DataTypeName, DataRef->GetRaw(), PackSetter);
							ParameterPackSetters.Add(InputVertex.VertexName, ParameterPackSetter);
						}

						Frontend::FLiteralAssignmentFunction LiteralSetter = DataTypeRegistry.GetLiteralAssignmentFunction(InputVertex.DataTypeName);
						if (LiteralSetter)
						{
							ParameterSetters.Add(InputVertex.VertexName, FParameterSetter{LiteralSetter, *DataRef});
						}
					}
				}
			}

			// Set data needed for graph
			return FMetasoundGeneratorData 
			{
				InInitParams.OperatorSettings,
				MoveTemp(InGraphOperatorAndInputs.Operator),
				MoveTemp(VertexData),
				MoveTemp(ParameterSetters),
				MoveTemp(ParameterPackSetters),
				MoveTemp(InAnalyzer),
				MoveTemp(OutputBuffers),
				MoveTemp(PlayTrigger),
				MoveTemp(FinishTrigger),
			};
		}

		void ApplyAudioParameters(const FOperatorSettings& InOperatorSettings, TArray<FAudioParameter>&& InParameters, FInputVertexInterfaceData& InInterface)
		{
			METASOUND_TRACE_CPUPROFILER_EVENT_SCOPE("GeneratorBuilder::ApplyAudioParameters");
			Frontend::IDataTypeRegistry& DataTypeRegistry = Frontend::IDataTypeRegistry::Get();
			for (FAudioParameter& Parameter : InParameters)
			{
				if (const FAnyDataReference* Ref = InInterface.FindDataReference(Parameter.ParamName))
				{
					if (EDataReferenceAccessType::Write == Ref->GetAccessType())
					{
						Frontend::FLiteralAssignmentFunction LiteralSetter = DataTypeRegistry.GetLiteralAssignmentFunction(Ref->GetDataTypeName());

						if (LiteralSetter)
						{
							FLiteral Literal = Frontend::ConvertParameterToLiteral(MoveTemp(Parameter)); 
							LiteralSetter(InOperatorSettings, Literal, *Ref);
						}
					}
				}
			}
		}
	}
}
