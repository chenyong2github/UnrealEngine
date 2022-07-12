// Copyright Epic Games, Inc. All Rights Reserved.

#include "Engine/SkinnedAsset.h"
#include "SkinnedAssetCompiler.h"

DEFINE_LOG_CATEGORY_STATIC(LogSkinnedAsset, Log, All);

void USkinnedAsset::PostLoad()
{
	Super::PostLoad();

#if WITH_EDITOR
	FSkinnedAssetAsyncBuildScope AsyncBuildScope(this);
#endif

	FSkinnedAssetPostLoadContext Context;
	BeginPostLoadInternal(Context);

#if WITH_EDITOR
	if (FSkinnedAssetCompilingManager::Get().IsAsyncCompilationAllowed(this))
	{
		PrepareForAsyncCompilation();

		FQueuedThreadPool* ThreadPool = FSkinnedAssetCompilingManager::Get().GetThreadPool();
		EQueuedWorkPriority BasePriority = FSkinnedAssetCompilingManager::Get().GetBasePriority(this);

		AsyncTask = MakeUnique<FSkinnedAssetAsyncBuildTask>(this, MoveTemp(Context));
		AsyncTask->StartBackgroundTask(ThreadPool, BasePriority, EQueuedWorkFlags::DoNotRunInsideBusyWait);
		FSkinnedAssetCompilingManager::Get().AddSkinnedAssets({ this });
	}
	else
#endif
	{
		ExecutePostLoadInternal(Context);
		FinishPostLoadInternal(Context);
	}
}

#if WITH_EDITOR
bool USkinnedAsset::IsCompiling() const
{
	return AsyncTask != nullptr || AccessedProperties.load(std::memory_order_relaxed) != 0;
}
#endif // WITH_EDITOR

void USkinnedAsset::AcquireAsyncProperty(uint64 AsyncProperties, ESkinnedAssetAsyncPropertyLockType LockType)
{
#if WITH_EDITOR
	if ((LockType & ESkinnedAssetAsyncPropertyLockType::ReadOnly) == ESkinnedAssetAsyncPropertyLockType::ReadOnly)
	{
		AccessedProperties |= AsyncProperties;
	}

	if ((LockType & ESkinnedAssetAsyncPropertyLockType::WriteOnly) == ESkinnedAssetAsyncPropertyLockType::WriteOnly)
	{
		ModifiedProperties |= AsyncProperties;
	}
#endif
}

void USkinnedAsset::ReleaseAsyncProperty(uint64 AsyncProperties, ESkinnedAssetAsyncPropertyLockType LockType)
{
#if WITH_EDITOR
	if ((LockType & ESkinnedAssetAsyncPropertyLockType::ReadOnly) == ESkinnedAssetAsyncPropertyLockType::ReadOnly)
	{
		AccessedProperties &= ~AsyncProperties;
	}

	if ((LockType & ESkinnedAssetAsyncPropertyLockType::WriteOnly) == ESkinnedAssetAsyncPropertyLockType::WriteOnly)
	{
		ModifiedProperties &= ~AsyncProperties;
	}
#endif
}

void USkinnedAsset::WaitUntilAsyncPropertyReleasedInternal(uint64 AsyncProperties, ESkinnedAssetAsyncPropertyLockType LockType) const
{
#if WITH_EDITOR
	// We need to protect internal skinned asset data from race conditions during async build
	if (IsCompiling())
	{
		if (FSkinnedAssetAsyncBuildScope::ShouldWaitOnLockedProperties(this))
		{
			bool bIsLocked = true;
			// We can remove the lock if we're accessing in read-only and there is no write-lock
			if ((LockType & ESkinnedAssetAsyncPropertyLockType::ReadOnly) == ESkinnedAssetAsyncPropertyLockType::ReadOnly)
			{
				// Maintain the lock if the write-lock bit is non-zero
				bIsLocked &= (ModifiedProperties & AsyncProperties) != 0;
			}

			if (bIsLocked)
			{
				FString PropertyName = GetAsyncPropertyName(AsyncProperties);
				TRACE_CPUPROFILER_EVENT_SCOPE_TEXT(*FString::Printf(TEXT("SkinnedAssetCompilationStall %s"), *PropertyName));

				if (IsInGameThread())
				{
					UE_LOG(
						LogSkinnedAsset,
						Verbose,
						TEXT("Accessing property %s of the SkinnedAsset while it is still being built asynchronously will force it to be compiled before continuing. "
							"For better performance, consider making the caller async aware so it can wait until the static mesh is ready to access this property."
							"To better understand where those calls are coming from, you can use Editor.AsyncAssetDumpStallStacks on the console."),
						*PropertyName
					);

					FSkinnedAssetCompilingManager::Get().FinishCompilation({ const_cast<USkinnedAsset*>(this) });
				}
				else
				{
					// Trying to access a property from another thread that cannot force finish the compilation is invalid
					ensureMsgf(
						false,
						TEXT("Accessing property %s of the SkinnedAsset while it is still being built asynchronously is only supported on the game-thread. "
							"To avoid any race-condition, consider finishing the compilation before pushing tasks to other threads or making higher-level game-thread code async aware so it "
							"schedules the task only when the static mesh's compilation is finished. If this is a blocker, you can disable async static mesh from the editor experimental settings."),
						*PropertyName
					);
				}
			}
		}
		// If we're accessing this property from the async build thread, make sure the property is still protected from access from other threads.
		else
		{
			bool bIsLocked = true;
			if ((LockType & ESkinnedAssetAsyncPropertyLockType::ReadOnly) == ESkinnedAssetAsyncPropertyLockType::ReadOnly)
			{
				bIsLocked &= (AccessedProperties & AsyncProperties) != 0;
			}

			if ((LockType & ESkinnedAssetAsyncPropertyLockType::WriteOnly) == ESkinnedAssetAsyncPropertyLockType::WriteOnly)
			{
				bIsLocked &= (ModifiedProperties & AsyncProperties) != 0;
			}
			ensureMsgf(bIsLocked, TEXT("Property %s has not been locked properly for use by async build"), *GetAsyncPropertyName(AsyncProperties));
		}
	}
#endif
}