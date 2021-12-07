// Copyright Epic Games, Inc. All Rights Reserved.

#include "VirtualizationSourceControlBackend.h"

#include "HAL/FileManager.h"
#include "ISourceControlModule.h"
#include "ISourceControlProvider.h"
#include "Misc/App.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "SourceControlOperations.h"
#include "Virtualization/PayloadId.h"
#include "VirtualizationSourceControlUtilities.h"
#include "VirtualizationUtilities.h"

// When the SourceControl module (or at least the perforce source control module) is thread safe we
// can enable this and stop using the hacky work around 'TryToDownloadFileFromBackgroundThread'
#define IS_SOURCE_CONTROL_THREAD_SAFE 0

namespace UE::Virtualization
{

/** Builds a changelist description to be used when submitting a payload to source control */
void CreateDescription(const FPackagePath& PackageContext, TStringBuilder<512>& OutDescription)
{
	// TODO: Maybe make writing out the project name an option or allow for a codename to be set via ini file?
	OutDescription << TEXT("Submitted for project: ");
	OutDescription << FApp::GetProjectName();

	if (PackageContext.IsMountedPath())
	{
		OutDescription << TEXT("\nPackage: ");
		PackageContext.AppendPackageName(OutDescription);
	}
}

FSourceControlBackend::FSourceControlBackend(FStringView ConfigName, FStringView InDebugName)
	: IVirtualizationBackend(ConfigName, InDebugName, EOperations::Both)
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

	// Optional config values
	FParse::Bool(*ConfigEntry, TEXT("UsePartitionedClient="), bUsePartitionedClient);
	UE_LOG(LogVirtualization, Log, TEXT("[%s] Using partitioned clients: '%s'"), *GetDebugName(), bUsePartitionedClient ? TEXT("true") : TEXT("false"));

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

	// When a source control depot is set up a file named 'payload_metainfo.txt' should be submitted to it's root.
	// This allows us to check for the existence of the file to confirm that the depot root is indeed valid.
	const FString PayloadMetaInfoPath = FString::Printf(TEXT("%spayload_metainfo.txt"), *DepotRoot);

#if IS_SOURCE_CONTROL_THREAD_SAFE
	TSharedRef<FDownloadFile, ESPMode::ThreadSafe> DownloadCommand = ISourceControlOperation::Create<FDownloadFile>();
	if (SCCProvider.Execute(DownloadCommand, PayloadMetaInfoPath, EConcurrency::Synchronous) != ECommandResult::Succeeded)
	{
		UE_LOG(LogVirtualization, Error, TEXT("Failed to find 'payload_metainfo.txt' in the depot '%s', is your config set up correctly?"), *DepotRoot);
		return false;
	}	
#else
	TSharedRef<FDownloadFile, ESPMode::ThreadSafe> DownloadCommand = ISourceControlOperation::Create<FDownloadFile>();
	if (!SCCProvider.TryToDownloadFileFromBackgroundThread(DownloadCommand, PayloadMetaInfoPath))
	{
		UE_LOG(LogVirtualization, Error, TEXT("Failed to find 'payload_metainfo.txt' in the depot '%s', is your config set up correctly?"), *DepotRoot);
		return false;
	}		
#endif //IS_SOURCE_CONTROL_THREAD_SAFE

	FSharedBuffer MetaInfoBuffer = DownloadCommand->GetFileData(PayloadMetaInfoPath);
	if (MetaInfoBuffer.IsNull())
	{
		UE_LOG(LogVirtualization, Error, TEXT("Failed to find 'payload_metainfo.txt' in the depot '%s', is your config set up correctly?"), *DepotRoot);
		return false;
	}

	// Currently we do not do anything with the payload meta info, in the future we could structure
	// it's format to include more information that might be worth logging or something. 
	// But for now being able to pull the payload meta info path at least shows that we can use the
	// depot.

	return true;
}

EPushResult FSourceControlBackend::PushData(const FPayloadId& Id, const FCompressedBuffer& Payload, const FPackagePath& PackageContext)
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
	TStringBuilder<260> RootDirectory;
	RootDirectory << FPlatformProcess::UserTempDir() << TEXT("UnrealEngine/VirtualizedPayloads/") << SessionGuid << TEXT("/");

	// First we need to save the payload to a file in the workspace client mapping so that it can be submitted
	TStringBuilder<52> LocalPayloadPath;
	Utils::PayloadIdToPath(Id, LocalPayloadPath);

	FString PayloadFilePath = *WriteToString<512>(RootDirectory, LocalPayloadPath);

	ON_SCOPE_EXIT
	{
		// Clean up the payload file from disk and the temp directories, but we do not need to give errors if any of these operations fail.
		IFileManager::Get().Delete(*PayloadFilePath, false, false, true);
		IFileManager::Get().DeleteDirectory(RootDirectory.ToString(), false, true);
	};

	// Write the payload to Disk
	{
		UE_LOG(LogVirtualization, Verbose, TEXT("[%s] Writing payload to '%s' for submission"), *GetDebugName(), *PayloadFilePath);

		TUniquePtr<FArchive> FileAr(IFileManager::Get().CreateFileWriter(*PayloadFilePath));
		if (!FileAr)
		{
			TStringBuilder<MAX_SPRINTF> SystemErrorMsg;
			Utils::GetFormattedSystemError(SystemErrorMsg);

			UE_LOG(LogVirtualization, Error, TEXT("[%s] Failed to write payload '%s' contents to '%s' due to system error: %s"),
				*GetDebugName(),
				*Id.ToString(),
				*PayloadFilePath,
				SystemErrorMsg.ToString());

			return EPushResult::Failed;
		}

		Payload.Save(*FileAr);

		if (!FileAr->Close())
		{
			TStringBuilder<MAX_SPRINTF> SystemErrorMsg;
			Utils::GetFormattedSystemError(SystemErrorMsg);

			UE_LOG(LogVirtualization, Error, TEXT("[%s] Failed to write payload '%s' contents to '%s' due to system error: %s"),
				*GetDebugName(),
				*Id.ToString(),
				*PayloadFilePath,
				SystemErrorMsg.
				ToString());

			return EPushResult::Failed;
		}
	}

	TStringBuilder<64> WorkspaceName;
	WorkspaceName << TEXT("MirageSubmission-") << SessionGuid;

	ISourceControlProvider& SCCProvider = ISourceControlModule::Get().GetProvider();

	// Create a temp workspace so that we can submit the payload from
	{
		TSharedRef<FCreateWorkspace> CreateWorkspaceCommand = ISourceControlOperation::Create<FCreateWorkspace>(WorkspaceName, RootDirectory);

		TStringBuilder<512> DepotMapping;
		DepotMapping << DepotRoot << TEXT("...");

		TStringBuilder<128> ClientMapping;
		ClientMapping << TEXT("//") << WorkspaceName << TEXT("/...");

		CreateWorkspaceCommand->AddNativeClientViewMapping(DepotMapping, ClientMapping);

		if (bUsePartitionedClient)
		{
			CreateWorkspaceCommand->SetType(FCreateWorkspace::EType::Partitioned);
		}

		if (SCCProvider.Execute(CreateWorkspaceCommand) != ECommandResult::Succeeded)
		{
			UE_LOG(LogVirtualization, Error, TEXT("[%s] Failed to create temp workspace '%s' to submit payload '%s' from"),
				*GetDebugName(),
				WorkspaceName.ToString(),
				*Id.ToString());

			return EPushResult::Failed;
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

	FSourceControlResultInfo SwitchToNewWorkspaceInfo;
	FString OriginalWorkspace;
	if (SCCProvider.SwitchWorkspace(WorkspaceName, SwitchToNewWorkspaceInfo, &OriginalWorkspace) != ECommandResult::Succeeded)
	{
		UE_LOG(LogVirtualization, Error, TEXT("[%s] Failed to switch to temp workspace '%s' when trying to submit payload '%s'"),
			*GetDebugName(),
			WorkspaceName.ToString(),
			*Id.ToString());

		return EPushResult::Failed;
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

	FSourceControlStatePtr FileState = SCCProvider.GetState(PayloadFilePath, EStateCacheUsage::ForceUpdate);
	if (!FileState.IsValid())
	{
		UE_LOG(LogVirtualization, Error, TEXT("[%s] Failed to find the current file state for '%s'"), *GetDebugName(), *PayloadFilePath);
		return EPushResult::Failed;
	}

	if (FileState->IsSourceControlled())
	{
		// TODO: Maybe check if the data is the same (could be different if the compression algorithm has changed)
		// TODO: Should we respect if the file is deleted as technically we can still get access to it?
		return EPushResult::PayloadAlreadyExisted;
	}
	else if (FileState->CanAdd())
	{
		if (SCCProvider.Execute(ISourceControlOperation::Create<FMarkForAdd>(), PayloadFilePath) != ECommandResult::Succeeded)
		{
			UE_LOG(LogVirtualization, Error, TEXT("[%s] Failed to mark the payload file '%s' for Add in source control"), *GetDebugName(), *PayloadFilePath);
			return EPushResult::Failed;
		}
	}
	else
	{
		UE_LOG(LogVirtualization, Error, TEXT("[%s] The the payload file '%s' is not in source control but also cannot be marked for Add"), *GetDebugName(), *PayloadFilePath);
		return EPushResult::Failed;
	}

	// Now submit the payload
	{
		TSharedRef<FCheckIn, ESPMode::ThreadSafe> CheckInOperation = ISourceControlOperation::Create<FCheckIn>();

		TStringBuilder<512> Description;
		CreateDescription(PackageContext, Description);

		CheckInOperation->SetDescription(FText::FromString(Description.ToString()));

		if (SCCProvider.Execute(CheckInOperation, PayloadFilePath) != ECommandResult::Succeeded)
		{
			UE_LOG(LogVirtualization, Error, TEXT("[%s] Failed to submit the payload file '%s' to source control"), *GetDebugName(), *PayloadFilePath);
			return EPushResult::Failed;
		}
	}

	return EPushResult::Success;
}

FCompressedBuffer FSourceControlBackend::PullData(const FPayloadId& Id)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FSourceControlBackend::PullData);

	TStringBuilder<512> DepotPath;
	CreateDepotPath(Id, DepotPath);

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

bool FSourceControlBackend::DoesPayloadExist(const FPayloadId& Id)
{
	TArray<bool> Result;

	if (FSourceControlBackend::DoPayloadsExist(MakeArrayView<const FPayloadId>(&Id, 1), Result))
	{
		check(Result.Num() == 1);
		return Result[0];
	}
	else
	{
		return false;
	}
}

bool FSourceControlBackend::DoPayloadsExist(TArrayView<const FPayloadId> PayloadIds, TArray<bool>& OutResults)
{
	ISourceControlProvider& SCCProvider = ISourceControlModule::Get().GetProvider();

	TArray<FString> DepotPaths;
	DepotPaths.Reserve(PayloadIds.Num());

	TArray<FSourceControlStateRef> PathStates;

	for (const FPayloadId& PayloadId : PayloadIds)
	{
		if (PayloadId.IsValid())
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
		if (PayloadIds[Index].IsValid())
		{
			OutResults[Index] = PathStates[StatusIndex++]->IsSourceControlled();
		}
	}

	return true;
}

void FSourceControlBackend::CreateDepotPath(const FPayloadId& PayloadId, FStringBuilderBase& OutPath)
{
	TStringBuilder<52> PayloadPath;
	Utils::PayloadIdToPath(PayloadId, PayloadPath);

	OutPath << DepotRoot << PayloadPath;
}

UE_REGISTER_VIRTUALIZATION_BACKEND_FACTORY(FSourceControlBackend, SourceControl);

} // namespace UE::Virtualization
