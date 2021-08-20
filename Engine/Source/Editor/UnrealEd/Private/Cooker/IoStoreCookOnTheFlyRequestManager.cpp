// Copyright Epic Games, Inc. All Rights Reserved.

#include "IoStoreCookOnTheFlyRequestManager.h"
#include "CookOnTheFlyServerInterface.h"
#include "CookOnTheFly.h"
#include "Modules/ModuleManager.h"
#include "Serialization/CookOnTheFlyPackageStore.h"
#include "PackageStoreWriter.h"
#include "ShaderCompiler.h"
#include "HAL/PlatformTime.h"
#include "Misc/PackageName.h"
#include "UObject/SavePackage.h"
#include "ProfilingDebugging/CountersTrace.h"

DEFINE_LOG_CATEGORY_STATIC(LogCookOnTheFlyTracker, Log, All);

class FCookOnTheFlyPackageTracker
{
	enum class EPackageStatus
	{
		None,
		Cooking,
		WaitingForImports,
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
			case EPackageStatus::WaitingForImports:
				return TEXT("WaitingForImports");
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
		TSet<FPackageId> WaitingPackages;
		TSet<FPackageId> PendingImports;
		bool bAnyImportFailed = false;
	};

public:

	struct FCompletedPackages
	{
		TArray<int32> CookedEntryIndices;
		TArray<FPackageId> FailedPackages;
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

	EPackageStoreEntryStatus GetStatus(const FName& PackageName)
	{
		FPackage& Package = GetPackage(FPackageId::FromName(PackageName));

		switch(Package.Status)
		{
			case EPackageStatus::None:
				return EPackageStoreEntryStatus::None;
			case EPackageStatus::Cooking:
			case EPackageStatus::WaitingForImports:
				return EPackageStoreEntryStatus::Pending;
			case EPackageStatus::Cooked:
				return EPackageStoreEntryStatus::Ok;
			default:
				return EPackageStoreEntryStatus::Missing;
		}
	}

	bool IsCooked(const FName& PackageName)
	{
		FPackage& Package = GetPackage(FPackageId::FromName(PackageName));
		return Package.Status != EPackageStatus::None;
	}

	void MarkAsRequested(const FName& PackageName)
	{
		FPackage& Package = GetPackage(FPackageId::FromName(PackageName));
		check(Package.Status == EPackageStatus::None);
		Package.Status = EPackageStatus::Cooking;
	}

	FCompletedPackages MarkAsFailed(const FName& PackageName)
	{
		return MarkAsCompleted(PackageName, INDEX_NONE, TArrayView<const FPackageStoreEntryResource>());
	}

	FCompletedPackages MarkAsCompleted(const FName& PackageName, const int32 EntryIndex, TArrayView<const FPackageStoreEntryResource> Entries)
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

		if (Package.Status == EPackageStatus::Cooked)
		{
			const FPackageStoreEntryResource& Entry = Entries[EntryIndex];

			for (FPackageId ImportedPackageId : Entry.ImportedPackageIds)
			{
				FPackage& ImportedPackage = GetPackage(ImportedPackageId);
				bool bWaitForImport = ImportedPackage.Status == EPackageStatus::None || ImportedPackage.Status == EPackageStatus::Cooking;

				if (ImportedPackage.Status == EPackageStatus::WaitingForImports)
				{
					// Check if this import or any of it's waiting imports is waiting for this cooked package
					const bool bAnyWaitingForPackage = IsAnyWaitingForPackage(Package, ImportedPackageId);
					bWaitForImport = !bAnyWaitingForPackage;
				}

				if (bWaitForImport)
				{
					Package.Status = EPackageStatus::WaitingForImports;

					bool bIsAlreadyInSet = false;
					ImportedPackage.WaitingPackages.Add(PackageId, &bIsAlreadyInSet);
					check(!bIsAlreadyInSet);

					Package.PendingImports.Add(ImportedPackageId, &bIsAlreadyInSet);
					check(!bIsAlreadyInSet);
				}
			}
		}

		if (Package.Status == EPackageStatus::Cooked || Package.Status == EPackageStatus::Failed)
		{
			AddCompletedPackage(Package);
			ResolveWaitingPackages(Package);
		}
		else
		{
			AddWaitingPackage(Package);
		}

		FCompletedPackages CompletedPackages;

		if (CookedEntries.Num() > NumCookedPackages || FailedPackages.Num() > NumFailedPackages)
		{
			for (int32 Idx = NumCookedPackages; Idx < CookedEntries.Num(); ++Idx)
			{
				CompletedPackages.CookedEntryIndices.Add(CookedEntries[Idx]);
			}

			for (int32 Idx = NumFailedPackages; Idx < FailedPackages.Num(); ++Idx)
			{
				CompletedPackages.FailedPackages.Add(FPackageId::FromName(FailedPackages[Idx]));
			}
		}

		return MoveTemp(CompletedPackages);
	}

	void AddCompletedPackages(TArrayView<const FPackageStoreEntryResource> Entries)
	{
		Packages.Reserve(Entries.Num());
		CookedEntries.Reserve(Entries.Num());

		for (int32 EntryIndex = 0, Num = Entries.Num(); EntryIndex < Num; ++EntryIndex)
		{
			const FPackageStoreEntryResource& Entry = Entries[EntryIndex];
			const FPackageId PackageId = Entry.GetPackageId();

			FPackage& Package	= GetPackage(PackageId);

			Package.PackageId	= PackageId;
			Package.PackageName = Entry.PackageName;
			Package.Status		= EPackageStatus::Cooked;
			Package.EntryIndex	= EntryIndex;

			CookedEntries.Add(Package.EntryIndex);
		}
	}

	FCompletedPackages Flush()
	{
		UE_LOG(LogCookOnTheFlyTracker, Log, TEXT("Flushing '%d' waiting package(s)"), WaitingPackages.Num());

		FCompletedPackages CompletedPackages;

		for (auto& KeyValue : Packages)
		{
			TUniquePtr<FPackage>& Package = KeyValue.Value;
			
			if (Package->Status == EPackageStatus::WaitingForImports)
			{
				check(Package->EntryIndex != INDEX_NONE);
				
				UE_LOG(LogCookOnTheFlyTracker, Warning, TEXT("Package '%s' is still waiting for:"), *Package->PackageName.ToString());
				for (FPackageId PendingImportId : Package->PendingImports)
				{
					const TUniquePtr<FPackage>* ImportedPackage = Packages.Find(PendingImportId);
					UE_LOG(LogCookOnTheFlyTracker, Warning, TEXT( " ==> '%s'"),
						ImportedPackage
							? *FString::Printf(TEXT("%s, (Status=%s)"), *(*ImportedPackage)->PackageName.ToString(), LexToString((*ImportedPackage)->Status))
							: *FString::Printf(TEXT("<Untracked PackageId='0x%llX'>"), PendingImportId.Value()));
				}
				
				Package->WaitingPackages.Empty();
				Package->PendingImports.Empty();

				CookedEntries.Add(Package->EntryIndex);
				CompletedPackages.CookedEntryIndices.Add(Package->EntryIndex);
			}
		}

		for (auto& KeyValue : Packages)
		{
			TUniquePtr<FPackage>& Package = KeyValue.Value;
			
			if (Package->Status == EPackageStatus::WaitingForImports)
			{
				Package->Status = EPackageStatus::Cooked;
			}
		}

		WaitingPackages.Empty();

		return MoveTemp(CompletedPackages);
	}

private:

	void ResolveWaitingPackages(FPackage& Package)
	{
		check(Package.Status == EPackageStatus::Cooked || Package.Status == EPackageStatus::Failed);

		TSet<FPackageId> ResolvedPackageIds;

		for (FPackageId WaitingPackageId : Package.WaitingPackages)
		{
			FPackage& WaitingPackage = GetPackage(WaitingPackageId);
			check(WaitingPackage.PendingImports.Contains(Package.PackageId));
			check(WaitingPackage.Status == EPackageStatus::WaitingForImports);
			WaitingPackage.PendingImports.Remove(Package.PackageId);
			WaitingPackage.bAnyImportFailed |= Package.Status == EPackageStatus::Failed;

			if (!WaitingPackage.PendingImports.Num())
			{
				WaitingPackage.Status = WaitingPackage.bAnyImportFailed ? EPackageStatus::Failed : Package.Status;
				ResolvedPackageIds.Add(WaitingPackage.PackageId);
				WaitingPackages.Remove(WaitingPackage.PackageName);
				AddCompletedPackage(WaitingPackage);
			}
		}

		Package.WaitingPackages.Empty();

		for (FPackageId ResolvedPackageId : ResolvedPackageIds)
		{
			FPackage& ResolvedPackage = GetPackage(ResolvedPackageId);
			ResolveWaitingPackages(ResolvedPackage);
		}
	}

	bool IsAnyWaitingForPackageRecursive(const FPackage& Package, FPackageId TargetPackageId, TSet<FPackageId>& Visited)
	{
		Visited.Add(TargetPackageId);

		FPackage& TargetPackage = GetPackage(TargetPackageId);
		
		if (TargetPackage.Status == EPackageStatus::WaitingForImports)
		{
			if (Package.WaitingPackages.Contains(TargetPackageId))
			{
				return true;
			}

			for (FPackageId PendingImportId : TargetPackage.PendingImports)
			{
				if (!Visited.Contains(PendingImportId))
				{
					const bool bIsWaitingForPackage = IsAnyWaitingForPackageRecursive(Package, PendingImportId, Visited);
					if (bIsWaitingForPackage)
					{
						return true;
					}
				}
			}
		}
	
		return false;
	}

	bool IsAnyWaitingForPackage(const FPackage& Package, FPackageId TargetPackageId)
	{
		TSet<FPackageId> Visited;
		return IsAnyWaitingForPackageRecursive(Package, TargetPackageId, Visited);
	}

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

		UE_LOG(LogCookOnTheFlyTracker, Log, TEXT("'%s' (0x%llX) [%s], Cooked/Fully/Waiting/Failed='%d/%d/%d/%d'"),
			*Package.PackageName.ToString(), Package.PackageId.Value(), LexToString(Package.Status),
			CookedEntries.Num() + WaitingPackages.Num(), CookedEntries.Num(), WaitingPackages.Num(), FailedPackages.Num());
	}

	void AddWaitingPackage(FPackage& Package)
	{
		check(Package.Status == EPackageStatus::WaitingForImports);

		bool bIsAlreadyInSet = false;
		WaitingPackages.Add(Package.PackageName, &bIsAlreadyInSet);
		check(!bIsAlreadyInSet);

		UE_LOG(LogCookOnTheFlyTracker, Log, TEXT("'%s' (0x%llX) [%s], Cooked/Fully/Waiting/Failed='%d/%d/%d/%d'"),
			*Package.PackageName.ToString(), Package.PackageId.Value(), LexToString(Package.Status),
			CookedEntries.Num() + WaitingPackages.Num(), CookedEntries.Num(), WaitingPackages.Num(), FailedPackages.Num());

		for (const FPackageId ImportedPackageId : Package.PendingImports)
		{
			FPackage& ImportedPackage = GetPackage(ImportedPackageId);

			UE_LOG(LogCookOnTheFlyTracker, Verbose, TEXT(" ==> '%s', Status='%s'"),
				ImportedPackage.PackageName.IsNone()
					? *FString::Printf(TEXT("0x%llX"), ImportedPackageId.Value())
					: *ImportedPackage.PackageName.ToString(),
				LexToString(ImportedPackage.Status));
		}
	}

	TMap<FPackageId, TUniquePtr<FPackage>> Packages;
	TArray<int32> CookedEntries;
	TArray<FName> FailedPackages;
	TSet<FName> WaitingPackages;
};

class FIoStoreCookOnTheFlyRequestManager final
	: public UE::Cook::ICookOnTheFlyRequestManager
{
public:
	FIoStoreCookOnTheFlyRequestManager(UE::Cook::ICookOnTheFlyServer& InCookOnTheFlyServer, UE::Cook::FIoStoreCookOnTheFlyServerOptions InOptions)
		: CookOnTheFlyServer(InCookOnTheFlyServer)
		, Options(MoveTemp(InOptions))
	{
		CookOnTheFlyServer.OnFlush().AddRaw(this, &FIoStoreCookOnTheFlyRequestManager::OnFlush);
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
			if (CookOnTheFlyServer.AddPlatform(Client.PlatformName))
			{
				if (!PlatformContexts.Contains(Client.PlatformName))
				{
					TUniquePtr<FPlatformContext>& Context = PlatformContexts.Add(Client.PlatformName, MakeUnique<FPlatformContext>());
					Context->PlatformName = Client.PlatformName;
					
					IPackageStoreWriter* PackageWriter = CookOnTheFlyServer.GetPackageWriter(Client.PlatformName)->AsPackageStoreWriter();
					check(PackageWriter); // This class should not be used except when COTFS is using an IPackageStoreWriter
					Context->PackageWriter = PackageWriter;
					
					PackageWriter->GetEntries([&Context](TArrayView<const FPackageStoreEntryResource> Entries)
					{
						Context->PackageTracker.AddCompletedPackages(Entries);
					});

					PackageWriter->OnCommit().AddRaw(this, &FIoStoreCookOnTheFlyRequestManager::OnPackageCooked);
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
		using namespace UE::PackageStore::Messaging;

		FPlatformContext& Context = GetContext(Client.PlatformName);
		FScopeLock _(&Context.CriticalSection);
		
		const TArrayView<const int32> CookedEntryIndices = Context.PackageTracker.GetCookedEntryIndices();
		const TArrayView<const FName> FailedPackages = Context.PackageTracker.GetFailedPackages();

		UE::PackageStore::Messaging::FPackageStoreData PackageStoreData;

		PackageStoreData.TotalCookedPackages	= CookedEntryIndices.Num();
		PackageStoreData.TotalFailedPackages	= FailedPackages.Num();
		//PackageStoreData.FailedPackages			= FailedPackages;

		Context.PackageWriter->GetEntries([&CookedEntryIndices, &PackageStoreData](TArrayView<const FPackageStoreEntryResource> Entries)
		{
			for (int32 EntryIndex : CookedEntryIndices)
			{
				check(EntryIndex != INDEX_NONE);
				PackageStoreData.CookedPackages.Add(Entries[EntryIndex]);
			}
		});

		for (const FName& FailedPackageName : FailedPackages)
		{
			PackageStoreData.FailedPackages.Add(FPackageId::FromName(FailedPackageName));
		}

		Response.SetBodyTo(FGetCookedPackagesResponse { MoveTemp(PackageStoreData) });
		Response.SetStatus(ECookOnTheFlyMessageStatus::Ok);

		return true;
	}

	bool HandleCookPackageRequest(UE::Cook::FCookOnTheFlyClient Client, const UE::Cook::FCookOnTheFlyRequest& Request, UE::Cook::FCookOnTheFlyResponse& Response)
	{
		using namespace UE::PackageStore::Messaging;

		TRACE_CPUPROFILER_EVENT_SCOPE(CookOnTheFly::HandleCookPackageRequest);

		const double FlushDurationInSeconds = CookOnTheFlyServer.WaitForPendingFlush();
		UE_CLOG(FlushDurationInSeconds > 0.0, LogCookOnTheFly, Log, TEXT("Waited '%.2llf's for cooker to flush pending package(s), Client='%s (%u)'"),
			FlushDurationInSeconds, *Client.PlatformName.ToString(), Client.ClientId);

		FPlatformContext& Context = GetContext(Client.PlatformName);
		FScopeLock _(&Context.CriticalSection);

		FCookPackageRequest CookRequest = Request.GetBodyAs<FCookPackageRequest>();
		
		UE_LOG(LogCookOnTheFly, Log, TEXT("Received cook request, PackageName='%s'"), *CookRequest.PackageName.ToString());

		const bool bIsCooked = Context.PackageTracker.IsCooked(CookRequest.PackageName);

		if (!bIsCooked)
		{
			Context.PackageTracker.MarkAsRequested(CookRequest.PackageName);

			FString Filename;
			if (FPackageName::TryConvertLongPackageNameToFilename(CookRequest.PackageName.ToString(), Filename))
			{
				const bool bEnqueued = CookOnTheFlyServer.EnqueueCookRequest(UE::Cook::FCookPackageRequest { Client.PlatformName, Filename });
				check(bEnqueued);
			}
			else
			{
				UE_LOG(LogCookOnTheFly, Warning, TEXT("Failed to cook package '%s' (File not found)"), *CookRequest.PackageName.ToString());
				Context.PackageTracker.MarkAsFailed(CookRequest.PackageName);
			}
		}

		Response.SetBodyTo(FCookPackageResponse { Context.PackageTracker.GetStatus(CookRequest.PackageName) });
		Response.SetStatus(UE::Cook::ECookOnTheFlyMessageStatus::Ok);

		return true;
	}

	void OnPackageCooked(const IPackageStoreWriter::FCommitEventArgs& EventArgs)
	{
		using namespace UE::Cook;
		using namespace UE::PackageStore::Messaging;

		TRACE_CPUPROFILER_EVENT_SCOPE(CookOnTheFly::OnPackageCooked);

		FPlatformContext& Context = GetContext(EventArgs.PlatformName);
		FScopeLock _(&Context.CriticalSection);

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

			ConnectionServer->BroadcastMessage(Message, Context.PlatformName);
		}

		FCookOnTheFlyPackageTracker::FCompletedPackages NewCompletedPackages;
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(CookOnTheFly::MarkAsCooked);
			NewCompletedPackages = Context.PackageTracker.MarkAsCompleted(EventArgs.PackageName, EventArgs.EntryIndex, EventArgs.Entries);
		}

		if (NewCompletedPackages.CookedEntryIndices.Num() || NewCompletedPackages.FailedPackages.Num())
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(CookOnTheFly::SendCookedPackagesMessage);

			FPackageStoreData PackageStoreData;
			PackageStoreData.TotalCookedPackages = Context.PackageTracker.GetCookedEntryIndices().Num();
			PackageStoreData.TotalFailedPackages = Context.PackageTracker.GetFailedPackages().Num();
			PackageStoreData.FailedPackages = MoveTemp(NewCompletedPackages.FailedPackages);

			for (int32 EntryIndex : NewCompletedPackages.CookedEntryIndices)
			{
				check(EntryIndex != INDEX_NONE);
				PackageStoreData.CookedPackages.Add(EventArgs.Entries[EntryIndex]);
			}

			UE_LOG(LogCookOnTheFly, Verbose, TEXT("Sending '%s' message, Cooked='%d', Failed='%d', Server total='%d/%d/%d' (Tracked/Cooked/Failed)"),
				LexToString(ECookOnTheFlyMessage::PackagesCooked),
				PackageStoreData.CookedPackages.Num(),
				PackageStoreData.FailedPackages.Num(),
				Context.PackageTracker.TotalTracked(),
				PackageStoreData.TotalCookedPackages,
				PackageStoreData.TotalFailedPackages);

			FCookOnTheFlyMessage Message(ECookOnTheFlyMessage::PackagesCooked);
			Message.SetBodyTo<FPackagesCookedMessage>(FPackagesCookedMessage { MoveTemp(PackageStoreData) });
			ConnectionServer->BroadcastMessage(Message, Context.PlatformName);
		}
	}

	bool HandleRecompileShadersRequest(UE::Cook::FCookOnTheFlyClient Client, const UE::Cook::FCookOnTheFlyRequest& Request, UE::Cook::FCookOnTheFlyResponse& Response)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(CookOnTheFly::HandleRecompileShadersRequest);

		TArray<FString> RecompileModifiedFiles;
		TArray<uint8> MeshMaterialMaps;

		FShaderRecompileData RecompileData;
		RecompileData.PlatformName = Client.PlatformName.ToString();
		RecompileData.ModifiedFiles = &RecompileModifiedFiles;
		RecompileData.MeshMaterialMaps = &MeshMaterialMaps;

		{
			TUniquePtr<FArchive> Ar = Request.ReadBody();
			*Ar << RecompileData.MaterialsToLoad;
			*Ar << RecompileData.ShaderPlatform;
			*Ar << RecompileData.bCompileChangedShaders;
			*Ar << RecompileData.ShadersToRecompile;
		}

		RecompileShaders(RecompileData);

		{
			TUniquePtr<FArchive> Ar = Response.WriteBody();
			*Ar << MeshMaterialMaps;
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
			FName(*RecompileData.PlatformName),
			RecompileData.ShaderPlatform,
			RecompileData.MaterialsToLoad,
			RecompileData.ShadersToRecompile,
			RecompileData.MeshMaterialMaps,
			RecompileData.ModifiedFiles,
			RecompileData.bCompileChangedShaders,
			MoveTemp(RecompileCompleted)
		});
		check(bEnqueued);

		RecompileCompletedEvent->Wait();
		FPlatformProcess::ReturnSynchEventToPool(RecompileCompletedEvent);
	}

	void OnFlush()
	{
		using namespace UE::Cook;
		using namespace UE::PackageStore::Messaging;

		FScopeLock _(&CriticalSection);

		for (auto& KeyValue : PlatformContexts)
		{
			const FName PlatformName = KeyValue.Key;
			FPlatformContext& Context = *KeyValue.Value;
			FScopeLock PlatformLock(&Context.CriticalSection);

			FCookOnTheFlyPackageTracker::FCompletedPackages CompletedPackages = Context.PackageTracker.Flush();

			if (CompletedPackages.CookedEntryIndices.Num() || CompletedPackages.FailedPackages.Num())
			{
				UE_LOG(LogCookOnTheFly, Warning, TEXT("Flushing '%d' pending package entry(s) that should have been completed"),
					CompletedPackages.CookedEntryIndices.Num() || CompletedPackages.FailedPackages.Num());

				FPackageStoreData PackageStoreData;
				PackageStoreData.TotalCookedPackages = Context.PackageTracker.GetCookedEntryIndices().Num();
				PackageStoreData.TotalFailedPackages = Context.PackageTracker.GetFailedPackages().Num();
				PackageStoreData.FailedPackages = MoveTemp(CompletedPackages.FailedPackages);

				Context.PackageWriter->GetEntries([&CompletedPackages, &PackageStoreData](TArrayView<const FPackageStoreEntryResource> Entries)
				{
					for (int32 EntryIndex : CompletedPackages.CookedEntryIndices)
					{
						check(EntryIndex != INDEX_NONE);
						PackageStoreData.CookedPackages.Add(Entries[EntryIndex]);
					}
				});

				UE_LOG(LogCookOnTheFly, Log, TEXT("Sending cooked package(s) message, Cooked = '%d', Failed = '%d', Total cooked = '%d', Total failed = '%d'"),
					PackageStoreData.CookedPackages.Num(), PackageStoreData.FailedPackages.Num(), PackageStoreData.TotalCookedPackages, PackageStoreData.TotalFailedPackages);

				FCookOnTheFlyMessage Message(ECookOnTheFlyMessage::PackagesCooked);
				Message.SetBodyTo<FPackagesCookedMessage>(FPackagesCookedMessage { MoveTemp(PackageStoreData) });
				ConnectionServer->BroadcastMessage(Message, Context.PlatformName);
			}
		}
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
};

namespace UE { namespace Cook
{

TUniquePtr<ICookOnTheFlyRequestManager> MakeIoStoreCookOnTheFlyRequestManager(ICookOnTheFlyServer& CookOnTheFlyServer, FIoStoreCookOnTheFlyServerOptions Options)
{
	return MakeUnique<FIoStoreCookOnTheFlyRequestManager>(CookOnTheFlyServer, Options);
}

}}
