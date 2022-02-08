// Copyright Epic Games, Inc. All Rights Reserved.

#include "IoStoreCookOnTheFlyRequestManager.h"
#include "CookOnTheFlyServerInterface.h"
#include "CookOnTheFly.h"
#include "CookOnTheFlyMessages.h"
#include "Modules/ModuleManager.h"
#include "PackageStoreWriter.h"
#include "ShaderCompiler.h"
#include "HAL/PlatformTime.h"
#include "Misc/PackageName.h"
#include "UObject/SavePackage.h"
#include "ProfilingDebugging/CountersTrace.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Templates/Function.h"

#if WITH_COTF

DEFINE_LOG_CATEGORY_STATIC(LogCookOnTheFlyTracker, Log, All);

class FIoStoreCookOnTheFlyRequestManager final
	: public UE::Cook::ICookOnTheFlyRequestManager
{
public:
	FIoStoreCookOnTheFlyRequestManager(UE::Cook::ICookOnTheFlyServer& InCookOnTheFlyServer, const IAssetRegistry* AssetRegistry, UE::Cook::FIoStoreCookOnTheFlyServerOptions InOptions)
		: CookOnTheFlyServer(InCookOnTheFlyServer)
		, Options(MoveTemp(InOptions))
	{
		TArray<FAssetData> AllAssets;
		AssetRegistry->GetAllAssets(AllAssets, true);
		for (const FAssetData& AssetData : AllAssets)
		{
			AllKnownPackagesMap.Add(FPackageId::FromName(AssetData.PackageName), AssetData.PackageName);
		}
	}

private:
	class FPlatformContext
	{
	public:
		enum class EPackageStatus
		{
			None,
			Cooking,
			Cooked,
			Failed,
		};

		struct FPackage
		{
			EPackageStatus Status = EPackageStatus::None;
			FPackageStoreEntryResource Entry;
		};

		FPlatformContext(FName InPlatformName, IPackageStoreWriter* InPackageWriter)
			: PlatformName(InPlatformName)
			, PackageWriter(InPackageWriter)
		{
		}

		FCriticalSection& GetLock()
		{
			return CriticalSection;
		}

		void GetCompletedPackages(UE::ZenCookOnTheFly::Messaging::FCompletedPackages& OutCompletedPackages)
		{
			OutCompletedPackages.CookedPackages.Reserve(OutCompletedPackages.CookedPackages.Num() + Packages.Num());
			for (const auto& KV : Packages)
			{
				if (KV.Value->Status == EPackageStatus::Cooked)
				{
					OutCompletedPackages.CookedPackages.Add(KV.Value->Entry);
				}
				else if (KV.Value->Status == EPackageStatus::Failed)
				{
					OutCompletedPackages.FailedPackages.Add(KV.Key);
				}
			}
		}

		EPackageStoreEntryStatus RequestCook(UE::Cook::ICookOnTheFlyServer& InCookOnTheFlyServer, const FPackageId& PackageId, TFunctionRef<FName()> GetPackageName, FPackageStoreEntryResource& OutEntry)
		{
			FPackage& Package = GetPackage(PackageId);
			if (Package.Status == EPackageStatus::Cooked)
			{
				UE_LOG(LogCookOnTheFly, Verbose, TEXT("0x%llX was already cooked"), PackageId.ValueForDebugging());
				OutEntry = Package.Entry;
				return EPackageStoreEntryStatus::Ok;
			}
			else if (Package.Status == EPackageStatus::Failed)
			{
				UE_LOG(LogCookOnTheFly, Verbose, TEXT("0x%llX was already failed"), PackageId.ValueForDebugging());
				return EPackageStoreEntryStatus::Missing;
			}
			else if (Package.Status == EPackageStatus::Cooking)
			{
				UE_LOG(LogCookOnTheFly, Verbose, TEXT("0x%llX was already cooking"), PackageId.ValueForDebugging());
				return EPackageStoreEntryStatus::Pending;
			}
			FName PackageName = GetPackageName();
			if (PackageName.IsNone())
			{
				UE_LOG(LogCookOnTheFly, Warning, TEXT("Received cook request for unknown package 0x%llX"), PackageId.ValueForDebugging());
				return EPackageStoreEntryStatus::Missing;
			}
			FString Filename;
			if (FPackageName::TryConvertLongPackageNameToFilename(PackageName.ToString(), Filename))
			{
				UE_LOG(LogCookOnTheFly, Verbose, TEXT("Cooking package 0x%llX '%s'"), PackageId.ValueForDebugging(), *PackageName.ToString());
				Package.Status = EPackageStatus::Cooking;
				const bool bEnqueued = InCookOnTheFlyServer.EnqueueCookRequest(UE::Cook::FCookPackageRequest{ PlatformName, Filename });
				check(bEnqueued);
				return EPackageStoreEntryStatus::Pending;
			}
			else
			{
				UE_LOG(LogCookOnTheFly, Warning, TEXT("Failed to cook package 0x%llX '%s' (File not found)"), PackageId.ValueForDebugging(), *PackageName.ToString());
				Package.Status = EPackageStatus::Failed;
				return EPackageStoreEntryStatus::Missing;
			}
		}

		void MarkAsFailed(FPackageId PackageId, UE::ZenCookOnTheFly::Messaging::FCompletedPackages& OutCompletedPackages)
		{
			UE_LOG(LogCookOnTheFly, Warning, TEXT("0x%llX failed"), PackageId.ValueForDebugging());
			FPackage& Package = GetPackage(PackageId);
			Package.Status = EPackageStatus::Failed;
			OutCompletedPackages.FailedPackages.Add(PackageId);
		}

		void MarkAsCooked(FPackageId PackageId, const FPackageStoreEntryResource& Entry, UE::ZenCookOnTheFly::Messaging::FCompletedPackages& OutCompletedPackages)
		{
			UE_LOG(LogCookOnTheFly, Verbose, TEXT("0x%llX cooked"), PackageId.ValueForDebugging());
			FPackage& Package = GetPackage(PackageId);
			Package.Status = EPackageStatus::Cooked;
			Package.Entry = Entry;
			OutCompletedPackages.CookedPackages.Add(Entry);
		}

		void AddExistingPackages(TArrayView<const FPackageStoreEntryResource> Entries, TArrayView<const IPackageStoreWriter::FOplogCookInfo> CookInfos)
		{
			Packages.Reserve(Entries.Num());

			for (int32 EntryIndex = 0, Num = Entries.Num(); EntryIndex < Num; ++EntryIndex)
			{
				const FPackageStoreEntryResource& Entry = Entries[EntryIndex];
				const IPackageStoreWriter::FOplogCookInfo& CookInfo = CookInfos[EntryIndex];
				const FPackageId PackageId = Entry.GetPackageId();

				FPackage& Package = GetPackage(PackageId);

				if (CookInfo.bUpToDate)
				{
					Package.Status = EPackageStatus::Cooked;
					Package.Entry = Entry;
				}
				else
				{
					Package.Status = EPackageStatus::None;
				}
			}
		}

		FPackage& GetPackage(FPackageId PackageId)
		{
			TUniquePtr<FPackage>& Package = Packages.FindOrAdd(PackageId, MakeUnique<FPackage>());
			check(Package.IsValid());
			return *Package;
		}

	private:
		FCriticalSection CriticalSection;
		FName PlatformName;
		TMap<FPackageId, TUniquePtr<FPackage>> Packages;
		IPackageStoreWriter* PackageWriter = nullptr;
	};

	virtual bool Initialize() override
	{
		using namespace UE::Cook;

		ICookOnTheFlyModule& CookOnTheFlyModule = FModuleManager::LoadModuleChecked<ICookOnTheFlyModule>(TEXT("CookOnTheFly"));

		const int32 Port = Options.Port > 0 ? Options.Port : UE::Cook::DefaultCookOnTheFlyServingPort;

		ConnectionServer = CookOnTheFlyModule.CreateConnectionServer(FCookOnTheFlyServerOptions
		{
			Port,
			[this](FCookOnTheFlyClient Client, ECookOnTheFlyConnectionStatus ConnectionStatus)
			{
				return HandleClientConnection(Client, ConnectionStatus);
			},
			[this](FCookOnTheFlyClient Client, const FCookOnTheFlyRequest& Request, FCookOnTheFlyResponse& Response)
			{ 
				return HandleClientRequest(Client, Request, Response);
			}
		});

		const bool bServerStarted = ConnectionServer->StartServer();

		return bServerStarted;
	}

	virtual void Shutdown() override
	{
		ConnectionServer->StopServer();
		ConnectionServer.Reset();
	}

	virtual void OnPackageGenerated(const FName& PackageName)
	{
		FPackageId PackageId = FPackageId::FromName(PackageName);
		UE_LOG(LogCookOnTheFly, Verbose, TEXT("Package 0x%llX '%s' generated"), PackageId.ValueForDebugging(), *PackageName.ToString());

		FScopeLock _(&AllKnownPackagesCriticalSection);
		AllKnownPackagesMap.Add(PackageId, PackageName);
	}

private:
	bool HandleClientConnection(UE::Cook::FCookOnTheFlyClient Client, UE::Cook::ECookOnTheFlyConnectionStatus ConnectionStatus)
	{
		FScopeLock _(&ContextsCriticalSection);

		if (ConnectionStatus == UE::Cook::ECookOnTheFlyConnectionStatus::Connected)
		{
			const ITargetPlatform* TargetPlatform = CookOnTheFlyServer.AddPlatform(Client.PlatformName);
			if (TargetPlatform)
			{
				if (!PlatformContexts.Contains(Client.PlatformName))
				{
					IPackageStoreWriter* PackageWriter = CookOnTheFlyServer.GetPackageWriter(TargetPlatform).AsPackageStoreWriter();
					check(PackageWriter); // This class should not be used except when COTFS is using an IPackageStoreWriter
					TUniquePtr<FPlatformContext>& Context = PlatformContexts.Add(Client.PlatformName, MakeUnique<FPlatformContext>(Client.PlatformName, PackageWriter));
					
					PackageWriter->GetEntries([&Context](TArrayView<const FPackageStoreEntryResource> Entries,
						TArrayView<const IPackageStoreWriter::FOplogCookInfo> CookInfos)
					{
						Context->AddExistingPackages(Entries, CookInfos);
					});

					PackageWriter->OnCommit().AddRaw(this, &FIoStoreCookOnTheFlyRequestManager::OnPackageCooked);
					PackageWriter->OnMarkUpToDate().AddRaw(this, &FIoStoreCookOnTheFlyRequestManager::OnPackagesMarkedUpToDate);
				}

				return true;
			}

			return false;
		}
		else
		{
			CookOnTheFlyServer.RemovePlatform(Client.PlatformName);
			return true;
		}
	}

	bool HandleClientRequest(UE::Cook::FCookOnTheFlyClient Client, const UE::Cook::FCookOnTheFlyRequest& Request, UE::Cook::FCookOnTheFlyResponse& Response)
	{
		bool bRequestOk = false;

		const double StartTime = FPlatformTime::Seconds();

		UE_LOG(LogCookOnTheFly, Verbose, TEXT("New request, Type='%s', Client='%s (%d)'"), LexToString(Request.GetHeader().MessageType), *Client.PlatformName.ToString(), Client.ClientId);

		switch (Request.GetHeader().MessageType)
		{
			case UE::Cook::ECookOnTheFlyMessage::CookPackage:
				bRequestOk = HandleCookPackageRequest(Client, Request, Response);
				break;
			case UE::Cook::ECookOnTheFlyMessage::GetCookedPackages:
				bRequestOk = HandleGetCookedPackagesRequest(Client, Request, Response);
				break;
			case UE::Cook::ECookOnTheFlyMessage::RecompileShaders:
				bRequestOk = HandleRecompileShadersRequest(Client, Request, Response);
				break;
			default:
				UE_LOG(LogCookOnTheFly, Fatal, TEXT("Unknown request, Type='%s', Client='%s (%d)'"), LexToString(Request.GetHeader().MessageType), *Client.PlatformName.ToString(), Client.ClientId);
				break;
		}

		const double Duration = FPlatformTime::Seconds() - StartTime;

		UE_LOG(LogCookOnTheFly, Verbose, TEXT("Request handled, Type='%s', Client='%s (%u)', Status='%s', Duration='%.6lfs'"),
			LexToString(Request.GetHeader().MessageType),
			*Client.PlatformName.ToString(), Client.ClientId,
			bRequestOk ? TEXT("Ok") : TEXT("Failed"),
			Duration);

		return bRequestOk;
	}

	bool HandleGetCookedPackagesRequest(UE::Cook::FCookOnTheFlyClient Client, const UE::Cook::FCookOnTheFlyRequest& Request, UE::Cook::FCookOnTheFlyResponse& Response)
	{
		using namespace UE::Cook;
		using namespace UE::ZenCookOnTheFly::Messaging;

		FCompletedPackages CompletedPackages;
		{
			FPlatformContext& Context = GetContext(Client.PlatformName);
			FScopeLock _(&Context.GetLock());
			Context.GetCompletedPackages(CompletedPackages);
		}

		Response.SetBodyTo( MoveTemp(CompletedPackages) );
		Response.SetStatus(ECookOnTheFlyMessageStatus::Ok);

		return true;
	}

	bool HandleCookPackageRequest(UE::Cook::FCookOnTheFlyClient Client, const UE::Cook::FCookOnTheFlyRequest& Request, UE::Cook::FCookOnTheFlyResponse& Response)
	{
		using namespace UE::ZenCookOnTheFly::Messaging;

		TRACE_CPUPROFILER_EVENT_SCOPE(CookOnTheFly::HandleCookPackageRequest);

		FCookPackageRequest CookRequest = Request.GetBodyAs<FCookPackageRequest>();
		UE_LOG(LogCookOnTheFly, Verbose, TEXT("Received cook request 0x%llX"), CookRequest.PackageId.ValueForDebugging());
		FPlatformContext& Context = GetContext(Client.PlatformName);
		{
			auto GetPackageNameFunc = [this, &CookRequest]()
			{
				FScopeLock _(&AllKnownPackagesCriticalSection);
				return AllKnownPackagesMap.FindRef(CookRequest.PackageId);
			};

			FScopeLock _(&Context.GetLock());
			FPackageStoreEntryResource Entry;
			EPackageStoreEntryStatus PackageStatus = Context.RequestCook(CookOnTheFlyServer, CookRequest.PackageId, GetPackageNameFunc, Entry);
			Response.SetBodyTo(FCookPackageResponse{ PackageStatus, MoveTemp(Entry) });
		}
		Response.SetStatus(UE::Cook::ECookOnTheFlyMessageStatus::Ok);

		return true;
	}

	void OnPackageCooked(const IPackageStoreWriter::FCommitEventArgs& EventArgs)
	{
		using namespace UE::Cook;
		using namespace UE::ZenCookOnTheFly::Messaging;

		TRACE_CPUPROFILER_EVENT_SCOPE(CookOnTheFly::OnPackageCooked);

		if (EventArgs.AdditionalFiles.Num())
		{
			TArray<FString> Filenames;
			TArray<FIoChunkId> ChunkIds;

			for (const ICookedPackageWriter::FAdditionalFileInfo& FileInfo : EventArgs.AdditionalFiles)
			{
				UE_LOG(LogCookOnTheFly, Verbose, TEXT("Sending additional cooked file '%s'"), *FileInfo.Filename);
				Filenames.Add(FileInfo.Filename);
				ChunkIds.Add(FileInfo.ChunkId);
			}

			FCookOnTheFlyMessage Message(ECookOnTheFlyMessage::FilesAdded);
			{
				TUniquePtr<FArchive> Ar = Message.WriteBody();
				*Ar << Filenames;
				*Ar << ChunkIds;
			}

			ConnectionServer->BroadcastMessage(Message, EventArgs.PlatformName);
		}

		FCompletedPackages NewCompletedPackages;
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(CookOnTheFly::MarkAsCooked);

			FPlatformContext& Context = GetContext(EventArgs.PlatformName);
			FScopeLock _(&Context.GetLock());

			if (EventArgs.EntryIndex >= 0)
			{
				Context.MarkAsCooked(FPackageId::FromName(EventArgs.PackageName), EventArgs.Entries[EventArgs.EntryIndex], NewCompletedPackages);
			}
			else
			{
				Context.MarkAsFailed(FPackageId::FromName(EventArgs.PackageName), NewCompletedPackages);
			}
		}
		BroadcastCompletedPackages(EventArgs.PlatformName, MoveTemp(NewCompletedPackages));
	}

	void OnPackagesMarkedUpToDate(const IPackageStoreWriter::FMarkUpToDateEventArgs& EventArgs)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(CookOnTheFly::OnPackagesMarkedUpToDate);

		UE::ZenCookOnTheFly::Messaging::FCompletedPackages NewCompletedPackages;
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(CookOnTheFly::MarkAsCooked);

			FPlatformContext& Context = GetContext(EventArgs.PlatformName);
			FScopeLock _(&Context.GetLock());

			for (int32 EntryIndex : EventArgs.PackageIndexes)
			{
				FName PackageName = EventArgs.Entries[EntryIndex].PackageName;
				Context.MarkAsCooked(FPackageId::FromName(PackageName), EventArgs.Entries[EntryIndex], NewCompletedPackages);
			}
		}
		BroadcastCompletedPackages(EventArgs.PlatformName, MoveTemp(NewCompletedPackages));
	}

	void BroadcastCompletedPackages(FName PlatformName, UE::ZenCookOnTheFly::Messaging::FCompletedPackages&& NewCompletedPackages)
	{
		using namespace UE::Cook;
		using namespace UE::ZenCookOnTheFly::Messaging;
		if (NewCompletedPackages.CookedPackages.Num() || NewCompletedPackages.FailedPackages.Num())
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(CookOnTheFly::SendCookedPackagesMessage);

			UE_LOG(LogCookOnTheFly, Verbose, TEXT("Sending '%s' message, Cooked='%d', Failed='%d'"),
				LexToString(ECookOnTheFlyMessage::PackagesCooked),
				NewCompletedPackages.CookedPackages.Num(),
				NewCompletedPackages.FailedPackages.Num());

			FCookOnTheFlyMessage Message(ECookOnTheFlyMessage::PackagesCooked);
			Message.SetBodyTo<FCompletedPackages>(MoveTemp(NewCompletedPackages));
			ConnectionServer->BroadcastMessage(Message, PlatformName);
		}
	}

	bool HandleRecompileShadersRequest(UE::Cook::FCookOnTheFlyClient Client, const UE::Cook::FCookOnTheFlyRequest& Request, UE::Cook::FCookOnTheFlyResponse& Response)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(CookOnTheFly::HandleRecompileShadersRequest);

		TArray<FString> RecompileModifiedFiles;
		TArray<uint8> MeshMaterialMaps;
		TArray<uint8> GlobalShaderMap;

		FShaderRecompileData RecompileData(Client.PlatformName.ToString(), &RecompileModifiedFiles, &MeshMaterialMaps, &GlobalShaderMap);
		{
			int32 iShaderPlatform = static_cast<int32>(RecompileData.ShaderPlatform);
			TUniquePtr<FArchive> Ar = Request.ReadBody();
			*Ar << RecompileData.MaterialsToLoad;
			*Ar << iShaderPlatform;
			*Ar << RecompileData.CommandType;
			*Ar << RecompileData.ShadersToRecompile;
		}

		RecompileShaders(RecompileData);

		{
			TUniquePtr<FArchive> Ar = Response.WriteBody();
			*Ar << MeshMaterialMaps;
			*Ar << GlobalShaderMap;
		}

		Response.SetStatus(UE::Cook::ECookOnTheFlyMessageStatus::Ok);

		return true;
	}

	void RecompileShaders(FShaderRecompileData& RecompileData)
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

	FPlatformContext& GetContext(const FName& PlatformName)
	{
		FScopeLock _(&ContextsCriticalSection);
		TUniquePtr<FPlatformContext>& Ctx = PlatformContexts.FindChecked(PlatformName);
		check(Ctx.IsValid());
		return *Ctx;
	}

	UE::Cook::ICookOnTheFlyServer& CookOnTheFlyServer;
	UE::Cook::FIoStoreCookOnTheFlyServerOptions Options;
	TUniquePtr<UE::Cook::ICookOnTheFlyConnectionServer> ConnectionServer;
	FCriticalSection ContextsCriticalSection;
	TMap<FName, TUniquePtr<FPlatformContext>> PlatformContexts;
	FCriticalSection AllKnownPackagesCriticalSection;
	TMap<FPackageId, FName> AllKnownPackagesMap;
};

namespace UE { namespace Cook
{

TUniquePtr<ICookOnTheFlyRequestManager> MakeIoStoreCookOnTheFlyRequestManager(ICookOnTheFlyServer& CookOnTheFlyServer, const IAssetRegistry* AssetRegistry, FIoStoreCookOnTheFlyServerOptions Options)
{
	return MakeUnique<FIoStoreCookOnTheFlyRequestManager>(CookOnTheFlyServer, AssetRegistry, Options);
}

}}

#endif
