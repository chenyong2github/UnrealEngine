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

#if WITH_COTF

DEFINE_LOG_CATEGORY_STATIC(LogCookOnTheFlyTracker, Log, All);

class FCookOnTheFlyPackageTracker
{
	enum class EPackageStatus
	{
		None,
		Cooking,
		Cooked,
		Failed
	};

	inline const TCHAR* LexToString(EPackageStatus Status)
	{
		switch (Status)
		{
			case EPackageStatus::None:
				return TEXT("None");
			case EPackageStatus::Cooking:
				return TEXT("Cooking");
			case EPackageStatus::Cooked:
				return TEXT("Cooked");
			case EPackageStatus::Failed:
				return TEXT("Failed");
			default:
				return TEXT("Unknown");
		};
	};

	struct FPackage
	{
		FPackageId PackageId;
		FName PackageName;
		EPackageStatus Status = EPackageStatus::None;
		int32 EntryIndex = INDEX_NONE;
	};

public:

	struct FCompletedPackages
	{
		TArray<int32> CookedEntryIndices;
		TArray<FPackageId> FailedPackages;
		int32 TotalCooked = 0;
		int32 TotalFailed = 0;
		int32 TotalTracked = 0;
	};

	TArrayView<const int32> GetCookedEntryIndices() const
	{
		return CookedEntries;
	}

	TArrayView<const FName> GetFailedPackages() const
	{
		return FailedPackages; 
	}

	int32 TotalTracked() const
	{
		return Packages.Num();
	}

	EPackageStoreEntryStatus GetStatus(FPackageId PackageId)
	{
		FPackage& Package = GetPackage(PackageId);

		switch(Package.Status)
		{
			case EPackageStatus::None:
				return EPackageStoreEntryStatus::None;
			case EPackageStatus::Cooking:
				return EPackageStoreEntryStatus::Pending;
			case EPackageStatus::Cooked:
				return EPackageStoreEntryStatus::Ok;
			default:
				return EPackageStoreEntryStatus::Missing;
		}
	}

	bool IsCooked(FPackageId PackageId)
	{
		FPackage& Package = GetPackage(PackageId);
		return Package.Status != EPackageStatus::None;
	}

	void MarkAsRequested(FPackageId PackageId)
	{
		FPackage& Package = GetPackage(PackageId);
		check(Package.Status == EPackageStatus::None);
		Package.Status = EPackageStatus::Cooking;
	}

	FCompletedPackages MarkAsFailed(const FName& PackageName)
	{
		FCompletedPackages ResultPackages;
		MarkAsCompleted(PackageName, INDEX_NONE, TArrayView<const FPackageStoreEntryResource>(), ResultPackages);
		return ResultPackages;
	}

	void MarkAsCompleted(const FName& PackageName, const int32 EntryIndex, TArrayView<const FPackageStoreEntryResource> Entries, FCompletedPackages& OutPackages)
	{
		const int32 NumCookedPackages = CookedEntries.Num();
		const int32 NumFailedPackages = FailedPackages.Num();

		const FPackageId PackageId = FPackageId::FromName(PackageName);

		FPackage& Package	= GetPackage(PackageId);
		check(Package.Status == EPackageStatus::None || Package.Status == EPackageStatus::Cooking);

		Package.PackageId	= PackageId;
		Package.PackageName = PackageName;
		Package.Status		= EntryIndex != INDEX_NONE ? EPackageStatus::Cooked : EPackageStatus::Failed;
		Package.EntryIndex	= EntryIndex;

		AddCompletedPackage(Package);
		
		OutPackages.TotalCooked = CookedEntries.Num();
		OutPackages.TotalFailed = FailedPackages.Num();
		OutPackages.TotalTracked = Packages.Num();

		if (CookedEntries.Num() > NumCookedPackages || FailedPackages.Num() > NumFailedPackages)
		{
			for (int32 Idx = NumCookedPackages; Idx < CookedEntries.Num(); ++Idx)
			{
				OutPackages.CookedEntryIndices.Add(CookedEntries[Idx]);
			}

			for (int32 Idx = NumFailedPackages; Idx < FailedPackages.Num(); ++Idx)
			{
				OutPackages.FailedPackages.Add(FPackageId::FromName(FailedPackages[Idx]));
			}
		}
	}

	void AddExistingPackages(TArrayView<const FPackageStoreEntryResource> Entries, TArrayView<const IPackageStoreWriter::FOplogCookInfo> CookInfos)
	{
		Packages.Reserve(Entries.Num());
		CookedEntries.Reserve(Entries.Num());

		for (int32 EntryIndex = 0, Num = Entries.Num(); EntryIndex < Num; ++EntryIndex)
		{
			const FPackageStoreEntryResource& Entry = Entries[EntryIndex];
			const IPackageStoreWriter::FOplogCookInfo& CookInfo = CookInfos[EntryIndex];
			const FPackageId PackageId = Entry.GetPackageId();

			FPackage& Package	= GetPackage(PackageId);
			Package.PackageId	= PackageId;
			Package.PackageName = Entry.PackageName;
			Package.EntryIndex	= EntryIndex;

			if (CookInfo.bUpToDate)
			{
				CookedEntries.Add(Package.EntryIndex);
				Package.Status = EPackageStatus::Cooked;
			}
			else
			{
				Package.Status = EPackageStatus::None;
			}
		}
	}

private:

	FPackage& GetPackage(FPackageId PackageId)
	{
		TUniquePtr<FPackage>& Package = Packages.FindOrAdd(PackageId, MakeUnique<FPackage>());
		check(Package.IsValid());
		return *Package;
	}
	
	void AddCompletedPackage(FPackage& Package)
	{
		check(Package.Status == EPackageStatus::Cooked || Package.Status == EPackageStatus::Failed);

		if (Package.Status == EPackageStatus::Cooked)
		{
			CookedEntries.Add(Package.EntryIndex);
		}
		else
		{
			FailedPackages.Add(Package.PackageName);
		}

		UE_LOG(LogCookOnTheFlyTracker, Log, TEXT("'%s' (0x%llX) [%s], Cooked/Failed='%d/%d'"),
			*Package.PackageName.ToString(), Package.PackageId.Value(), LexToString(Package.Status),
			CookedEntries.Num(), FailedPackages.Num());
	}

	TMap<FPackageId, TUniquePtr<FPackage>> Packages;
	TArray<int32> CookedEntries;
	TArray<FName> FailedPackages;
};

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
	struct FPlatformContext
	{
		FCriticalSection CriticalSection;
		FName PlatformName;
		FCookOnTheFlyPackageTracker PackageTracker;
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

private:
	bool HandleClientConnection(UE::Cook::FCookOnTheFlyClient Client, UE::Cook::ECookOnTheFlyConnectionStatus ConnectionStatus)
	{
		FScopeLock _(&CriticalSection);

		if (ConnectionStatus == UE::Cook::ECookOnTheFlyConnectionStatus::Connected)
		{
			const ITargetPlatform* TargetPlatform = CookOnTheFlyServer.AddPlatform(Client.PlatformName);
			if (TargetPlatform)
			{
				if (!PlatformContexts.Contains(Client.PlatformName))
				{
					TUniquePtr<FPlatformContext>& Context = PlatformContexts.Add(Client.PlatformName, MakeUnique<FPlatformContext>());
					Context->PlatformName = Client.PlatformName;
					IPackageStoreWriter* PackageWriter = CookOnTheFlyServer.GetPackageWriter(TargetPlatform).AsPackageStoreWriter();
					check(PackageWriter); // This class should not be used except when COTFS is using an IPackageStoreWriter
					Context->PackageWriter = PackageWriter;
					
					PackageWriter->GetEntries([&Context](TArrayView<const FPackageStoreEntryResource> Entries,
						TArrayView<const IPackageStoreWriter::FOplogCookInfo> CookInfos)
					{
						Context->PackageTracker.AddExistingPackages(Entries, CookInfos);
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

		FPlatformContext& Context = GetContext(Client.PlatformName);
		
		UE::ZenCookOnTheFly::Messaging::FPackageStoreData PackageStoreData;
		
		Context.PackageWriter->GetEntries(
			[&Context, &PackageStoreData](TArrayView<const FPackageStoreEntryResource> Entries,
				TArrayView<const IPackageStoreWriter::FOplogCookInfo> CookInfos)
		{
			FScopeLock _(&Context.CriticalSection);
			
			const TArrayView<const int32> CookedEntryIndices = Context.PackageTracker.GetCookedEntryIndices();
			const TArrayView<const FName> FailedPackages = Context.PackageTracker.GetFailedPackages();
			
			for (int32 EntryIndex : CookedEntryIndices)
			{
				check(EntryIndex != INDEX_NONE);
				PackageStoreData.CookedPackages.Add(Entries[EntryIndex]);
			}
			
			for (const FName& FailedPackageName : FailedPackages)
			{
				PackageStoreData.FailedPackages.Add(FPackageId::FromName(FailedPackageName));
			}
			
			PackageStoreData.TotalCookedPackages	= CookedEntryIndices.Num();
			PackageStoreData.TotalFailedPackages	= FailedPackages.Num();
		});
		
		Response.SetBodyTo(FGetCookedPackagesResponse { MoveTemp(PackageStoreData) });
		Response.SetStatus(ECookOnTheFlyMessageStatus::Ok);

		return true;
	}

	bool HandleCookPackageRequest(UE::Cook::FCookOnTheFlyClient Client, const UE::Cook::FCookOnTheFlyRequest& Request, UE::Cook::FCookOnTheFlyResponse& Response)
	{
		using namespace UE::ZenCookOnTheFly::Messaging;

		TRACE_CPUPROFILER_EVENT_SCOPE(CookOnTheFly::HandleCookPackageRequest);

		const double FlushDurationInSeconds = CookOnTheFlyServer.WaitForPendingFlush();
		UE_CLOG(FlushDurationInSeconds > 0.0, LogCookOnTheFly, Log, TEXT("Waited '%.2llf's for cooker to flush pending package(s), Client='%s (%u)'"),
			FlushDurationInSeconds, *Client.PlatformName.ToString(), Client.ClientId);

		FPlatformContext& Context = GetContext(Client.PlatformName);
		FScopeLock _(&Context.CriticalSection);

		FCookPackageRequest CookRequest = Request.GetBodyAs<FCookPackageRequest>();
		
		const bool bIsCooked = Context.PackageTracker.IsCooked(CookRequest.PackageId);
		if (!bIsCooked)
		{
			FName PackageName = AllKnownPackagesMap.FindRef(CookRequest.PackageId);
			if (PackageName.IsNone())
			{
				UE_LOG(LogCookOnTheFly, Log, TEXT("Received cook request for unknown package 0x%llX"), CookRequest.PackageId.ValueForDebugging());
				Response.SetBodyTo(FCookPackageResponse { EPackageStoreEntryStatus::Missing });
			}
			else
			{
				UE_LOG(LogCookOnTheFly, Log, TEXT("Received cook request, PackageName='%s'"), *PackageName.ToString());
			
				Context.PackageTracker.MarkAsRequested(CookRequest.PackageId);

				FString Filename;
				if (FPackageName::TryConvertLongPackageNameToFilename(PackageName.ToString(), Filename))
				{
					const bool bEnqueued = CookOnTheFlyServer.EnqueueCookRequest(UE::Cook::FCookPackageRequest{ Client.PlatformName, Filename });
					check(bEnqueued);
				}
				else
				{
					UE_LOG(LogCookOnTheFly, Warning, TEXT("Failed to cook package '%s' (File not found)"), *PackageName.ToString());
					Context.PackageTracker.MarkAsFailed(PackageName);
				}
			}

			Response.SetBodyTo(FCookPackageResponse{ Context.PackageTracker.GetStatus(CookRequest.PackageId) });
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

		FCookOnTheFlyPackageTracker::FCompletedPackages NewCompletedPackages;
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(CookOnTheFly::MarkAsCooked);

			FPlatformContext& Context = GetContext(EventArgs.PlatformName);
			FScopeLock _(&Context.CriticalSection);

			Context.PackageTracker.MarkAsCompleted(EventArgs.PackageName, EventArgs.EntryIndex, EventArgs.Entries, NewCompletedPackages);
		}
		BroadcastCompletedPackages(EventArgs.PlatformName, MoveTemp(NewCompletedPackages), EventArgs.Entries);
	}

	void OnPackagesMarkedUpToDate(const IPackageStoreWriter::FMarkUpToDateEventArgs& EventArgs)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(CookOnTheFly::OnPackagesMarkedUpToDate);

		FCookOnTheFlyPackageTracker::FCompletedPackages NewCompletedPackages;
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(CookOnTheFly::MarkAsCooked);

			FPlatformContext& Context = GetContext(EventArgs.PlatformName);
			FScopeLock _(&Context.CriticalSection);

			for (int32 EntryIndex : EventArgs.PackageIndexes)
			{
				FName PackageName = EventArgs.Entries[EntryIndex].PackageName;
				Context.PackageTracker.MarkAsCompleted(PackageName, EntryIndex, EventArgs.Entries, NewCompletedPackages);
			}
		}
		BroadcastCompletedPackages(EventArgs.PlatformName, MoveTemp(NewCompletedPackages), EventArgs.Entries);
	}

	void BroadcastCompletedPackages(FName PlatformName,
		FCookOnTheFlyPackageTracker::FCompletedPackages&& NewCompletedPackages, TArrayView<const FPackageStoreEntryResource> Entries)
	{
		using namespace UE::Cook;
		using namespace UE::ZenCookOnTheFly::Messaging;
		if (NewCompletedPackages.CookedEntryIndices.Num() || NewCompletedPackages.FailedPackages.Num())
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(CookOnTheFly::SendCookedPackagesMessage);

			FPackageStoreData PackageStoreData;
			PackageStoreData.TotalCookedPackages = NewCompletedPackages.TotalCooked;
			PackageStoreData.TotalFailedPackages = NewCompletedPackages.TotalFailed;
			PackageStoreData.FailedPackages = MoveTemp(NewCompletedPackages.FailedPackages);

			for (int32 EntryIndex : NewCompletedPackages.CookedEntryIndices)
			{
				check(EntryIndex != INDEX_NONE);
				PackageStoreData.CookedPackages.Add(Entries[EntryIndex]);
			}

			UE_LOG(LogCookOnTheFly, Verbose, TEXT("Sending '%s' message, Cooked='%d', Failed='%d', Server total='%d/%d/%d' (Tracked/Cooked/Failed)"),
				LexToString(ECookOnTheFlyMessage::PackagesCooked),
				PackageStoreData.CookedPackages.Num(),
				PackageStoreData.FailedPackages.Num(),
				NewCompletedPackages.TotalTracked,
				PackageStoreData.TotalCookedPackages,
				PackageStoreData.TotalFailedPackages);

			FCookOnTheFlyMessage Message(ECookOnTheFlyMessage::PackagesCooked);
			Message.SetBodyTo<FPackagesCookedMessage>(FPackagesCookedMessage { MoveTemp(PackageStoreData) });
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
		FScopeLock _(&CriticalSection);
		TUniquePtr<FPlatformContext>& Ctx = PlatformContexts.FindChecked(PlatformName);
		check(Ctx.IsValid());
		return *Ctx;
	}

	UE::Cook::ICookOnTheFlyServer& CookOnTheFlyServer;
	UE::Cook::FIoStoreCookOnTheFlyServerOptions Options;
	TUniquePtr<UE::Cook::ICookOnTheFlyConnectionServer> ConnectionServer;
	FCriticalSection CriticalSection;
	TMap<FName, TUniquePtr<FPlatformContext>> PlatformContexts;
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