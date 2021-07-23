// Copyright Epic Games, Inc. All Rights Reserved.

#include "DerivedDataBuildRemoteExecutor.h"

#include "Algo/Find.h"
#include "DerivedDataBuild.h"
#include "DerivedDataBuildAction.h"
#include "DerivedDataBuildInputs.h"
#include "DerivedDataBuildOutput.h"
#include "DerivedDataBuildWorker.h"
#include "Features/IModularFeatures.h"
#include "IContentAddressableStorage.h"
#include "IRemoteExecutor.h"
#include "Messages.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Optional.h"
#include "Misc/PathViews.h"
#include "Modules/ModuleManager.h"
#include "Serialization/CompactBinaryValidation.h"
#include "Serialization/CompactBinaryWriter.h"
#include <atomic>

static uint32 GetTypeHash(const FDigest& Digest)
{
	return GetTypeHash(Digest.Hash);
}

static bool operator==(const FDigest& A, const FDigest& B)
{
	return A.Hash == B.Hash;
}

namespace UE::DerivedData
{

DEFINE_LOG_CATEGORY_STATIC(LogDerivedDataBuildRemoteExecutor, Log, All);

class FRemoteBuildWorkerExecutor final: public IBuildWorkerExecutor
{
public:
	FRemoteBuildWorkerExecutor()
	: GlobalExecutionTimeoutSeconds(-1)
	, Salt(TEXT("807c6a49-0657-49f3-b498-fd457213c0a7"))
	, RemoteExecutor(nullptr)
	, ContentAddressableStorage(nullptr)
	, bEnabled(false)
	{
		check(IsInGameThread()); // initialization from the main thread is expected to allow config reading for the limiting heuristics
		check(GConfig && GConfig->IsReadyForUse());

		GConfig->GetString(TEXT("DerivedDataBuildRemoteExecutor"), TEXT("InstanceName"), InstanceName, GEngineIni);
		GConfig->GetInt(TEXT("DerivedDataBuildRemoteExecutor"), TEXT("GlobalExecutionTimeoutSeconds"), GlobalExecutionTimeoutSeconds, GEngineIni);

		const FName RemoteExecutionFeatureName(TEXT("RemoteExecution"));
		IModularFeatures& ModularFeatures = IModularFeatures::Get();

		if (FParse::Param(FCommandLine::Get(), TEXT("DDC2RemoteExecution")))
		{
			FModuleManager::Get().LoadModule("BazelExecutor");
			if (ModularFeatures.IsModularFeatureAvailable(RemoteExecutionFeatureName))
			{
				RemoteExecutor = &ModularFeatures.GetModularFeature<IRemoteExecutor>(RemoteExecutionFeatureName);
				if (RemoteExecutor)
				{
					ContentAddressableStorage = RemoteExecutor->GetContentAddressableStorage();
				}
			}
		}

		bEnabled = (RemoteExecutor != nullptr) && (ContentAddressableStorage != nullptr) && !InstanceName.IsEmpty();
		if (bEnabled)
		{
			ModularFeatures.RegisterModularFeature(IBuildWorkerExecutor::GetFeatureName(), this);
		}
	}

	virtual ~FRemoteBuildWorkerExecutor()
	{
		if (bEnabled)
		{
			IModularFeatures::Get().UnregisterModularFeature(IBuildWorkerExecutor::GetFeatureName(), this);
		}
	}

	enum class ENodeType
	{
		File,
		Directory,
		//Symlink,
	};


	enum class EFileType
	{
		Worker,
		Input,
		BuildAction,
	};

	static const TCHAR* LexToString(EFileType FileType)
	{
		switch (FileType)
		{
			case EFileType::Worker:
				return TEXT("Worker");
			case EFileType::Input:
				return TEXT("Input");
			case EFileType::BuildAction:
				return TEXT("BuildAction");
			default:
				return TEXT("Unknown");
		}
	}

	struct FVariantIndex
	{
		ENodeType NodeType;
		int32 Index;

		FVariantIndex(ENodeType InNodeType, int32 InIndex)
		: NodeType(InNodeType)
		, Index(InIndex)
		{
		}
	};

	struct FMerkleTreeFileBuilder
	{
		FFileNode File;
		EFileType Type;
		FCompositeBuffer ContentBytes;
	};

	struct FMerkleTreeDirectoryBuilder
	{
		FStringView Name;
		FDirectory Directory;
		TOptional<FDigest> Digest;
		TArray<char> ContentBytes;
		TArray<int32> SubDirIndices;
	};

	struct FRemoteExecutionState
	{
		const FBuildAction& BuildAction;
		const FOptionalBuildInputs& BuildInputs;
		const FBuildWorker& BuildWorker;
		EBuildPolicy BuildPolicy;
		EPriority BuildPriority;

		// Unordered arrays that are indexed into
		TArray<FMerkleTreeDirectoryBuilder> Directories;
		TArray<FMerkleTreeFileBuilder> Files;

		// Lookup tables for indexing in different scenarios
		TMultiMap<FDigest, FVariantIndex> DigestFilesystemIndex;
		TMap<FStringView, int32> PathToDirectoryIndex;
		TMap<int32, FString> FileIndexToInputKey;
		FString BaseDirectoryPath;

		// Unique items in the tree
		FCommand Command;
		TArray<char> CommandContentBytes;
		FAction Action;
		TArray<char> ActionContentBytes;
		FDigest ActionDigest;
		FCompositeBuffer BuildActionContentBytes {};
		FDigest BuildActionDigest;
		TArray<FString> InputPaths;

		// Step 1: Find missing blobs
		FFindMissingBlobsRequest FindMissingBlobsRequest;
		FFindMissingBlobsResponse FindMissingBlobsResponse;

		// Step 3: Batch update blobs (upload)
		FBatchUpdateBlobsRequest BatchUpdateBlobsRequest;
		FBatchUpdateBlobsResponse BatchUpdateBlobsResponse;

		// Step 4: Execute
		FExecuteRequest ExecuteRequest;
		FExecuteResponse ExecuteResponse;

		// Step 5: Batch read blobs (download)
		FBatchReadBlobsRequest BatchReadBlobsRequest;
		FBatchReadBlobsResponse BatchReadBlobsResponse;
	};

	static FMerkleTreeDirectoryBuilder& GetOrAddMerkleTreeDirectory(FRemoteExecutionState& State, FStringView Path, int32& OutDirectoryBuilderIndex)
	{
		int32& DirectoryBuilderIndex = State.PathToDirectoryIndex.FindOrAdd(Path, INDEX_NONE);
		if (DirectoryBuilderIndex == INDEX_NONE)
		{
			DirectoryBuilderIndex = State.Directories.Num();
			FMerkleTreeDirectoryBuilder& NewNode = State.Directories.AddDefaulted_GetRef();
			NewNode.Name = Path.IsEmpty() ? Path : FPathViews::GetCleanFilename(Path);
		}

		OutDirectoryBuilderIndex = DirectoryBuilderIndex;

		int32 ContainingDirectoryIndex = DirectoryBuilderIndex;
		while (!Path.IsEmpty())
		{
			int32 SubDirIndex = ContainingDirectoryIndex;
			Path = FPathViews::GetPath(Path);
			GetOrAddMerkleTreeDirectory(State, Path, ContainingDirectoryIndex).SubDirIndices.AddUnique(SubDirIndex);
		}

		return State.Directories[OutDirectoryBuilderIndex];
	}

	static int32 AddMerkleTreeFile(FRemoteExecutionState& State, FStringView Path, const FIoHash& RawHash, uint64 RawSize, bool bIsExecutable, EFileType FileType, FCompositeBuffer ContentBytes = FCompositeBuffer::Null)
	{
		FStringView ContainingDirectoryPath = FPathViews::GetPath(Path);
		int32 ContainingDirectoryIndex = INDEX_NONE;
		FMerkleTreeDirectoryBuilder* ContainingDirectoryBuilder = &GetOrAddMerkleTreeDirectory(State, ContainingDirectoryPath, ContainingDirectoryIndex);
		FFileNode& NewNode = ContainingDirectoryBuilder->Directory.Files.AddDefaulted_GetRef();
		NewNode.Name = FPathViews::GetCleanFilename(Path);
		NewNode.Digest.Hash = RawHash;
		NewNode.Digest.SizeBytes = RawSize;
		NewNode.IsExecutable = bIsExecutable;

		int32 NewFileIndex = State.Files.Num();
		State.DigestFilesystemIndex.Add(NewNode.Digest, FVariantIndex(ENodeType::File, NewFileIndex));
		FMerkleTreeFileBuilder& FileBuilder = State.Files.AddDefaulted_GetRef();
		FileBuilder.File = NewNode; // Duplicates the node in the state's file array
		FileBuilder.Type = FileType;
		if (ContentBytes)
		{
			FileBuilder.ContentBytes = ContentBytes;
		}

		return NewFileIndex;
	}

	const FDigest& BuildMerkleTreeDirectoryDigest(FRemoteExecutionState& State, int32 Index)
	{
		FMerkleTreeDirectoryBuilder& DirBuilder = State.Directories[Index];

		if (DirBuilder.Digest.IsSet())
		{
			return DirBuilder.Digest.GetValue();
		}

		for (int32 SubDirIndex : DirBuilder.SubDirIndices)
		{
			FDirectoryNode& SubDirNode = DirBuilder.Directory.Directories.AddDefaulted_GetRef();
			SubDirNode.Name = State.Directories[SubDirIndex].Name;
			SubDirNode.Digest = BuildMerkleTreeDirectoryDigest(State, SubDirIndex);
			State.DigestFilesystemIndex.Add(SubDirNode.Digest, FVariantIndex(ENodeType::Directory, SubDirIndex));
		}

		DirBuilder.Directory.Directories.Sort([] (const FDirectoryNode& A, const FDirectoryNode& B)
		{
			return A.Name < B.Name;
		});

		DirBuilder.Directory.Files.Sort([] (const FFileNode& A, const FFileNode& B)
		{
			return A.Name < B.Name;
		});

		ContentAddressableStorage->ToBlob(DirBuilder.Directory, DirBuilder.ContentBytes, DirBuilder.Digest.Emplace());

		return DirBuilder.Digest.GetValue();
	}

	void BuildMerkleTreeNodes(FRemoteExecutionState& State)
	{
		TArray<FIoHash> WorkerFileHashes;
		TArray<TTuple<FStringView, bool>> WorkerFileMeta;
		State.BuildWorker.IterateExecutables([&WorkerFileHashes, &WorkerFileMeta] (FStringView Path, const FIoHash& RawHash, uint64 RawSize)
			{
				WorkerFileHashes.Emplace(RawHash);
				WorkerFileMeta.Emplace(Path, true);
			});

		State.BuildWorker.IterateFiles([&WorkerFileHashes, &WorkerFileMeta] (FStringView Path, const FIoHash& RawHash, uint64 RawSize)
			{
				WorkerFileHashes.Emplace(RawHash);
				WorkerFileMeta.Emplace(Path, false);
			});

		State.BuildWorker.FindFileData(WorkerFileHashes, EPriority::Normal,
			[&State, &WorkerFileMeta] (FBuildWorkerFileDataCompleteParams&& Params)
			{
				uint32 MetaIndex = 0;
				for (const FCompressedBuffer& Buffer : Params.Files)
				{
					const TTuple<FStringView, bool>& Meta = WorkerFileMeta[MetaIndex];
					FCompositeBuffer DecompressedComposite = Buffer.DecompressToComposite();
					AddMerkleTreeFile(State, Meta.Key, FIoHash::HashBuffer(DecompressedComposite), DecompressedComposite.GetSize(), Meta.Value, EFileType::Worker, Buffer.DecompressToComposite());
					++MetaIndex;
				}
			}).Wait();

		State.BuildAction.IterateInputs([&State] (FStringView Key, const FIoHash& RawHash, uint64 RawSize)
			{
				TStringBuilder<128> InputPath;
				InputPath << TEXT("Inputs/") << RawHash;
				const FString& NewInputPath = State.InputPaths.Emplace_GetRef(InputPath);
				const FCompressedBuffer& Buffer = State.BuildInputs.Get().FindInput(Key);
				check(!Buffer.IsNull());
				int32 FileIndex = AddMerkleTreeFile(State, NewInputPath, FIoHash::HashBuffer(Buffer.GetCompressed()), Buffer.GetCompressedSize(), false, EFileType::Input, Buffer.GetCompressed());
				State.FileIndexToInputKey.Emplace(FileIndex, Key);
			});

		// This base directory must be created as worker executables (even those that don't exist in this directory) will attempt to change directories into it during startup.
		int32 BaseDirectoryIndex = INDEX_NONE;
		TStringBuilder<128> BaseDirectoryPathBuilder;
		FPathViews::Append(BaseDirectoryPathBuilder, TEXT("Engine/Binaries/"), State.BuildWorker.GetHostPlatform());
		State.BaseDirectoryPath = BaseDirectoryPathBuilder.ToString();
		GetOrAddMerkleTreeDirectory(State, State.BaseDirectoryPath, BaseDirectoryIndex);

		FCbWriter BuildActionWriter;
		State.BuildAction.Save(BuildActionWriter);
		FUniqueBuffer UncompressedBuildActionContentBytes = FUniqueBuffer::Alloc(BuildActionWriter.GetSaveSize());
		BuildActionWriter.Save(UncompressedBuildActionContentBytes);
		State.BuildActionContentBytes = FCompositeBuffer(UncompressedBuildActionContentBytes.MoveToShared());
		State.BuildActionDigest.Hash = FIoHash::HashBuffer(State.BuildActionContentBytes);
		State.BuildActionDigest.SizeBytes = State.BuildActionContentBytes.GetSize();
		AddMerkleTreeFile(State, TEXT("Build.action"), State.BuildActionDigest.Hash, State.BuildActionDigest.SizeBytes, false, EFileType::BuildAction, State.BuildActionContentBytes);


		if (!State.PathToDirectoryIndex.IsEmpty())
		{
			int32 RootDirectoryIndex = State.PathToDirectoryIndex.FindChecked(TEXT(""));
			State.Action.InputRootDigest = BuildMerkleTreeDirectoryDigest(State, RootDirectoryIndex);
			State.DigestFilesystemIndex.Add(State.Action.InputRootDigest, FVariantIndex(ENodeType::Directory, RootDirectoryIndex));
		}

		State.Command.OutputPaths.Add("Outputs");
		State.Command.OutputPaths.Add("Build.output");
		State.Command.Arguments.Add(FString(State.BuildWorker.GetPath()));
		State.Command.Arguments.Add("-Build=Build.action");
		State.BuildWorker.IterateEnvironment([&State] (FStringView Name, FStringView Value)
			{
				FCommand::FEnvironmentVariable& EnvVar = State.Command.EnvironmentVariables.AddDefaulted_GetRef();
				EnvVar.Name = Name;
				EnvVar.Value = Value;
			});
		State.Command.EnvironmentVariables.Sort([] (const FCommand::FEnvironmentVariable& A, const FCommand::FEnvironmentVariable& B)
			{
				if (A.Name < B.Name)
				{
					return true;
				}
				else if (B.Name < A.Name)
				{
					return false;
				}
				return A.Value < B.Value;
			});
		ContentAddressableStorage->ToBlob(State.Command, State.CommandContentBytes, State.Action.CommandDigest);

		State.Action.Salt = Salt;
		ContentAddressableStorage->ToBlob(State.Action, State.ActionContentBytes, State.ActionDigest);
	}

	// Each step represents a synchronous unit of work that ends with an operation we want to have async in the future
	FStatus DetermineMissingBlobs(FRemoteExecutionState& State)
	{
		BuildMerkleTreeNodes(State);

		State.FindMissingBlobsRequest.InstanceName = InstanceName;
		State.FindMissingBlobsRequest.BlobDigests.Add(State.ActionDigest);
		State.FindMissingBlobsRequest.BlobDigests.Add(State.Action.CommandDigest);
		for (const TPair<FDigest, FVariantIndex>& FilesystemItem : State.DigestFilesystemIndex)
		{
			State.FindMissingBlobsRequest.BlobDigests.AddUnique(FilesystemItem.Key);
		}
		return ContentAddressableStorage->FindMissingBlobs(State.FindMissingBlobsRequest, State.FindMissingBlobsResponse);
	}

	void GatherMissingInputFileBlobs(FRemoteExecutionState& State, TArray<FStringView>& OutMissingInputs)
	{
		for (const FDigest& MissingItem : State.FindMissingBlobsResponse.MissingBlobDigests)
		{
			TArray<FVariantIndex> DigestFilesystemEntries;
			State.DigestFilesystemIndex.MultiFind(MissingItem, DigestFilesystemEntries);

			for (FVariantIndex& VariantIndex : DigestFilesystemEntries)
			{
				if (VariantIndex.NodeType == ENodeType::File)
				{
					const FMerkleTreeFileBuilder& File = State.Files[VariantIndex.Index];
					if ((File.Type == EFileType::Input) && File.ContentBytes.IsNull())
					{
						OutMissingInputs.Add(State.FileIndexToInputKey[VariantIndex.Index]);
					}
				}
			}
		}
	}

	void LoadMissingWorkerFileBlobs(FRemoteExecutionState& State)
	{
		TArray<FIoHash> WorkerFileHashes;
		TMultiMap<FIoHash, uint32> WorkerFileMapping;
		for (const FDigest& MissingItem : State.FindMissingBlobsResponse.MissingBlobDigests)
		{
			TArray<FVariantIndex> DigestFilesystemEntries;
			State.DigestFilesystemIndex.MultiFind(MissingItem, DigestFilesystemEntries);

			for (FVariantIndex& VariantIndex : DigestFilesystemEntries)
			{
				if (VariantIndex.NodeType == ENodeType::File)
				{
					const FMerkleTreeFileBuilder& File = State.Files[VariantIndex.Index];
					if ((File.Type == EFileType::Worker) && File.ContentBytes.IsNull())
					{
						WorkerFileHashes.Emplace(MissingItem.Hash);
						WorkerFileMapping.Add(MissingItem.Hash, VariantIndex.Index);
					}
				}
			}
		}

		State.BuildWorker.FindFileData(WorkerFileHashes, EPriority::Normal,
			[&State, &WorkerFileMapping] (FBuildWorkerFileDataCompleteParams&& Params)
			{
				uint32 MetaIndex = 0;
				for (const FCompressedBuffer& Buffer : Params.Files)
				{
					FCompositeBuffer UncompressedWorkerFile = Buffer.DecompressToComposite();
					TArray<uint32> WorkerFileIndices;
					WorkerFileMapping.MultiFind(Buffer.GetRawHash(), WorkerFileIndices);

					for (int32 FileIndex : WorkerFileIndices)
					{
						State.Files[FileIndex].ContentBytes = UncompressedWorkerFile;
					}
				}
			}).Wait();
	}

	FStatus UploadMissingBlobs(FRemoteExecutionState& State)
	{
		State.BatchUpdateBlobsRequest.InstanceName = InstanceName;
		for (const FDigest& MissingItem : State.FindMissingBlobsResponse.MissingBlobDigests)
		{
			if (MissingItem == State.ActionDigest)
			{
				Stats.TotalActionBlobsUploaded.AddBlob(State.ActionContentBytes.Num());
				FBatchUpdateBlobsRequest::FRequest& NewRequest = State.BatchUpdateBlobsRequest.Requests.AddDefaulted_GetRef();
				NewRequest.Digest = State.ActionDigest;
				NewRequest.Data = MoveTemp(State.ActionContentBytes);
				UE_LOG(LogDerivedDataBuildRemoteExecutor, Verbose, TEXT("Uploading action (hash: %s) of upload size %d."), *::LexToString(NewRequest.Digest.Hash), NewRequest.Data.Num());
			}
			else if (MissingItem == State.Action.CommandDigest)
			{
				Stats.TotalCommandBlobsUploaded.AddBlob(State.CommandContentBytes.Num());
				FBatchUpdateBlobsRequest::FRequest& NewRequest = State.BatchUpdateBlobsRequest.Requests.AddDefaulted_GetRef();
				NewRequest.Digest = State.Action.CommandDigest;
				NewRequest.Data = MoveTemp(State.CommandContentBytes);
				UE_LOG(LogDerivedDataBuildRemoteExecutor, Verbose, TEXT("Uploading command (hash: %s) of upload size %d."), *::LexToString(NewRequest.Digest.Hash), NewRequest.Data.Num());
			}
			else
			{
				const FVariantIndex VariantIndex = State.DigestFilesystemIndex.FindChecked(MissingItem);
				FBatchUpdateBlobsRequest::FRequest& NewRequest = State.BatchUpdateBlobsRequest.Requests.AddDefaulted_GetRef();
				NewRequest.Digest = MissingItem;
				switch (VariantIndex.NodeType)
				{
				case ENodeType::Directory:
					Stats.TotalDirectoryBlobsUploaded.AddBlob(State.Directories[VariantIndex.Index].ContentBytes.Num());
					NewRequest.Digest = State.Directories[VariantIndex.Index].Digest.GetValue();
					NewRequest.Data = MoveTemp(State.Directories[VariantIndex.Index].ContentBytes);
					UE_LOG(LogDerivedDataBuildRemoteExecutor, Verbose, TEXT("Uploading directory '%s' (hash: %s) of upload size %d."), *FString(State.Directories[VariantIndex.Index].Name), *::LexToString(NewRequest.Digest.Hash), NewRequest.Data.Num());
					break;
				case ENodeType::File:
					{
						Stats.TotalFileBlobsUploaded.AddBlob(State.Files[VariantIndex.Index].ContentBytes.GetSize());
						NewRequest.Digest.Hash = FIoHash::HashBuffer(State.Files[VariantIndex.Index].ContentBytes);
						NewRequest.Digest.SizeBytes = State.Files[VariantIndex.Index].ContentBytes.GetSize();
						FCompositeBuffer& FileBuffer = State.Files[VariantIndex.Index].ContentBytes;
						check(!FileBuffer.IsNull());
						NewRequest.Data.Reserve(FileBuffer.GetSize());
						for (const FSharedBuffer& Segment : State.Files[VariantIndex.Index].ContentBytes.GetSegments())
						{
							NewRequest.Data.Append((const char *)Segment.GetData(), Segment.GetSize());
						}
						FileBuffer.Reset();
						UE_LOG(LogDerivedDataBuildRemoteExecutor, Verbose, TEXT("Uploading file '%s' (hash: %s, type: %s) of upload size %d."), *State.Files[VariantIndex.Index].File.Name, *::LexToString(NewRequest.Digest.Hash), *LexToString(State.Files[VariantIndex.Index].Type), NewRequest.Data.Num());
					}
					break;
				default:
					checkNoEntry();
					break;
				}
			}
		}

		return ContentAddressableStorage->BatchUpdateBlobs(State.BatchUpdateBlobsRequest, State.BatchUpdateBlobsResponse);
	}

	bool ValidateUploadSuccess(FRemoteExecutionState& State)
	{
		bool bSuccess = true;
		for (const FBatchUpdateBlobsRequest::FRequest& RequestedUpload : State.BatchUpdateBlobsRequest.Requests)
		{
			if (const FBatchUpdateBlobsResponse::FResponse* FoundResponse = Algo::FindByPredicate(State.BatchUpdateBlobsResponse.Responses, [&RequestedUpload] (const FBatchUpdateBlobsResponse::FResponse& Response)
				{
					return Response.Digest == RequestedUpload.Digest;
				}))
			{
				if (!FoundResponse->Status.Ok())
				{
					FStringView ActionName = State.BuildAction.GetName();
					UE_LOG(LogDerivedDataBuildRemoteExecutor, Log, TEXT("Remote execution system error: data for action '%.*s' could not be uploaded (hash: %s, size: %u)"), ActionName.Len(), ActionName.GetData(), *::LexToString(RequestedUpload.Digest.Hash), RequestedUpload.Digest.SizeBytes);
					bSuccess = false;
				}
			}
		}
		return bSuccess;
	}

	bool ExecuteBuild(FRemoteExecutionState& State)
	{
		State.ExecuteRequest.InstanceName = InstanceName;
		State.ExecuteRequest.ActionDigest = State.ActionDigest;
		State.ExecuteRequest.SkipCacheLookup = true;
		int64 TimeoutMs = 0;
		if (GlobalExecutionTimeoutSeconds > 0)
		{
			TimeoutMs = GlobalExecutionTimeoutSeconds * 1000LL;
		}
		return RemoteExecutor->GetExecution()->Execute(State.ExecuteRequest, State.ExecuteResponse, TimeoutMs);
	}

	FStatus DownloadResults(FRemoteExecutionState& State)
	{
		State.BatchReadBlobsRequest.InstanceName = InstanceName;
		//State.BatchReadBlobsRequest.Digests.Add(State.ExecuteResponse.Result.StdoutDigest);
		//State.BatchReadBlobsRequest.Digests.Add(State.ExecuteResponse.Result.StderrDigest);

		for (const FOutputFile& OutputFile : State.ExecuteResponse.Result.OutputFiles)
		{
			State.BatchReadBlobsRequest.Digests.Add(OutputFile.Digest);
		}

		return ContentAddressableStorage->BatchReadBlobs(State.BatchReadBlobsRequest, State.BatchReadBlobsResponse);
	}

	FOptionalBuildOutput ComposeBuildOutput(FRemoteExecutionState& State, EStatus& OutStatus)
	{
		OutStatus = EStatus::Error;

		TOptional<FDigest> BuildOutputDigest;
		for (const FOutputFile& ExecuteOutputFile : State.ExecuteResponse.Result.OutputFiles)
		{
			if (ExecuteOutputFile.Path == TEXT("Build.output"))
			{
				BuildOutputDigest = ExecuteOutputFile.Digest;
				break;
			}
		}

		if (!BuildOutputDigest.IsSet())
		{
			UE_LOG(LogDerivedDataBuildRemoteExecutor, Warning, TEXT("Worker error: build output structure not produced!"));
			return FOptionalBuildOutput();
		}

		FOptionalBuildOutput RemoteBuildOutput;

		TMap<FIoHash, FCompressedBuffer> PayloadResponses;
		for (FBatchReadBlobsResponse::FResponse& OutputReadResponse : State.BatchReadBlobsResponse.Responses)
		{
			if (!OutputReadResponse.Status.Ok())
			{
				UE_LOG(LogDerivedDataBuildRemoteExecutor, Warning, TEXT("Remote execution system error: output not downloaded!"));
				return FOptionalBuildOutput();
			}
			Stats.TotalFileBlobsDownloaded.AddBlob(OutputReadResponse.Data.Num());

			if (OutputReadResponse.Digest == BuildOutputDigest)
			{
				FSharedBuffer BuildOutputBuffer = MakeSharedBufferFromArray(MoveTemp(OutputReadResponse.Data));

				if (ValidateCompactBinaryRange(BuildOutputBuffer, ECbValidateMode::Default) != ECbValidateError::None)
				{
					UE_LOG(LogDerivedDataBuildRemoteExecutor, Warning, TEXT("Worker error: build output structure not valid!"));
					return FOptionalBuildOutput();
				}

				RemoteBuildOutput = GetDerivedDataBuildRef().LoadOutput(State.BuildAction.GetName(), State.BuildAction.GetFunction(), FCbObject(BuildOutputBuffer));
			}
			else
			{
				FCompressedBuffer NewBuffer = FCompressedBuffer::FromCompressed(MakeSharedBufferFromArray(MoveTemp(OutputReadResponse.Data)));
				PayloadResponses.Add(NewBuffer.GetRawHash(), MoveTemp(NewBuffer));
			}
		}

		if (RemoteBuildOutput.IsNull())
		{
			UE_LOG(LogDerivedDataBuildRemoteExecutor, Warning, TEXT("Remote execution system error: build output blob missing!"));
			return FOptionalBuildOutput();
		}

		FBuildOutputBuilder OutputBuilder = GetDerivedDataBuildRef().CreateOutput(State.BuildAction.GetName(), State.BuildAction.GetFunction());
		
		RemoteBuildOutput.Get().IterateDiagnostics( [&OutputBuilder](const FBuildDiagnostic& Diagnostic)
		{
			if (Diagnostic.Level == EBuildDiagnosticLevel::Warning)
			{
				OutputBuilder.AddWarning(Diagnostic.Category, Diagnostic.Message);
			}
			else if (Diagnostic.Level == EBuildDiagnosticLevel::Error)
			{
				OutputBuilder.AddError(Diagnostic.Category, Diagnostic.Message);
			}
		});

		for (const FPayload& Payload : RemoteBuildOutput.Get().GetPayloads())
		{
			FCompressedBuffer* BufferForPayload = PayloadResponses.Find(Payload.GetRawHash());
			if (!BufferForPayload)
			{
				UE_LOG(LogDerivedDataBuildRemoteExecutor, Warning, TEXT("Remote execution system error: payload blob missing!"));
				return FOptionalBuildOutput();
			}

			OutputBuilder.AddPayload(FPayload(Payload.GetId(), *BufferForPayload));
		}

		OutStatus = State.ExecuteResponse.Result.ExitCode == 0 ? EStatus::Ok : EStatus::Error;
		return OutputBuilder.Build();
	}

	FRequest BuildAction(
		const FBuildAction& Action,
		const FOptionalBuildInputs& Inputs,
		const FBuildWorker& Worker,
		EBuildPolicy Policy,
		EPriority Priority,
		FOnBuildWorkerActionComplete&& OnComplete) final
	{
		TArray<FString> MissingInputs;
		TArray<FStringView> MissingInputViews;
		uint64 TotalInputSize = 0;
		uint64 TotalMissingInputSize = 0;
		{
			// TODO: This block forces resolution of inputs before we attempt to determine which
			//		 inputs need to be uploaded.  This is required because we can't refer to inputs
			//		 in the Merkle tree by their RawHash/RawSize but instead must send their CompressedHash/
			//		 CompressedSize.  Once the remote execution API allows us to represent inputs with RawHash/
			//		 RawSize, this block can be removed and we can find missing CAS inputs without having resolved
			//		 the inputs first.
			Action.IterateInputs([&MissingInputs, &MissingInputViews, &Inputs, &TotalInputSize, &TotalMissingInputSize] (FStringView Key, const FIoHash& RawHash, uint64 RawSize)
				{
					if (Inputs.IsNull() || Inputs.Get().FindInput(Key).IsNull())
					{
						MissingInputs.Emplace(Key);
						MissingInputViews.Add(MissingInputs.Last());
						TotalMissingInputSize += RawSize;
					}
					TotalInputSize += RawSize;
				});

			if (!LimitingHeuristics.PassesPreResolveRequirements(TotalInputSize, TotalMissingInputSize))
			{
				OnComplete({Action.GetKey(), {}, {}, EStatus::Error});
				return FRequest();
			}

			if (!MissingInputViews.IsEmpty())
			{
				OnComplete({Action.GetKey(), {}, MissingInputViews, EStatus::Ok});
				return FRequest();
			}
		}

		FRemoteExecutionState State { Action, Inputs, Worker, Policy, Priority };

		DetermineMissingBlobs(State);

		GatherMissingInputFileBlobs(State, MissingInputViews);
		if (!MissingInputViews.IsEmpty())
		{
			OnComplete({State.BuildAction.GetKey(), {}, MissingInputViews, EStatus::Ok});
			return FRequest();
		}

		if (!LimitingHeuristics.TryStartNewBuild(Stats))
		{
			OnComplete({Action.GetKey(), {}, {}, EStatus::Error});
			return FRequest();
		}
		ON_SCOPE_EXIT
		{
			LimitingHeuristics.FinishBuild(Stats);
		};

		LoadMissingWorkerFileBlobs(State);
		if (!State.FindMissingBlobsResponse.MissingBlobDigests.IsEmpty())
		{
			UploadMissingBlobs(State);
			UE_LOG(LogDerivedDataBuildRemoteExecutor, Verbose, TEXT("Uploaded %d data blobs for remote execution."), State.BatchUpdateBlobsRequest.Requests.Num());
			if (!ValidateUploadSuccess(State))
			{
				OnComplete({Action.GetKey(), {}, {}, EStatus::Error});
				return FRequest();
			}
		}

		if (ExecuteBuild(State))
		{
			DownloadResults(State);

			EStatus BuildStatus = EStatus::Error;
			FOptionalBuildOutput BuildOutput = ComposeBuildOutput(State, BuildStatus);
			if (BuildStatus == EStatus::Ok)
			{
				Stats.TotalSuccessfulRemoteBuilds.fetch_add(1, std::memory_order_relaxed);
			}
			OnComplete({State.BuildAction.GetKey(), MoveTemp(BuildOutput), {}, BuildStatus});
		}
		else
		{
			UE_LOG(LogDerivedDataBuildRemoteExecutor, Warning, TEXT("Remote execution system error: Failed to execute build operation!"));
			OnComplete({State.BuildAction.GetKey(), {}, {}, EStatus::Error});
		}

		return FRequest();
	}

	TConstArrayView<FStringView> GetHostPlatforms() const final
	{
		static constexpr FStringView HostPlatforms[]{TEXT("Win64"_SV), TEXT("Linux"_SV), TEXT("Mac"_SV)};
		return HostPlatforms;
	}

	void DumpStats()
	{
		if (Stats.TotalRemoteBuilds.load() == 0)
		{
			return;
		}

		Stats.Dump();
	}

private:
	struct FStats
	{
		std::atomic<uint64> TotalRemoteBuilds{0};
		std::atomic<uint32> InFlightRemoteBuilds{0};

		std::atomic<uint64> TotalSuccessfulRemoteBuilds{0};

		struct FBlobStat
		{
			std::atomic<uint64> Quantity{0};
			std::atomic<uint64> Bytes{0};

			void AddBlob(uint64 InBytes)
			{
				Quantity.fetch_add(1, std::memory_order_relaxed);
				Bytes.fetch_add(InBytes, std::memory_order_relaxed);
			}
		};
		FBlobStat TotalActionBlobsUploaded;
		FBlobStat TotalCommandBlobsUploaded;
		FBlobStat TotalDirectoryBlobsUploaded;
		FBlobStat TotalFileBlobsUploaded;
		FBlobStat TotalFileBlobsDownloaded;

		void Dump()
		{
			UE_LOG(LogDerivedDataBuildRemoteExecutor, Display, TEXT(""));
			UE_LOG(LogDerivedDataBuildRemoteExecutor, Display, TEXT("DDC Remote Execution Stats"));
			UE_LOG(LogDerivedDataBuildRemoteExecutor, Display, TEXT("=========================="));
			UE_LOG(LogDerivedDataBuildRemoteExecutor, Display, TEXT("%-35s=%10") UINT64_FMT, TEXT("Total remote builds"), TotalRemoteBuilds.load());
			UE_LOG(LogDerivedDataBuildRemoteExecutor, Display, TEXT("%-35s=%10") UINT64_FMT, TEXT("Successful remote builds"), TotalSuccessfulRemoteBuilds.load());
			UE_LOG(LogDerivedDataBuildRemoteExecutor, Display, TEXT("%-35s=%10") UINT64_FMT, TEXT("Uploaded actions (quantity)"), TotalActionBlobsUploaded.Quantity.load());
			UE_LOG(LogDerivedDataBuildRemoteExecutor, Display, TEXT("%-35s=%10") UINT64_FMT, TEXT("Uploaded actions (KB)"), TotalActionBlobsUploaded.Bytes.load()/1024);
			UE_LOG(LogDerivedDataBuildRemoteExecutor, Display, TEXT("%-35s=%10") UINT64_FMT, TEXT("Uploaded commands (quantity)"), TotalCommandBlobsUploaded.Quantity.load());
			UE_LOG(LogDerivedDataBuildRemoteExecutor, Display, TEXT("%-35s=%10") UINT64_FMT, TEXT("Uploaded commands (KB)"), TotalCommandBlobsUploaded.Bytes.load()/1024);
			UE_LOG(LogDerivedDataBuildRemoteExecutor, Display, TEXT("%-35s=%10") UINT64_FMT, TEXT("Uploaded directories (quantity)"), TotalDirectoryBlobsUploaded.Quantity.load());
			UE_LOG(LogDerivedDataBuildRemoteExecutor, Display, TEXT("%-35s=%10") UINT64_FMT, TEXT("Uploaded directories (KB)"), TotalDirectoryBlobsUploaded.Bytes.load()/1024);
			UE_LOG(LogDerivedDataBuildRemoteExecutor, Display, TEXT("%-35s=%10") UINT64_FMT, TEXT("Uploaded files (quantity)"), TotalFileBlobsUploaded.Quantity.load());
			UE_LOG(LogDerivedDataBuildRemoteExecutor, Display, TEXT("%-35s=%10") UINT64_FMT, TEXT("Uploaded files (KB)"), TotalFileBlobsUploaded.Bytes.load()/1024);
			UE_LOG(LogDerivedDataBuildRemoteExecutor, Display, TEXT("%-35s=%10") UINT64_FMT, TEXT("Downloaded files (quantity)"), TotalFileBlobsDownloaded.Quantity.load());
			UE_LOG(LogDerivedDataBuildRemoteExecutor, Display, TEXT("%-35s=%10") UINT64_FMT, TEXT("Downloaded files (KB)"), TotalFileBlobsDownloaded.Bytes.load()/1024);
		}
	};

	// Temporary heuristics until a scheduler makes higher level decisions about how to limit remote execution of builds
	class FLimitingHeuristics
	{
	public:
		FLimitingHeuristics()
		{
			check(IsInGameThread()); // initialization from the main thread is expected to allow config reading for the limiting heuristics
			check(GConfig && GConfig->IsReadyForUse());
			const TCHAR* Section = TEXT("DerivedDataBuildRemoteExecutor.LimitingHeuristics");
			GConfig->GetBool(Section, TEXT("bEnableLimits"), bEnableLimits, GEngineIni);

			int32 SignedMaxTotalRemoteBuilds{MAX_int32};
			GConfig->GetInt(Section, TEXT("MaxTotalRemoteBuilds"), SignedMaxTotalRemoteBuilds, GEngineIni);
			if ((SignedMaxTotalRemoteBuilds >= 0) && (SignedMaxTotalRemoteBuilds < MAX_int32))
			{
				MaxTotalRemoteBuilds = (uint64)SignedMaxTotalRemoteBuilds;
			}

			int32 SignedMaxInFlightRemoteBuilds{MAX_int32};
			GConfig->GetInt(Section, TEXT("MaxInFlightRemoteBuilds"), SignedMaxInFlightRemoteBuilds, GEngineIni);
			if ((SignedMaxInFlightRemoteBuilds >= 0) && (SignedMaxInFlightRemoteBuilds < MAX_int32))
			{
				MaxInFlightRemoteBuilds = (uint32)SignedMaxInFlightRemoteBuilds;
			}

			int32 SignedMinInputSizeForRemoteBuilds{0};
			GConfig->GetInt(Section, TEXT("MinInputSizeForRemoteBuilds"), SignedMinInputSizeForRemoteBuilds, GEngineIni);
			if ((SignedMinInputSizeForRemoteBuilds >= 0) && (SignedMinInputSizeForRemoteBuilds < MAX_int32))
			{
				MinInputSizeForRemoteBuilds = (uint64)SignedMinInputSizeForRemoteBuilds;
			}

			int32 SignedMaxMissingInputSizeForRemoteBuilds{MAX_int32};
			GConfig->GetInt(Section, TEXT("MaxMissingInputSizeForRemoteBuilds"), SignedMaxMissingInputSizeForRemoteBuilds, GEngineIni);
			if ((SignedMaxMissingInputSizeForRemoteBuilds >= 0) && (SignedMaxMissingInputSizeForRemoteBuilds < MAX_int32))
			{
				MaxMissingInputSizeForRemoteBuilds = (uint64)SignedMaxMissingInputSizeForRemoteBuilds;
			}
		}

		bool PassesPreResolveRequirements(uint64 InputSize, uint64 MissingInputSize)
		{
			if (!bEnableLimits)
			{
				return true;
			}

			if (InputSize < MinInputSizeForRemoteBuilds)
			{
				return false;
			}

			if (MissingInputSize > MaxMissingInputSizeForRemoteBuilds)
			{
				return false;
			}

			return true;
		}

		bool TryStartNewBuild(FStats& InStats)
		{
			if ((InStats.TotalRemoteBuilds.fetch_add(1, std::memory_order_relaxed) >= MaxTotalRemoteBuilds) && bEnableLimits)
			{
				InStats.TotalRemoteBuilds.fetch_sub(1, std::memory_order_relaxed);
				return false;
			}

			if ((InStats.InFlightRemoteBuilds.fetch_add(1, std::memory_order_relaxed) >= MaxInFlightRemoteBuilds) && bEnableLimits)
			{
				InStats.TotalRemoteBuilds.fetch_sub(1, std::memory_order_relaxed);
				InStats.InFlightRemoteBuilds.fetch_sub(1, std::memory_order_relaxed);
				return false;
			}

			return true;
		}

		void FinishBuild(FStats& InStats)
		{
			InStats.InFlightRemoteBuilds.fetch_sub(1, std::memory_order_relaxed);
		}

	private:
		uint64 MaxTotalRemoteBuilds{MAX_uint64};
		uint32 MaxInFlightRemoteBuilds{MAX_uint32};
		uint64 MinInputSizeForRemoteBuilds{0};
		uint64 MaxMissingInputSizeForRemoteBuilds{MAX_uint64};
		bool bEnableLimits{false};
	};

	FStats Stats;
	FLimitingHeuristics LimitingHeuristics;
	FString InstanceName;
	int GlobalExecutionTimeoutSeconds;
	const FStringView Salt;
	IRemoteExecutor* RemoteExecutor;
	IContentAddressableStorage* ContentAddressableStorage;
	bool bEnabled;
};

} // namespace UE::DerivedData

TOptional<UE::DerivedData::FRemoteBuildWorkerExecutor> GRemoteBuildWorkerExecutor;

void InitDerivedDataBuildRemoteExecutor()
{
	if (!GRemoteBuildWorkerExecutor.IsSet())
	{
		GRemoteBuildWorkerExecutor.Emplace();
	}
}

void DumpDerivedDataBuildRemoteExecutorStats()
{
	static bool bHasRun = false;
	if (GRemoteBuildWorkerExecutor.IsSet() && !bHasRun)
	{
		bHasRun = true;
		GRemoteBuildWorkerExecutor->DumpStats();
	}
}
