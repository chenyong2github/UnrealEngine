// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorkerRequestsRemote.h"

#include "CookPlatformManager.h"
#include "CookTypes.h"
#include "Logging/LogMacros.h"
#include "Misc/AssertionMacros.h" 

namespace UE::Cook
{

FWorkerRequestsRemote::FWorkerRequestsRemote(UCookOnTheFlyServer& InCOTFS)
	: CookWorkerClient(*InCOTFS.CookWorkerClient)
{
}

bool FWorkerRequestsRemote::HasExternalRequests() const
{
	return ExternalRequests.HasRequests();
}

int32 FWorkerRequestsRemote::GetNumExternalRequests() const
{
	return ExternalRequests.GetNumRequests();
}

EExternalRequestType FWorkerRequestsRemote::DequeueNextCluster(TArray<FSchedulerCallback>& OutCallbacks, TArray<FFilePlatformRequest>& OutBuildRequests)
{
	return ExternalRequests.DequeueNextCluster(OutCallbacks, OutBuildRequests);
}

bool FWorkerRequestsRemote::DequeueSchedulerCallbacks(TArray<FSchedulerCallback>& OutCallbacks)
{
	return ExternalRequests.DequeueCallbacks(OutCallbacks);
}

void FWorkerRequestsRemote::DequeueAllExternal(TArray<FSchedulerCallback>& OutCallbacks, TArray<FFilePlatformRequest>& OutCookRequests)
{
	ExternalRequests.DequeueAll(OutCallbacks, OutCookRequests);
}

void FWorkerRequestsRemote::AddDiscoveredPackage(FPackageData& PackageData, FInstigator& Instigator, bool bLoadReady, bool& bOutShouldAddToQueue)
{
	bOutShouldAddToQueue = false;
	// MPCOOKTODO: Not yet implemented
	// Send package information to the Director, and make sure to send the instigator.
	// The director needs to handle GeneratedPackages by forcing them to go to this CookWorker, or if they need to go to another worker, add handling that calls pumpsave on the generator first
}

void FWorkerRequestsRemote::AddStartCookByTheBookRequest(FFilePlatformRequest&& Request)
{
	LogCalledCookByTheBookError(TEXT("AddStartCookByTheBookRequest"));
}

void FWorkerRequestsRemote::InitializeCookOnTheFly()
{
	LogCalledCookOnTheFlyError(TEXT("InitializeCookOnTheFly"));
}

void FWorkerRequestsRemote::AddCookOnTheFlyRequest(FFilePlatformRequest&& Request)
{
	LogCalledCookOnTheFlyError(TEXT("AddCookOnTheFlyRequest"));
}

void FWorkerRequestsRemote::AddCookOnTheFlyCallback(FSchedulerCallback&& Callback)
{
	LogCalledCookOnTheFlyError(TEXT("AddCookOnTheFlyCallback"));
}

void FWorkerRequestsRemote::WaitForCookOnTheFlyEvents(int TimeoutMs)
{
	LogCalledCookOnTheFlyError(TEXT("WaitForCookOnTheFlyEvents"));
}

void FWorkerRequestsRemote::AddEditorActionCallback(FSchedulerCallback&& Callback)
{
	LogCalledEditorActionError(TEXT("AddEditorActionCallback"));
}

void FWorkerRequestsRemote::AddPublicInterfaceRequest(FFilePlatformRequest&& Request, bool bForceFrontOfQueue)
{
	LogCalledPublicInterfaceError(TEXT("AddPublicInterfaceRequest"));
}

void FWorkerRequestsRemote::RemapTargetPlatforms(const TMap<ITargetPlatform*, ITargetPlatform*>& Remap)
{
	ExternalRequests.RemapTargetPlatforms(Remap);
}

void FWorkerRequestsRemote::OnRemoveSessionPlatform(const ITargetPlatform* TargetPlatform)
{
	ExternalRequests.OnRemoveSessionPlatform(TargetPlatform);
}

void FWorkerRequestsRemote::ReportAccessedIniSettings(UCookOnTheFlyServer& COTFS, const FConfigFile& Config)
{
	FIniSettingContainer AccessedIniStrings;
	COTFS.ProcessAccessedIniSettings(&Config, COTFS.AccessedIniStrings);
	// MPCOOKTODO: Not yet implemented
}

void FWorkerRequestsRemote::ReportDemoteToIdle(UE::Cook::FPackageData& PackageData, ESuppressCookReason Reason)
{
	CookWorkerClient.ReportDemoteToIdle(PackageData, Reason);
}

void FWorkerRequestsRemote::ReportPromoteToSaveComplete(UE::Cook::FPackageData& PackageData)
{
	CookWorkerClient.ReportPromoteToSaveComplete(PackageData);
}

void FWorkerRequestsRemote::GetInitializeConfigSettings(UCookOnTheFlyServer& COTFS,
	const FString& OutputDirectoryOverride, UE::Cook::FInitializeConfigSettings& Settings)
{
	Settings = CookWorkerClient.ConsumeInitializeConfigSettings();
}

void FWorkerRequestsRemote::GetBeginCookConfigSettings(UCookOnTheFlyServer& COTFS, FBeginCookContext& BeginContext, UE::Cook::FBeginCookConfigSettings& Settings)
{
	Settings = CookWorkerClient.ConsumeBeginCookConfigSettings();
}

void FWorkerRequestsRemote::GetBeginCookIterativeFlags(UCookOnTheFlyServer& COTFS, FBeginCookContext& BeginContext)
{
	for (FBeginCookContextPlatform& PlatformContext : BeginContext.PlatformContexts)
	{
		const ITargetPlatform* TargetPlatform = PlatformContext.TargetPlatform;
		UE::Cook::FPlatformData* PlatformData = PlatformContext.PlatformData;
		PlatformContext.CurrentCookSettings = COTFS.CalculateCookSettingStrings(); // MPCOOKTODO: Copy from Director
		PlatformContext.bHasMemoryResults = PlatformData->bIsSandboxInitialized;
		PlatformContext.bFullBuild = true; // MPCOOKTODO: Copy from Director
		PlatformContext.bClearMemoryResults = true;
		PlatformContext.bPopulateMemoryResultsFromDiskResults = false;
		PlatformContext.bIterateSharedBuild = false; // MPCOOKTODO: Copy from Director
		PlatformContext.bWorkerOnSharedSandbox = true;
		PlatformData->bFullBuild = PlatformContext.bFullBuild;
	}
}

ECookMode::Type FWorkerRequestsRemote::GetDirectorCookMode(UCookOnTheFlyServer& COTFS)
{
	return CookWorkerClient.GetDirectorCookMode();
}

void FWorkerRequestsRemote::LogCalledCookByTheBookError(const TCHAR* FunctionName) const
{
	check(FunctionName);
	UE_LOG(LogCook, Error, TEXT("Calling %s (a CookByTheBook function) is not allowed in a CookWorker."), FunctionName);
}

void FWorkerRequestsRemote::LogCalledCookOnTheFlyError(const TCHAR* FunctionName) const
{
	check(FunctionName);
	UE_LOG(LogCook, Error, TEXT("Calling %s (a CookOnTheFly function) is not allowed in a CookWorker."), FunctionName);
}

void FWorkerRequestsRemote::LogCalledPublicInterfaceError(const TCHAR* FunctionName) const
{
	check(FunctionName);
	UE_LOG(LogCook, Error, TEXT("Calling %s (a CookOnTheFlyServer public interface function) is not allowed in a CookWorker."), FunctionName);
}

void FWorkerRequestsRemote::LogCalledEditorActionError(const TCHAR* FunctionName) const
{
	check(FunctionName);
	UE_LOG(LogCook, Error, TEXT("Calling %s (an editor-mode-only function) is not allowed in a CookWorker."), FunctionName);
}

void FWorkerRequestsRemote::LogAllRequestedFiles()
{
	ExternalRequests.LogAllRequestedFiles();
}

}