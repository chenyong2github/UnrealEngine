// Copyright Epic Games, Inc. All Rights Reserved.

#include "Serialization/FilePackageStore.h"
#include "IO/PackageStore.h"
#include "Serialization/MappedName.h"
#include "Serialization/AsyncLoading2.h"
#include "Serialization/LargeMemoryReader.h"
#include "Serialization/MemoryReader.h"
#include "IO/IoDispatcher.h"
#include "IO/IoContainerId.h"
#include "Internationalization/Culture.h"
#include "Internationalization/Internationalization.h"
#include "Async/Async.h"
#include "Misc/CommandLine.h"

/*
 * File/container based package store.
 */
class FFilePackageStore final
	: public IPackageStore
{
public:
	FFilePackageStore(FIoDispatcher& InIoDispatcher)
		: IoDispatcher(InIoDispatcher) { }

	virtual ~FFilePackageStore() { }

	virtual void Initialize() override
	{
		// Setup culture
		{
			FInternationalization& Internationalization = FInternationalization::Get();
			FString CurrentCulture = Internationalization.GetCurrentCulture()->GetName();
			FParse::Value(FCommandLine::Get(), TEXT("CULTURE="), CurrentCulture);
			CurrentCultureNames = Internationalization.GetPrioritizedCultureNames(CurrentCulture);
		}

		LoadContainers(IoDispatcher.GetMountedContainers().Array());
		IoDispatcher.OnContainerMounted().AddRaw(this, &FFilePackageStore::OnContainerMounted);
	}

	virtual bool DoesPackageExist(FPackageId PackageId) override
	{
		FScopeLock Lock(&PackageNameMapsCritical);
		return nullptr != StoreEntriesMap.Find(PackageId);
	}

	virtual FPackageStoreEntryHandle GetPackageEntryHandle(FPackageId PackageId, const FName& PackageName) override
	{
		FScopeLock Lock(&PackageNameMapsCritical);
		const uint64 Handle = uint64(StoreEntriesMap.FindRef(PackageId));
		return FPackageStoreEntryHandle::Create(Handle, Handle ? EPackageStoreEntryStatus::Ok : EPackageStoreEntryStatus::Missing);
	}
	
	virtual FPackageStoreEntry GetPackageEntry(FPackageStoreEntryHandle Handle) override
	{
		const FFilePackageStoreEntry* Entry = reinterpret_cast<const FFilePackageStoreEntry*>(Handle.Value());

		return FPackageStoreEntry
		{
			FPackageStoreExportInfo
			{
				Entry->ExportCount,
				Entry->ExportBundleCount
			},
			MakeArrayView(Entry->ImportedPackages.Data(), Entry->ImportedPackages.Num()),
			MakeArrayView(Entry->ShaderMapHashes.Data(), Entry->ShaderMapHashes.Num())
		};
	}

	virtual bool GetPackageRedirectInfo(FPackageId PackageId, FName& OutSourcePackageName, FPackageId& OutRedirectedToPackageId) override
	{
		FScopeLock Lock(&PackageNameMapsCritical);
		TTuple<FName, FPackageId>* FindRedirect = RedirectsPackageMap.Find(PackageId);
		if (FindRedirect)
		{
			OutSourcePackageName = FindRedirect->Get<0>();
			OutRedirectedToPackageId = FindRedirect->Get<1>();
			return true;
		}
		else
		{
			return false;
		}
	}

private:
	void LoadContainers(TArrayView<const FIoContainerId> Containers)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(LoadContainers);

		int32 ContainersToLoad = 0;

		for (const FIoContainerId& ContainerId : Containers)
		{
			if (ContainerId.IsValid())
			{
				++ContainersToLoad;
			}
		}

		if (!ContainersToLoad)
		{
			return;
		}

		TAtomic<int32> Remaining(ContainersToLoad);

		FEvent* Event = FPlatformProcess::GetSynchEventFromPool();
		FIoBatch IoBatch = IoDispatcher.NewBatch();

		for (const FIoContainerId& ContainerId : Containers)
		{
			if (!ContainerId.IsValid())
			{
				continue;
			}

			TUniquePtr<FLoadedContainer>& LoadedContainerPtr = LoadedContainers.FindOrAdd(ContainerId);
			if (!LoadedContainerPtr)
			{
				LoadedContainerPtr.Reset(new FLoadedContainer);
			}
			FLoadedContainer& LoadedContainer = *LoadedContainerPtr;

			UE_LOG(LogStreaming, Log, TEXT("Loading mounted container ID '0x%llX'"), ContainerId.Value());
			LoadedContainer.bValid = true;

			FIoChunkId HeaderChunkId = CreateIoChunkId(ContainerId.Value(), 0, EIoChunkType::ContainerHeader);
			IoBatch.ReadWithCallback(HeaderChunkId, FIoReadOptions(), IoDispatcherPriority_High, [this, &Remaining, Event, &LoadedContainer, ContainerId](TIoStatusOr<FIoBuffer> Result)
			{
				// Execution method Thread will run the async block synchronously when multithreading is NOT supported
				const EAsyncExecution ExecutionMethod = FPlatformProcess::SupportsMultithreading() ? EAsyncExecution::TaskGraph : EAsyncExecution::Thread;

				if (!Result.IsOk())
				{
					if (EIoErrorCode::NotFound == Result.Status().GetErrorCode())
					{
						UE_LOG(LogStreaming, Warning, TEXT("Header for container '0x%llX' not found."), ContainerId.Value());
					}
					else
					{
						UE_LOG(LogStreaming, Fatal, TEXT("Failed reading header for container '0x%llX' (%s)"), ContainerId.Value(), *Result.Status().ToString());
					}

					if (--Remaining == 0)
					{
						Event->Trigger();
					}
					return;
				}

				Async(ExecutionMethod, [this, &Remaining, Event, IoBuffer = Result.ConsumeValueOrDie(), &LoadedContainer]()
				{
					LLM_SCOPE(ELLMTag::AsyncLoading);

					FMemoryReaderView Ar(MakeArrayView(IoBuffer.Data(), IoBuffer.DataSize()));

					FContainerHeader ContainerHeader;
					Ar << ContainerHeader;

					LoadedContainer.PackageCount = ContainerHeader.PackageCount;
					LoadedContainer.StoreEntries = MoveTemp(ContainerHeader.StoreEntries);
					{
						TRACE_CPUPROFILER_EVENT_SCOPE(AddPackages);
						FScopeLock Lock(&PackageNameMapsCritical);

						TArrayView<FFilePackageStoreEntry> StoreEntries(reinterpret_cast<FFilePackageStoreEntry*>(LoadedContainer.StoreEntries.GetData()), LoadedContainer.PackageCount);

						int32 Index = 0;
						StoreEntriesMap.Reserve(StoreEntriesMap.Num() + LoadedContainer.PackageCount);
						for (FFilePackageStoreEntry& StoreEntry : StoreEntries)
						{
							const FPackageId& PackageId = ContainerHeader.PackageIds[Index];

							FFilePackageStoreEntry*& GlobalEntry = StoreEntriesMap.FindOrAdd(PackageId);
							if (!GlobalEntry)
							{
								GlobalEntry = &StoreEntry;
							}
							++Index;
						}

						{
							TRACE_CPUPROFILER_EVENT_SCOPE(LoadPackageStoreLocalization);
							const FSourceToLocalizedPackageIdMap* LocalizedPackages = nullptr;
							for (const FString& CultureName : CurrentCultureNames)
							{
								LocalizedPackages = ContainerHeader.CulturePackageMap.Find(CultureName);
								if (LocalizedPackages)
								{
									break;
								}
							}

							if (LocalizedPackages)
							{
								for (const FContainerHeaderPackageRedirect& Redirect : *LocalizedPackages)
								{
									FNameEntryId NameEntry = ContainerHeader.RedirectsNameMap[Redirect.SourcePackageName.GetIndex()];
									FName SourcePackageName = FName::CreateFromDisplayId(NameEntry, Redirect.SourcePackageName.GetNumber());
									RedirectsPackageMap.Emplace(Redirect.SourcePackageId, MakeTuple(SourcePackageName, Redirect.TargetPackageId));
								}
							}
						}

						{
							TRACE_CPUPROFILER_EVENT_SCOPE(LoadPackageStoreRedirects);
							for (const FContainerHeaderPackageRedirect& Redirect : ContainerHeader.PackageRedirects)
							{
								FNameEntryId NameEntry = ContainerHeader.RedirectsNameMap[Redirect.SourcePackageName.GetIndex()];
								FName SourcePackageName = FName::CreateFromDisplayId(NameEntry, Redirect.SourcePackageName.GetNumber());
								RedirectsPackageMap.Emplace(Redirect.SourcePackageId, MakeTuple(SourcePackageName, Redirect.TargetPackageId));
							}
						}
					}

					if (--Remaining == 0)
					{
						Event->Trigger();
					}
				});
			});
		}

		IoBatch.Issue();
		Event->Wait();
		FPlatformProcess::ReturnSynchEventToPool(Event);
	}

	void OnContainerMounted(const FIoContainerId& ContainerId)
	{
		LLM_SCOPE(ELLMTag::AsyncLoading);
		LoadContainers(MakeArrayView(&ContainerId, 1));
	}

	struct FLoadedContainer
	{
		TUniquePtr<FNameMap> ContainerNameMap;
		TArray<uint8> StoreEntries; //FFilePackageStoreEntry[PackageCount];
		uint32 PackageCount = 0;
		bool bValid = false;
	};

	FIoDispatcher& IoDispatcher;
	TMap<FIoContainerId, TUniquePtr<FLoadedContainer>> LoadedContainers;

	TArray<FString> CurrentCultureNames;

	FCriticalSection PackageNameMapsCritical;

	TMap<FPackageId, FFilePackageStoreEntry*> StoreEntriesMap;
	TMap<FPackageId, TTuple<FName, FPackageId>> RedirectsPackageMap;
	int32 NextCustomPackageIndex = 0;
};

TUniquePtr<IPackageStore> MakeFilePackageStore(FIoDispatcher& IoDispatcher)
{
	return MakeUnique<FFilePackageStore>(IoDispatcher);
}
