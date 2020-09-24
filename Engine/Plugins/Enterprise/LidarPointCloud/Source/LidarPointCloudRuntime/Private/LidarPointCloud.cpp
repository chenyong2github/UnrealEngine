// Copyright Epic Games, Inc. All Rights Reserved.

#include "LidarPointCloud.h"
#include "LidarPointCloudShared.h"
#include "LidarPointCloudActor.h"
#include "LidarPointCloudComponent.h"
#include "IO/LidarPointCloudFileIO.h"
#include "Async/Async.h"
#include "Async/Future.h"
#include "Serialization/CustomVersion.h"
#include "Misc/ScopeTryLock.h"
#include "Misc/ConfigCacheIni.h"
#include "Engine/Engine.h"
#include "LatentActions.h"
#include "PhysicsEngine/BodySetup.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Engine/CollisionProfile.h"
#include "EngineUtils.h"
#include "Components/BrushComponent.h"

#if WITH_EDITOR
#include "IContentBrowserSingleton.h"
#include "ContentBrowserModule.h"
#include "AssetRegistryModule.h"
#include "Editor.h"
#include "Styling/SlateStyleRegistry.h"
#include "Misc/MessageDialog.h"
#include "UObject/UObjectGlobals.h"
#endif

#define IS_PROPERTY(Name) PropertyChangedEvent.MemberProperty->GetName().Equals(#Name)

const FGuid ULidarPointCloud::PointCloudFileGUID('P', 'C', 'P', 'F');
const int32 ULidarPointCloud::PointCloudFileVersion(19);
FCustomVersionRegistration PCPFileVersion(ULidarPointCloud::PointCloudFileGUID, ULidarPointCloud::PointCloudFileVersion, TEXT("LiDAR Point Cloud File Version"));

#define LOCTEXT_NAMESPACE "LidarPointCloud"

class FPointCloudLatentAction : public FPendingLatentAction
{
public:
	FName ExecutionFunction;
	int32 OutputLink;
	FWeakObjectPtr CallbackTarget;
	ELidarPointCloudAsyncMode* Mode = nullptr;

	FPointCloudLatentAction(const FLatentActionInfo& LatentInfo, ELidarPointCloudAsyncMode& Mode)
		: ExecutionFunction(LatentInfo.ExecutionFunction)
		, OutputLink(LatentInfo.Linkage)
		, CallbackTarget(LatentInfo.CallbackTarget)
		, Mode(&Mode)
	{
	}

	virtual void UpdateOperation(FLatentResponse& Response) override
	{
		if (*Mode != ELidarPointCloudAsyncMode::Progress)
		{
			Response.FinishAndTriggerIf(true, ExecutionFunction, OutputLink, CallbackTarget);
		}
		else
		{
			Response.TriggerLink(ExecutionFunction, OutputLink, CallbackTarget);
		}
	}
};

/////////////////////////////////////////////////
// FLidarPointCloudNotification

FLidarPointCloudNotification::FLidarPointCloudNotification(UObject* Owner)
	: Owner(Owner)
	, CurrentText("")
	, CurrentProgress(-1)
{
}

void FLidarPointCloudNotification::Create(const FString& Text, FThreadSafeBool* bCancelPtr, const FString& Icon)
{
	SetTextWithProgress(Text, -1);

#if WITH_EDITOR
	if (Owner && !IsValid() && GIsEditor)
	{
		// Build the notification widget
		FNotificationInfo Info(FText::FromString(CurrentText));
		Info.bFireAndForget = false;
		Info.Image = FSlateStyleRegistry::FindSlateStyle("LidarPointCloudStyle")->GetBrush(*Icon);

		if (Owner->HasAnyFlags(RF_Public | RF_Standalone))
		{
			Info.Hyperlink = FSimpleDelegate::CreateLambda([this] {
				// Select the cloud in Content Browser when the hyperlink is clicked
				TArray<FAssetData> AssetData;
				AssetData.Add(FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get().GetAssetByObjectPath(FSoftObjectPath(Owner).GetAssetPathName()));
				FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser").Get().SyncBrowserToAssets(AssetData);
			});
			Info.HyperlinkText = FText::FromString(FPaths::GetBaseFilename(FSoftObjectPath(Owner).ToString()));
		}

		if (bCancelPtr)
		{
			Info.ButtonDetails.Emplace(
				LOCTEXT("OpCancel", "Cancel"),
				LOCTEXT("OpCancelToolTip", "Cancels the point cloud operation in progress."),
				FSimpleDelegate::CreateLambda([bCancelPtr] { *bCancelPtr = true; })
			);
		}

		NotificationItem = FSlateNotificationManager::Get().AddNotification(Info);
		if (IsValid())
		{
			NotificationItem->SetCompletionState(SNotificationItem::CS_Pending);
		}
	}
#endif
}

void FLidarPointCloudNotification::SetText(const FString& Text)
{
	CurrentText = Text;
	UpdateStatus();
}

void FLidarPointCloudNotification::SetProgress(int8 Progress)
{
	CurrentProgress = Progress;
	UpdateStatus();
}

void FLidarPointCloudNotification::SetTextWithProgress(const FString& Text, int8 Progress)
{
	CurrentText = Text;
	CurrentProgress = Progress;
	UpdateStatus();
}

void FLidarPointCloudNotification::Close(bool bSuccess)
{
#if WITH_EDITOR
	if (Owner && IsValid())
	{
		// Do not use fadeout if the engine is shutting down
		if (!GEditor->HasAnyFlags(RF_BeginDestroyed))
		{
			CurrentText.Append(bSuccess ? " Complete" : " Failed");
			CurrentProgress = -1;
			UpdateStatus();
			NotificationItem->SetCompletionState(bSuccess ? SNotificationItem::CS_Success : SNotificationItem::CS_Fail);
			NotificationItem->ExpireAndFadeout();
		}
		NotificationItem.Reset();
	}
#endif
}

void FLidarPointCloudNotification::UpdateStatus()
{
	if (!Owner || !IsValid())
	{
		return;
	}

	if (IsInGameThread())
	{
		// Update Text
		{
			FString Message;

			if (CurrentProgress >= 0)
			{
				Message = FString::Printf(TEXT("%s: %d%%"), *CurrentText, CurrentProgress);
			}
			else
			{
				Message = FString::Printf(TEXT("%s"), *CurrentText);
			}

			NotificationItem->SetText(FText::FromString(Message));
		}
	}
	else
	{
		AsyncTask(ENamedThreads::GameThread, [this] { UpdateStatus(); });
	}
}

/////////////////////////////////////////////////
// ULidarPointCloud

ULidarPointCloud::ULidarPointCloud()
	: MaxCollisionError(100)
	, NormalsQuality(40)
	, NormalsNoiseTolerance(1)
	, Octree(this)
	, OriginalCoordinates(FDoubleVector::ZeroVector)
	, LocationOffset(FDoubleVector::ZeroVector)
	, Notification(this)
	, BodySetup(nullptr)
	, bCollisionBuildInProgress(false)
{
	// Make sure we are transactional to allow undo redo
	this->SetFlags(RF_Transactional);
}

void ULidarPointCloud::Serialize(FArchive& Ar)
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("ULidarPointCloud::Serialize"), STAT_PointCLoud_Serialize, STATGROUP_LoadTime);

	Ar.UsingCustomVersion(PointCloudFileGUID);

	Super::Serialize(Ar);
		
	int32 Version = Ar.CustomVer(PointCloudFileGUID);

	if (Version > 13)
	{
		Ar << BodySetup;

		if (Ar.IsCountingMemory())
		{
			if (BodySetup)
			{
				BodySetup->Serialize(Ar);
			}
		}
	}

	// Make sure to serialize only actual data
	if (Ar.ShouldSkipBulkData() || Ar.IsObjectReferenceCollector() || !Ar.IsPersistent())
	{
		return;
	}
	
	ULidarPointCloudFileIO::SerializeImportSettings(Ar, ImportSettings);

	// Do not save the Octree, if in the middle of processing or the access to the data is blocked
	{
		FScopeTryLock LockProcessing(&ProcessingLock);
		FScopeTryLock LockOctree(&Octree.DataLock);

		bool bValidOctree = LockProcessing.IsLocked() && LockOctree.IsLocked();
		Ar << bValidOctree;
		if (bValidOctree)
		{
			Ar << Octree;
		}
	}
}

void ULidarPointCloud::PostLoad()
{
	Super::PostLoad();

	InitializeCollisionRendering();
}

void ULidarPointCloud::GetAssetRegistryTags(TArray<FAssetRegistryTag>& OutTags) const
{
	OutTags.Add(FAssetRegistryTag("PointCount", PointCloudAssetRegistryCache.PointCount, FAssetRegistryTag::TT_Numerical));
	OutTags.Add(FAssetRegistryTag("ApproxSize", PointCloudAssetRegistryCache.ApproxSize, FAssetRegistryTag::TT_Dimensional));

	Super::GetAssetRegistryTags(OutTags);
}

void ULidarPointCloud::BeginDestroy()
{
	Super::BeginDestroy();

	// Cancel async import and wait for it to exit
	bAsyncCancelled = true;
	FScopeLock LockImport(&ProcessingLock);

	// Hide any notifications, if still present
	Notification.Close(false);

	// Wait for ongoing data access to finish
	FScopeLock LockOctree(&Octree.DataLock);

	// Release any collision rendering data, if present
	ReleaseCollisionRendering();
}

void ULidarPointCloud::PreSave(const class ITargetPlatform* TargetPlatform)
{
	Super::PreSave(TargetPlatform);
	OnPreSaveCleanupEvent.Broadcast();
}

#if WITH_EDITOR
void ULidarPointCloud::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	if (PropertyChangedEvent.MemberProperty)
	{		
		if (IS_PROPERTY(SourcePath))
		{
			SetSourcePath(SourcePath.FilePath);
		}

		if (IS_PROPERTY(MaxCollisionError))
		{
			if (MaxCollisionError < Octree.GetEstimatedPointSpacing())
			{
				FMessageDialog::Open(EAppMsgType::Type::Ok, FText::FromString(FString::Printf(TEXT("Average point spacing is estimated to be around %f cm.\nSetting accuracy close to or lower than that value may result in collision holes."), FMath::RoundToFloat(Octree.GetEstimatedPointSpacing() * 100) * 0.01f)));
			}
		}
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif

int32 ULidarPointCloud::GetDataSize() const
{
	const int64 OctreeSize = Octree.GetAllocatedSize();
	const int64 CollisionSize = Octree.GetCollisionData()->Indices.GetAllocatedSize() + Octree.GetCollisionData()->Vertices.GetAllocatedSize();

	return (OctreeSize + CollisionSize) >> 20;
}

bool ULidarPointCloud::HasCollisionData() const
{
	return Octree.HasCollisionData();
}

TArray<FLidarPointCloudPoint> ULidarPointCloud::GetPointsAsCopies(bool bReturnWorldSpace, int32 StartIndex, int32 Count) const
{
	TArray<FLidarPointCloudPoint> Points;
	GetPointsAsCopies(Points, bReturnWorldSpace, StartIndex, Count);
	return Points;
}

TArray<FLidarPointCloudPoint> ULidarPointCloud::GetPointsInSphereAsCopies(FVector Center, float Radius, bool bVisibleOnly, bool bReturnWorldSpace)
{
	TArray<FLidarPointCloudPoint> Points;
	GetPointsInSphereAsCopies(Points, FSphere(Center, Radius), bVisibleOnly, bReturnWorldSpace);
	return Points;
}

TArray<FLidarPointCloudPoint> ULidarPointCloud::GetPointsInBoxAsCopies(FVector Center, FVector Extent, bool bVisibleOnly, bool bReturnWorldSpace)
{
	TArray<FLidarPointCloudPoint> Points;
	GetPointsInBoxAsCopies(Points, FBox(Center - Extent, Center + Extent), bVisibleOnly, bReturnWorldSpace);
	return Points;
}

bool ULidarPointCloud::LineTraceSingle(FVector Origin, FVector Direction, float Radius, bool bVisibleOnly, FLidarPointCloudPoint& PointHit)
{
	FLidarPointCloudPoint* Point = LineTraceSingle(FLidarPointCloudRay(Origin, Direction), Radius, bVisibleOnly);
	if (Point)
	{
		PointHit = *Point;
		return true;
	}

	return false;
}

void ULidarPointCloud::SetSourcePath(const FString& NewSourcePath)
{
	SourcePath.FilePath = NewSourcePath;

	if (FPaths::FileExists(SourcePath.FilePath))
	{
		if (FPaths::IsRelative(SourcePath.FilePath))
		{
			SourcePath.FilePath = FPaths::ConvertRelativePathToFull(SourcePath.FilePath);
		}

		// Generate new ImportSettings if the source path has changed
		ImportSettings = ULidarPointCloudFileIO::GetImportSettings(SourcePath.FilePath);
	}
	else
	{
		// Invalidate ImportSettings if the source path is invalid too
		ImportSettings = nullptr;
	}
}

void ULidarPointCloud::BuildCollision()
{
	if (bCollisionBuildInProgress)
	{
		PC_ERROR("Another collision operation already in progress.");
		return;
	}

	Notification.Create("Building Collision", nullptr, "LidarPointCloudEditor.BuildCollision");
	
	bCollisionBuildInProgress = true;
	MarkPackageDirty();

	UBodySetup* NewBodySetup = NewObject<UBodySetup>(this);
	NewBodySetup->BodySetupGuid = FGuid::NewGuid();
	NewBodySetup->CollisionTraceFlag = CTF_UseComplexAsSimple;
	NewBodySetup->bHasCookedCollisionData = true;

	Async(EAsyncExecution::Thread, [this, NewBodySetup]{
		Octree.BuildCollision(MaxCollisionError, true);

		FBenchmarkTimer::Reset();
#if WITH_PHYSX  && PHYSICS_INTERFACE_PHYSX
		AsyncTask(ENamedThreads::GameThread, [this, NewBodySetup] {
			NewBodySetup->CreatePhysicsMeshesAsync(FOnAsyncPhysicsCookFinished::CreateUObject(this, &ULidarPointCloud::FinishPhysicsAsyncCook, NewBodySetup));
		});
#elif WITH_CHAOS
		NewBodySetup->CreatePhysicsMeshes();
		AsyncTask(ENamedThreads::GameThread, [this, NewBodySetup] { FinishPhysicsAsyncCook(true, NewBodySetup); });
#endif		
	});
}

void ULidarPointCloud::RemoveCollision()
{
	if (bCollisionBuildInProgress)
	{
		PC_ERROR("Another collision operation already in progress.");
		return;
	}

	bCollisionBuildInProgress = true;
	
	MarkPackageDirty();

	Octree.RemoveCollision();

	BodySetup = NewObject<UBodySetup>(this);
	ReleaseCollisionRendering();
	InitializeCollisionRendering();
	OnPointCloudUpdateCollisionEvent.Broadcast();

	bCollisionBuildInProgress = false;
}

void ULidarPointCloud::SetLocationOffset(FDoubleVector Offset)
{
	LocationOffset = Offset;
	MarkPackageDirty();
	OnPointCloudRebuiltEvent.Broadcast();
}

void ULidarPointCloud::Reimport(const FLidarPointCloudAsyncParameters& AsyncParameters)
{
	if (FPaths::FileExists(SourcePath.FilePath))
	{
		FScopeTryLock Lock(&ProcessingLock);

		if (!Lock.IsLocked())
		{
			PC_ERROR("Cannot reimport the asset - data is currently being used.");
			return;
		}

		bAsyncCancelled = false;
		Notification.Create("Importing Point Cloud", &bAsyncCancelled);

		const bool bCenter = GetDefault<ULidarPointCloudSettings>()->bAutoCenterOnImport;

		// The actual import function to be executed
		auto ImportFunction = [this, AsyncParameters, bCenter]
		{
			// This will take over the lock
			FScopeLock Lock(&ProcessingLock);

			bool bSuccess = false;

			// Wait for rendering to complete before proceeding and lock the access to the data
			FScopeLock DataLock(&Octree.DataLock);

			FLidarPointCloudImportResults ImportResults;

			// If the file supports concurrent insertion, we can stream the data in chunks and perform async insertion at the same time
			if (ULidarPointCloudFileIO::FileSupportsConcurrentInsertion(SourcePath.FilePath))
			{
				PC_LOG("Using Concurrent Insertion");

				ImportResults = FLidarPointCloudImportResults(&bAsyncCancelled,
				[this, AsyncParameters](float Progress)
				{
					Notification.SetProgress(100.0f * Progress);
					if (AsyncParameters.ProgressCallback)
					{
						AsyncParameters.ProgressCallback(100.0f * Progress);
					}
				},
				[this](const FDoubleBox& Bounds, FDoubleVector InOriginalCoordinates)
				{
					Initialize(Bounds.ShiftBy(-InOriginalCoordinates).ToBox());
				},
				[this](TArray64<FLidarPointCloudPoint>* Points)
				{
					Octree.InsertPoints(Points->GetData(), Points->Num(), GetDefault<ULidarPointCloudSettings>()->DuplicateHandling, false, -LocationOffset.ToVector());
				});

				bSuccess = ULidarPointCloudFileIO::Import(SourcePath.FilePath, ImportSettings, ImportResults);
			}
			else
			{
				ImportResults = FLidarPointCloudImportResults(&bAsyncCancelled, [this, AsyncParameters](float Progress)
				{
					Notification.SetProgress(50.0f * Progress);
					if (AsyncParameters.ProgressCallback)
					{
						AsyncParameters.ProgressCallback(50.0f * Progress);
					}
				});

				if (ULidarPointCloudFileIO::Import(SourcePath.FilePath, ImportSettings, ImportResults))
				{
					// Re-initialize the Octree
					Initialize(ImportResults.Bounds);

					FScopeBenchmarkTimer BenchmarkTimer("Octree Build-Up");

					bSuccess = InsertPoints_NoLock(ImportResults.Points.GetData(), ImportResults.Points.Num(), GetDefault<ULidarPointCloudSettings>()->DuplicateHandling, false, -LocationOffset.ToVector(), &bAsyncCancelled, [this, AsyncParameters](float Progress)
					{
						Notification.SetProgress(50.0f + 50.0f * Progress);
						if (AsyncParameters.ProgressCallback)
						{
							AsyncParameters.ProgressCallback(50.0f + 50.0f * Progress);
						}
					});

					if (!bSuccess)
					{
						BenchmarkTimer.bActive = false;
					}
				}
			}

			if (bSuccess)
			{
				ClassificationsImported = ImportResults.ClassificationsImported;

				RefreshBounds();
				OriginalCoordinates = LocationOffset + ImportResults.OriginalCoordinates;

				// Show the cloud at its original location, if selected
				LocationOffset = bCenter ? FDoubleVector::ZeroVector : OriginalCoordinates;
			}
			else
			{
				Octree.Empty(true);

				OriginalCoordinates = FDoubleVector::ZeroVector;
				LocationOffset = FDoubleVector::ZeroVector;

				// Update PointCloudAssetRegistryCache
				PointCloudAssetRegistryCache.PointCount = FString::FromInt(Octree.GetNumPoints());
			}

			// Only process those if not being destroyed
			if (!HasAnyFlags(RF_BeginDestroyed))
			{
				auto PostFunction = [this, bSuccess]() {
					MarkPackageDirty();
					Notification.Close(bSuccess);
					OnPointCloudRebuiltEvent.Broadcast();
				};

				// Make sure the call is executed on the correct thread if using async
				if (IsInGameThread())
				{
					PostFunction();
				}
				else
				{
					AsyncTask(ENamedThreads::GameThread, MoveTemp(PostFunction));
				}
			}	
			
			if (AsyncParameters.CompletionCallback)
			{
				AsyncParameters.CompletionCallback(bSuccess);
			}

			if(!bSuccess)
			{
				PC_ERROR("Point Cloud importing failed or cancelled.");
			}
		};

		if (AsyncParameters.bUseAsync)
		{
			Async(EAsyncExecution::Thread, MoveTemp(ImportFunction));
		}
		else
		{
			ImportFunction();
		}
	}
	else
	{
		PC_ERROR("Reimport failed, provided source path '%s' could not be found.", *SourcePath.FilePath);

		if (AsyncParameters.CompletionCallback)
		{
			AsyncParameters.CompletionCallback(false);
		}
	}
}

void ULidarPointCloud::Reimport(UObject* WorldContextObject, bool bUseAsync, FLatentActionInfo LatentInfo, ELidarPointCloudAsyncMode& AsyncMode, float& Progress)
{
	if (UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull))
	{
		FLatentActionManager& LatentActionManager = World->GetLatentActionManager();
		if (LatentActionManager.FindExistingAction<FPointCloudLatentAction>(LatentInfo.CallbackTarget, LatentInfo.UUID) == nullptr)
		{
			AsyncMode = ELidarPointCloudAsyncMode::Progress;
			FPointCloudLatentAction* CompletionAction = new FPointCloudLatentAction(LatentInfo, AsyncMode);

			LatentActionManager.AddNewAction(LatentInfo.CallbackTarget, LatentInfo.UUID, CompletionAction);

			Reimport(FLidarPointCloudAsyncParameters(bUseAsync,
				[&Progress, &AsyncMode](float InProgress)
				{
					Progress = InProgress;
				},
				[&AsyncMode](bool bSuccess)
				{
					AsyncMode = bSuccess ? ELidarPointCloudAsyncMode::Success : ELidarPointCloudAsyncMode::Failure;
				}));
		}
	}
}

bool ULidarPointCloud::Export(const FString& Filename)
{
	return ULidarPointCloudFileIO::Export(Filename, this);
}

void ULidarPointCloud::InsertPoint(const FLidarPointCloudPoint& Point, ELidarPointCloudDuplicateHandling DuplicateHandling, bool bRefreshPointsBounds, const FVector& Translation)
{
	FScopeLock Lock(&Octree.DataLock);

	Octree.InsertPoint(&Point, DuplicateHandling, bRefreshPointsBounds, Translation);

	// Update PointCloudAssetRegistryCache
	PointCloudAssetRegistryCache.PointCount = FString::FromInt(Octree.GetNumPoints());
}

template<typename T>
bool ULidarPointCloud::InsertPoints_NoLock(T InPoints, const int64& Count, ELidarPointCloudDuplicateHandling DuplicateHandling, bool bRefreshPointsBounds, const FVector& Translation, FThreadSafeBool* bCanceled, TFunction<void(float)> ProgressCallback)
{
	const int32 MaxBatchSize = GetDefault<ULidarPointCloudSettings>()->MultithreadingInsertionBatchSize;
	
	// Minimum amount of points to progress to count as 1%
	int64 RefreshStatusFrequency = Count * 0.01f;
	FThreadSafeCounter64 ProcessedPoints(0);
	int64 TotalProcessedPoints = 0;

	const int32 NumThreads = FMath::Min(FPlatformMisc::NumberOfCoresIncludingHyperthreads() - 1, (int32)(Count / MaxBatchSize) + 1);
	TArray<TFuture<void>>ThreadResults;
	ThreadResults.Reserve(NumThreads);
	const int64 NumPointsPerThread = Count / NumThreads + 1;

	FCriticalSection ProgressCallbackLock;

	// Fire threads
	for (int32 ThreadID = 0; ThreadID < NumThreads; ThreadID++)
	{
		ThreadResults.Add(Async(EAsyncExecution::Thread, [this, ThreadID, DuplicateHandling, bRefreshPointsBounds, MaxBatchSize, NumPointsPerThread, RefreshStatusFrequency, &ProcessedPoints, &TotalProcessedPoints, InPoints, Count, &ProgressCallback, &ProgressCallbackLock, &bCanceled, &Translation]
		{
			int64 Idx = ThreadID * NumPointsPerThread;
			int64 MaxIdx = FMath::Min(Idx + NumPointsPerThread, Count);
			T DataPointer = InPoints + Idx;

			while (Idx < MaxIdx)
			{
				int32 BatchSize = FMath::Min(MaxIdx - Idx, (int64)MaxBatchSize);

				Octree.InsertPoints(DataPointer, BatchSize, DuplicateHandling, bRefreshPointsBounds, Translation);

				if (ProgressCallback)
				{
					ProcessedPoints.Add(BatchSize);
					if (ProcessedPoints.GetValue() > RefreshStatusFrequency)
					{
						FScopeLock Lock(&ProgressCallbackLock);
						TotalProcessedPoints += ProcessedPoints.GetValue();
						ProcessedPoints.Reset();
						ProgressCallback((double)TotalProcessedPoints / Count);
					}
				}

				if (bCanceled && *bCanceled)
				{
					break;
				}

				Idx += BatchSize;
				DataPointer += BatchSize;
			}
		}));
	}

	// Sync
	for (const TFuture<void>& ThreadResult : ThreadResults)
	{
		ThreadResult.Get();
	}

	// Do not attempt to touch Render Data if being destroyed
	if (!HasAnyFlags(RF_BeginDestroyed))
	{
		// Update PointCloudAssetRegistryCache
		PointCloudAssetRegistryCache.PointCount = FString::FromInt(Octree.GetNumPoints());
	}

	return !bCanceled || !(*bCanceled);
}

template<typename T>
bool ULidarPointCloud::SetData(T Points, const int64& Count, TFunction<void(float)> ProgressCallback)
{
	// Lock the point cloud
	FScopeLock Lock(&ProcessingLock);

	// Calculate the bounds
	FBox Bounds = CalculateBoundsFromPoints(Points, Count);

	bool bSuccess = false;

	// Only proceed if the bounds are valid
	if (Bounds.IsValid)
	{
		// Wait for rendering to complete before proceeding and lock the access to the data
		FScopeLock DataLock(&Octree.DataLock);

		// Initialize the Octree
		Initialize(Bounds);

		bSuccess = InsertPoints_NoLock(Points, Count, GetDefault<ULidarPointCloudSettings>()->DuplicateHandling, false, -LocationOffset.ToVector(), nullptr, MoveTemp(ProgressCallback));

		if (!bSuccess)
		{
			Octree.Empty(true);
		}

		// Only process those if not being destroyed
		if (!HasAnyFlags(RF_BeginDestroyed))
		{
			auto PostFunction = [this, bSuccess]() {
				MarkPackageDirty();
				Notification.Close(bSuccess);
				OnPointCloudRebuiltEvent.Broadcast();
			};

			// Make sure the call is executed on the correct thread if using async
			if (IsInGameThread())
			{
				PostFunction();
			}
			else
			{
				AsyncTask(ENamedThreads::GameThread, MoveTemp(PostFunction));
			}
		}
	}

	if (!bSuccess)
	{
		PC_ERROR("Setting Point Cloud data failed.");
	}

	return bSuccess;
}

void ULidarPointCloud::Merge(TArray<ULidarPointCloud*> PointCloudsToMerge, TFunction<void(void)> ProgressCallback)
{
	for (int32 i = 0; i < PointCloudsToMerge.Num(); i++)
	{
		if (!IsValid(PointCloudsToMerge[i]) || PointCloudsToMerge[i] == this)
		{
			PointCloudsToMerge.RemoveAtSwap(i--, 1, false);
		}
	}

	PointCloudsToMerge.Shrink();

	// Abort if no valid assets are found
	if (PointCloudsToMerge.Num() == 0)
	{
		return;
	}

	FScopeBenchmarkTimer Timer("Merge");

	// Lock the point cloud
	FScopeLock Lock(&ProcessingLock);
	FScopeLock DataLock(&Octree.DataLock);

	if (ProgressCallback)
	{
		ProgressCallback();
	}

	// Calculate new, combined bounds
	FDoubleBox NewBounds(EForceInit::ForceInit);
	FDoubleBox NewAbsoluteBounds(EForceInit::ForceInit);

	// Only include this asset if it actually has any data
	if (GetNumPoints() > 0)
	{
		NewBounds += GetPreciseBounds(false);
		NewAbsoluteBounds += GetPreciseBounds(true);
	}

	for (ULidarPointCloud* Asset : PointCloudsToMerge)
	{
		NewBounds += Asset->GetPreciseBounds(false);
		NewAbsoluteBounds += Asset->GetPreciseBounds(true);

		for (uint8& Classification : Asset->ClassificationsImported)
		{
			ClassificationsImported.AddUnique(Classification);
		}
	}

	// Make a copy of current points, as the data will be reinitialized
	TArray<FLidarPointCloudPoint> Points;
	GetPointsAsCopies(Points, false);

	FDoubleVector OldLocationOffset = LocationOffset;

	// Initialize the Octree
	Initialize(NewBounds);

	OriginalCoordinates = NewAbsoluteBounds.GetCenter();

	// Re-insert original points
	InsertPoints(Points, GetDefault<ULidarPointCloudSettings>()->DuplicateHandling, false, (OldLocationOffset - LocationOffset).ToVector());

	Points.Empty();

	TArray<TFuture<void>> ThreadResults;

	const ULidarPointCloudSettings* Settings = GetDefault<ULidarPointCloudSettings>();
	const int32 MaxBatchSize = Settings->MultithreadingInsertionBatchSize;
	const ELidarPointCloudDuplicateHandling DuplicateHandling = Settings->DuplicateHandling;

	// Insert other points
	for (ULidarPointCloud* Asset : PointCloudsToMerge)
	{
		if (ProgressCallback)
		{
			ProgressCallback();
		}

		const FVector Translation = (Asset->LocationOffset - LocationOffset).ToVector();
		Asset->Octree.GetPointsAsCopiesInBatches([this, &ThreadResults, DuplicateHandling, Translation](TSharedPtr<TArray64<FLidarPointCloudPoint>> Points)
		{
			ThreadResults.Add(Async(EAsyncExecution::ThreadPool, [this, Points, DuplicateHandling, Translation]() {
				Octree.InsertPoints(Points->GetData(), Points->Num(), DuplicateHandling, false, Translation);
			}));
		}, MaxBatchSize, false);
	}

	// Sync
	if (ProgressCallback)
	{
		ProgressCallback();
	}

	for (const TFuture<void>& ThreadResult : ThreadResults)
	{
		ThreadResult.Get();
	}

	MarkPackageDirty();
	OnPointCloudRebuiltEvent.Broadcast();
}

void ULidarPointCloud::CalculateNormals(FLatentActionInfo LatentInfo)
{
	if (UWorld* World = GetWorld())
	{
		FLatentActionManager& LatentActionManager = World->GetLatentActionManager();
		if (LatentActionManager.FindExistingAction<FPointCloudLatentAction>(LatentInfo.CallbackTarget, LatentInfo.UUID) == nullptr)
		{
			ELidarPointCloudAsyncMode AsyncMode = ELidarPointCloudAsyncMode::Progress;
			FPointCloudLatentAction* CompletionAction = new FPointCloudLatentAction(LatentInfo, AsyncMode);
			LatentActionManager.AddNewAction(LatentInfo.CallbackTarget, LatentInfo.UUID, CompletionAction);
			CalculateNormals(nullptr, [&AsyncMode] { AsyncMode = ELidarPointCloudAsyncMode::Success; });
		}
	}
}

void ULidarPointCloud::CalculateNormals(TArray64<FLidarPointCloudPoint*>* Points, TFunction<void(void)> CompletionCallback)
{
	FScopeTryLock Lock(&ProcessingLock);

	if (!Lock.IsLocked())
	{
		PC_ERROR("Cannot calculate normals for the asset - data is currently being used.");
		return;
	}

	bAsyncCancelled = false;
	Notification.Create("Calculating Normals", &bAsyncCancelled);
	Async(EAsyncExecution::Thread,
	[this, Points]
	{
		// This will take over the lock
		FScopeLock Lock(&ProcessingLock);

		// Wait for rendering to complete before proceeding and lock the access to the data
		FScopeLock DataLock(&Octree.DataLock);

		Octree.CalculateNormals(&bAsyncCancelled, NormalsQuality, NormalsNoiseTolerance, Points);
	},
	[this, _CompletionCallback = MoveTemp(CompletionCallback)]
	{
		AsyncTask(ENamedThreads::GameThread, [this]
		{
			MarkPackageDirty();
			Notification.Close(!bAsyncCancelled);
		});

		if (_CompletionCallback)
		{
			_CompletionCallback();
		}
	});
}

bool ULidarPointCloud::GetPhysicsTriMeshData(FTriMeshCollisionData* CollisionData, bool InUseAllTriData)
{
	CollisionData->Vertices = Octree.GetCollisionData()->Vertices;
	CollisionData->Indices = Octree.GetCollisionData()->Indices;

	return true;
}

UBodySetup* ULidarPointCloud::GetBodySetup()
{
	return BodySetup && !BodySetup->IsPendingKill() ? BodySetup : nullptr;
}

void ULidarPointCloud::AlignClouds(TArray<ULidarPointCloud*> PointCloudsToAlign)
{
	FDoubleBox CombinedBounds(EForceInit::ForceInit);

	// Calculate combined bounds
	for (ULidarPointCloud* Asset : PointCloudsToAlign)
	{
		CombinedBounds += Asset->GetPreciseBounds(true);
	}

	// Calculate and apply individual shifts
	for (ULidarPointCloud* Asset : PointCloudsToAlign)
	{
		Asset->SetLocationOffset(Asset->OriginalCoordinates - CombinedBounds.GetCenter());
	}
}

ULidarPointCloud* ULidarPointCloud::CreateFromFile(const FString& Filename, const FLidarPointCloudAsyncParameters& AsyncParameters, TSharedPtr<struct FLidarPointCloudImportSettings> ImportSettings, UObject* InParent, FName InName, EObjectFlags Flags)
{
#if WITH_EDITOR
	FOnPointCloudChanged OnPointCloudRebuiltEvent;
	FOnPointCloudChanged OnPointCloudUpdateCollisionEvent;
	bool bOldPointCloudExists = false;

	// See if Point Cloud already exists
	ULidarPointCloud* OldPointCloud = Cast<ULidarPointCloud>(StaticFindObjectFast(nullptr, InParent, InName, true));
	if (OldPointCloud)
	{
		bOldPointCloudExists = true;

		// If so, store event references to re-apply to the new object
		OnPointCloudRebuiltEvent = OldPointCloud->OnPointCloudRebuiltEvent;
		OnPointCloudUpdateCollisionEvent = OldPointCloud->OnPointCloudUpdateCollisionEvent;
	}
#endif

	ULidarPointCloud* PointCloud = NewObject<ULidarPointCloud>(InParent, InName, Flags);

#if WITH_EDITOR
	if (bOldPointCloudExists)
	{
		PointCloud->OnPointCloudRebuiltEvent = OnPointCloudRebuiltEvent;
		PointCloud->OnPointCloudUpdateCollisionEvent = OnPointCloudUpdateCollisionEvent;
	}
#endif

	PointCloud->SetSourcePath(Filename);
	PointCloud->ImportSettings = ImportSettings;
	PointCloud->Reimport(AsyncParameters);

	return PointCloud;
}

template<typename T>
ULidarPointCloud* ULidarPointCloud::CreateFromData(T Points, const int64& Count, const FLidarPointCloudAsyncParameters& AsyncParameters)
{
	ULidarPointCloud* PC = NewObject<ULidarPointCloud>();

	// Process points, if there are any available
	if (Points && Count > 0)
	{
		// Start the process
		if (AsyncParameters.bUseAsync)
		{
			Async(EAsyncExecution::Thread, [PC, AsyncParameters, Points, Count]
			{
				bool bSuccess = PC->SetData(Points, Count, AsyncParameters.ProgressCallback);				
				if (AsyncParameters.CompletionCallback)
				{
					AsyncParameters.CompletionCallback(bSuccess);
				}
			});
		}
		else
		{
			PC->SetData(Points, Count);
		}
	}

	return PC;
}

FBox ULidarPointCloud::CalculateBoundsFromPoints(const FLidarPointCloudPoint* Points, const int64& Count)
{
	FBox Bounds(EForceInit::ForceInit);

	// Process points, if there are any available
	if (Points && Count > 0)
	{
		for (const FLidarPointCloudPoint* Data = Points, *DataEnd = Data + Count; Data != DataEnd; ++Data)
		{
			Bounds += Data->Location;
		}
	}

	return Bounds;
}

FBox ULidarPointCloud::CalculateBoundsFromPoints(FLidarPointCloudPoint** Points, const int64& Count)
{
	FBox Bounds(EForceInit::ForceInit);

	// Process points, if there are any available
	if (Points && Count > 0)
	{
		for (FLidarPointCloudPoint** Data = Points, **DataEnd = Data + Count; Data != DataEnd; ++Data)
		{
			Bounds += (*Data)->Location;
		}
	}

	return Bounds;
}

void ULidarPointCloud::FinishPhysicsAsyncCook(bool bSuccess, UBodySetup* NewBodySetup)
{
	FBenchmarkTimer::Log("CookingCollision");
	Notification.Close(bSuccess);

	if (bSuccess)
	{
		BodySetup = NewBodySetup;
		OnPointCloudUpdateCollisionEvent.Broadcast();
		InitializeCollisionRendering();
	}

	bCollisionBuildInProgress = false;
}

/*********************************************************************************************** ULidarPointCloudBlueprintLibrary */

#define ITERATE_CLOUDS(Action)\
if (UWorld* World = WorldContextObject ? WorldContextObject->GetWorld() : nullptr)\
{\
	for (TActorIterator<ALidarPointCloudActor> Itr(World); Itr; ++Itr)\
	{\
		ALidarPointCloudActor* Actor = *Itr;\
		ULidarPointCloudComponent* Component = Actor->GetPointCloudComponent();\
		{ Action }\
	}\
}

void ULidarPointCloudBlueprintLibrary::CreatePointCloudFromFile(UObject* WorldContextObject, const FString& Filename, bool bUseAsync, FLatentActionInfo LatentInfo, ELidarPointCloudAsyncMode& AsyncMode, float& Progress, ULidarPointCloud*& PointCloud)
{	
	CreatePointCloudFromFile(WorldContextObject, Filename, bUseAsync, LatentInfo, FLidarPointCloudImportSettings::MakeGeneric(Filename), AsyncMode, Progress, PointCloud);
}

void ULidarPointCloudBlueprintLibrary::CreatePointCloudFromFile(UObject* WorldContextObject, const FString& Filename, bool bUseAsync, FLatentActionInfo LatentInfo, TSharedPtr<FLidarPointCloudImportSettings> ImportSettings, ELidarPointCloudAsyncMode& AsyncMode, float& Progress, ULidarPointCloud*& PointCloud)
{
	PointCloud = nullptr;
	if (UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull))
	{
		FLatentActionManager& LatentActionManager = World->GetLatentActionManager();
		if (LatentActionManager.FindExistingAction<FPointCloudLatentAction>(LatentInfo.CallbackTarget, LatentInfo.UUID) == nullptr)
		{
			AsyncMode = ELidarPointCloudAsyncMode::Progress;
			FPointCloudLatentAction* CompletionAction = new FPointCloudLatentAction(LatentInfo, AsyncMode);

			LatentActionManager.AddNewAction(LatentInfo.CallbackTarget, LatentInfo.UUID, CompletionAction);

			PointCloud = ULidarPointCloud::CreateFromFile(Filename, FLidarPointCloudAsyncParameters(bUseAsync,
				[&Progress, &AsyncMode](float InProgress)
				{
					Progress = InProgress;
				},
				[&AsyncMode](bool bSuccess)
				{
					AsyncMode = bSuccess ? ELidarPointCloudAsyncMode::Success : ELidarPointCloudAsyncMode::Failure;
				}),
				ImportSettings);
		}
	}
}

void ULidarPointCloudBlueprintLibrary::CreatePointCloudFromData(UObject* WorldContextObject, const TArray<FLidarPointCloudPoint>& Points, bool bUseAsync, FLatentActionInfo LatentInfo, ELidarPointCloudAsyncMode& AsyncMode, float& Progress, ULidarPointCloud*& PointCloud)
{
	PointCloud = nullptr;
	if (UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull))
	{
		FLatentActionManager& LatentActionManager = World->GetLatentActionManager();
		if (LatentActionManager.FindExistingAction<FPointCloudLatentAction>(LatentInfo.CallbackTarget, LatentInfo.UUID) == nullptr)
		{
			AsyncMode = ELidarPointCloudAsyncMode::Progress;
			FPointCloudLatentAction* CompletionAction = new FPointCloudLatentAction(LatentInfo, AsyncMode);

			LatentActionManager.AddNewAction(LatentInfo.CallbackTarget, LatentInfo.UUID, CompletionAction);

			PointCloud = ULidarPointCloud::CreateFromData(Points, FLidarPointCloudAsyncParameters(bUseAsync,
				[&Progress, &AsyncMode](float InProgress)
				{
					Progress = InProgress;
				},
				[&AsyncMode](bool bSuccess)
				{
					AsyncMode = bSuccess ? ELidarPointCloudAsyncMode::Success : ELidarPointCloudAsyncMode::Failure;
				}));
		}
	}
}

bool ULidarPointCloudBlueprintLibrary::ArePointsInSphere(UObject* WorldContextObject, FVector Center, float Radius, bool bVisibleOnly)
{
	ITERATE_CLOUDS({
		if (Component->HasPointsInSphere(Center, Radius, bVisibleOnly))
		{
			return true;
		}
	});
	return false;
}

bool ULidarPointCloudBlueprintLibrary::ArePointsInBox(UObject* WorldContextObject, FVector Center, FVector Extent, bool bVisibleOnly)
{
	ITERATE_CLOUDS({
		if(Component->HasPointsInBox(Center, Extent, bVisibleOnly))
		{
			return true;
		} 
	});
	return false;
}

bool ULidarPointCloudBlueprintLibrary::ArePointsByRay(UObject* WorldContextObject, FVector Origin, FVector Direction, float Radius, bool bVisibleOnly)
{
	ITERATE_CLOUDS({
		if(Component->HasPointsByRay(Origin, Direction, Radius, bVisibleOnly))
		{
			return true;
		} 
	});
	return false;
}

void ULidarPointCloudBlueprintLibrary::GetPointsInSphereAsCopies(UObject* WorldContextObject, TArray<FLidarPointCloudPoint>& SelectedPoints, FVector Center, float Radius, bool bVisibleOnly)
{
	SelectedPoints.Reset();

	const FSphere Sphere(Center, Radius);

	ITERATE_CLOUDS({
		TArray<FLidarPointCloudPoint> _SelectedPoints;
		Component->GetPointsInSphereAsCopies(_SelectedPoints, Sphere, bVisibleOnly, true);
		SelectedPoints.Append(_SelectedPoints);
	});
}

void ULidarPointCloudBlueprintLibrary::GetPointsInBoxAsCopies(UObject* WorldContextObject, TArray<FLidarPointCloudPoint>& SelectedPoints, FVector Center, FVector Extent, const bool& bVisibleOnly)
{
	SelectedPoints.Reset();

	const FBox Box(Center - Extent, Center + Extent);

	ITERATE_CLOUDS({
		TArray<FLidarPointCloudPoint> _SelectedPoints;
		Component->GetPointsInBoxAsCopies(_SelectedPoints, Box, bVisibleOnly, true);
		SelectedPoints.Append(_SelectedPoints);
	});
}

bool ULidarPointCloudBlueprintLibrary::LineTraceSingle(UObject* WorldContextObject, FVector Origin, FVector Direction, float Radius, bool bVisibleOnly, FLidarPointCloudTraceHit& Hit)
{
	const FLidarPointCloudRay Ray(Origin, Direction);

	ITERATE_CLOUDS({
		if (FLidarPointCloudPoint* Point = Component->LineTraceSingle(Ray, Radius, bVisibleOnly))
		{
			Hit = FLidarPointCloudTraceHit(Actor, Component);
			Hit.Points.Add(*Point);
			return true;
		}
	});

	return false;
}

bool ULidarPointCloudBlueprintLibrary::LineTraceMulti(UObject* WorldContextObject, FVector Origin, FVector Direction, float Radius, bool bVisibleOnly, TArray<FLidarPointCloudTraceHit>& Hits)
{
	Hits.Reset();
	const FLidarPointCloudRay Ray(Origin, Direction);

	ITERATE_CLOUDS({
		FLidarPointCloudTraceHit Hit(Actor, Component);
		if (Component->LineTraceMulti(Ray, Radius, bVisibleOnly, true, Hit.Points))
		{
			Hits.Add(Hit);
			return true;
		}
	});

	return Hits.Num() > 0;
}

void ULidarPointCloudBlueprintLibrary::SetVisibilityOfPointsInSphere(UObject* WorldContextObject, bool bNewVisibility, FVector Center, float Radius)
{
	ITERATE_CLOUDS({ Component->SetVisibilityOfPointsInSphere(bNewVisibility, Center, Radius); });
}

void ULidarPointCloudBlueprintLibrary::SetVisibilityOfPointsInBox(UObject* WorldContextObject, bool bNewVisibility, FVector Center, FVector Extent)
{
	ITERATE_CLOUDS({ Component->SetVisibilityOfPointsInBox(bNewVisibility, Center, Extent); });
}

void ULidarPointCloudBlueprintLibrary::SetVisibilityOfFirstPointByRay(UObject* WorldContextObject, bool bNewVisibility, FVector Origin, FVector Direction, float Radius)
{
	float MinDistance = FLT_MAX;
	ULidarPointCloudComponent* ClosestComponent = nullptr;

	const FLidarPointCloudRay Ray(Origin, Direction);

	ITERATE_CLOUDS({
		if (FLidarPointCloudPoint* Point = Component->LineTraceSingle(Ray, Radius, false))
		{
			const float DistanceSq = (Point->Location - Origin).SizeSquared();
			if (DistanceSq < MinDistance)
			{
				MinDistance = DistanceSq;
				ClosestComponent = Component;
			}
		}
	});

	if (ClosestComponent)
	{
		ClosestComponent->SetVisibilityOfFirstPointByRay(bNewVisibility, Ray, Radius);
	}
}

void ULidarPointCloudBlueprintLibrary::SetVisibilityOfPointsByRay(UObject* WorldContextObject, bool bNewVisibility, FVector Origin, FVector Direction, float Radius)
{
	ITERATE_CLOUDS({ Component->SetVisibilityOfPointsByRay(bNewVisibility, Origin, Direction, Radius); });
}

void ULidarPointCloudBlueprintLibrary::ApplyColorToPointsInSphere(UObject* WorldContextObject, FColor NewColor, FVector Center, float Radius, bool bVisibleOnly)
{
	ITERATE_CLOUDS({ Component->ApplyColorToPointsInSphere(NewColor, Center, Radius, bVisibleOnly); });
}

void ULidarPointCloudBlueprintLibrary::ApplyColorToPointsInBox(UObject* WorldContextObject, FColor NewColor, FVector Center, FVector Extent, bool bVisibleOnly)
{
	ITERATE_CLOUDS({ Component->ApplyColorToPointsInBox(NewColor, Center, Extent, bVisibleOnly); });
}

void ULidarPointCloudBlueprintLibrary::ApplyColorToFirstPointByRay(UObject* WorldContextObject, FColor NewColor, FVector Origin, FVector Direction, float Radius, bool bVisibleOnly)
{
	float MinDistance = FLT_MAX;
	ULidarPointCloudComponent* ClosestComponent = nullptr;

	const FLidarPointCloudRay Ray(Origin, Direction);

	ITERATE_CLOUDS({
		if (FLidarPointCloudPoint* Point = Component->LineTraceSingle(Ray, Radius, bVisibleOnly))
		{
			const float DistanceSq = (Point->Location - Origin).SizeSquared();
			if (DistanceSq < MinDistance)
			{
				MinDistance = DistanceSq;
				ClosestComponent = Component;
			}
		}
		});

	if (ClosestComponent)
	{
		ClosestComponent->ApplyColorToFirstPointByRay(NewColor, Ray, Radius, bVisibleOnly);
	}
}

void ULidarPointCloudBlueprintLibrary::ApplyColorToPointsByRay(UObject* WorldContextObject, FColor NewColor, FVector Origin, FVector Direction, float Radius, bool bVisibleOnly)
{
	ITERATE_CLOUDS({ Component->ApplyColorToPointsByRay(NewColor, Origin, Direction, Radius, bVisibleOnly); });
}

void ULidarPointCloudBlueprintLibrary::RemovePointsInSphere(UObject* WorldContextObject, FVector Center, float Radius, bool bVisibleOnly)
{
	ITERATE_CLOUDS({ Component->RemovePointsInSphere(Center, Radius, bVisibleOnly); });
}

void ULidarPointCloudBlueprintLibrary::RemovePointsInBox(UObject* WorldContextObject, FVector Center, FVector Extent, bool bVisibleOnly)
{
	ITERATE_CLOUDS({ Component->RemovePointsInBox(Center, Extent, bVisibleOnly); });
}

void ULidarPointCloudBlueprintLibrary::RemoveFirstPointByRay(UObject* WorldContextObject, FVector Origin, FVector Direction, float Radius, bool bVisibleOnly)
{
	float MinDistance = FLT_MAX;
	ULidarPointCloudComponent* ClosestComponent = nullptr;

	const FLidarPointCloudRay Ray(Origin, Direction);

	ITERATE_CLOUDS({
		if (FLidarPointCloudPoint* Point = Component->LineTraceSingle(Ray, Radius, bVisibleOnly))
		{
			const float DistanceSq = (Point->Location - Origin).SizeSquared();
			if (DistanceSq < MinDistance)
			{
				MinDistance = DistanceSq;
				ClosestComponent = Component;
			}
		}
		});

	if (ClosestComponent)
	{
		ClosestComponent->RemoveFirstPointByRay(Ray, Radius, bVisibleOnly);
	}
}

void ULidarPointCloudBlueprintLibrary::RemovePointsByRay(UObject* WorldContextObject, FVector Origin, FVector Direction, float Radius, bool bVisibleOnly)
{
	ITERATE_CLOUDS({ Component->RemovePointsByRay(Origin, Direction, Radius, bVisibleOnly); });
}

#undef ITERATE_CLOUDS

/*********************************************************************************************** ALidarClippingVolume */

ALidarClippingVolume::ALidarClippingVolume()
	: bEnabled(true)
	, Mode(ELidarClippingVolumeMode::ClipOutside)
	, Priority(0)
{
	bColored = true;
	BrushColor.R = 0;
	BrushColor.G = 128;
	BrushColor.B = 128;
	BrushColor.A = 255;

	GetBrushComponent()->SetMobility(EComponentMobility::Movable);

	SetActorScale3D(FVector(50));
}

/*********************************************************************************************** Templates */

template bool ULidarPointCloud::InsertPoints_NoLock<FLidarPointCloudPoint*>(FLidarPointCloudPoint*, const int64&, ELidarPointCloudDuplicateHandling, bool, const FVector&, FThreadSafeBool*, TFunction<void(float)>);
template bool ULidarPointCloud::InsertPoints_NoLock<const FLidarPointCloudPoint*>(const FLidarPointCloudPoint*, const int64&, ELidarPointCloudDuplicateHandling, bool, const FVector&, FThreadSafeBool*, TFunction<void(float)>);
template bool ULidarPointCloud::InsertPoints_NoLock<FLidarPointCloudPoint**>(FLidarPointCloudPoint**, const int64&, ELidarPointCloudDuplicateHandling, bool, const FVector&, FThreadSafeBool*, TFunction<void(float)>);
template bool ULidarPointCloud::SetData<const FLidarPointCloudPoint*>(const FLidarPointCloudPoint*, const int64&, TFunction<void(float)>);
template bool ULidarPointCloud::SetData<FLidarPointCloudPoint**>(FLidarPointCloudPoint**, const int64&, TFunction<void(float)>);
template void ULidarPointCloud::GetPoints(TArray<FLidarPointCloudPoint*>&, int64, int64);
template void ULidarPointCloud::GetPoints(TArray64<FLidarPointCloudPoint*>&, int64, int64);
template void ULidarPointCloud::GetPointsInSphere(TArray<FLidarPointCloudPoint*>&, FSphere, const bool&);
template void ULidarPointCloud::GetPointsInSphere(TArray64<FLidarPointCloudPoint*>&, FSphere, const bool&);
template void ULidarPointCloud::GetPointsInBox(TArray<FLidarPointCloudPoint*>&, const FBox&, const bool&);
template void ULidarPointCloud::GetPointsInBox(TArray64<FLidarPointCloudPoint*>&, const FBox&, const bool&);
template void ULidarPointCloud::GetPointsAsCopies(TArray<FLidarPointCloudPoint>&, bool, int64, int64) const;
template void ULidarPointCloud::GetPointsAsCopies(TArray64<FLidarPointCloudPoint>&, bool, int64, int64) const;
template void ULidarPointCloud::GetPointsInSphereAsCopies(TArray<FLidarPointCloudPoint>&, FSphere, const bool&, bool) const;
template void ULidarPointCloud::GetPointsInSphereAsCopies(TArray64<FLidarPointCloudPoint>&, FSphere, const bool&, bool) const;
template void ULidarPointCloud::GetPointsInBoxAsCopies(TArray<FLidarPointCloudPoint>&, const FBox&, const bool&, bool) const;
template void ULidarPointCloud::GetPointsInBoxAsCopies(TArray64<FLidarPointCloudPoint>&, const FBox&, const bool&, bool) const;
template FBox ULidarPointCloud::CalculateBoundsFromPoints(TArray<FLidarPointCloudPoint*>&);
template FBox ULidarPointCloud::CalculateBoundsFromPoints(TArray64<FLidarPointCloudPoint*>&);
template FBox ULidarPointCloud::CalculateBoundsFromPoints(TArray<const FLidarPointCloudPoint>&);
template FBox ULidarPointCloud::CalculateBoundsFromPoints(TArray64<const FLidarPointCloudPoint>&);

#undef LOCTEXT_NAMESPACE
