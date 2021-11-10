// Copyright Epic Games, Inc. All Rights Reserved.

#include "DerivedDataBuildRemoteExecutor.h"

#include "Algo/Find.h"
#include "DerivedDataBuild.h"
#include "DerivedDataBuildAction.h"
#include "DerivedDataBuildInputs.h"
#include "DerivedDataBuildOutput.h"
#include "DerivedDataBuildTypes.h"
#include "DerivedDataBuildWorker.h"
#include "DerivedDataPayload.h"
#include "DerivedDataRequest.h"
#include "DerivedDataRequestOwner.h"
#include "Features/IModularFeatures.h"
#include "HAL/Event.h"
#include "HAL/PlatformProcess.h"
#include "IContentAddressableStorage.h"
#include "IRemoteExecutor.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Optional.h"
#include "Misc/PathViews.h"
#include "Modules/ModuleManager.h"
#include "Serialization/CompactBinaryValidation.h"
#include "Serialization/CompactBinaryWriter.h"
#include "TickableEditorObject.h"
#include <atomic>

#include "HttpManager.h"
#include "HttpModule.h"
#include "RemoteMessages.h"

namespace UE::DerivedData
{

DEFINE_LOG_CATEGORY_STATIC(LogDerivedDataBuildRemoteExecutor, Log, All);

class FRemoteBuildWorkerExecutor;

class FRemoteBuildExecutionRequest final : public FRequestBase
{
public:
	FRemoteBuildExecutionRequest(
		FRemoteBuildWorkerExecutor& InExecutor,
		const FBuildAction& Action,
		const FOptionalBuildInputs& Inputs,
		const FBuildPolicy& Policy,
		const FBuildWorker& Worker,
		IBuild& BuildSystem,
		IRequestOwner& Owner,
		FOnBuildWorkerActionComplete&& OnComplete);

	~FRemoteBuildExecutionRequest() final;

	// IRequest interface
	void SetPriority(EPriority Priority) final
	{
	}

	void Cancel() final
	{
		bCancelPending.store(true, std::memory_order_relaxed);
		Wait();
	}

	void Wait() final
	{
		CompletionEvent->Wait();
	}

private:
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
		FStringView Path;
		UE::RemoteExecution::FFileNode File;
		EFileType Type;
		FCompositeBuffer ContentBytes;
	};

	struct FMerkleTreeDirectoryBuilder
	{
		FStringView Name;
		FStringView Path;
		UE::RemoteExecution::FDirectoryTree Directory;
		TOptional<FIoHash> Digest;
		TArray<uint8> ContentBytes;
		TArray<int32> SubDirIndices;
	};

	struct FRemoteExecutionState
	{
		const FBuildAction& BuildAction;
		const FOptionalBuildInputs& BuildInputs;
		const FBuildWorker& BuildWorker;
		IBuild& BuildSystem;
		IRequestOwner& Owner;
		FBuildPolicy BuildPolicy;

		// Unordered arrays that are indexed into
		TArray<FMerkleTreeDirectoryBuilder> Directories;
		TArray<FMerkleTreeFileBuilder> Files;

		// Lookup tables for indexing in different scenarios
		TMultiMap<FIoHash, FVariantIndex> DigestFilesystemIndex;
		TMap<FStringView, int32> PathToDirectoryIndex;
		TMap<int32, FString> FileIndexToInputKey;
		FString BaseDirectoryPath;

		// Unique items in the tree
		UE::RemoteExecution::FTask Task;
		TArray<uint8>TaskContentBytes;
		FIoHash TaskDigest;
		UE::RemoteExecution::FRequirements Requirements;
		TArray<uint8> RequirementsContentBytes;
		FCompositeBuffer BuildActionContentBytes {};
		FIoHash BuildActionDigest;
		TArray<FString> InputPaths;

		// Step 1: Find missing blobs
		TSet<FIoHash> DoBlobsExistRequest;
		TMap<FIoHash, UE::RemoteExecution::EStatusCode> DoBlobsExistResponse;

		// Step 3: Batch update blobs (upload)
		TMap<FIoHash, TArray<uint8>> PutBlobsRequest;

		// Step 3: Execute
		UE::RemoteExecution::FAddTasksRequest ExecuteRequest;
		UE::RemoteExecution::FGetTaskUpdateResponse ExecuteResponse;

		// Step 4: Get execute results
		UE::RemoteExecution::FTaskResult ExecuteResult;
		TMap<FString, FIoHash> ExecuteResultFiles;

		// Step 5: Batch read blobs (download)
		TSet<FIoHash> GetBlobsRequest;
	};

	FRemoteExecutionState State;
	FOnBuildWorkerActionComplete CompletionCallback;
	FRemoteBuildWorkerExecutor& Executor;
	FEventRef CompletionEvent{EEventMode::ManualReset};
	std::atomic<bool> bCancelPending;
	bool bHeuristicBuildStarted;

	// Merkle tree operations
	FMerkleTreeDirectoryBuilder& GetOrAddMerkleTreeDirectory(FStringView Path, int32& OutDirectoryBuilderIndex);
	int32 AddMerkleTreeFile(FStringView Path, const FIoHash& RawHash, uint64 RawSize, bool bIsExecutable, EFileType FileType, FCompositeBuffer ContentBytes = FCompositeBuffer::Null);
	const FIoHash& BuildMerkleTreeDirectoryDigest(int32 Index);
	void BuildMerkleTreeNodes();

	// General utility methods
	void GatherMissingInputFileBlobs(TArray<FStringView>& OutMissingInputs);
	bool ValidateUploadSuccess(const TMap<FIoHash, UE::RemoteExecution::EStatusCode>& PutBlobsResponse);
	void GatherExecuteFileResults(const TMap<FIoHash, TArray<uint8>>& Data, const FString& Parent, const FIoHash& DirectoryTreeHash);
	FOptionalBuildOutput ComposeBuildOutput(TMap<FIoHash, TPair<UE::RemoteExecution::EStatusCode, TArray<uint8>>>&& GetBlobsResponse, EStatus& OutStatus);
	bool ProcessCancellation();
	bool IsStatusOk(UE::RemoteExecution::EStatusCode Status, const TCHAR* OperationDesc);

	// Async steps
	TFuture<TMap<FIoHash, UE::RemoteExecution::EStatusCode>> DetermineMissingBlobsAsync();
	void LoadMissingWorkerFileBlobsAsync();
	TFuture<TMap<FIoHash, UE::RemoteExecution::EStatusCode>> UploadMissingBlobsAsync();
	TFuture<TPair<UE::RemoteExecution::EStatusCode, UE::RemoteExecution::FGetTaskUpdatesResponse>> ExecuteBuildAsync();
	TFuture<TPair<UE::RemoteExecution::EStatusCode, UE::RemoteExecution::FGetObjectTreeResponse>> GetTaskResultAsync();
	TFuture<TMap<FIoHash, TPair<UE::RemoteExecution::EStatusCode, TArray<uint8>>>> DownloadResultsAsync();

	// Post-step flow
	void OnMissingBlobsDetermined(TMap<FIoHash, UE::RemoteExecution::EStatusCode>&& Result);
	void OnMissingBlobsUploaded(const TMap<FIoHash, UE::RemoteExecution::EStatusCode>& Result);
	void OnExecutionCompleted(UE::RemoteExecution::EStatusCode Status, UE::RemoteExecution::FGetTaskUpdatesResponse&& Result);
	void OnTaskResultDownloaded(UE::RemoteExecution::EStatusCode Status, UE::RemoteExecution::FGetObjectTreeResponse&& Result);
	void OnOutputBlobsDownloaded(TMap<FIoHash, TPair<UE::RemoteExecution::EStatusCode, TArray<uint8>>>&& Result);
};

class FRemoteBuildWorkerExecutor final: public IBuildWorkerExecutor
{
public:
	FRemoteBuildWorkerExecutor()
	: GlobalExecutionTimeoutSeconds(-1)
	, RemoteExecutor(nullptr)
	, ContentAddressableStorage(nullptr)
	, Execution(nullptr)
	, bEnabled(false)
	{
		check(IsInGameThread()); // initialization from the main thread is expected to allow config reading for the limiting heuristics
		check(GConfig && GConfig->IsReadyForUse());

		bool bConfigEnabled = false;
		GConfig->GetBool(TEXT("DerivedDataBuildRemoteExecutor"), TEXT("bEnabled"), bConfigEnabled, GEngineIni);
		GConfig->GetString(TEXT("DerivedDataBuildRemoteExecutor"), TEXT("NameSpaceId"), NameSpaceId, GEngineIni);
		GConfig->GetInt(TEXT("DerivedDataBuildRemoteExecutor"), TEXT("GlobalExecutionTimeoutSeconds"), GlobalExecutionTimeoutSeconds, GEngineIni);

		const FName RemoteExecutionFeatureName(TEXT("RemoteExecution"));
		IModularFeatures& ModularFeatures = IModularFeatures::Get();

		if (bConfigEnabled || FParse::Param(FCommandLine::Get(), TEXT("DDC2RemoteExecution")))
		{
			FModuleManager::Get().LoadModule("HordeExecutor");
			if (ModularFeatures.IsModularFeatureAvailable(RemoteExecutionFeatureName))
			{
				RemoteExecutor = &ModularFeatures.GetModularFeature<UE::RemoteExecution::IRemoteExecutor>(RemoteExecutionFeatureName);
				if (RemoteExecutor)
				{
					ContentAddressableStorage = RemoteExecutor->GetContentAddressableStorage();
					Execution = RemoteExecutor->GetExecution();
				}
			}
		}

		bEnabled = (RemoteExecutor != nullptr) && (ContentAddressableStorage != nullptr) && (Execution != nullptr) && !NameSpaceId.IsEmpty();
		if (bEnabled)
		{
			ModularFeatures.RegisterModularFeature(IBuildWorkerExecutor::GetFeatureName(), this);
		}
		RemoteBuildTickable.SetTickable(bEnabled);
	}

	virtual ~FRemoteBuildWorkerExecutor()
	{
		if (bEnabled)
		{
			IModularFeatures::Get().UnregisterModularFeature(IBuildWorkerExecutor::GetFeatureName(), this);
		}
	}

	void Build(
		const FBuildAction& Action,
		const FOptionalBuildInputs& Inputs,
		const FBuildPolicy& Policy,
		const FBuildWorker& Worker,
		IBuild& BuildSystem,
		IRequestOwner& Owner,
		FOnBuildWorkerActionComplete&& OnComplete) final
	{
		{
			// TODO: This block forces resolution of inputs before we attempt to determine which
			//		 inputs need to be uploaded.  This is required because we can't refer to inputs
			//		 in the Merkle tree by their RawHash/RawSize but instead must send their CompressedHash/
			//		 CompressedSize.  Once the remote execution API allows us to represent inputs with RawHash/
			//		 RawSize, this block can be removed and we can find missing CAS inputs without having resolved
			//		 the inputs first.
			TArray<FString> MissingInputs;
			TArray<FStringView> MissingInputViews;
			uint64 TotalInputSize = 0;
			uint64 TotalMissingInputSize = 0;

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
				return;
			}

			if (!MissingInputViews.IsEmpty())
			{
				OnComplete({Action.GetKey(), {}, MissingInputViews, EStatus::Ok});
				return;
			}
		}

		new FRemoteBuildExecutionRequest(*this, Action, Inputs, Policy, Worker, BuildSystem, Owner, MoveTemp(OnComplete));
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
		FBlobStat TotalRequirementBlobsUploaded;
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
			UE_LOG(LogDerivedDataBuildRemoteExecutor, Display, TEXT("%-35s=%10") UINT64_FMT, TEXT("Uploaded requirements (quantity)"), TotalRequirementBlobsUploaded.Quantity.load());
			UE_LOG(LogDerivedDataBuildRemoteExecutor, Display, TEXT("%-35s=%10") UINT64_FMT, TEXT("Uploaded requirements (KB)"), TotalRequirementBlobsUploaded.Bytes.load()/1024);
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

	class FRemoteBuildTickableObject : public FTickableEditorObject, public FTickableCookObject
	{
		bool bIsTickable;
		// FTickableEditorObject/FTickableCookObject interface
		virtual void Tick(float DeltaTime) override
		{
			TickCook(DeltaTime, false  /* bTickComplete */);
		}
		virtual void TickCook(float DeltaTime, bool bTickComplete) override
		{
			FHttpModule* HttpModule = static_cast<FHttpModule*>(FModuleManager::Get().GetModule("HTTP"));
			if (!HttpModule)
			{
				bIsTickable = false;
				return;
			}
			HttpModule->GetHttpManager().Tick(DeltaTime);
		}
		virtual bool IsTickable() const { return bIsTickable; }
		virtual ETickableTickType GetTickableTickType() const override { return ETickableTickType::Conditional; }
		virtual TStatId GetStatId() const override { return TStatId(); }
	public:
		FRemoteBuildTickableObject() : bIsTickable(false) {}
		void SetTickable(bool IsTickable) { bIsTickable = IsTickable; }
	};

	friend class FRemoteBuildExecutionRequest;

	FStats Stats;
	FLimitingHeuristics LimitingHeuristics;
	FRemoteBuildTickableObject RemoteBuildTickable;
	FString NameSpaceId;
	int GlobalExecutionTimeoutSeconds;
	UE::RemoteExecution::IRemoteExecutor* RemoteExecutor;
	UE::RemoteExecution::IContentAddressableStorage* ContentAddressableStorage;
	UE::RemoteExecution::IExecution* Execution;
	bool bEnabled;
};

FRemoteBuildExecutionRequest::FRemoteBuildExecutionRequest(
	FRemoteBuildWorkerExecutor& InExecutor,
	const FBuildAction& Action,
	const FOptionalBuildInputs& Inputs,
	const FBuildPolicy& Policy,
	const FBuildWorker& Worker,
	IBuild& BuildSystem,
	IRequestOwner& Owner,
	FOnBuildWorkerActionComplete&& OnComplete)
: State{Action, Inputs, Worker, BuildSystem, Owner, Policy}
, CompletionCallback(MoveTemp(OnComplete))
, Executor(InExecutor)
, bCancelPending(false)
, bHeuristicBuildStarted(false)
{
	Owner.Begin(this);
	DetermineMissingBlobsAsync()
		.Next([this] (TMap<FIoHash, UE::RemoteExecution::EStatusCode>&& Result) { OnMissingBlobsDetermined(MoveTemp(Result)); });
}

FRemoteBuildExecutionRequest::~FRemoteBuildExecutionRequest()
{
	if (bHeuristicBuildStarted)
	{
		Executor.LimitingHeuristics.FinishBuild(Executor.Stats);
	}
}

FRemoteBuildExecutionRequest::FMerkleTreeDirectoryBuilder& FRemoteBuildExecutionRequest::GetOrAddMerkleTreeDirectory(FStringView Path, int32& OutDirectoryBuilderIndex)
{
	int32& DirectoryBuilderIndex = State.PathToDirectoryIndex.FindOrAdd(Path, INDEX_NONE);
	if (DirectoryBuilderIndex == INDEX_NONE)
	{
		DirectoryBuilderIndex = State.Directories.Num();
		FMerkleTreeDirectoryBuilder& NewNode = State.Directories.AddDefaulted_GetRef();
		NewNode.Path = Path;
		NewNode.Name = Path.IsEmpty() ? Path : FPathViews::GetCleanFilename(Path);
	}

	OutDirectoryBuilderIndex = DirectoryBuilderIndex;

	int32 ContainingDirectoryIndex = DirectoryBuilderIndex;
	while (!Path.IsEmpty())
	{
		int32 SubDirIndex = ContainingDirectoryIndex;
		Path = FPathViews::GetPath(Path);
		GetOrAddMerkleTreeDirectory(Path, ContainingDirectoryIndex).SubDirIndices.AddUnique(SubDirIndex);
	}

	return State.Directories[OutDirectoryBuilderIndex];
}

int32 FRemoteBuildExecutionRequest::AddMerkleTreeFile(FStringView Path, const FIoHash& RawHash, uint64 RawSize, bool bIsExecutable, EFileType FileType, FCompositeBuffer ContentBytes)
{
	FStringView ContainingDirectoryPath = FPathViews::GetPath(Path);
	int32 ContainingDirectoryIndex = INDEX_NONE;
	FMerkleTreeDirectoryBuilder* ContainingDirectoryBuilder = &GetOrAddMerkleTreeDirectory(ContainingDirectoryPath, ContainingDirectoryIndex);
	UE::RemoteExecution::FFileNode& NewNode = ContainingDirectoryBuilder->Directory.Files.AddDefaulted_GetRef();
	NewNode.Name = FPathViews::GetCleanFilename(Path);
	NewNode.Hash = RawHash;
	NewNode.Size = RawSize;

	constexpr int32 ExecutableFileAttribute = 0x1ED; // 0755 octal
	constexpr int32 NormalFileAttribute = 0x1A4; // 0644 octal
	NewNode.Attributes = bIsExecutable ? ExecutableFileAttribute : NormalFileAttribute;

	UE_LOG(LogDerivedDataBuildRemoteExecutor, Verbose, TEXT("Remote execution: added merkle tree file '%.*s' (hash: %s, size: %u)"), Path.Len(), Path.GetData(), *::LexToString(RawHash), RawSize);

	int32 NewFileIndex = State.Files.Num();
	State.DigestFilesystemIndex.Add(NewNode.Hash, FVariantIndex(ENodeType::File, NewFileIndex));
	FMerkleTreeFileBuilder& FileBuilder = State.Files.AddDefaulted_GetRef();
	FileBuilder.Path = Path;
	FileBuilder.File = NewNode; // Duplicates the node in the state's file array
	FileBuilder.Type = FileType;
	if (ContentBytes)
	{
		FileBuilder.ContentBytes = ContentBytes;
	}

	return NewFileIndex;
}

const FIoHash& FRemoteBuildExecutionRequest::BuildMerkleTreeDirectoryDigest(int32 Index)
{
	FMerkleTreeDirectoryBuilder& DirBuilder = State.Directories[Index];

	if (DirBuilder.Digest.IsSet())
	{
		return DirBuilder.Digest.GetValue();
	}

	for (int32 SubDirIndex : DirBuilder.SubDirIndices)
	{
		UE::RemoteExecution::FDirectoryNode& SubDirNode = DirBuilder.Directory.Directories.AddDefaulted_GetRef();
		SubDirNode.Name = State.Directories[SubDirIndex].Name;
		SubDirNode.Hash = BuildMerkleTreeDirectoryDigest(SubDirIndex);
		State.DigestFilesystemIndex.Add(SubDirNode.Hash, FVariantIndex(ENodeType::Directory, SubDirIndex));
	}

	DirBuilder.Directory.Directories.Sort([] (const UE::RemoteExecution::FDirectoryNode& A, const UE::RemoteExecution::FDirectoryNode& B)
	{
		return A.Name < B.Name;
	});

	DirBuilder.Directory.Files.Sort([] (const UE::RemoteExecution::FFileNode& A, const UE::RemoteExecution::FFileNode& B)
	{
		return A.Name < B.Name;
	});

	Executor.ContentAddressableStorage->ToBlob(DirBuilder.Directory, DirBuilder.ContentBytes, DirBuilder.Digest.Emplace());

	return DirBuilder.Digest.GetValue();
}

void FRemoteBuildExecutionRequest::BuildMerkleTreeNodes()
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

	FRequestOwner BlockingOwner(EPriority::Blocking);
	State.BuildWorker.FindFileData(WorkerFileHashes, BlockingOwner,
		[this, &WorkerFileMeta] (FBuildWorkerFileDataCompleteParams&& Params)
		{
			uint32 MetaIndex = 0;
			for (const FCompressedBuffer& Buffer : Params.Files)
			{
				const TTuple<FStringView, bool>& Meta = WorkerFileMeta[MetaIndex];
				FCompositeBuffer DecompressedComposite = Buffer.DecompressToComposite();
				AddMerkleTreeFile(Meta.Key, FIoHash::HashBuffer(DecompressedComposite), DecompressedComposite.GetSize(), Meta.Value, EFileType::Worker, Buffer.DecompressToComposite());
				++MetaIndex;
			}
		});
	BlockingOwner.Wait();

	State.BuildAction.IterateInputs([this] (FStringView Key, const FIoHash& RawHash, uint64 RawSize)
		{
			TStringBuilder<128> InputPath;
			InputPath << TEXT("Inputs/") << RawHash;
			const FString& NewInputPath = State.InputPaths.Emplace_GetRef(InputPath);
			const FCompressedBuffer& Buffer = State.BuildInputs.Get().FindInput(Key);
			check(!Buffer.IsNull());
			int32 FileIndex = AddMerkleTreeFile(NewInputPath, FIoHash::HashBuffer(Buffer.GetCompressed()), Buffer.GetCompressedSize(), false, EFileType::Input, Buffer.GetCompressed());
			State.FileIndexToInputKey.Emplace(FileIndex, Key);
		});

	// This base directory must be created as worker executables (even those that don't exist in this directory) will attempt to change directories into it during startup.
	int32 BaseDirectoryIndex = INDEX_NONE;
	TStringBuilder<128> BaseDirectoryPathBuilder;
	FPathViews::Append(BaseDirectoryPathBuilder, TEXT("Engine/Binaries/"), State.BuildWorker.GetHostPlatform());
	State.BaseDirectoryPath = BaseDirectoryPathBuilder.ToString();
	GetOrAddMerkleTreeDirectory(State.BaseDirectoryPath, BaseDirectoryIndex);

	FCbWriter BuildActionWriter;
	State.BuildAction.Save(BuildActionWriter);
	FUniqueBuffer UncompressedBuildActionContentBytes = FUniqueBuffer::Alloc(BuildActionWriter.GetSaveSize());
	BuildActionWriter.Save(UncompressedBuildActionContentBytes);
	State.BuildActionContentBytes = FCompositeBuffer(UncompressedBuildActionContentBytes.MoveToShared());
	State.BuildActionDigest = FIoHash::HashBuffer(State.BuildActionContentBytes);
	AddMerkleTreeFile(TEXT("Build.action"), State.BuildActionDigest, State.BuildActionContentBytes.GetSize(), false, EFileType::BuildAction, State.BuildActionContentBytes);


	if (!State.PathToDirectoryIndex.IsEmpty())
	{
		int32 RootDirectoryIndex = State.PathToDirectoryIndex.FindChecked(TEXT(""));
		State.Task.SandboxHash = BuildMerkleTreeDirectoryDigest(RootDirectoryIndex);
		State.DigestFilesystemIndex.Add(State.Task.SandboxHash, FVariantIndex(ENodeType::Directory, RootDirectoryIndex));
	}

	{
		State.Requirements.Condition = TEXT("OSFamily == 'Windows'");
		Executor.ContentAddressableStorage->ToBlob(State.Requirements, State.RequirementsContentBytes, State.Task.RequirementsHash);
	}

	State.Task.OutputPaths.Add("Outputs");
	State.Task.OutputPaths.Add("Build.output");
	State.Task.Executable = State.BuildWorker.GetPath();
	State.Task.Arguments.Add("-Build=Build.action");
	State.BuildWorker.IterateEnvironment([this] (FStringView Name, FStringView Value)
		{
			State.Task.EnvVars.Add(FString(Name), FString(Value));
		});
	Executor.ContentAddressableStorage->ToBlob(State.Task, State.TaskContentBytes, State.TaskDigest);
}

void FRemoteBuildExecutionRequest::GatherMissingInputFileBlobs(TArray<FStringView>& OutMissingInputs)
{
	for (const TPair<FIoHash, UE::RemoteExecution::EStatusCode>& MissingItem : State.DoBlobsExistResponse)
	{
		if (MissingItem.Value == UE::RemoteExecution::EStatusCode::Ok)
		{
			continue;
		}

		TArray<FVariantIndex> DigestFilesystemEntries;
		State.DigestFilesystemIndex.MultiFind(MissingItem.Key, DigestFilesystemEntries);

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

bool FRemoteBuildExecutionRequest::ValidateUploadSuccess(const TMap<FIoHash, UE::RemoteExecution::EStatusCode>& PutBlobsResponse)
{
	bool bSuccess = true;
	for (const TPair<FIoHash, TArray<uint8>>& BlobRequest : State.PutBlobsRequest)
	{
		const UE::RemoteExecution::EStatusCode* StatusCode = PutBlobsResponse.Find(BlobRequest.Key);
		if (StatusCode == nullptr || *StatusCode != UE::RemoteExecution::EStatusCode::Ok)
		{
			FStringView ActionName = State.BuildAction.GetName();
			UE_LOG(LogDerivedDataBuildRemoteExecutor, Log, TEXT("Remote execution system error: data for action '%.*s' could not be uploaded (hash: %s, size: %u)"), ActionName.Len(), ActionName.GetData(), *::LexToString(BlobRequest.Key), BlobRequest.Value.Num());
			bSuccess = false;
		}
	}
	return bSuccess;
}

void FRemoteBuildExecutionRequest::GatherExecuteFileResults(const TMap<FIoHash, TArray<uint8>>& Data, const FString& Parent, const FIoHash& DirectoryTreeHash)
{
	if (DirectoryTreeHash == FIoHash::Zero)
	{
		return;
	}

	UE::RemoteExecution::FDirectoryTree DirectoryTree;
	{
		const FCbObjectView View = FCbObjectView(Data[DirectoryTreeHash].GetData());
		DirectoryTree.Load(View);
	}

	for (UE::RemoteExecution::FFileNode& FileNode : DirectoryTree.Files)
	{
		FString Path = Parent;
		Path.Append(MoveTemp(FileNode.Name));
		State.ExecuteResultFiles.Add(MoveTemp(Path), MoveTemp(FileNode.Hash));
	}

	for (UE::RemoteExecution::FDirectoryNode& DirectoryNode : DirectoryTree.Directories)
	{
		FString Path = Parent;
		Path.Append(MoveTemp(DirectoryNode.Name));
		Path.AppendChar('/');
		GatherExecuteFileResults(Data, Parent, DirectoryNode.Hash);
	}
}

FOptionalBuildOutput FRemoteBuildExecutionRequest::ComposeBuildOutput(TMap<FIoHash, TPair<UE::RemoteExecution::EStatusCode, TArray<uint8>>>&& GetBlobsResponse, EStatus& OutStatus)
{
	OutStatus = EStatus::Error;

	for (const TPair<FIoHash, TPair<UE::RemoteExecution::EStatusCode, TArray<uint8>>>& BlobResponse : GetBlobsResponse)
	{
		if (BlobResponse.Value.Key == UE::RemoteExecution::EStatusCode::Ok)
		{
			continue;
		}
	}

	if (!State.ExecuteResultFiles.Contains(TEXT("Build.output")))
	{
		UE_LOG(LogDerivedDataBuildRemoteExecutor, Warning, TEXT("Worker error: build output structure not produced!"));
		return FOptionalBuildOutput();
	};

	FIoHash BuildOutputDigest = State.ExecuteResultFiles[TEXT("Build.output")];

	FOptionalBuildOutput RemoteBuildOutput;

	TMap<FIoHash, FCompressedBuffer> PayloadResponses;
	for (TPair<FString, FIoHash>& ResultFile : State.ExecuteResultFiles)
	{
		TArray<uint8>&& FileData = MoveTemp(GetBlobsResponse[ResultFile.Value].Value);
		Executor.Stats.TotalFileBlobsDownloaded.AddBlob(FileData.Num());

		if (ResultFile.Value == BuildOutputDigest)
		{
			FSharedBuffer BuildOutputBuffer = MakeSharedBufferFromArray(MoveTemp(FileData));

			if (ValidateCompactBinary(BuildOutputBuffer, ECbValidateMode::Default) != ECbValidateError::None)
			{
				UE_LOG(LogDerivedDataBuildRemoteExecutor, Warning, TEXT("Worker error: build output structure not valid!"));
				return FOptionalBuildOutput();
			}

			RemoteBuildOutput = FBuildOutput::Load(State.BuildAction.GetName(), State.BuildAction.GetFunction(), FCbObject(BuildOutputBuffer));
		}
		else
		{
			FCompressedBuffer NewBuffer = FCompressedBuffer::FromCompressed(MakeSharedBufferFromArray(MoveTemp(FileData)));
			PayloadResponses.Add(NewBuffer.GetRawHash(), MoveTemp(NewBuffer));
		}
	}

	if (RemoteBuildOutput.IsNull())
	{
		UE_LOG(LogDerivedDataBuildRemoteExecutor, Warning, TEXT("Remote execution system error: build output blob missing!"));
		return FOptionalBuildOutput();
	}

	FBuildOutputBuilder OutputBuilder = State.BuildSystem.CreateOutput(State.BuildAction.GetName(), State.BuildAction.GetFunction());
	
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

	OutStatus = State.ExecuteResult.ExitCode == 0 ? EStatus::Ok : EStatus::Error;
	return OutputBuilder.Build();
}

bool FRemoteBuildExecutionRequest::ProcessCancellation()
{
	if (bCancelPending.load(std::memory_order_relaxed))
	{
		State.Owner.End(this, [this]
		{
			CompletionCallback({State.BuildAction.GetKey(), {}, {}, EStatus::Canceled});
			CompletionEvent->Trigger();
		});
		return true;
	}
	return false;
}

bool FRemoteBuildExecutionRequest::IsStatusOk(UE::RemoteExecution::EStatusCode Status, const TCHAR* OperationDesc)
{
	if (Status != UE::RemoteExecution::EStatusCode::Ok)
	{
		UE_LOG(LogDerivedDataBuildRemoteExecutor, Warning, TEXT("Remote execution system error: operation '%s' produced an error result (%d)!"), OperationDesc, (int)Status);

		State.Owner.End(this, [this]
		{
			CompletionCallback({State.BuildAction.GetKey(), {}, {}, EStatus::Error});
			CompletionEvent->Trigger();
		});
		return false;
	}
	return true;
}

TFuture<TMap<FIoHash, UE::RemoteExecution::EStatusCode>> FRemoteBuildExecutionRequest::DetermineMissingBlobsAsync()
{
	BuildMerkleTreeNodes();

	State.DoBlobsExistRequest.Add(State.TaskDigest);
	UE_LOG(LogDerivedDataBuildRemoteExecutor, Verbose, TEXT("Checking CAS presence of task (hash: %s) of size %d."), *::LexToString(State.TaskDigest), State.TaskContentBytes.Num());

	State.DoBlobsExistRequest.Add(State.Task.RequirementsHash);
	UE_LOG(LogDerivedDataBuildRemoteExecutor, Verbose, TEXT("Checking CAS presence of requirements (hash: %s) of size %d."), *::LexToString(State.Task.RequirementsHash), State.RequirementsContentBytes.Num());

	for (const TPair<FIoHash, FVariantIndex>& FilesystemItem : State.DigestFilesystemIndex)
	{
		State.DoBlobsExistRequest.Add(FilesystemItem.Key);
		switch (FilesystemItem.Value.NodeType)
		{
		case ENodeType::Directory:
			UE_LOG(LogDerivedDataBuildRemoteExecutor, Verbose, TEXT("Checking CAS presence of directory '%s' (hash: %s) of size %d."),
				*FString(State.Directories[FilesystemItem.Value.Index].Path),
				*::LexToString(FilesystemItem.Key),
				State.Directories[FilesystemItem.Value.Index].ContentBytes.Num());
			break;
		case ENodeType::File:
			UE_LOG(LogDerivedDataBuildRemoteExecutor, Verbose, TEXT("Checking CAS presence of file '%s' (hash: %s, type: %s) of size %d."),
				*FString(State.Files[FilesystemItem.Value.Index].Path),
				*::LexToString(FilesystemItem.Key),
				LexToString(State.Files[FilesystemItem.Value.Index].Type), State.Files[FilesystemItem.Value.Index].File.Size);
			break;
		default:
			checkNoEntry();
			break;
		}
	}
	return Executor.ContentAddressableStorage->DoBlobsExistAsync(Executor.NameSpaceId, State.DoBlobsExistRequest);
}

void FRemoteBuildExecutionRequest::LoadMissingWorkerFileBlobsAsync()
{
	TArray<FIoHash> WorkerFileHashes;
	TMultiMap<FIoHash, uint32> WorkerFileMapping;
	for (const TPair<FIoHash, UE::RemoteExecution::EStatusCode>& MissingItem : State.DoBlobsExistResponse)
	{
		if (MissingItem.Value == UE::RemoteExecution::EStatusCode::Ok)
		{
			continue;
		}
		TArray<FVariantIndex> DigestFilesystemEntries;
		State.DigestFilesystemIndex.MultiFind(MissingItem.Key, DigestFilesystemEntries);

		for (FVariantIndex& VariantIndex : DigestFilesystemEntries)
		{
			if (VariantIndex.NodeType == ENodeType::File)
			{
				const FMerkleTreeFileBuilder& File = State.Files[VariantIndex.Index];
				if ((File.Type == EFileType::Worker) && File.ContentBytes.IsNull())
				{
					WorkerFileHashes.Emplace(MissingItem.Key);
					WorkerFileMapping.Add(MissingItem.Key, VariantIndex.Index);
				}
			}
		}
	}

	FRequestOwner BlockingOwner(EPriority::Blocking);
	State.BuildWorker.FindFileData(WorkerFileHashes, BlockingOwner,
		[this, &WorkerFileMapping] (FBuildWorkerFileDataCompleteParams&& Params)
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
		});
	BlockingOwner.Wait();
}

TFuture<TMap<FIoHash, UE::RemoteExecution::EStatusCode>> FRemoteBuildExecutionRequest::UploadMissingBlobsAsync()
{
	for (const TPair<FIoHash, UE::RemoteExecution::EStatusCode>& MissingItem : State.DoBlobsExistResponse)
	{
		if (MissingItem.Value == UE::RemoteExecution::EStatusCode::Ok)
		{
			continue;
		}

		if (MissingItem.Key == State.TaskDigest)
		{
			Executor.Stats.TotalActionBlobsUploaded.AddBlob(State.TaskContentBytes.Num());
			State.PutBlobsRequest.Add(State.TaskDigest, MoveTemp(State.TaskContentBytes));
			UE_LOG(LogDerivedDataBuildRemoteExecutor, Verbose, TEXT("Uploading task (hash: %s) of upload size %d."), *::LexToString(State.TaskDigest), State.PutBlobsRequest[State.TaskDigest].Num());
		}
		else if (MissingItem.Key == State.Task.RequirementsHash)
		{
			Executor.Stats.TotalRequirementBlobsUploaded.AddBlob(State.RequirementsContentBytes.Num());
			State.PutBlobsRequest.Add(State.Task.RequirementsHash, MoveTemp(State.RequirementsContentBytes));
			UE_LOG(LogDerivedDataBuildRemoteExecutor, Verbose, TEXT("Uploading requirements (hash: %s) of upload size %d."), *::LexToString(State.Task.RequirementsHash), State.PutBlobsRequest[State.Task.RequirementsHash].Num());
		}
		else
		{
			const FVariantIndex VariantIndex = State.DigestFilesystemIndex.FindChecked(MissingItem.Key);
			switch (VariantIndex.NodeType)
			{
			case ENodeType::Directory:
				Executor.Stats.TotalDirectoryBlobsUploaded.AddBlob(State.Directories[VariantIndex.Index].ContentBytes.Num());
				State.PutBlobsRequest.Add(State.Directories[VariantIndex.Index].Digest.GetValue(), MoveTemp(State.Directories[VariantIndex.Index].ContentBytes));
				UE_LOG(LogDerivedDataBuildRemoteExecutor, Verbose, TEXT("Uploading directory '%s' (hash: %s) of upload size %d."),
					*FString(State.Directories[VariantIndex.Index].Path),
					*::LexToString(State.Directories[VariantIndex.Index].Digest.GetValue()),
					State.PutBlobsRequest[State.Directories[VariantIndex.Index].Digest.GetValue()].Num());
				break;
			case ENodeType::File:
				{
					Executor.Stats.TotalFileBlobsUploaded.AddBlob(State.Files[VariantIndex.Index].ContentBytes.GetSize());
					FCompositeBuffer& FileBuffer = State.Files[VariantIndex.Index].ContentBytes;
					check(!FileBuffer.IsNull());
					TArray<uint8> FileData;
					FileData.Reserve(FileBuffer.GetSize());
					for (const FSharedBuffer& Segment : State.Files[VariantIndex.Index].ContentBytes.GetSegments())
					{
						FileData.Append((const uint8 *)Segment.GetData(), Segment.GetSize());
					}
					FileBuffer.Reset();
					State.PutBlobsRequest.Add(State.Files[VariantIndex.Index].File.Hash, MoveTemp(FileData));
					UE_LOG(LogDerivedDataBuildRemoteExecutor, Verbose, TEXT("Uploading file '%s' (hash: %s, type: %s) of upload size %d."),
						*FString(State.Files[VariantIndex.Index].Path),
						*::LexToString(State.Files[VariantIndex.Index].File.Hash), LexToString(State.Files[VariantIndex.Index].Type),
						State.PutBlobsRequest[State.Files[VariantIndex.Index].File.Hash].Num());
				}
				break;
			default:
				checkNoEntry();
				break;
			}
		}
	}

	return Executor.ContentAddressableStorage->PutBlobsAsync(Executor.NameSpaceId, State.PutBlobsRequest);
}

TFuture<TPair<UE::RemoteExecution::EStatusCode, UE::RemoteExecution::FGetTaskUpdatesResponse>> FRemoteBuildExecutionRequest::ExecuteBuildAsync()
{
	State.ExecuteRequest.RequirementsHash = State.Task.RequirementsHash;
	State.ExecuteRequest.TaskHashes.Empty();
	State.ExecuteRequest.TaskHashes.Add(State.TaskDigest);
	//State.ExecuteRequest.DoNotCache = true;
	return Executor.Execution->RunTasksAsync(State.ExecuteRequest, Executor.GlobalExecutionTimeoutSeconds);
}

TFuture<TPair<UE::RemoteExecution::EStatusCode, UE::RemoteExecution::FGetObjectTreeResponse>> FRemoteBuildExecutionRequest::GetTaskResultAsync()
{
	return Executor.ContentAddressableStorage->GetObjectTreeAsync(Executor.NameSpaceId, State.ExecuteResponse.ResultHash);
}

TFuture<TMap<FIoHash, TPair<UE::RemoteExecution::EStatusCode, TArray<uint8>>>> FRemoteBuildExecutionRequest::DownloadResultsAsync()
{
	return Executor.ContentAddressableStorage->GetBlobsAsync(Executor.NameSpaceId, State.GetBlobsRequest);
}

void FRemoteBuildExecutionRequest::OnMissingBlobsDetermined(TMap<FIoHash, UE::RemoteExecution::EStatusCode>&& Result)
{
	if (ProcessCancellation())
	{
		return;
	}

	for (const TPair<FIoHash, UE::RemoteExecution::EStatusCode>& Entry : Result)
	{
		if (Entry.Value != UE::RemoteExecution::EStatusCode::NotFound && !IsStatusOk(Entry.Value, TEXT("FindMissingBlobs")))
		{
			return;
		}
	}

	State.DoBlobsExistResponse = MoveTemp(Result);
	constexpr bool bForceUploads = false;
	if (bForceUploads)
	{
		for (TPair<FIoHash, UE::RemoteExecution::EStatusCode>& Entry : State.DoBlobsExistResponse)
		{
			Entry.Value = UE::RemoteExecution::EStatusCode::NotFound;
		}
	}

	TArray<FStringView> MissingInputViews;
	GatherMissingInputFileBlobs(MissingInputViews);
	if (!MissingInputViews.IsEmpty())
	{
		State.Owner.End(this, [this, &MissingInputViews]
		{
			CompletionCallback({State.BuildAction.GetKey(), {}, MissingInputViews, EStatus::Ok});
			CompletionEvent->Trigger();
		});
		return;
	}

	if (!Executor.LimitingHeuristics.TryStartNewBuild(Executor.Stats))
	{
		State.Owner.End(this, [this]
		{
			CompletionCallback({State.BuildAction.GetKey(), {}, {}, EStatus::Error});
			CompletionEvent->Trigger();
		});
		return;
	}

	bHeuristicBuildStarted = true;

	// TODO: This should be async but isn't.  Requires IRequest chaining.
	LoadMissingWorkerFileBlobsAsync();

	TMap<FIoHash, UE::RemoteExecution::EStatusCode> Missing = State.DoBlobsExistResponse.FilterByPredicate([](const TPair<FIoHash, UE::RemoteExecution::EStatusCode>& Entry) { return Entry.Value == UE::RemoteExecution::EStatusCode::NotFound; });

	if (!Missing.IsEmpty())
	{
		UploadMissingBlobsAsync()
			.Next([this] (const TMap<FIoHash, UE::RemoteExecution::EStatusCode>& InnerResult) { OnMissingBlobsUploaded(InnerResult); });
	}
	else
	{
		ExecuteBuildAsync()
			.Next([this] (TPair<UE::RemoteExecution::EStatusCode, UE::RemoteExecution::FGetTaskUpdatesResponse>&& Result) { OnExecutionCompleted(Result.Key, MoveTemp(Result.Value)); });
	}
}

void FRemoteBuildExecutionRequest::OnMissingBlobsUploaded(const TMap<FIoHash, UE::RemoteExecution::EStatusCode>& Result)
{
	if (ProcessCancellation())
	{
		return;
	}

	for (const TPair<FIoHash, UE::RemoteExecution::EStatusCode>& Entry : Result)
	{
		if (!IsStatusOk(Entry.Value, TEXT("BatchUploadBlobs")))
		{
			return;
		}
	}

	UE_LOG(LogDerivedDataBuildRemoteExecutor, Verbose, TEXT("Uploaded %d data blobs for remote execution."), State.PutBlobsRequest.Num());
	if (!ValidateUploadSuccess(Result))
	{
		State.Owner.End(this, [this]
		{
			CompletionCallback({State.BuildAction.GetKey(), {}, {}, EStatus::Error});
			CompletionEvent->Trigger();
		});
		return;
	}

	ExecuteBuildAsync()
		.Next([this] (TPair<UE::RemoteExecution::EStatusCode, UE::RemoteExecution::FGetTaskUpdatesResponse>&& Result) { OnExecutionCompleted(Result.Key, MoveTemp(Result.Value)); });

}

void FRemoteBuildExecutionRequest::OnExecutionCompleted(UE::RemoteExecution::EStatusCode Status, UE::RemoteExecution::FGetTaskUpdatesResponse&& Result)
{
	if (ProcessCancellation())
	{
		return;
	}

	if (!IsStatusOk(Status, TEXT("OnExecutionCompleted")))
	{
		if (!Result.Updates.IsEmpty())
		{
			UE_LOG(LogDerivedDataBuildRemoteExecutor, Warning, TEXT("Remote execution system error: Task %s Outcome %s: %s"),
				*FString::FromHexBlob(Result.Updates[0].TaskHash.GetBytes(), sizeof(FIoHash::ByteArray)),
				*UE::RemoteExecution::ComputeTaskOutcomeString(Result.Updates[0].Outcome),
				*Result.Updates[0].Detail);
		}
		return;
	}

	if (Result.Updates.IsEmpty())
	{
		UE_LOG(LogDerivedDataBuildRemoteExecutor, Warning, TEXT("Remote execution system error: Failed to get results from remote build operation!"));
		State.Owner.End(this, [this]
			{
				CompletionCallback({ State.BuildAction.GetKey(), {}, {}, EStatus::Error });
				CompletionEvent->Trigger();
			});
		return;
	}

	if (Result.Updates[0].Outcome != UE::RemoteExecution::EComputeTaskOutcome::Success)
	{
		UE_LOG(LogDerivedDataBuildRemoteExecutor, Warning, TEXT("Remote execution system error: Task %s Outcome %s: %s"),
			*FString::FromHexBlob(Result.Updates[0].TaskHash.GetBytes(), sizeof(FIoHash::ByteArray)),
			*UE::RemoteExecution::ComputeTaskOutcomeString(Result.Updates[0].Outcome),
			*Result.Updates[0].Detail);
		State.Owner.End(this, [this]
			{
				CompletionCallback({ State.BuildAction.GetKey(), {}, {}, EStatus::Error });
				CompletionEvent->Trigger();
			});
		return;
	}

	if (Result.Updates[0].ResultHash == FIoHash::Zero)
	{
		UE_LOG(LogDerivedDataBuildRemoteExecutor, Warning, TEXT("Remote execution system error: Zero ResultHash returned from remote build operation!"));
		State.Owner.End(this, [this]
			{
				CompletionCallback({ State.BuildAction.GetKey(), {}, {}, EStatus::Error });
				CompletionEvent->Trigger();
			});
		return;
	}

	State.ExecuteResponse = MoveTemp(Result.Updates[0]);
	GetTaskResultAsync()
		.Next([this] (TPair<UE::RemoteExecution::EStatusCode, UE::RemoteExecution::FGetObjectTreeResponse>&& InnerResult)
			{
				OnTaskResultDownloaded(InnerResult.Key, MoveTemp(InnerResult.Value));
			});

}

void FRemoteBuildExecutionRequest::OnTaskResultDownloaded(UE::RemoteExecution::EStatusCode Status, UE::RemoteExecution::FGetObjectTreeResponse&& Result)
{
	if (ProcessCancellation())
	{
		return;
	}

	if (!IsStatusOk(Status, TEXT("OnTaskResultDownloaded")))
	{
		return;
	}

	const FCbObjectView View = FCbObjectView(Result.Objects[State.ExecuteResponse.ResultHash].GetData());
	State.ExecuteResult.Load(View);
	State.GetBlobsRequest = MoveTemp(Result.BinaryAttachments);
	GatherExecuteFileResults(Result.Objects, TEXT(""), State.ExecuteResult.OutputHash);

	DownloadResultsAsync()
		.Next([this](TMap<FIoHash, TPair<UE::RemoteExecution::EStatusCode, TArray<uint8>>>&& InnerResult)
			{
				OnOutputBlobsDownloaded(MoveTemp(InnerResult));
			});
}

void FRemoteBuildExecutionRequest::OnOutputBlobsDownloaded(TMap<FIoHash, TPair<UE::RemoteExecution::EStatusCode, TArray<uint8>>>&& Result)
{
	if (ProcessCancellation())
	{
		return;
	}

	EStatus BuildStatus = EStatus::Error;
	FOptionalBuildOutput BuildOutput = ComposeBuildOutput(MoveTemp(Result), BuildStatus);
	if (BuildStatus == EStatus::Ok)
	{
		Executor.Stats.TotalSuccessfulRemoteBuilds.fetch_add(1, std::memory_order_relaxed);
	}

	State.Owner.End(this, [this, &BuildOutput, BuildStatus]() mutable
	{
		CompletionCallback({State.BuildAction.GetKey(), MoveTemp(BuildOutput), {}, BuildStatus});
		CompletionEvent->Trigger();
	});
}

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
