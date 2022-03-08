// Copyright Epic Games, Inc. All Rights Reserved.

#include "VirtualizationSourceControlBackend.h"

#include "Containers/Ticker.h"
#include "HAL/FileManager.h"
#include "IO/IoHash.h"
#include "ISourceControlModule.h"
#include "ISourceControlProvider.h"
#include "Logging/MessageLog.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/PathViews.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "SourceControlOperations.h"
#include "VirtualizationSourceControlUtilities.h"
#include "VirtualizationUtilities.h"

// When the SourceControl module (or at least the perforce source control module) is thread safe we
// can enable this and stop using the hacky work around 'TryToDownloadFileFromBackgroundThread'
#define IS_SOURCE_CONTROL_THREAD_SAFE 0

#define LOCTEXT_NAMESPACE "Virtualization"

namespace UE::Virtualization
{

/** Utility function to create a directory to submit payloads from. */
[[nodiscard]] static bool TryCreateSubmissionSessionDirectory(FStringView SessionDirectoryPath)
{
	// Write out an ignore file to the submission directory (will create the directory if needed)
	{
		TStringBuilder<260> IgnoreFilePath;

		// TODO: We should find if P4IGNORE is actually set and if so extract the filename to use.
		// This will require extending the source control module
		FPathViews::Append(IgnoreFilePath, SessionDirectoryPath, TEXT(".p4ignore.txt"));

		// A very basic .p4ignore file that should make sure that we are only submitting valid .upayload files.
		// 
		// Since the file should only exist while we are pushing payloads, it is not expected that anyone will need
		// to read the file. Due to this we only include the bare essentials in terms of documentation.

		TStringBuilder<512> FileContents;

		FileContents << TEXT("# Ignore all files\n*\n\n");
		FileContents << TEXT("# Allow.payload files as long as they are the expected 3 directories deep\n!*/*/*/*.upayload\n\n");

		if (!FFileHelper::SaveStringToFile(FileContents, IgnoreFilePath.ToString()))
		{
			return false;
		}
	}

	return true;
}

/** Builds a changelist description to be used when submitting a payload to source control */
static void CreateDescription(const FString& ProjectName, const TArray<const FPushRequest*>& FileRequests, TStringBuilder<512>& OutDescription)
{
	// TODO: Maybe make writing out the project name an option or allow for a codename to be set via ini file?
	OutDescription << TEXT("Submitted for project: ");
	OutDescription << ProjectName;

	bool bInitialNewline = false;

	for (const FPushRequest* Request : FileRequests)
	{
		if (!Request->Context.IsEmpty())
		{
			if (!bInitialNewline)
			{
				OutDescription << TEXT("\n");
				bInitialNewline = true;
			}

			OutDescription << TEXT("\n") << Request->Identifier << "\t: " << Request->Context;
		}
	}
}

FSourceControlBackend::FSourceControlBackend(FStringView ProjectName, FStringView ConfigName, FStringView InDebugName)
	: IVirtualizationBackend(ConfigName, InDebugName, EOperations::Both)
	, ProjectName(ProjectName)
{
}

bool FSourceControlBackend::Initialize(const FString& ConfigEntry)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FSourceControlBackend::Initialize);

	// We require that a valid depot root has been provided
	if (!FParse::Value(*ConfigEntry, TEXT("DepotRoot="), DepotRoot))
	{
		UE_LOG(LogVirtualization, Error, TEXT("'DepotRoot=' not found in the config file"));
		return false;
	}

	if (!FindSubmissionWorkingDir(ConfigEntry))
	{
		return false;
	}

	// Optional config values
	FParse::Bool(*ConfigEntry, TEXT("UsePartitionedClient="), bUsePartitionedClient);
	UE_LOG(LogVirtualization, Display, TEXT("[%s] Using partitioned clients: '%s'"), *GetDebugName(), bUsePartitionedClient ? TEXT("true") : TEXT("false"));

	ISourceControlModule& SSCModule = ISourceControlModule::Get();

	// We require perforce as the source control provider as it is currently the only one that has the virtualization functionality implemented
	const FName SourceControlName = SSCModule.GetProvider().GetName();
	if (SourceControlName.IsNone())
	{
		// No source control provider is set so we can try to set it to "Perforce"
		// Note this call will fatal error if "Perforce" is not a valid option
		SSCModule.SetProvider(FName("Perforce"));
	}
	else if (SourceControlName != TEXT("Perforce"))
	{
		UE_LOG(LogVirtualization, Error, TEXT("Attempting to initialize FSourceControlBackend but source control is '%s' and only Perforce is currently supported!"), *SourceControlName.ToString());
		return false;
	}

	ISourceControlProvider& SCCProvider = SSCModule.GetProvider();

	if (!SCCProvider.IsAvailable())
	{
		SCCProvider.Init();
	}

	// Note that if the connect is failing then we expect it to fail here rather than in the subsequent attempts to get the meta info file
	TSharedRef<FConnect, ESPMode::ThreadSafe> ConnectCommand = ISourceControlOperation::Create<FConnect>();
	if (SCCProvider.Execute(ConnectCommand, FString(), EConcurrency::Synchronous) != ECommandResult::Succeeded)
	{		
		FTextBuilder Errors;
		for (const FText& Msg : ConnectCommand->GetResultInfo().ErrorMessages)
		{
			Errors.AppendLine(Msg);
		}

		FMessageLog Log("LogVirtualization");
		Log.Error(	FText::Format(LOCTEXT("FailedSourceControlConnection", "Failed to connect to source control backend with the following errors:\n{0}\nThe source control backend will be unable to pull payloads!\nTrying logging in with the 'p4 login' command or by using p4vs/UnrealGameSync."),
					Errors.ToText()));
		
		OnConnectionError();
		return true;
	}

	// When a source control depot is set up a file named 'payload_metainfo.txt' should be submitted to it's root.
	// This allows us to check for the existence of the file to confirm that the depot root is indeed valid.
	const FString PayloadMetaInfoPath = FString::Printf(TEXT("%spayload_metainfo.txt"), *DepotRoot);

#if IS_SOURCE_CONTROL_THREAD_SAFE
	TSharedRef<FDownloadFile, ESPMode::ThreadSafe> DownloadCommand = ISourceControlOperation::Create<FDownloadFile>();
	if (SCCProvider.Execute(DownloadCommand, PayloadMetaInfoPath, EConcurrency::Synchronous) != ECommandResult::Succeeded)
	{
		FMessageLog Log("LogVirtualization");

		Log.Error(	FText::Format(LOCTEXT("FailedMetaInfo", "Failed to find 'payload_metainfo.txt' in the depot '{0}'\nThe source control backend will be unable to pull payloads, is your source control  config set up correctly?"),
					FText::FromString(DepotRoot)));
		
		OnConnectionError();
		return true;
	}	
#else
	TSharedRef<FDownloadFile, ESPMode::ThreadSafe> DownloadCommand = ISourceControlOperation::Create<FDownloadFile>();
	if (!SCCProvider.TryToDownloadFileFromBackgroundThread(DownloadCommand, PayloadMetaInfoPath))
	{
		FMessageLog Log("LogVirtualization");

		Log.Error(	FText::Format(LOCTEXT("FailedMetaInfo", "Failed to find 'payload_metainfo.txt' in the depot '{0}'\nThe source control backend will be unable to pull payloads, is your source control  config set up correctly?"),
					FText::FromString(DepotRoot)));
		
		OnConnectionError();
		return true;
	}		
#endif //IS_SOURCE_CONTROL_THREAD_SAFE

	FSharedBuffer MetaInfoBuffer = DownloadCommand->GetFileData(PayloadMetaInfoPath);
	if (MetaInfoBuffer.IsNull())
	{
		FMessageLog Log("LogVirtualization");

		Log.Error(	FText::Format(LOCTEXT("FailedMetaInfo", "Failed to find 'payload_metainfo.txt' in the depot '{0}'\nThe source control backend will be unable to pull payloads, is your source control  config set up correctly?"),
					FText::FromString(DepotRoot)));

		OnConnectionError();
		return true;
	}

	// Currently we do not do anything with the payload meta info, in the future we could structure
	// it's format to include more information that might be worth logging or something. 
	// But for now being able to pull the payload meta info path at least shows that we can use the
	// depot.

	return true;
}

FCompressedBuffer FSourceControlBackend::PullData(const FIoHash& Id)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FSourceControlBackend::PullData);

	TStringBuilder<512> DepotPath;
	CreateDepotPath(Id, DepotPath);

	UE_LOG(LogVirtualization, Verbose, TEXT("[%s] Attempting to pull '%s' from source control"), *GetDebugName(), *DepotPath);

	ISourceControlProvider& SCCProvider = ISourceControlModule::Get().GetProvider();

#if IS_SOURCE_CONTROL_THREAD_SAFE
	TSharedRef<FDownloadFile, ESPMode::ThreadSafe> DownloadCommand = ISourceControlOperation::Create<FDownloadFile>(FDownloadFile::EVerbosity::None);
	if (SCCProvider.Execute(DownloadCommand, DepotPath.ToString(), EConcurrency::Synchronous) != ECommandResult::Succeeded)
	{
		return FCompressedBuffer();
	}
#else
	TSharedRef<FDownloadFile> DownloadCommand = ISourceControlOperation::Create<FDownloadFile>(FDownloadFile::EVerbosity::None);
	if (!SCCProvider.TryToDownloadFileFromBackgroundThread(DownloadCommand, DepotPath.ToString()))
	{
		return FCompressedBuffer();
	}
#endif

	// The payload was created by FCompressedBuffer::Compress so we can return it 
	// as a FCompressedBuffer.
	FSharedBuffer Buffer = DownloadCommand->GetFileData(DepotPath);
	return FCompressedBuffer::FromCompressed(Buffer);
}

bool FSourceControlBackend::DoesPayloadExist(const FIoHash& Id)
{
	TArray<bool> Result;

	if (FSourceControlBackend::DoPayloadsExist(MakeArrayView<const FIoHash>(&Id, 1), Result))
	{
		check(Result.Num() == 1);
		return Result[0];
	}
	else
	{
		return false;
	}
}

EPushResult FSourceControlBackend::PushData(const FIoHash& Id, const FCompressedBuffer& Payload, const FString& Context)
{
	FPushRequest Request(Id, Payload, Context);
	return FSourceControlBackend::PushData(MakeArrayView(&Request, 1)) ? EPushResult::Success : EPushResult::Failed;
}

bool FSourceControlBackend::PushData(TArrayView<FPushRequest> Requests)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FSourceControlBackend::PushData);

	// TODO: Consider creating one workspace and one temp dir per session rather than per push.
	// Although this would require more checking on start up to check for lingering workspaces
	// and directories in case of editor crashes.
	// We'd also need to remove each submitted file from the workspace after submission so that
	// we can delete the local file

	// We cannot easily submit files from within the project root due to p4 ignore rules
	// so we will use the user temp directory instead. We append a guid to the root directory
	// to avoid potentially conflicting with other editor processes that might be running.

	const FGuid SessionGuid = FGuid::NewGuid();
	
	UE_LOG(LogVirtualization, Display, TEXT("[%s] Started payload submission session '%s' for '%d' payload(s)"), *GetDebugName(), *LexToString(SessionGuid), Requests.Num());

	TStringBuilder<260> SessionDirectory;
	FPathViews::Append(SessionDirectory, SubmissionRootDir, SessionGuid);

	if (!TryCreateSubmissionSessionDirectory(SessionDirectory))
	{
		UE_LOG(LogVirtualization, Error, TEXT("[%s] Failed to created directory '%s' to submit payloads from"), *GetDebugName(), SessionDirectory.ToString());
		return false;
	}

	UE_LOG(LogVirtualization, Display, TEXT("[%s] Created directory '%s' to submit payloads from"), *GetDebugName(), SessionDirectory.ToString());

	ON_SCOPE_EXIT
	{
		// Clean up the payload file from disk and the temp directories, but we do not need to give errors if any of these operations fail.
		IFileManager::Get().DeleteDirectory(SessionDirectory.ToString(), false, true);
	};

	TArray<FString> FilesToSubmit;
	FilesToSubmit.Reserve(Requests.Num());

	// Write the payloads to disk so that they can be submitted (source control module currently requires the files to
	// be on disk)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FSourceControlBackend::PushData::CreateFiles);

		for (const FPushRequest& Request : Requests)
		{
			TStringBuilder<52> LocalPayloadPath;
			Utils::PayloadIdToPath(Request.Identifier, LocalPayloadPath);

			TStringBuilder<260> PayloadFilePath;
			FPathViews::Append(PayloadFilePath, SessionDirectory, LocalPayloadPath);

			UE_LOG(LogVirtualization, Verbose, TEXT("[%s] Writing payload to '%s' for submission"), *GetDebugName(), PayloadFilePath.ToString());

			TUniquePtr<FArchive> FileAr(IFileManager::Get().CreateFileWriter(PayloadFilePath.ToString()));
			if (!FileAr)
			{
				TStringBuilder<MAX_SPRINTF> SystemErrorMsg;
				Utils::GetFormattedSystemError(SystemErrorMsg);

				UE_LOG(LogVirtualization, Error, TEXT("[%s] Failed to write payload '%s' contents to '%s' due to system error: %s"),
					*GetDebugName(),
					*LexToString(Request.Identifier),
					PayloadFilePath.ToString(),
					SystemErrorMsg.ToString());

				return false;
			}

			Request.Payload.Save(*FileAr);

			if (!FileAr->Close())
			{
				TStringBuilder<MAX_SPRINTF> SystemErrorMsg;
				Utils::GetFormattedSystemError(SystemErrorMsg);

				UE_LOG(LogVirtualization, Error, TEXT("[%s] Failed to write payload '%s' contents to '%s' due to system error: %s"),
					*GetDebugName(),
					*LexToString(Request.Identifier),
					*PayloadFilePath,
					SystemErrorMsg.
					ToString());

				return false;
			}

			FilesToSubmit.Emplace(MoveTemp(PayloadFilePath));
		}
	}

	check(Requests.Num() == FilesToSubmit.Num());

	TStringBuilder<64> WorkspaceName;
	WorkspaceName << TEXT("VASubmission-") << SessionGuid;

	ISourceControlProvider& SCCProvider = ISourceControlModule::Get().GetProvider();

	// Create a temp workspace so that we can submit the payload from
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FSourceControlBackend::PushData::CreateWorkspace);
		TSharedRef<FCreateWorkspace> CreateWorkspaceCommand = ISourceControlOperation::Create<FCreateWorkspace>(WorkspaceName, SessionDirectory);

		TStringBuilder<512> DepotMapping;
		DepotMapping << DepotRoot << TEXT("...");

		TStringBuilder<128> ClientMapping;
		ClientMapping << TEXT("//") << WorkspaceName << TEXT("/...");

		CreateWorkspaceCommand->AddNativeClientViewMapping(DepotMapping, ClientMapping);

		if (bUsePartitionedClient)
		{
			CreateWorkspaceCommand->SetType(FCreateWorkspace::EType::Partitioned);
		}

		CreateWorkspaceCommand->SetDescription(TEXT("This workspace was autogenerated when submitting virtualized payloads to source control"));

		if (SCCProvider.Execute(CreateWorkspaceCommand) != ECommandResult::Succeeded)
		{
			UE_LOG(LogVirtualization, Error, TEXT("[%s] Failed to create temp workspace '%s' to submit payloads from"),
				*GetDebugName(),
				WorkspaceName.ToString());

			return false;
		}
	}

	ON_SCOPE_EXIT
	{
		// Remove the temp workspace mapping
		if (SCCProvider.Execute(ISourceControlOperation::Create<FDeleteWorkspace>(WorkspaceName)) != ECommandResult::Succeeded)
		{
			UE_LOG(LogVirtualization, Warning, TEXT("[%s] Failed to remove temp workspace '%s' please delete manually"), *GetDebugName(), WorkspaceName.ToString());
		}
	};

	FString OriginalWorkspace;
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FSourceControlBackend::PushData::SwitchWorkspace);

		FSourceControlResultInfo SwitchToNewWorkspaceInfo;
		if (SCCProvider.SwitchWorkspace(WorkspaceName, SwitchToNewWorkspaceInfo, &OriginalWorkspace) != ECommandResult::Succeeded)
		{
			UE_LOG(LogVirtualization, Error, TEXT("[%s] Failed to switch to temp workspace '%s' when trying to submit payloads"),
				*GetDebugName(),
				WorkspaceName.ToString());

			return false;
		}
	}

	ON_SCOPE_EXIT
	{
		FSourceControlResultInfo SwitchToOldWorkspaceInfo;
		if (SCCProvider.SwitchWorkspace(OriginalWorkspace, SwitchToOldWorkspaceInfo, nullptr) != ECommandResult::Succeeded)
		{
			// Failing to restore the old workspace could result in confusing editor issues and data loss, so for now it is fatal.
			// The medium term plan should be to refactor the SourceControlModule so that we could use an entirely different 
			// ISourceControlProvider so as not to affect the rest of the editor.
			UE_LOG(LogVirtualization, Fatal, TEXT("[%s] Failed to restore the original workspace to temp workspace '%s' continuing would risk editor instability and potential data loss"),
					*GetDebugName(),
					*OriginalWorkspace);
		}
	};

	TArray<FSourceControlStateRef> FileStates;
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FSourceControlBackend::PushData::GetFileStates);
		if (SCCProvider.GetState(FilesToSubmit, FileStates, EStateCacheUsage::ForceUpdate) != ECommandResult::Succeeded)
		{
			UE_LOG(LogVirtualization, Error, TEXT("[%s] Failed to find the current file state for payloads"), *GetDebugName());
			return false;
		}
	}
	check(Requests.Num() == FileStates.Num());

	TArray<FString> FilesToAdd;
	FilesToAdd.Reserve(FilesToSubmit.Num());

	TArray<const FPushRequest*> FileRequests;
	FileRequests.Reserve(FilesToSubmit.Num());

	for (int32 Index = 0; Index < FilesToSubmit.Num(); ++Index)
	{
		if (FileStates[Index]->IsSourceControlled())
		{
			// TODO: Maybe check if the data is the same (could be different if the compression algorithm has changed)
			// TODO: Should we respect if the file is deleted as technically we can still get access to it?
			Requests[Index].Status = FPushRequest::EStatus::Success;
		}
		else if (FileStates[Index]->CanAdd())
		{
			FilesToAdd.Add(FilesToSubmit[Index]);
			FileRequests.Add(&Requests[Index]);
		}
		else
		{
			UE_LOG(LogVirtualization, Error, TEXT("[%s] The the payload file '%s' is not in source control but also cannot be marked for Add"), *GetDebugName(), *FilesToSubmit[Index]);
			return false;
		}
	}

	check(FileRequests.Num() == FilesToAdd.Num());

	if (FilesToAdd.IsEmpty())
	{
		return true;
	}

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FSourceControlBackend::PushData::AddFiles);

		if (SCCProvider.Execute(ISourceControlOperation::Create<FMarkForAdd>(), FilesToAdd) != ECommandResult::Succeeded)
		{
			UE_LOG(LogVirtualization, Error, TEXT("[%s] Failed to mark the payload file for Add in source control"), *GetDebugName());
			return false;
		}
	}

	// Now submit the payload
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FSourceControlBackend::PushData::SubmitFiles);

		TSharedRef<FCheckIn, ESPMode::ThreadSafe> CheckInOperation = ISourceControlOperation::Create<FCheckIn>();

		TStringBuilder<512> Description;
		CreateDescription(ProjectName, FileRequests, Description);

		CheckInOperation->SetDescription(FText::FromString(Description.ToString()));

		if (SCCProvider.Execute(CheckInOperation, FilesToAdd) != ECommandResult::Succeeded)
		{
			UE_LOG(LogVirtualization, Error, TEXT("[%s] Failed to submit the payload file(s) to source control"), *GetDebugName());
			return false;
		}
	}

	// TODO: We really should be setting a more fine grain status for each request, or not bother with the status at all
	for (FPushRequest& Request : Requests)
	{
		Request.Status = FPushRequest::EStatus::Success;
	}

	return true;
}

bool FSourceControlBackend::DoPayloadsExist(TArrayView<const FIoHash> PayloadIds, TArray<bool>& OutResults)
{
	ISourceControlProvider& SCCProvider = ISourceControlModule::Get().GetProvider();

	TArray<FString> DepotPaths;
	DepotPaths.Reserve(PayloadIds.Num());

	TArray<FSourceControlStateRef> PathStates;

	for (const FIoHash& PayloadId : PayloadIds)
	{
		if (!PayloadId.IsZero())
		{
			TStringBuilder<52> LocalPayloadPath;
			Utils::PayloadIdToPath(PayloadId, LocalPayloadPath);

			DepotPaths.Emplace(WriteToString<512>(DepotRoot, LocalPayloadPath));
		}
	}

	ECommandResult::Type Result = SCCProvider.GetState(DepotPaths, PathStates, EStateCacheUsage::ForceUpdate);
	if (Result != ECommandResult::Type::Succeeded)
	{
		UE_LOG(LogVirtualization, Error, TEXT("[%s] Failed to query the state of files in the source control depot"), *GetDebugName());
		return false;
	}

	check(DepotPaths.Num() == PathStates.Num()); // We expect that all paths return a state

	OutResults.SetNum(PayloadIds.Num());

	int32 StatusIndex = 0;
	for (int32 Index = 0; Index < PayloadIds.Num(); ++Index)
	{
		if (!PayloadIds[Index].IsZero())
		{
			OutResults[Index] = PathStates[StatusIndex++]->IsSourceControlled();
		}
	}

	return true;
}

void FSourceControlBackend::CreateDepotPath(const FIoHash& PayloadId, FStringBuilderBase& OutPath)
{
	TStringBuilder<52> PayloadPath;
	Utils::PayloadIdToPath(PayloadId, PayloadPath);

	OutPath << DepotRoot << PayloadPath;
}

bool FSourceControlBackend::FindSubmissionWorkingDir(const FString& ConfigEntry)
{
	// Note regarding path lengths.
	// During submission each payload path will be 90 characters in length which will then be appended to
	// the SubmissionWorkingDir

	SubmissionRootDir = FPlatformMisc::GetEnvironmentVariable(TEXT("UE-VirtualizationWorkingDir"));

	if (!SubmissionRootDir.IsEmpty())
	{
		FPaths::NormalizeDirectoryName(SubmissionRootDir);
		UE_LOG(LogVirtualization, Display, TEXT("[%s] Found Environment Variable: UE-VirtualizationWorkingDir"), *GetDebugName());	
	}
	else
	{

		bool bSubmitFromTempDir = false;
		FParse::Bool(*ConfigEntry, TEXT("SubmitFromTempDir="), bSubmitFromTempDir);

		TStringBuilder<260> PathBuilder;
		if (bSubmitFromTempDir)
		{
			FPathViews::Append(PathBuilder, FPlatformProcess::UserTempDir(), TEXT("UnrealEngine/VASubmission"));	
		}
		else
		{
			FPathViews::Append(PathBuilder, FPaths::ProjectSavedDir(), TEXT("VASubmission"));
		}

		SubmissionRootDir = PathBuilder;
	}

	if (IFileManager::Get().DirectoryExists(*SubmissionRootDir) || IFileManager::Get().MakeDirectory(*SubmissionRootDir))
	{
		UE_LOG(LogVirtualization, Display, TEXT("[%s] Setting '%s' as the working directory"), *GetDebugName(), *SubmissionRootDir);
		return true;
	}
	else
	{
		TStringBuilder<MAX_SPRINTF> SystemErrorMsg;
		Utils::GetFormattedSystemError(SystemErrorMsg);

		UE_LOG(LogVirtualization, Error, TEXT("[%s] Failed to set the  working directory to '%s' due to %s"), *GetDebugName(), *SubmissionRootDir, SystemErrorMsg.ToString());
		SubmissionRootDir.Empty();

		return false;
	}
}

void FSourceControlBackend::OnConnectionError()
{
	auto Callback = [](float Delta)->bool
	{
		FMessageLog Log("LogVirtualization");
		Log.Notify(LOCTEXT("ConnectionError", "Asset virtualization connect errors were encountered, see the message log for more info"));

		// This tick callback is one shot, so return false to prevent it being invoked again
		return false;
	};

	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(Callback));
}

UE_REGISTER_VIRTUALIZATION_BACKEND_FACTORY(FSourceControlBackend, SourceControl);

} // namespace UE::Virtualization

#undef LOCTEXT_NAMESPACE
