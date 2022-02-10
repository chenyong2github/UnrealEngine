// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundAudioFormats.h"

#include "CoreMinimal.h"
#include "MetasoundArrayNodesRegistration.h"
#include "MetasoundAudioBuffer.h"
#include "MetasoundDataReference.h"
#include "MetasoundDataTypeRegistrationMacro.h"
#include "MetasoundLiteral.h"
#include "MetasoundOutputNode.h"
#include "MetasoundVertex.h"

#define LOCTEXT_NAMESPACE "MetasoundStandardNodes_AudioFormats"

namespace Metasound
{
	/* FMultichannelAudioFormat */

	FMultichannelAudioFormat::FMultichannelAudioFormat()
	:	NumChannels(0)
	{
	}

	FMultichannelAudioFormat::FMultichannelAudioFormat(int32 InNumFrames, int32 InNumChannels)
	:	NumChannels(InNumChannels)
	{
		NumChannels = FMath::Max(0, NumChannels);
		InNumFrames = FMath::Max(0, InNumFrames);

		for (int32 i = 0; i < NumChannels; i++)
		{
			FAudioBufferWriteRef Audio = FAudioBufferWriteRef::CreateNew(InNumFrames);
			Audio->Zero();

			WritableBufferStorage.Add(Audio);
			ReadableBufferStorage.Add(Audio);
		}

		WritableBuffers = WritableBufferStorage;
		ReadableBuffers = ReadableBufferStorage;
	}

	FMultichannelAudioFormat::FMultichannelAudioFormat(const FOperatorSettings& InSettings, int32 InNumChannels)
		: FMultichannelAudioFormat(InSettings.GetNumFramesPerBlock(), InNumChannels)
	{}

	FMultichannelAudioFormat::FMultichannelAudioFormat(TArrayView<const FAudioBufferWriteRef> InWriteRefs)
	:	NumChannels(InWriteRefs.Num())
	{
		if (NumChannels > 0)
		{
			const int32 NumFrames = InWriteRefs[0]->Num();

			for (const FAudioBufferWriteRef& Ref : InWriteRefs)
			{
				checkf(NumFrames == Ref->Num(), TEXT("All buffers must have same number of frames (%d != %d)"), NumFrames, Ref->Num());

				WritableBufferStorage.Add(Ref);
				ReadableBufferStorage.Add(Ref);
			}

			WritableBuffers = WritableBufferStorage;
			ReadableBuffers = ReadableBufferStorage;
		}
	}

	FMultichannelAudioFormat::FMultichannelAudioFormat(TArrayView<const FAudioBufferReadRef> InReadRefs)
	:	NumChannels(InReadRefs.Num())
	{
		if (NumChannels > 0)
		{
			const int32 NumFrames = InReadRefs[0]->Num();

			for (const FAudioBufferReadRef& Ref : InReadRefs)
			{
				checkf(NumFrames == Ref->Num(), TEXT("All buffers must have same number of frames (%d != %d)"), NumFrames, Ref->Num());

				WritableBufferStorage.Add(WriteCast(Ref));
				ReadableBufferStorage.Add(Ref);
			}

			WritableBuffers = WritableBufferStorage;
			ReadableBuffers = ReadableBufferStorage;
		}
	}

	// Special vertex keys for stereo input/output nodes.
	namespace StereoAudioFormatVertexKeys
	{
		static const FVertexName LeftChannelVertexKey = "Left";
		static const FVertexName RightChannelVertexKey = "Right";
	}

	// Specialization of TOutputNode<FStereoAudio> to support direct connection
	// of audio buffers to left/right inputs. 
	template<>
	class METASOUNDSTANDARDNODES_API TOutputNode<FStereoAudioFormat> : public FNode
	{
		// FOutputOperator primarly used to report inputs and outputs. Has no execute function.
		class FOutputOperator : public IOperator
		{
		public:
			FOutputOperator(const FVertexName& InOutputName, TDataReadReference<FAudioBuffer> InLeft, TDataReadReference<FAudioBuffer> InRight, TDataReadReference<FStereoAudioFormat> InStereo)
			: OutputName(InOutputName)
			, Left(InLeft)
			, Right(InRight)
			, Stereo(InStereo)
			{
			}

			virtual ~FOutputOperator() {}

			virtual FDataReferenceCollection GetInputs() const override
			{
				FDataReferenceCollection Inputs;

				Inputs.AddDataReadReference(StereoAudioFormatVertexKeys::LeftChannelVertexKey, Left);
				Inputs.AddDataReadReference(StereoAudioFormatVertexKeys::RightChannelVertexKey, Right);

				return Inputs;
			}

			virtual FDataReferenceCollection GetOutputs() const override
			{
				FDataReferenceCollection Outputs;

				Outputs.AddDataReadReference(OutputName, Stereo);

				return Outputs;
			}

			virtual FExecuteFunction GetExecuteFunction() override
			{
				return nullptr;
			}

		private:

			FVertexName OutputName;
			TDataReadReference<FAudioBuffer> Left;
			TDataReadReference<FAudioBuffer> Right;
			TDataReadReference<FStereoAudioFormat> Stereo;
		};

		class FOutputOperatorFactory : public IOperatorFactory
		{
		public:
			FOutputOperatorFactory(const FVertexName& InOutputName)
			: OutputName(InOutputName)
			{
			}

			TUniquePtr<IOperator> CreateOperator(const FCreateOperatorParams& InParams, FBuildErrorArray& OutErrors) override
			{
				// Construct stereo from left and right audio buffers. 
				TDataReadReference<FAudioBuffer> Left = InParams.InputDataReferences.GetDataReadReferenceOrConstruct<FAudioBuffer>(StereoAudioFormatVertexKeys::LeftChannelVertexKey, InParams.OperatorSettings);
				TDataReadReference<FAudioBuffer> Right = InParams.InputDataReferences.GetDataReadReferenceOrConstruct<FAudioBuffer>(StereoAudioFormatVertexKeys::RightChannelVertexKey, InParams.OperatorSettings);

				TDataReadReference<FStereoAudioFormat> Stereo = TDataReadReferenceFactory<FStereoAudioFormat>::CreateExplicitArgs(InParams.OperatorSettings, WriteCast(Left), WriteCast(Right));

				return MakeUnique<FOutputOperator>(OutputName, Left, Right, Stereo);
			}

		private:
			FVertexName OutputName;
		};

		static FVertexInterface GetVertexInterface(const FVertexName& InVertexName)
		{
			return FVertexInterface(
				FInputVertexInterface(
					TInputDataVertexModel<FAudioBuffer>(StereoAudioFormatVertexKeys::LeftChannelVertexKey, METASOUND_LOCTEXT("Metasound_LeftStereoOutputVertexDescription", "Left channel audio output.")),
					TInputDataVertexModel<FAudioBuffer>(StereoAudioFormatVertexKeys::RightChannelVertexKey, METASOUND_LOCTEXT("Metasound_RightStereoOutputVertexDescription", "Right channel audio output."))
				),
				FOutputVertexInterface(
					TOutputDataVertexModel<FStereoAudioFormat>(InVertexName, METASOUND_LOCTEXT("Metasound_StereoOutputVertexDescription", "Stereo Output."))
				)
			);
		}

		static FNodeClassMetadata GetNodeClassMetadata(const FVertexName& InOutputName)
		{
			FNodeClassMetadata Info;

			Info.ClassName = { "Output", GetMetasoundDataTypeName<FStereoAudioFormat>(), "" };
			Info.MajorVersion = 1;
			Info.MinorVersion = 0;
			Info.DisplayName = METASOUND_LOCTEXT_FORMAT("Metasound_OutputNodeDisplayNameFormat", "Output {0}", GetMetasoundDataTypeDisplayText<FStereoAudioFormat>());
			Info.Description = METASOUND_LOCTEXT("Metasound_OutputNodeDescription", "Output from the parent Metasound graph.");
			Info.Author = PluginAuthor;
			Info.PromptIfMissing = PluginNodeMissingPrompt;
			Info.DefaultInterface = GetVertexInterface(InOutputName);

			return Info;
		};


	public:
		TOutputNode(const FVertexName& InInstanceName, const FGuid& InInstanceID, const FVertexName& InVertexName)
		:	FNode(InInstanceName, InInstanceID, GetNodeClassMetadata(InVertexName))
		,	VertexInterface(GetVertexInterface(InVertexName))
		,	Factory(MakeShared<FOutputOperatorFactory, ESPMode::ThreadSafe>(InVertexName))
		{
		}

		const FVertexInterface& GetVertexInterface() const override
		{
			return VertexInterface;
		}

		bool SetVertexInterface(const FVertexInterface& InInterface) override
		{
			return VertexInterface == InInterface;
		}

		bool IsVertexInterfaceSupported(const FVertexInterface& InInterface) const 
		{
			return VertexInterface == InInterface;
		}

		virtual TSharedRef<IOperatorFactory, ESPMode::ThreadSafe> GetDefaultOperatorFactory() const override
		{
			return Factory;
		}

	private:
		FVertexInterface VertexInterface;

		TSharedRef<FOutputOperatorFactory, ESPMode::ThreadSafe> Factory;

	};

	// Input node specialization to expose left/right audio buffers 
	template<>
	class METASOUNDSTANDARDNODES_API TInputNode<FStereoAudioFormat> : public FNode
	{
		// Noop operator. Used to return inputs / outputs for debugging.
		class FInputOperator : public IOperator
		{
		public:

			FInputOperator(const FVertexName& InInputName, TDataReadReference<FAudioBuffer> InLeft, TDataReadReference<FAudioBuffer> InRight, TDataReadReference<FStereoAudioFormat> InStereo)
			: InputName(InInputName)
			, Left(InLeft)
			, Right(InRight)
			, Stereo(InStereo)
			{
			}

			virtual ~FInputOperator() {}


			virtual FDataReferenceCollection GetInputs() const override
			{
				FDataReferenceCollection Inputs;

				Inputs.AddDataReadReference(InputName, Stereo);

				return Inputs;
			}

			virtual FDataReferenceCollection GetOutputs() const override
			{
				FDataReferenceCollection Outputs;

				Outputs.AddDataReadReference(StereoAudioFormatVertexKeys::LeftChannelVertexKey, Left);
				Outputs.AddDataReadReference(StereoAudioFormatVertexKeys::RightChannelVertexKey, Right);

				return Outputs;
			}

			virtual FExecuteFunction GetExecuteFunction() override
			{
				return nullptr;
			}

		private:

			FVertexName InputName;
			TDataReadReference<FAudioBuffer> Left;
			TDataReadReference<FAudioBuffer> Right;
			TDataReadReference<FStereoAudioFormat> Stereo;
		};

		class FInputOperatorFactory : public IOperatorFactory
		{
		public:
			FInputOperatorFactory(const FVertexName& InInputName)
			: InputName(InInputName)
			{
			}

			TUniquePtr<IOperator> CreateOperator(const FCreateOperatorParams& InParams, FBuildErrorArray& OutErrors) override
			{
				// Split a stereo signal into left/right.
				TDataReadReference<FStereoAudioFormat> Stereo = InParams.InputDataReferences.GetDataReadReferenceOrConstruct<FStereoAudioFormat>(InputName, InParams.OperatorSettings);
				TDataReadReference<FAudioBuffer> Left = Stereo->GetLeft();
				TDataReadReference<FAudioBuffer> Right = Stereo->GetRight();

				return MakeUnique<FInputOperator>(InputName, Left, Right, Stereo);
			}

		private:
			FVertexName InputName;
		};

		static FVertexInterface GetVertexInterface(const FVertexName& InVertexName)
		{
			return FVertexInterface(
				FInputVertexInterface(
					TInputDataVertexModel<FStereoAudioFormat>(InVertexName, METASOUND_LOCTEXT("Metasound_StereoInputVertexDescription", "Stereo Input."))
				),
				FOutputVertexInterface(
					TOutputDataVertexModel<FAudioBuffer>(StereoAudioFormatVertexKeys::LeftChannelVertexKey, METASOUND_LOCTEXT("Metasound_LeftStereoInputVertexDescription", "Left channel audio output.")),
					TOutputDataVertexModel<FAudioBuffer>(StereoAudioFormatVertexKeys::RightChannelVertexKey, METASOUND_LOCTEXT("Metasound_RightStereoInputVertexDescription", "Right channel audio output."))
				)
			);
		}

		static FNodeClassMetadata GetNodeClassMetadata(const FVertexName& InInputName)
		{
			FNodeClassMetadata Info;

			Info.ClassName = { "Input", GetMetasoundDataTypeName<FStereoAudioFormat>(), "" };
			Info.MajorVersion = 1;
			Info.MinorVersion = 0;
			Info.DisplayName = METASOUND_LOCTEXT_FORMAT("Metasound_InputNodeDisplayNameFormat", "Input {0}", GetMetasoundDataTypeDisplayText<FStereoAudioFormat>());
			Info.Description = METASOUND_LOCTEXT("Metasound_InputNodeDescription", "Input from the parent Metasound graph.");
			Info.Author = PluginAuthor;
			Info.PromptIfMissing = PluginNodeMissingPrompt;
			Info.DefaultInterface = GetVertexInterface(InInputName);

			return Info;
		};


	public:
		static constexpr bool bCanRegister = true;

		TInputNode(const FVertexName& InInstanceName, const FGuid& InInstanceID, const FVertexName& InVertexName, FLiteral&& InLiteral)
		:	FNode(InInstanceName, InInstanceID, GetNodeClassMetadata(InVertexName))
		,	VertexInterface(GetVertexInterface(InVertexName))
		,	Factory(MakeShared<FInputOperatorFactory, ESPMode::ThreadSafe>(InVertexName))
		{
		}

		const FVertexInterface& GetVertexInterface() const override
		{
			return VertexInterface;
		}

		bool SetVertexInterface(const FVertexInterface& InInterface) override
		{
			return VertexInterface == InInterface;
		}

		bool IsVertexInterfaceSupported(const FVertexInterface& InInterface) const 
		{
			return VertexInterface == InInterface;
		}

		TSharedRef<IOperatorFactory, ESPMode::ThreadSafe> GetDefaultOperatorFactory() const override
		{
			return Factory;
		}

	private:
		FVertexInterface VertexInterface;

		TSharedRef<FInputOperatorFactory, ESPMode::ThreadSafe> Factory;

	};

	// TOutputNode<> specialization for FMonoAudioFormat. Allows an audio buffer
	// to be directly connected to an mono audio output.
	template<>
	class METASOUNDSTANDARDNODES_API TOutputNode<FMonoAudioFormat> : public FNode
	{
		class FOutputOperator : public IOperator
		{
		public:
			FOutputOperator(const FVertexName& InOutputName, TDataReadReference<FAudioBuffer> InCenter, TDataReadReference<FMonoAudioFormat> InMono)
			: OutputName(InOutputName)
			, Center(InCenter)
			, Mono(InMono)
			{
			}

			virtual ~FOutputOperator() {}

			virtual FDataReferenceCollection GetInputs() const override
			{
				FDataReferenceCollection Inputs;

				Inputs.AddDataReadReference(OutputName, Center);

				return Inputs;
			}

			virtual FDataReferenceCollection GetOutputs() const override
			{
				FDataReferenceCollection Outputs;

				Outputs.AddDataReadReference(OutputName, Mono);

				return Outputs;
			}

			virtual FExecuteFunction GetExecuteFunction() override
			{
				return nullptr;
			}

		private:

			FVertexName OutputName;
			TDataReadReference<FAudioBuffer> Center;
			TDataReadReference<FMonoAudioFormat> Mono;
		};

		class FOutputOperatorFactory : public IOperatorFactory
		{
		public:
			FOutputOperatorFactory(const FVertexName& InOutputName)
			: OutputName(InOutputName)
			{
			}

			TUniquePtr<IOperator> CreateOperator(const FCreateOperatorParams& InParams, FBuildErrorArray& OutErrors) override
			{
				TDataReadReference<FAudioBuffer> Center = InParams.InputDataReferences.GetDataReadReferenceOrConstruct<FAudioBuffer>(OutputName, InParams.OperatorSettings);

				TDataReadReference<FMonoAudioFormat> Mono = TDataReadReferenceFactory<FMonoAudioFormat>::CreateExplicitArgs(InParams.OperatorSettings, WriteCast(Center));

				return MakeUnique<FOutputOperator>(OutputName, Center, Mono);
			}

		private:
			FVertexName OutputName;
		};

		static FVertexInterface GetVertexInterface(const FVertexName& InVertexName)
		{
			return FVertexInterface(
				FInputVertexInterface(
					TInputDataVertexModel<FAudioBuffer>(InVertexName, METASOUND_LOCTEXT("Metasound_CenterMonoOutputVertexDescription", "Center channel audio output."))
				),
				FOutputVertexInterface(
					TOutputDataVertexModel<FMonoAudioFormat>(InVertexName, METASOUND_LOCTEXT("Metasound_MonoOutputVertexDescription", "Mono Output."))
				)
			);
		}

		static FNodeClassMetadata GetNodeClassMetadata(const FVertexName& InOutputName)
		{
			FNodeClassMetadata Info;

			Info.ClassName = { "Output", GetMetasoundDataTypeName<FMonoAudioFormat>(), "" };
			Info.MajorVersion = 1;
			Info.MinorVersion = 0;
			Info.DisplayName = METASOUND_LOCTEXT_FORMAT("Metasound_OutputNodeDisplayNameFormat", "Output {0}", GetMetasoundDataTypeDisplayText<FMonoAudioFormat>());
			Info.Description = METASOUND_LOCTEXT("Metasound_OutputNodeDescription", "Output from the parent Metasound graph.");
			Info.Author = PluginAuthor;
			Info.PromptIfMissing = PluginNodeMissingPrompt;
			Info.DefaultInterface = GetVertexInterface(InOutputName);

			return Info;
		};


	public:
		TOutputNode(const FVertexName& InInstanceName, const FGuid& InInstanceID, const FVertexName& InVertexName)
		:	FNode(InInstanceName, InInstanceID, GetNodeClassMetadata(InVertexName))
		,	VertexInterface(GetVertexInterface(InVertexName))
		,	Factory(MakeShared<FOutputOperatorFactory, ESPMode::ThreadSafe>(InVertexName))
		{
		}

		const FVertexInterface& GetVertexInterface() const override
		{
			return VertexInterface;
		}

		bool SetVertexInterface(const FVertexInterface& InInterface) override
		{
			return VertexInterface == InInterface;
		}

		bool IsVertexInterfaceSupported(const FVertexInterface& InInterface) const 
		{
			return VertexInterface == InInterface;
		}

		TSharedRef<IOperatorFactory, ESPMode::ThreadSafe> GetDefaultOperatorFactory() const override
		{
			return Factory;
		}

	private:
		FVertexInterface VertexInterface;

		TSharedRef<FOutputOperatorFactory, ESPMode::ThreadSafe> Factory;

	};

	// TInputNode<> specializastion for FMonoAudioFormat. Allows an input mono audio
	// format to be exposed as a single buffer.
	template<>
	class METASOUNDSTANDARDNODES_API TInputNode<FMonoAudioFormat> : public FNode
	{
		class FInputOperator : public IOperator
		{
		public:

			FInputOperator(const FVertexName& InInputName, TDataReadReference<FAudioBuffer> InCenter, TDataReadReference<FMonoAudioFormat> InMono)
			: InputName(InInputName)
			, Center(InCenter)
			, Mono(InMono)
			{
			}

			virtual ~FInputOperator() {}


			virtual FDataReferenceCollection GetInputs() const override
			{
				FDataReferenceCollection Inputs;

				Inputs.AddDataReadReference(InputName, Mono);

				return Inputs;
			}

			virtual FDataReferenceCollection GetOutputs() const override
			{
				FDataReferenceCollection Outputs;

				Outputs.AddDataReadReference(InputName, Center);

				return Outputs;
			}

			virtual FExecuteFunction GetExecuteFunction() override
			{
				return nullptr;
			}

		private:

			FVertexName InputName;
			TDataReadReference<FAudioBuffer> Center;
			TDataReadReference<FMonoAudioFormat> Mono;
		};

		class FInputOperatorFactory : public IOperatorFactory
		{
		public:
			FInputOperatorFactory(const FVertexName& InInputName)
			: InputName(InInputName)
			{
			}

			TUniquePtr<IOperator> CreateOperator(const FCreateOperatorParams& InParams, FBuildErrorArray& OutErrors) override
			{
				TDataReadReference<FMonoAudioFormat> Mono = InParams.InputDataReferences.GetDataReadReferenceOrConstruct<FMonoAudioFormat>(InputName, InParams.OperatorSettings);
				TDataReadReference<FAudioBuffer> Center = Mono->GetCenter();

				return MakeUnique<FInputOperator>(InputName, Center, Mono);
			}

		private:
			FVertexName InputName;
		};

		static FVertexInterface GetVertexInterface(const FVertexName& InVertexName)
		{
			return FVertexInterface(
				FInputVertexInterface(
					TInputDataVertexModel<FMonoAudioFormat>(InVertexName, METASOUND_LOCTEXT("Metasound_MonoInputVertexDescription", "Mono Input."))
				),
				FOutputVertexInterface(
					TOutputDataVertexModel<FAudioBuffer>(InVertexName, METASOUND_LOCTEXT("Metasound_CenterMonoInputVertexDescription", "Center channel audio output."))
				)
			);
		}

		static FNodeClassMetadata GetNodeClassMetadata(const FVertexName& InInputName)
		{
			FNodeClassMetadata Info;

			Info.ClassName = { "Input", GetMetasoundDataTypeName<FMonoAudioFormat>(), "" };
			Info.MajorVersion = 1;
			Info.MinorVersion = 0;
			Info.DisplayName = METASOUND_LOCTEXT_FORMAT("Metasound_InputNodeDisplayNameFormat", "Input {0}", GetMetasoundDataTypeDisplayText<FMonoAudioFormat>());
			Info.Description = METASOUND_LOCTEXT("Metasound_InputNodeDescription", "Input from the parent Metasound graph.");
			Info.Author = PluginAuthor;
			Info.PromptIfMissing = PluginNodeMissingPrompt;
			Info.DefaultInterface = GetVertexInterface(InInputName);

			return Info;
		};


	public:
		static constexpr bool bCanRegister = true;

		TInputNode(const FVertexName& InInstanceName, const FGuid& InInstanceID, const FVertexName& InVertexName, FLiteral&& InLiteral)
		:	FNode(InInstanceName, InInstanceID, GetNodeClassMetadata(InVertexName))
		,	VertexInterface(GetVertexInterface(InVertexName))
		,	Factory(MakeShared<FInputOperatorFactory, ESPMode::ThreadSafe>(InVertexName))
		{
		}

		const FVertexInterface& GetVertexInterface() const override
		{
			return VertexInterface;
		}

		bool SetVertexInterface(const FVertexInterface& InInterface) override
		{
			return VertexInterface == InInterface;
		}

		bool IsVertexInterfaceSupported(const FVertexInterface& InInterface) const 
		{
			return VertexInterface == InInterface;
		}

		TSharedRef<IOperatorFactory, ESPMode::ThreadSafe> GetDefaultOperatorFactory() const override
		{
			return Factory;
		}

	private:
		FVertexInterface VertexInterface;

		TSharedRef<FInputOperatorFactory, ESPMode::ThreadSafe> Factory;

	};

	// Disable arrays of audio formats. 
	template<>
	struct TEnableArrayNodes<FMonoAudioFormat>
	{
		static constexpr bool Value = false;
	};

	template<>
	struct TEnableArrayNodes<FStereoAudioFormat>
	{
		static constexpr bool Value = false;
	};

	// Disable transmission of audio formats
	template<>
	struct TEnableTransmissionNodeRegistration<FMonoAudioFormat>
	{
		static constexpr bool Value = false;
	};

	template<>
	struct TEnableTransmissionNodeRegistration<FStereoAudioFormat>
	{
		static constexpr bool Value = false;
	};

	// Disable auto converts using audio format constructors
	template<typename FromDataType>
	struct TEnableAutoConverterNodeRegistration<FromDataType, FMonoAudioFormat>
	{
		static constexpr bool Value = !std::is_arithmetic<FromDataType>::value;
	};

	template<typename FromDataType>
	struct TEnableAutoConverterNodeRegistration<FromDataType, FStereoAudioFormat>
	{
		static constexpr bool Value = !std::is_arithmetic<FromDataType>::value;
	};

	// Disable arrays of audio formats
	template<>
	struct TEnableAutoArrayTypeRegistration<FMonoAudioFormat>
	{
		static constexpr bool Value = false;
	};

	template<>
	struct TEnableAutoArrayTypeRegistration<FStereoAudioFormat>
	{
		static constexpr bool Value = false;
	};
}

// Data type registration has to happen after TInputNode<> and TOutputNode<> specializations
// so that the registration macro has access to the specializations.
REGISTER_METASOUND_DATATYPE(Metasound::FMonoAudioFormat, "Audio:Mono");
REGISTER_METASOUND_DATATYPE(Metasound::FStereoAudioFormat, "Audio:Stereo");
//REGISTER_METASOUND_DATATYPE(Metasound::FMultichannelAudioFormat, "Audio:Multichannel", ELiteralType::Integer);

#undef LOCTEXT_NAMESPACE // MetasoundStandardNodes
