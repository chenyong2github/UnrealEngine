// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundWaveWriterNode.h"

#include "HAL/FileManager.h"
#include "MetasoundBuildError.h"
#include "MetasoundEnumRegistrationMacro.h"
#include "MetasoundExecutableOperator.h"
#include "MetasoundNodeRegistrationMacro.h"
#include "MetasoundPrimitives.h"
#include "MetasoundStandardNodesCategories.h"
#include "MetasoundStandardNodesNames.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"

#define LOCTEXT_NAMESPACE "MetasoundStandardNodes"

namespace Metasound
{	
	// Incremental wave writer class.
	class FWaveWriter
	{		
		// Local definition so we don't depend on platform includes.
		enum EFormatType { IEEE_FLOAT = 0x3 }; // WAVE_FORMAT_IEEE_FLOAT
		struct FWaveFormatEx
		{
			uint16	FormatTag;
			uint16	NumChannels;
			uint32	NumSamplesPerSec;
			uint32	AverageBytesPerSec;
			uint16	BlockAlign;
			uint16	NumBitsPerSample;
			uint16	Size;
		};
		
	public:
		FWaveWriter(TUniquePtr<FArchive>&& InOutputStream, int32 InSampleRate, int32 InNumChannels, bool bInUpdateHeaderAfterEveryWrite)
			: OutputStream{ MoveTemp(InOutputStream) }
			, bUpdateHeaderAfterEveryWrite{ bInUpdateHeaderAfterEveryWrite }
		{
			WriteHeader(InSampleRate, InNumChannels);
		}

		~FWaveWriter()
		{
			UpdateHeader();
		}

		void Write(TArrayView<const float> InBuffer)
		{
			OutputStream->Serialize((void*)InBuffer.GetData(), InBuffer.GetTypeSize()*InBuffer.Num());
			
			if (bUpdateHeaderAfterEveryWrite)
			{
				UpdateHeader();
			}
		}

	private:
		void UpdateHeader()
		{
			// RIFF/fmt/data. (bytes per chunk)
			static const int32 HeaderSize = sizeof(FWaveFormatEx) + sizeof(int32) + sizeof(int32) + sizeof(int32) + sizeof(int32) + sizeof(int32);

			int32 WritePos = OutputStream->Tell();

			// update data chunk size
			OutputStream->Seek(DataSizePos);
			int32 DataSize = WritePos - DataSizePos - 4;
			*OutputStream << DataSize;

			// update top riff size
			OutputStream->Seek(RiffSizePos);
			int32 RiffSize = HeaderSize + DataSize - 4;
			*OutputStream << RiffSize;

			OutputStream->Seek(WritePos);
		}

		TUniquePtr<FArchive> OutputStream;
		int32 RiffSizePos = 0;
		int32 DataSizePos = 0;
		bool bUpdateHeaderAfterEveryWrite = false;

		void WriteHeader(int32 InSampleRate, int32 InNumChannels)
		{	
			FWaveFormatEx Fmt = { 0 };
			Fmt.NumChannels = InNumChannels;
			Fmt.NumSamplesPerSec = InSampleRate;
			Fmt.NumBitsPerSample = sizeof(float) * 8;
			Fmt.BlockAlign = (Fmt.NumBitsPerSample * InNumChannels) / 8;
			Fmt.AverageBytesPerSec = Fmt.BlockAlign * InSampleRate;
			Fmt.FormatTag = EFormatType::IEEE_FLOAT;// WAVE_FORMAT_IEEE_FLOAT;
		
			int32 ID = 'FFIR';
			*OutputStream << ID;
			RiffSizePos = OutputStream->Tell();
			int32 RiffChunkSize = 0;
			*OutputStream << RiffChunkSize;

			ID = 'EVAW';
			*OutputStream << ID;

			ID = ' tmf';
			*OutputStream << ID;
			int32 FmtSize = sizeof(Fmt);
			*OutputStream << FmtSize;
			OutputStream->Serialize((void*)&Fmt, FmtSize);

			ID = 'atad';
			*OutputStream << ID;
			DataSizePos = OutputStream->Tell();
			int32 DataChunkSize = 0;
			*OutputStream << DataChunkSize;
		}
	};

	class FFileWriteError : public FBuildErrorBase
	{
	public:
		static const FName ErrorType;

		virtual ~FFileWriteError() = default;

		FFileWriteError(const FNode& InNode, const FString& InFilename)
			: FBuildErrorBase(ErrorType, FText::Format(FTextFormat(LOCTEXT("MetasoundFileWriterErrorDescription", "File Writer Error while trying to write '{0}'")), FText::FromString(InFilename)))
		{
			AddNode(InNode);
		}
	};
	const FName FFileWriteError::ErrorType = FName(TEXT("MetasoundFileWriterError"));

	class FNumberedFileCache
	{
	public:
		static const FString Seperator;

		FNumberedFileCache(const FString& InRootPath, const FString& InExt)
			: RootPath{ InRootPath }, FileExtention{ InExt }
		{
			CacheFilenames();
		}

		FString GenerateNextNumberedFilename(const FString& InPrefix)
		{
			FScopeLock Lock{ &Cs };
			uint32& CurrentMax = FileIndexMap.FindOrAdd(InPrefix.ToUpper());
			FString Filename{ InPrefix };
			Filename.Append(*Seperator);
			Filename.AppendInt(++CurrentMax);
			Filename.Append(*FileExtention);
			return RootPath / Filename;
		}
	private:

		// Slow directory search of the root path for filenames.
		void CacheFilenames()
		{
			FScopeLock Lock{ &Cs };
			
			// Find all files, split filenames into prefix + number, saving max number we find.
			TArray<FString> Files;
			IFileManager::Get().FindFiles(Files , *RootPath, *FileExtention);
			for (const FString& i : Files)
			{
				FString Prefix, Postfix;
				if (i.Split(Seperator, &Prefix, &Postfix, ESearchCase::IgnoreCase, ESearchDir::FromEnd))
				{
					FString NumberString = FPaths::GetBaseFilename(Postfix);
					if (FCString::IsNumeric(*NumberString))
					{
						int32 Number = FCString::Atoi(*NumberString);
						if (Number >= 0)
						{
							uint32& CurrentMax = FileIndexMap.FindOrAdd(*Prefix.ToUpper());
							if (static_cast<uint32>(Number) > CurrentMax)
							{
								CurrentMax = static_cast<uint32>(Number);
							}
						}
					}
				}
			}
		}
		FCriticalSection Cs;
		FString RootPath;
		FString FileExtention;
		TMap<FString, uint32> FileIndexMap;
	};
	const FString FNumberedFileCache::Seperator{ TEXT("_") };

	class FWaveWriterOperator : public TExecutableOperator<FWaveWriterOperator>
	{
	public:
		FWaveWriterOperator(const FOperatorSettings& InSettings, FAudioBufferReadRef&& InAudioBuffer, FBoolReadRef&& InEnabled, TUniquePtr<FWaveWriter>&& InStream)
			: AudioInput{ MoveTemp(InAudioBuffer) }
			, Enabled{ MoveTemp(InEnabled) }
			, Writer{ MoveTemp(InStream) }
		{}

		FDataReferenceCollection GetInputs() const override
		{
			FDataReferenceCollection InputDataReferences;
			InputDataReferences.AddDataReadReference(AudioInputPinName, AudioInput);
			return InputDataReferences;
		}
		FDataReferenceCollection GetOutputs() const override
		{
			FDataReferenceCollection OutputDataReferences;
			return OutputDataReferences;
		}

		static FVertexInterface DeclareVertexInterface() 
		{
			static const FVertexInterface Interface(
				FInputVertexInterface(
					TInputDataVertexModel<FString>(FilenamePrefixPinName, LOCTEXT("WaveWriterFilenamePrefixDescriptionX", "Filename Prefix of file you are writing."), FString(DefaultFileName)),
					TInputDataVertexModel<bool>(EnabledPinName, LOCTEXT("WaveWriterEnabledDescription", "If this wave writer is enabled or not. File will remain open if disabled until graph termination."), true),
					TInputDataVertexModel<FAudioBuffer>(AudioInputPinName, LOCTEXT("WaveWriterAudioInputDescription", "Audio input that you want serialized."))
				),
				FOutputVertexInterface()
			);
			return Interface;
		}

		static const FNodeClassMetadata& GetNodeInfo()
		{
			auto InitNodeInfo = []() -> FNodeClassMetadata
			{
				FNodeClassMetadata Info;
				Info.ClassName = { Metasound::StandardNodes::Namespace, TEXT("WaveWriter"), Metasound::StandardNodes::AudioVariant };
				Info.MajorVersion = 1;
				Info.MinorVersion = 0;
				Info.DisplayName = LOCTEXT("Metasound_WaveWriterNodeDisplayName", "Wave Writer");
				Info.Description = LOCTEXT("Metasound_WaveWriterNodeDescription", "Write a the incoming audio signal to disk");
				Info.Author = PluginAuthor;
				Info.PromptIfMissing = PluginNodeMissingPrompt;
				Info.DefaultInterface = DeclareVertexInterface();
				Info.CategoryHierarchy.Emplace(StandardNodes::Io);
				return Info;
			};
			static const FNodeClassMetadata Info = InitNodeInfo();
			return Info;
		}

		static TUniquePtr<IOperator> CreateOperator(const FCreateOperatorParams& InParams, FBuildErrorArray& OutErrors)
		{
			const FWaveWriterNode& Node = static_cast<const FWaveWriterNode&>(InParams.Node);
			const FDataReferenceCollection& InputCol = InParams.InputDataReferences;
			const FOperatorSettings& Settings = InParams.OperatorSettings;
			const FInputVertexInterface& InputInterface = DeclareVertexInterface().GetInputInterface();
			
			static const TCHAR* WaveExt = TEXT(".wav");

			// Build cache of numbered files (do this once only).
			static TUniquePtr<FNumberedFileCache> NumberedFileCache = MakeUnique<FNumberedFileCache>(*FPaths::AudioCaptureDir(), WaveExt);
			
			FStringReadRef FilenamePrefix = InputCol.GetDataReadReferenceOrConstructWithVertexDefault<FString>(InputInterface, FilenamePrefixPinName, Settings);
			FString Filename = NumberedFileCache->GenerateNextNumberedFilename(*FilenamePrefix);

			if (TUniquePtr<FArchive> FileWriter = TUniquePtr<FArchive>(IFileManager::Get().CreateFileWriter(*Filename, IO_WRITE)))
			{
				return MakeUnique<FWaveWriterOperator>(
					Settings,
					InputCol.GetDataReadReferenceOrConstruct<FAudioBuffer>(AudioInputPinName, Settings),
					InputCol.GetDataReadReferenceOrConstructWithVertexDefault<bool>(InputInterface, EnabledPinName, Settings),
					MakeUnique<FWaveWriter>(MoveTemp(FileWriter), Settings.GetSampleRate(), 1, true)
				);
			}
			
			// Failed to open a writer object. Log an error.
			OutErrors.Emplace(MakeUnique<FFileWriteError>(Node, Filename));

			// Create a default operator with no-writer which will do nothing.
			return MakeUnique<FWaveWriterOperator>(
				Settings,
				InputCol.GetDataReadReferenceOrConstruct<FAudioBuffer>(AudioInputPinName, Settings),
				InputCol.GetDataReadReferenceOrConstructWithVertexDefault<bool>(InputInterface, EnabledPinName, Settings),
				nullptr
			);
		}

		void Execute()
		{
			if (Writer && *Enabled)
			{
				Writer->Write(MakeArrayView(AudioInput->GetData(), AudioInput->Num()));
			}
		}

	protected:
		FAudioBufferReadRef AudioInput;
		FBoolReadRef Enabled;
		TUniquePtr<FArchive> Output;
		TUniquePtr<FWaveWriter> Writer;
		static constexpr const TCHAR* DefaultFileName = TEXT("Output");
		static constexpr const TCHAR* AudioInputPinName = TEXT("In");
		static constexpr const TCHAR* EnabledPinName = TEXT("Enabled");
		static constexpr const TCHAR* FilenamePrefixPinName = TEXT("Filename Prefix");
	};

	// Linkage for constexpr on older clang.
	constexpr const TCHAR* FWaveWriterOperator::DefaultFileName;
	constexpr const TCHAR* FWaveWriterOperator::AudioInputPinName;
	constexpr const TCHAR* FWaveWriterOperator::EnabledPinName;
	constexpr const TCHAR* FWaveWriterOperator::FilenamePrefixPinName;

	FWaveWriterNode::FWaveWriterNode(const FString& InName, const FGuid& InInstanceID)
		: FNodeFacade{ InName, InInstanceID, TFacadeOperatorClass<FWaveWriterOperator>() }
	{}

	FWaveWriterNode::FWaveWriterNode(const FNodeInitData& InInitData)
		: FWaveWriterNode{ InInitData.InstanceName, InInitData.InstanceID }
	{}

	METASOUND_REGISTER_NODE(FWaveWriterNode)
}

#undef LOCTEXT_NAMESPACE
