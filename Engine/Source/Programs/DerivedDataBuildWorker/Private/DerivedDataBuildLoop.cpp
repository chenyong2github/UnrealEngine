// Copyright Epic Games, Inc. All Rights Reserved.

#include "DerivedDataBuildLoop.h"

#include "Compression/CompressedBuffer.h"
#include "DerivedDataPayload.h"
#include "Memory/SharedBuffer.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/WildcardString.h"
#include "Serialization/CompactBinaryValidation.h"
#include "Serialization/CompactBinaryWriter.h"

DEFINE_LOG_CATEGORY_STATIC(LogDerivedDataBuildLoop, Log, All);

namespace UE::DerivedData
{

static void ParseCommandLine( const TCHAR* CmdLine, TArray<FString>& Tokens, TArray<FString>& Switches )
{
	FString NextToken;
	while ( FParse::Token(CmdLine, NextToken, false) )
	{
		if ( **NextToken == TCHAR('-') )
		{
			new(Switches) FString(NextToken.Mid(1));
		}
		else
		{
			new(Tokens) FString(NextToken);
		}
	}
}

static FSharedBuffer LoadFile(const FString& Path)
{
	FSharedBuffer Buffer;

	if (TUniquePtr<FArchive> Ar{IFileManager::Get().CreateFileReader(*Path, FILEREAD_Silent)})
	{
		const int64 TotalSize = Ar->TotalSize();
		FUniqueBuffer MutableBuffer = FUniqueBuffer::Alloc(uint64(TotalSize));
		Ar->Serialize(MutableBuffer.GetData(), TotalSize);
		if (Ar->Close())
		{
			Buffer = MutableBuffer.MoveToShared();
		}
	}

	return Buffer;
}

static FIoHash HashAndWriteToCompressedBufferFile(const FString& Directory, const void* InData, uint64 InDataSize)
{
	FIoHash DataHash = FIoHash::HashBuffer(InData, InDataSize);
	TStringBuilder<128> DataHashStringBuilder;
	DataHashStringBuilder << DataHash;

	FCompressedBuffer CompressedBufferContents = FCompressedBuffer::Compress(NAME_None, FSharedBuffer::MakeView(InData, InDataSize));
	if (TUniquePtr<FArchive> FileAr{IFileManager::Get().CreateFileWriter(*(Directory / *DataHashStringBuilder), FILEWRITE_NoReplaceExisting)})
	{
		*FileAr << CompressedBufferContents;
	}
	return DataHash;
}

class FWorkerBuildContext : public FBuildContext
{
public:
	FWorkerBuildContext(const FBuildLoop::FBuildActionRecord& InBuildActionRecord)
	: BuildActionRecord(InBuildActionRecord)
	{
		BuildWriter.BeginObject("BuildOutput");
		BuildWriter.BeginArray("Payloads");
	}

	virtual ~FWorkerBuildContext()
	{
		BuildWriter.EndArray();

		BuildWriter.EndObject();

		if (TUniquePtr<FArchive> FileAr{IFileManager::Get().CreateFileWriter(*BuildActionRecord.OutputFilePath)})
		{
			BuildWriter.Save(*FileAr);
		}
	}

	virtual FCbObject GetConstant(FStringView Key) const override
	{
		TStringConversion<FTCHARToUTF8_Convert> ConvertedKey(Key.GetData(), Key.Len());
		return BuildActionRecord.BuildAction["Constants"].AsObject()[ConvertedKey].AsObject();
	}

	virtual FSharedBuffer GetInput(FStringView Key) const override
	{
		TStringConversion<FTCHARToUTF8_Convert> ConvertedKey(Key.GetData(), Key.Len());
		FIoHash InputHash(BuildActionRecord.BuildAction["Inputs"].AsObject()[ConvertedKey].AsBinaryAttachment());
		if (!InputHash.IsZero())
		{
			TStringBuilder<256> Path;
			Path << BuildActionRecord.InputPath << TEXT('/') << InputHash;
			return FCompressedBuffer::FromCompressed(LoadFile(*Path)).Decompress();
		}
		return FSharedBuffer();
	}

	virtual void AddPayload(const FPayload& Payload) override
	{
		AddPayload(Payload.GetId(), Payload.GetData());
	}
	virtual void AddPayload(const FPayloadId& Id, const FCompressedBuffer& Buffer)
	{
		BuildWriter.BeginObject();
		BuildWriter.AddObjectId("Id", FCbObjectId(Id.GetView()));
		BuildWriter.AddInteger("RawSize", Buffer.GetRawSize());

		FIoHash IoHash(Buffer.GetRawHash());
		TStringBuilder<128> DataHashStringBuilder;
		DataHashStringBuilder << IoHash;
		if (TUniquePtr<FArchive> FileAr{IFileManager::Get().CreateFileWriter(*(BuildActionRecord.OutputPath / *DataHashStringBuilder), FILEWRITE_NoReplaceExisting)})
		{
			*FileAr << const_cast<FCompressedBuffer&>(Buffer);
		}
		BuildWriter.AddBinaryAttachment("RawHash", IoHash);
		BuildWriter.EndObject();
	}
	virtual void AddPayload(const FPayloadId& Id, const FSharedBuffer& Buffer) override
	{
		BuildWriter.BeginObject();
		BuildWriter.AddObjectId("Id", FCbObjectId(Id.GetView()));
		BuildWriter.AddInteger("RawSize", Buffer.GetSize());
		FIoHash Hash = HashAndWriteToCompressedBufferFile(BuildActionRecord.OutputPath, Buffer.GetData(), Buffer.GetSize());
		BuildWriter.AddBinaryAttachment("RawHash", Hash);
		BuildWriter.EndObject();
	}
	virtual void AddPayload(const FPayloadId& Id, const FCbObject& Object) override
	{
		AddPayload(Id, Object.GetBuffer());
	}

	virtual void SetCachePolicy(ECachePolicy Policy) override
	{
		checkNoEntry();
	}

	virtual void BeginAsyncBuild() override
	{
		check(false);
	}

	virtual void EndAsyncBuild() override
	{
		check(false);
	}

private:
	FCbWriter BuildWriter;
	const FBuildLoop::FBuildActionRecord& BuildActionRecord;
};

FBuildLoop::FBuildActionRecord::FBuildActionRecord(const FString& InSourceFilePath, const FString& InCommonInputPath, const FString& InCommonOutputPath, FSharedBuffer&& InSharedBuffer)
: SourceFilePath(InSourceFilePath)
, OutputFilePath(FPaths::ChangeExtension(InSourceFilePath, TEXT("uddbo")))
, InputPath(InCommonInputPath.IsEmpty() ? FPaths::GetPath(InSourceFilePath) / TEXT("Inputs") : InCommonInputPath)
, OutputPath(InCommonOutputPath.IsEmpty() ? FPaths::GetPath(InSourceFilePath) / TEXT("Outputs") : InCommonOutputPath)
, BuildAction(MoveTemp(InSharedBuffer))
{
}

bool FBuildLoop::Init()
{
	TArray<FString> Tokens;
	TArray<FString> Switches;
	ParseCommandLine(FCommandLine::Get(), Tokens, Switches);

	TArray<FString> InputDirectoryPaths;
	TArray<FString> OutputDirectoryPaths;
	TArray<FString> BuildFilePathPatterns;
	for (int32 SwitchIdx = 0; SwitchIdx < Switches.Num(); SwitchIdx++)
	{
		const FStringView Switch = Switches[SwitchIdx];

		auto GetSwitchValueElements = [&Switch](const FStringView SwitchKey) -> TArray<FString>
		{
			TArray<FString> ValueElements;
			if (Switch.StartsWith(SwitchKey) == true)
			{
				FStringView ValuesList = Switch.Right(Switch.Len() - SwitchKey.Len());

				// Allow support for -KEY=Value1+Value2+Value3 as well as -KEY=Value1 -KEY=Value2
				for (int32 PlusIdx = ValuesList.Find(TEXT("+"), ESearchCase::CaseSensitive); PlusIdx != INDEX_NONE; PlusIdx = ValuesList.Find(TEXT("+"), ESearchCase::CaseSensitive))
				{
					ValueElements.Emplace(ValuesList.Left(PlusIdx));

					ValuesList.RightInline(ValuesList.Len() - (PlusIdx + 1));
				}
				ValueElements.Emplace(ValuesList);
			}
			return ValueElements;
		};

		BuildFilePathPatterns += GetSwitchValueElements(TEXT("B="));
		BuildFilePathPatterns += GetSwitchValueElements(TEXT("BUILD="));

		InputDirectoryPaths += GetSwitchValueElements(TEXT("I="));
		InputDirectoryPaths += GetSwitchValueElements(TEXT("INPUT="));

		OutputDirectoryPaths += GetSwitchValueElements(TEXT("O="));
		OutputDirectoryPaths += GetSwitchValueElements(TEXT("OUTPUT="));
	}

	switch (InputDirectoryPaths.Num())
	{
		case 1:
		CommonInputPath = InputDirectoryPaths[0];
		if (FPaths::IsRelative(CommonInputPath))
		{
			CommonInputPath = FPaths::Combine(FPaths::LaunchDir(), CommonInputPath);
		}
		case 0: // deliberate fallthrough
		break;
		default:
		UE_LOG(LogDerivedDataBuildLoop, Error, TEXT("A maximum of one input directory can be specified, but '%d' were specified."), InputDirectoryPaths.Num());
		return false;
	}

	switch (OutputDirectoryPaths.Num())
	{
		case 1:
		CommonOutputPath = OutputDirectoryPaths[0];
		if (FPaths::IsRelative(CommonOutputPath))
		{
			CommonOutputPath = FPaths::Combine(FPaths::LaunchDir(), CommonOutputPath);
		}
		case 0: // deliberate fallthrough
		break;
		default:
		UE_LOG(LogDerivedDataBuildLoop, Error, TEXT("A maximum of one output directory can be specified, but '%d' were specified."), OutputDirectoryPaths.Num());
		return false;
	}

	if (BuildFilePathPatterns.Num() == 0)
	{
		UE_LOG(LogDerivedDataBuildLoop, Error, TEXT("No build files specified on the commandline."));
		return false;
	}

	for (const FString& BuildFilePathPattern : BuildFilePathPatterns)
	{
		TArray<FString> BuildActionFilePaths;
		FWildcardString BuildFilePathWildcardString(BuildFilePathPattern);
		if (BuildFilePathWildcardString.ContainsWildcards())
		{
			// TODO: Support wildcard matching - at least for filename pattern in a single directory.
			UE_LOG(LogDerivedDataBuildLoop, Error, TEXT("Wildcards in the build commandline arguments are currently unsupported: '%s'"), *BuildFilePathWildcardString);
			return false;
		}
		else
		{
			if (FPaths::IsRelative(BuildFilePathPattern))
			{
				BuildActionFilePaths.Add(FPaths::Combine(FPaths::LaunchDir(), BuildFilePathPattern));
			}
			else
			{
				BuildActionFilePaths.Add(BuildFilePathPattern);
			}
		}

		for (const FString& BuildActionFilePath : BuildActionFilePaths)
		{
			UE_LOG(LogDerivedDataBuildLoop, Log, TEXT("Loading build file: '%s'"), *BuildActionFilePath);
			FSharedBuffer BuildActionFileBuffer = LoadFile(BuildActionFilePath);

			if (BuildActionFileBuffer.IsNull())
			{
				UE_LOG(LogDerivedDataBuildLoop, Error, TEXT("Missing build file: '%s'"), *BuildActionFilePath);
				return false;
			}

			if (ValidateCompactBinaryRange(BuildActionFileBuffer, ECbValidateMode::Default) != ECbValidateError::None)
			{
				UE_LOG(LogDerivedDataBuildLoop, Error, TEXT("Invalid build file: '%s'"), *BuildActionFilePath);
				return false;
			}

			BuildActionRecords.Emplace(BuildActionFilePath, CommonInputPath, CommonOutputPath, MoveTemp(BuildActionFileBuffer));
		}
	}

	if (BuildActionRecords.Num() == 0)
	{
		UE_LOG(LogDerivedDataBuildLoop, Error, TEXT("No build actions to operate on."));
		return false;
	}

	return true;
}

void FBuildLoop::PerformBuilds(const FBuildFunctionCallback& BuildFunctionCallback)
{
	for (const FBuildActionRecord& BuildActionRecord : BuildActionRecords)
	{
		FWorkerBuildContext Context(BuildActionRecord);
		BuildFunctionCallback(FName(BuildActionRecord.BuildAction["Function"].AsObject()["Name"].AsString()), Context);
	}
}

void FBuildLoop::Teardown()
{
}

}
