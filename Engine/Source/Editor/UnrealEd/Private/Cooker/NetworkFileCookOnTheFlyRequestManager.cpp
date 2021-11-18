// Copyright Epic Games, Inc. All Rights Reserved.

#include "Cooker/NetworkFileCookOnTheFlyRequestManager.h"
#include "CookOnTheFlyServerInterface.h"
#include "INetworkFileServer.h"
#include "INetworkFileSystemModule.h"
#include "Modules/ModuleManager.h"

class FNetworkFileCookOnTheFlyRequestManager final
	: public UE::Cook::ICookOnTheFlyRequestManager
{
public:
	FNetworkFileCookOnTheFlyRequestManager(UE::Cook::ICookOnTheFlyServer& InCookOnTheFlyServer, FNetworkFileServerOptions FileServerOptions)
		: CookOnTheFlyServer(InCookOnTheFlyServer)
	{
		FileServerOptions.Delegates.NewConnectionDelegate			= FNewConnectionDelegate::CreateRaw(this, &FNetworkFileCookOnTheFlyRequestManager::OnNewConnection);
		FileServerOptions.Delegates.FileRequestDelegate				= FFileRequestDelegate::CreateRaw(this, &FNetworkFileCookOnTheFlyRequestManager::OnFileRequest);
		FileServerOptions.Delegates.RecompileShadersDelegate		= FRecompileShadersDelegate::CreateRaw(this, &FNetworkFileCookOnTheFlyRequestManager::OnRecompileShaders);
		FileServerOptions.Delegates.SandboxPathOverrideDelegate		= FSandboxPathDelegate::CreateRaw(this, &FNetworkFileCookOnTheFlyRequestManager::OnGetSandboxPath);
		FileServerOptions.Delegates.OnFileModifiedCallback			= &FileModifiedDelegate;
		FileServerOptions.bRestrictPackageAssetsToSandbox			= true; // prevents sending uncooked packages if the package fails to cook

		NetworkFileServer.Reset(FModuleManager::LoadModuleChecked<INetworkFileSystemModule>("NetworkFileSystem")
			.CreateNetworkFileServer(MoveTemp(FileServerOptions), /* bLoadTargetPlatforms */ true));
	}

	virtual ~FNetworkFileCookOnTheFlyRequestManager()
	{
		Shutdown();
	}

	virtual bool Initialize() override
	{
		return NetworkFileServer.IsValid();
	}

	virtual void Shutdown() override
	{
		if (NetworkFileServer.IsValid())
		{
			NetworkFileServer->Shutdown();
			NetworkFileServer.Reset();
		}
	}

private:
	bool OnNewConnection(const FString& VersionInfo, const FString& PlatformName)
	{
		return CookOnTheFlyServer.AddPlatform(FName(*PlatformName)) != nullptr;
	}

	void OnFileRequest(FString& Filename, const FString& PlatformNameStr, TArray<FString>& UnsolicitedFiles)
	{
		using namespace UE::Cook;

		FName PlatformName(*PlatformNameStr);
		const bool bIsCookable = FPackageName::IsPackageExtension(*FPaths::GetExtension(Filename, true));

		if (bIsCookable)
		{
			FEvent* CookCompletedEvent = FPlatformProcess::GetSynchEventFromPool();
			FCookRequestCompletedCallback CookRequestCompleted = [this, CookCompletedEvent](const UE::Cook::ECookResult)
			{
				CookCompletedEvent->Trigger();
			};

			const bool bEnqueued = CookOnTheFlyServer.EnqueueCookRequest(UE::Cook::FCookPackageRequest
			{ 
				PlatformName,
				Filename,
				MoveTemp(CookRequestCompleted)
			});
			check(bEnqueued);

			CookCompletedEvent->Wait();
			FPlatformProcess::ReturnSynchEventToPool(CookCompletedEvent);
			CookOnTheFlyServer.GetUnsolicitedFiles(PlatformName, Filename, bIsCookable, UnsolicitedFiles);
		}
		else
		{
			CookOnTheFlyServer.GetUnsolicitedFiles(PlatformName, Filename, bIsCookable, UnsolicitedFiles);
		}
	}

	void OnRecompileShaders(const FShaderRecompileData& RecompileData)
	{
		FEvent* RecompileCompletedEvent = FPlatformProcess::GetSynchEventFromPool();
		UE::Cook::FRecompileShaderCompletedCallback RecompileCompleted = [this, RecompileCompletedEvent]()
		{
			RecompileCompletedEvent->Trigger();
		};

		const bool bEnqueued = CookOnTheFlyServer.EnqueueRecompileShaderRequest(UE::Cook::FRecompileShaderRequest
		{
			RecompileData,
			MoveTemp(RecompileCompleted)
		});
		check(bEnqueued);

		RecompileCompletedEvent->Wait();
		FPlatformProcess::ReturnSynchEventToPool(RecompileCompletedEvent);
	}

	FString OnGetSandboxPath()
	{
		return CookOnTheFlyServer.GetSandboxDirectory();
	}

	UE::Cook::ICookOnTheFlyServer& CookOnTheFlyServer;
	TUniquePtr<INetworkFileServer> NetworkFileServer;
	FOnFileModifiedDelegate FileModifiedDelegate;
};

namespace UE { namespace Cook
{

TUniquePtr<ICookOnTheFlyRequestManager> MakeNetworkFileCookOnTheFlyRequestManager(ICookOnTheFlyServer& CookOnTheFlyServer, const FNetworkFileServerOptions& FileServerOptions)
{
	return MakeUnique<FNetworkFileCookOnTheFlyRequestManager>(CookOnTheFlyServer, FileServerOptions);
}

}} // namespace UE::Cook
