// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundAudioFormatNodes.h"

#include "MetasoundAudioFormats.h"
#include "MetasoundFacade.h"
#include "MetasoundOperatorInterface.h"

#define LOCTEXT_NAMESPACE "MetasoundStandardNodes"

namespace Metasound
{
	class FMonoAudioFormatOperator : public IOperator
	{

		static const FVertexKey& GetCenterName()
		{
			static const FVertexKey Name(TEXT("Center"));
			return Name;
		}

		static const FVertexKey& GetMonoName()
		{
			static const FVertexKey Name(TEXT("Mono"));
			return Name;
		}

		TDataReadReference<FMonoAudioFormat> MonoAudioRef;

	public:

		FMonoAudioFormatOperator(TDataReadReference<FMonoAudioFormat> InMonoAudioRef)
		:	MonoAudioRef(InMonoAudioRef)
		{
		}


		FDataReferenceCollection GetInputs() const override
		{
			FDataReferenceCollection Inputs;
			Inputs.AddDataReadReference(GetCenterName(), MonoAudioRef->GetCenter());
			return Inputs;	
		}

		FDataReferenceCollection GetOutputs() const override
		{
			FDataReferenceCollection Outputs;
			Outputs.AddDataReadReference(GetMonoName(), MonoAudioRef);
			return Outputs;
		}

		FExecuteFunction GetExecuteFunction() 
		{
			// No execution required. Mono format is created during operator instantiation.
			return nullptr;
		}
		
		
		static TUniquePtr<IOperator> CreateOperator(const FCreateOperatorParams& InParams, TArray<TUniquePtr<IOperatorBuildError>>& OutErrors) 
		{
			TDataReadReference<FAudioBuffer> Center = InParams.InputDataReferences.GetDataReadReferenceOrConstruct<FAudioBuffer>(GetCenterName(), InParams.OperatorSettings);
			TDataReadReference<FMonoAudioFormat> Mono = TDataReadReference<FMonoAudioFormat>::CreateNew(WriteCast(Center));
			return MakeUnique<FMonoAudioFormatOperator>(Mono);
		}

		static const FNodeInfo& GetNodeInfo()
		{
			auto InitNodeInfo = []() -> FNodeInfo
			{
				FNodeInfo Info;

				Info.ClassName = FName(TEXT("MonoAudioFormat"));
				Info.MajorVersion = 1;
				Info.MinorVersion = 0;
				Info.Description = LOCTEXT("MonoAudioFormat_NodeDescription", "Mono Audio Format");
				Info.Author = PluginAuthor;
				Info.PromptIfMissing = PluginNodeMissingPrompt;

				Info.DefaultInterface = FVertexInterface(
					FInputVertexInterface(
						TInputDataVertexModel<FAudioBuffer>(GetCenterName(), LOCTEXT("MonoAudioFormat_CenterChannel", "Audio"))
					),
					FOutputVertexInterface(
						TOutputDataVertexModel<FMonoAudioFormat>(GetMonoName(), LOCTEXT("MonoAudioFormat_MonoOut", "Mono"))
					)
				);

				return Info;
			};

			static const FNodeInfo Info = InitNodeInfo();
			return Info;
		}
	};

	FMonoAudioFormatNode::FMonoAudioFormatNode(const FString& InInstanceName)
	: FNodeFacade(InInstanceName, TFacadeOperatorClass<FMonoAudioFormatOperator>())
	{
	}

	FMonoAudioFormatNode::FMonoAudioFormatNode(const FNodeInitData& InInitData)
	: FMonoAudioFormatNode(InInitData.InstanceName)
	{
	}

	class FStereoAudioFormatOperator : public IOperator
	{

		static const FVertexKey& GetLeftName()
		{
			static const FVertexKey Name(TEXT("Left"));
			return Name;
		}

		static const FVertexKey& GetRightName()
		{
			static const FVertexKey Name(TEXT("Right"));
			return Name;
		}

		static const FVertexKey& GetStereoName()
		{
			static const FVertexKey Name(TEXT("Stereo"));
			return Name;
		}

		TDataReadReference<FStereoAudioFormat> StereoAudio;

	public:

		FStereoAudioFormatOperator(TDataReadReference<FStereoAudioFormat> InStereoAudio)
		:	StereoAudio(InStereoAudio)
		{
		}


		FDataReferenceCollection GetInputs() const override
		{
			FDataReferenceCollection Inputs;
			Inputs.AddDataReadReference(GetLeftName(), StereoAudio->GetLeft());
			Inputs.AddDataReadReference(GetRightName(), StereoAudio->GetRight());
			return Inputs;	
		}

		FDataReferenceCollection GetOutputs() const override
		{
			FDataReferenceCollection Outputs;
			Outputs.AddDataReadReference(GetStereoName(), StereoAudio);
			return Outputs;
		}

		FExecuteFunction GetExecuteFunction() 
		{
			// No execution required. Stereo format is created during operator instantiation.
			return nullptr;
		}
		
		
		static TUniquePtr<IOperator> CreateOperator(const FCreateOperatorParams& InParams, TArray<TUniquePtr<IOperatorBuildError>>& OutErrors) 
		{
			TDataReadReference<FAudioBuffer> Left = InParams.InputDataReferences.GetDataReadReferenceOrConstruct<FAudioBuffer>(GetLeftName(), InParams.OperatorSettings);
			TDataReadReference<FAudioBuffer> Right = InParams.InputDataReferences.GetDataReadReferenceOrConstruct<FAudioBuffer>(GetRightName(), InParams.OperatorSettings);
			TDataReadReference<FStereoAudioFormat> Stereo = TDataReadReference<FStereoAudioFormat>::CreateNew(WriteCast(Left), WriteCast(Right));

			return MakeUnique<FStereoAudioFormatOperator>(Stereo);
		}

		static const FNodeInfo& GetNodeInfo()
		{
			auto InitNodeInfo = []() -> FNodeInfo
			{
				FNodeInfo Info;

				Info.ClassName = FName(TEXT("StereoAudioFormat"));
				Info.MajorVersion = 1;
				Info.MinorVersion = 0;
				Info.Description = LOCTEXT("StereoAudioFormat_NodeDescription", "Stereo Audio Format");
				Info.Author = PluginAuthor;
				Info.PromptIfMissing = PluginNodeMissingPrompt;

				Info.DefaultInterface = FVertexInterface(
					FInputVertexInterface(
						TInputDataVertexModel<FAudioBuffer>(GetLeftName(), LOCTEXT("StereoAudioFormat_LeftChannel", "Left Audio")),
						TInputDataVertexModel<FAudioBuffer>(GetRightName(), LOCTEXT("StereoAudioFormat_RightChannel", "Right Audio"))
					),
					FOutputVertexInterface(
						TOutputDataVertexModel<FStereoAudioFormat>(GetStereoName(), LOCTEXT("StereoAudioFormat_StereoOut", "Stereo"))
					)
				);

				return Info;
			};

			static const FNodeInfo Info = InitNodeInfo();
			return Info;
		}
	};

	FStereoAudioFormatNode::FStereoAudioFormatNode(const FString& InInstanceName)
	: FNodeFacade(InInstanceName, TFacadeOperatorClass<FStereoAudioFormatOperator>())
	{
	}

	FStereoAudioFormatNode::FStereoAudioFormatNode(const FNodeInitData& InInitData)
	: FStereoAudioFormatNode(InInitData.InstanceName)
	{
	}

	METASOUND_REGISTER_NODE(FMonoAudioFormatNode);
	METASOUND_REGISTER_NODE(FStereoAudioFormatNode);
}


#undef LOCTEXT_NAMESPACE
