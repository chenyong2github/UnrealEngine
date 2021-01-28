// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundAudioFormatNodes.h"

#include "MetasoundAudioFormats.h"
#include "MetasoundFacade.h"
#include "MetasoundOperatorInterface.h"
#include "MetasoundStandardNodesNames.h"

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

		static const FNodeClassMetadata& GetNodeInfo()
		{
			auto InitNodeInfo = []() -> FNodeClassMetadata
			{
				FNodeDisplayStyle DisplayStyle;
				DisplayStyle.bShowName = false;

				FNodeClassMetadata Info;
				Info.ClassName = {Metasound::StandardNodes::Namespace, TEXT("FormatAudio"), TEXT("Mono")};
				Info.DisplayStyle = DisplayStyle;
				Info.MajorVersion = 1;
				Info.MinorVersion = 0;
				Info.DisplayName = LOCTEXT("MonoAudioFormat_NodeDisplayName", "Mono");
				Info.Description = LOCTEXT("MonoAudioFormat_NodeDescription", "Converts Audio:Buffer To Mono Format");
				Info.CategoryHierarchy = { LOCTEXT("Metasound_ConvertNodeCategory", "Conversions") };
				Info.Author = PluginAuthor;
				Info.PromptIfMissing = PluginNodeMissingPrompt;

				Info.DefaultInterface = FVertexInterface(
					FInputVertexInterface(
						TInputDataVertexModel<FAudioBuffer>(GetCenterName(), LOCTEXT("MonoAudioFormat_CenterChannel", "Center"))
					),
					FOutputVertexInterface(
						TOutputDataVertexModel<FMonoAudioFormat>(GetMonoName(), LOCTEXT("MonoAudioFormat_MonoOut", "Mono"))
					)
				);

				return Info;
			};

			static const FNodeClassMetadata Info = InitNodeInfo();
			return Info;
		}
	};

	FMonoAudioFormatNode::FMonoAudioFormatNode(const FString& InInstanceName, const FGuid& InInstanceID)
	: FNodeFacade(InInstanceName, InInstanceID, TFacadeOperatorClass<FMonoAudioFormatOperator>())
	{
	}

	FMonoAudioFormatNode::FMonoAudioFormatNode(const FNodeInitData& InInitData)
	: FMonoAudioFormatNode(InInitData.InstanceName, InInitData.InstanceID)
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

		static const FNodeClassMetadata& GetNodeInfo()
		{
			auto InitNodeInfo = []() -> FNodeClassMetadata
			{
				FNodeDisplayStyle DisplayStyle;
				DisplayStyle.bShowName = false;

				FNodeClassMetadata Info;
				Info.ClassName = {Metasound::StandardNodes::Namespace, TEXT("FormatAudio"), TEXT("Stereo")};
				Info.DisplayStyle = DisplayStyle;
				Info.MajorVersion = 1;
				Info.MinorVersion = 0;
				Info.DisplayName = LOCTEXT("StereoAudioFormat_NodeDisplayName", "Stereo");
				Info.Description = LOCTEXT("StereoAudioFormat_NodeDescription", "Converts Audio:Buffer to Stereo Format");
				Info.CategoryHierarchy = { LOCTEXT("Metasound_ConvertNodeCategory", "Conversions") };
				Info.Author = PluginAuthor;
				Info.PromptIfMissing = PluginNodeMissingPrompt;

				Info.DefaultInterface = FVertexInterface(
					FInputVertexInterface(
						TInputDataVertexModel<FAudioBuffer>(GetLeftName(), LOCTEXT("StereoAudioFormat_LeftChannel", "Left")),
						TInputDataVertexModel<FAudioBuffer>(GetRightName(), LOCTEXT("StereoAudioFormat_RightChannel", "Right"))
					),
					FOutputVertexInterface(
						TOutputDataVertexModel<FStereoAudioFormat>(GetStereoName(), LOCTEXT("StereoAudioFormat_StereoOut", "Stereo"))
					)
				);

				return Info;
			};

			static const FNodeClassMetadata Info = InitNodeInfo();
			return Info;
		}
	};

	FStereoAudioFormatNode::FStereoAudioFormatNode(const FString& InInstanceName, const FGuid& InInstanceID)
	: FNodeFacade(InInstanceName, InInstanceID, TFacadeOperatorClass<FStereoAudioFormatOperator>())
	{
	}

	FStereoAudioFormatNode::FStereoAudioFormatNode(const FNodeInitData& InInitData)
	: FStereoAudioFormatNode(InInitData.InstanceName, InInitData.InstanceID)
	{
	}

	METASOUND_REGISTER_NODE(FMonoAudioFormatNode);
	METASOUND_REGISTER_NODE(FStereoAudioFormatNode);
}


#undef LOCTEXT_NAMESPACE
