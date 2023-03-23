// Copyright Epic Games, Inc. All Rights Reserved.

#include "PrecachePayloadsCommandlet.h"

#include "Async/ParallelFor.h"
#include "CommandletUtils.h"
#include "Tasks/Task.h"
#include "UObject/PackageTrailer.h"
#include "Virtualization/VirtualizationSystem.h"

namespace 
{

class FWorkQueue
{
public:
	using FJob = TArrayView<const FIoHash>;

	FWorkQueue(TArray<FIoHash>&& InWork, int32 JobSize)
		: Work(InWork)
	{
		const int32 NumJobs = FMath::DivideAndRoundUp<int32>(Work.Num(), JobSize);
		Jobs.Reserve(NumJobs);

		for (int32 JobIndex = 0; JobIndex < NumJobs; ++JobIndex)
		{
			const int32 JobStart = JobIndex * JobSize;
			const int32 JobEnd = FMath::Min((JobIndex + 1) * JobSize, Work.Num());

			FJob Job = MakeArrayView(&Work[JobStart], JobEnd - JobStart);
			Jobs.Add(Job);
		}
	}

	FJob GetJob()
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FWorkQueue::GetJob);

		if (!Jobs.IsEmpty())
		{
			return Jobs.Pop(false);
		}
		else
		{
			return FJob();
		}
	}

	bool IsEmpty() const
	{
		return Jobs.IsEmpty();
	}

	int32 Num() const
	{
		return Jobs.Num();
	}

private:
	TArray<FIoHash> Work;
	TArray<FJob> Jobs;
};

/** Utility to turn an array of FIoHash into an array of FPullRequest */
TArray<UE::Virtualization::FPullRequest> ToRequestArray(TConstArrayView<FIoHash> IdentifierArray)
{
	TArray<UE::Virtualization::FPullRequest> Requests;
	Requests.Reserve(IdentifierArray.Num());

	for (const FIoHash& Id : IdentifierArray)
	{
		Requests.Emplace(Id);
	}

	return Requests;
}

} // namespace


UPrecachePayloadsCommandlet::UPrecachePayloadsCommandlet(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

int32 UPrecachePayloadsCommandlet::Main(const FString& Params)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UPrecachePayloadsCommandlet);

	TArray<FString> PackageNames = UE::Virtualization::FindPackages(UE::Virtualization::EFindPackageFlags::ExcludeEngineContent);

	UE_LOG(LogVirtualization, Display, TEXT("Found %d packages"), PackageNames.Num());

	TArray<FIoHash> PayloadIds = UE::Virtualization::FindVirtualizedPayloads(PackageNames);

	if (PayloadIds.IsEmpty())
	{
		UE_LOG(LogVirtualization, Display, TEXT("No virtualized payloads found"));
		return 0;
	}

	UE_LOG(LogVirtualization, Display, TEXT("Found %d virtualized payloads to precache"), PayloadIds.Num());
	UE_LOG(LogVirtualization, Display, TEXT("Precaching payloads..."));

	UE::Virtualization::IVirtualizationSystem& System = UE::Virtualization::IVirtualizationSystem::Get();
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(Precache_ThreadedBatches);

		std::atomic<int32> NumCompletedPayloads = 0;

		const int32 NumPayloads = PayloadIds.Num();
		const int32 BatchSize = 64;

		// This seems to be the sweet spot when it comes to our internal infrastructure, so use that as the default.
		const int32 MaxConcurrentTasks = 16;

		// We always want to leave at least one foreground worker free to avoid saturation. If we issue too many
		// concurrent task then we can potentially cause the DDC/Zen to be unable to run clean up tasks for long 
		// periods of times which can cause quite high memory spikes.
		const int32 ConcurrentTasks = FMath::Min(MaxConcurrentTasks, FTaskGraphInterface::Get().GetNumWorkerThreads() - 1);

		UE_LOG(LogVirtualization, Display, TEXT("Will run up to %d precache tasks concurrently"), ConcurrentTasks);

		FWorkQueue WorkQueue(MoveTemp(PayloadIds), BatchSize);

		UE::Tasks::FTaskEvent Event(UE_SOURCE_LOCATION);

		std::atomic<int32> NumTasks = 0;
		double LogTimer = FPlatformTime::Seconds();

		while (NumTasks != 0 || !WorkQueue.IsEmpty())
		{
			int32 NumTaskAllowed = ConcurrentTasks - NumTasks;
			while (NumTaskAllowed > 0 && !WorkQueue.IsEmpty())
			{
				NumTasks++;

				UE::Tasks::Launch(UE_SOURCE_LOCATION,
					[Job = WorkQueue.GetJob(), &System, &NumCompletedPayloads, &NumTasks, &Event]()
					{
						TArray<UE::Virtualization::FPullRequest> Requests = ToRequestArray(Job);

						if (!System.PullData(Requests))
						{
							for (const UE::Virtualization::FPullRequest& Request : Requests)
							{
								if (!Request.IsSuccess())
								{
									UE_LOG(LogVirtualization, Error, TEXT("%s: Failed to precache payload"), *LexToString(Request.GetIdentifier()));
								}
							}
						}

						NumCompletedPayloads += Requests.Num();
						--NumTasks;

						Event.Trigger();
					});

				--NumTaskAllowed;
			}

			Event.Wait(FTimespan::FromSeconds(30.0));

			if (FPlatformTime::Seconds() - LogTimer >= 30.0)
			{
				const int32 CurrentCompletedPayloads = NumCompletedPayloads;
				const float Progress = ((float)CurrentCompletedPayloads / (float)NumPayloads) * 100.0f;
				UE_LOG(LogVirtualization, Display, TEXT("Cached %d/%d (%.1f%%)"), CurrentCompletedPayloads, NumPayloads, Progress);

				LogTimer = FPlatformTime::Seconds();
			}
		}
	}

	UE_LOG(LogVirtualization, Display, TEXT("Precaching complete!"));

	UE::Virtualization::IVirtualizationSystem::Get().DumpStats();
	
	return  0;
}
