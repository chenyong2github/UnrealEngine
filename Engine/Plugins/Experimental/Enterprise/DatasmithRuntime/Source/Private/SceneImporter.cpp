// Copyright Epic Games, Inc. All Rights Reserved.

#include "SceneImporter.h"

#include "DatasmithRuntime.h"
#include "DatasmithRuntimeUtils.h"
#include "LogCategory.h"

#include "IDatasmithSceneElements.h"

#include "Camera/PlayerCameraManager.h"
#include "CineCameraActor.h"
#include "CineCameraComponent.h"
#include "Engine/Scene.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "ProfilingDebugging/MiscTrace.h"

namespace DatasmithRuntime
{
	extern void UpdateMaterials(TSet<FSceneGraphId>& MaterialElementSet, TMap< FSceneGraphId, FAssetData >& AssetDataList);

#ifdef LIVEUPDATE_TIME_LOGGING
	Timer::Timer(double InTimeOrigin, const char* InText)
		: TimeOrigin(InTimeOrigin)
		, StartTime(FPlatformTime::Seconds())
		, Text(InText)
	{
	}

	Timer::~Timer()
	{
		const double EndTime = FPlatformTime::Seconds();
		const double ElapsedMilliSeconds = (EndTime - StartTime) * 1000.;

		double SecondsSinceOrigin = EndTime - TimeOrigin;

		const int MinSinceOrigin = int(SecondsSinceOrigin / 60.);
		SecondsSinceOrigin -= 60.0 * (double)MinSinceOrigin;

		UE_LOG(LogDatasmithRuntime, Log, TEXT("%s in [%.3f ms] ( since beginning [%d min %.3f s] )"), *Text, ElapsedMilliSeconds, MinSinceOrigin, SecondsSinceOrigin);
	}
#endif

	const FString TexturePrefix( TEXT( "Texture." ) );
	const FString MaterialPrefix( TEXT( "Material." ) );
	const FString MeshPrefix( TEXT( "Mesh." ) );

	FAssetData FAssetData::EmptyAsset(DirectLink::InvalidId);

	FSceneImporter::FSceneImporter(ADatasmithRuntimeActor* InDatasmithRuntimeActor)
		: RootComponent( InDatasmithRuntimeActor->GetRootComponent() )
		, TasksToComplete( EWorkerTask::NoTask )
		, OverallProgress(InDatasmithRuntimeActor->Progress)
	{
		SceneKey = GetTypeHash(FGuid::NewGuid());
		FAssetRegistry::RegisterMapping(SceneKey, &AssetDataList);

		FAssetData::EmptyAsset.SetState(EAssetState::Processed | EAssetState::Completed);
	}

	FSceneImporter::~FSceneImporter()
	{
		DeleteData();
		FAssetRegistry::UnregisterMapping(SceneKey);
	}

	TStatId FSceneImporter::GetStatId() const
	{
		RETURN_QUICK_DECLARE_CYCLE_STAT(FSceneImporter, STATGROUP_Tickables);
	}

	void FSceneImporter::ParseScene( const TSharedPtr<IDatasmithActorElement>& ActorElement, FSceneGraphId ParentId, FParsingCallback Callback )
	{
		Callback( ActorElement, ParentId );

		FSceneGraphId ActorId = ActorElement->GetNodeId();

		for (int32 Index = 0; Index < ActorElement->GetChildrenCount(); ++Index)
		{
			ParseScene( ActorElement->GetChild(Index), ActorId, Callback );
		}
	}

	void FSceneImporter::StartImport(TSharedRef<IDatasmithScene> InSceneElement)
	{
		Reset(true);

		SceneElement = InSceneElement;

		TasksToComplete |= SceneElement.IsValid() ? EWorkerTask::CollectSceneData : EWorkerTask::NoTask;

#ifdef LIVEUPDATE_TIME_LOGGING
		GlobalStartTime = FPlatformTime::Seconds();
#endif
	}

	void FSceneImporter::AddAsset(TSharedPtr<IDatasmithElement>&& InElementPtr, const FString& AssetPrefix, EDataType::Type DataType)
	{
		if (IDatasmithElement* Element = InElementPtr.Get())
		{
			const FString AssetKey = AssetPrefix + Element->GetName();
			const FSceneGraphId ElementId = Element->GetNodeId();

			AssetElementMapping.Add( AssetKey, ElementId );

			Elements.Add( ElementId, MoveTemp( InElementPtr ) );

			FAssetData AssetData(ElementId, DataType);
			AssetDataList.Emplace(ElementId, MoveTemp(AssetData));
		}
	}

	void FSceneImporter::CollectSceneData()
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FSceneImporter::CollectSceneData);

		LIVEUPDATE_LOG_TIME;

		int32 ActorElementCount = 0;
		FParsingCallback CountingCallback = 
		[&](const TSharedPtr< IDatasmithActorElement>& ActorElement, FSceneGraphId ActorId) -> void
		{
			++ActorElementCount;
		};

		for (int32 Index = 0; Index < SceneElement->GetActorsCount(); ++Index)
		{
			ParseScene( SceneElement->GetActor(Index), DirectLink::InvalidId, CountingCallback );
		}

		int32 AssetElementCount = SceneElement->GetTexturesCount() + SceneElement->GetMaterialsCount() +
			SceneElement->GetMeshesCount() + SceneElement->GetLevelSequencesCount();

		// Make sure to pre-allocate enough memory as pointer on values in those maps are used
		TextureDataList.Empty( SceneElement->GetTexturesCount() );
		AssetDataList.Empty( AssetElementCount );
		ActorDataList.Empty( ActorElementCount );
		Elements.Empty( AssetElementCount + ActorElementCount );

		AssetElementMapping.Empty( AssetElementCount );

		for (int32 Index = 0; Index < SceneElement->GetTexturesCount(); ++Index)
		{
			// Only add a texture if its associated resource file is available
			if (IDatasmithTextureElement* TextureElement = static_cast<IDatasmithTextureElement*>(SceneElement->GetTexture(Index).Get()))
			{
				// If resource file does not exist, add scene's resource path if valid
				if (!FPaths::FileExists(TextureElement->GetFile()) && FPaths::DirectoryExists(SceneElement->GetResourcePath()))
				{
					TextureElement->SetFile( *FPaths::Combine(SceneElement->GetResourcePath(), TextureElement->GetFile()) );
				}

				if (FPaths::FileExists(TextureElement->GetFile()))
				{
					AddAsset(SceneElement->GetTexture(Index), TexturePrefix, EDataType::Texture);
				}
			}
			// #ueent_datasmithruntime: Inform user resource file does not exist
		}

		for (int32 Index = 0; Index < SceneElement->GetMaterialsCount(); ++Index)
		{
			AddAsset(SceneElement->GetMaterial(Index), MaterialPrefix, EDataType::Material);
		}

		for (int32 Index = 0; Index < SceneElement->GetMeshesCount(); ++Index)
		{
			// Only add a mesh if its associated resource is available
			if (IDatasmithMeshElement* MeshElement = static_cast<IDatasmithMeshElement*>(SceneElement->GetMesh(Index).Get()))
			{
				// If resource file does not exist, add scene's resource path if valid
				if (!FPaths::FileExists(MeshElement->GetFile()) && FPaths::DirectoryExists(SceneElement->GetResourcePath()))
				{
					MeshElement->SetFile( *FPaths::Combine(SceneElement->GetResourcePath(), MeshElement->GetFile()) );
				}

				if (FPaths::FileExists(MeshElement->GetFile()))
				{
					AddAsset(SceneElement->GetMesh(Index), MeshPrefix, EDataType::Mesh);
				}
			}
			// #ueent_datasmithruntime: Inform user resource file does not exist
		}

		// Collect set of materials and meshes used in scene
		// Collect set of textures used in scene
		TextureElementSet.Empty(SceneElement->GetTexturesCount());
		MeshElementSet.Empty(SceneElement->GetMeshesCount());
		MaterialElementSet.Empty(SceneElement->GetMaterialsCount());

		for (int32 Index = 0; Index < SceneElement->GetActorsCount(); ++Index)
		{
			ParseScene(SceneElement->GetActor(Index), DirectLink::InvalidId,
				[this](const TSharedPtr<IDatasmithActorElement>& ActorElement, FSceneGraphId ParentId) -> void
				{
					this->ProcessActorElement(ActorElement, ParentId);
				}
			);
		}

		TasksToComplete |= EWorkerTask::SetupTasks;
	}

	void FSceneImporter::SetupTasks()
	{
		LIVEUPDATE_LOG_TIME;

		for ( TPair<FSceneGraphId, FActorData>& Pair : ActorDataList)
		{
			FActorData& ActorData = Pair.Value;

			ActorData.WorldTransform = ActorData.RelativeTransform;
			for (FSceneGraphId ParentId = ActorData.ParentId; ParentId != DirectLink::InvalidId; )
			{
				const FActorData& ParentActorData = ActorDataList[ParentId];

				ActorData.WorldTransform = ActorData.WorldTransform * ParentActorData.RelativeTransform;

				ParentId = ParentActorData.ParentId;
			}
		}

		// Compute parameters for update on progress
		int32 ActionsCount = QueuedTaskCount;

		ActionsCount += MaterialElementSet.Num();

		// Add texture creation + texture assignments
		for (FSceneGraphId ElementId : TextureElementSet)
		{
			const FAssetData& AssetData = AssetDataList[ElementId];
			ActionsCount += AssetData.Referencers.Num() + 1;
		}

		OverallProgress = 0.05f;
		double MaxActions = FMath::FloorToDouble( (double)ActionsCount / 0.95 );
		ActionCounter.Set((int32)FMath::CeilToDouble( MaxActions * 0.05 ));
		ProgressStep = 1. / MaxActions;

		MeshPreProcessing();

		OnGoingTasks.Reserve(TextureElementSet.Num() + MeshElementSet.Num());

		if (TextureElementSet.Num() > 0)
		{
			ImageReaderInitialize();

			TasksToComplete |= EWorkerTask::TextureLoad;
		}
	}

	void FSceneImporter::ProcessActorElement(const TSharedPtr< IDatasmithActorElement >& ActorElement, FSceneGraphId ParentId)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FSceneImporter::ProcessActorElement);

		FSceneGraphId ElementId = ActorElement->GetNodeId();

		if (!Elements.Contains(ElementId))
		{
			Elements.Add(ElementId, ActorElement);

			FActorData ActorData(ElementId);
			ActorDataList.Emplace(ElementId, MoveTemp(ActorData));
		}

		ensure(ActorDataList.Contains(ElementId));
		FActorData& ActorData = ActorDataList[ElementId];

		if (ActorData.HasState(EAssetState::Processed))
		{
			return;
		}

		ActorData.ParentId = ParentId;
		ActorData.RelativeTransform = ActorElement->GetRelativeTransform();

		const FTransform& ParentTransform = (ParentId != DirectLink::InvalidId) ? ActorDataList[ParentId].WorldTransform : FTransform::Identity;
		ActorData.WorldTransform = ActorData.RelativeTransform * ParentTransform;

		if (ActorElement->IsA(EDatasmithElementType::StaticMeshActor))
		{
			IDatasmithMeshActorElement* MeshActorElement = static_cast<IDatasmithMeshActorElement*>(ActorElement.Get());

			ProcessMeshActorData(ActorData, MeshActorElement);
		}
		else if (ActorElement->IsA(EDatasmithElementType::Light))
		{
			IDatasmithLightActorElement* LightActorElement = static_cast<IDatasmithLightActorElement*>(ActorElement.Get());

			ProcessLightActorData(ActorData, LightActorElement);
		}
		else if (ActorElement->IsA(EDatasmithElementType::Camera))
		{
			IDatasmithCameraActorElement* CameraElement = static_cast<IDatasmithCameraActorElement*>(ActorElement.Get());

			ProcessCameraActorData(ActorData, CameraElement);
		}
		else
		{
			ActorData.SetState(EAssetState::Processed);
		}
	}

	void FSceneImporter::Tick(float DeltaSeconds)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FSceneImporter::Tick);

		if (TasksToComplete == EWorkerTask::NoTask)
		{
			return;
		}

		// Full reset of the world. Resume tasks on next tick
		if (EnumHasAnyFlags( TasksToComplete, EWorkerTask::ResetScene))
		{
			// Wait for ongoing tasks to be completed
			for (TFuture<bool>& OnGoingTask : OnGoingTasks)
			{
				OnGoingTask.Wait();
			}

			OnGoingTasks.Empty();

			bool bGarbageCollect = DeleteData();

			Elements.Empty();
			AssetElementMapping.Empty();

			AssetDataList.Empty();
			TextureDataList.Empty();
			ActorDataList.Empty();

			bGarbageCollect |= FAssetRegistry::CleanUp();

			TasksToComplete &= ~EWorkerTask::ResetScene;

			// If there is no more tasks to complete, delete assets which are not used
			if (bGarbageCollect)
			{
				if (!IsGarbageCollecting())
				{
					CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
				}
				else
				{
					// Post-pone garbage collection for next frame
					TasksToComplete = EWorkerTask::GarbageCollect;
				}
			}

			return;
		}

		struct FLocalUpdate
		{
			FLocalUpdate(float& InProgress, FThreadSafeCounter& InCounter, double InStep)
				: Progress(InProgress)
				, Counter(InCounter)
				, Step(InStep)
			{
			}

			~FLocalUpdate()
			{
				Progress = (float)((double)Counter.GetValue() * Step);
			}

			float& Progress;
			FThreadSafeCounter& Counter;
			double Step;
		};

		FLocalUpdate LocalUpdate(OverallProgress, ActionCounter, ProgressStep);

		// Execute work by chunk of 10 milliseconds timespan
		double EndTime = FPlatformTime::Seconds() + 0.02;

		if (EnumHasAnyFlags( TasksToComplete, EWorkerTask::GarbageCollect))
		{
			// Do not take any risk, wait for next frame to continue the process
			if (IsGarbageCollecting())
			{
				return;
			}

			CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
			TasksToComplete &= ~EWorkerTask::GarbageCollect;
		}

		bool bContinue = FPlatformTime::Seconds() < EndTime;

		if (EnumHasAnyFlags( TasksToComplete, EWorkerTask::CollectSceneData))
		{
			CollectSceneData();
			TasksToComplete &= ~EWorkerTask::CollectSceneData;
		}

		bContinue = FPlatformTime::Seconds() < EndTime;

		if (bContinue && !ActionQueues[UPDATE_QUEUE].IsEmpty())
		{
			ProcessQueue(UPDATE_QUEUE, EndTime, EWorkerTask::UpdateElement, EWorkerTask::SetupTasks);
		}

		bContinue = FPlatformTime::Seconds() < EndTime;

		if (bContinue && EnumHasAnyFlags( TasksToComplete, EWorkerTask::SetupTasks))
		{
			SetupTasks();
			TasksToComplete &= ~EWorkerTask::SetupTasks;
		}

		bContinue = FPlatformTime::Seconds() < EndTime;

		if (bContinue && !ActionQueues[MESH_QUEUE].IsEmpty())
		{
			ProcessQueue(MESH_QUEUE, EndTime, EWorkerTask::MeshCreate);
		}

		bContinue = FPlatformTime::Seconds() < EndTime;

		if (bContinue && !ActionQueues[MATERIAL_QUEUE].IsEmpty())
		{
			FActionTask ActionTask;
			while (FPlatformTime::Seconds() < EndTime)
			{
				if (!ActionQueues[MATERIAL_QUEUE].Dequeue(ActionTask))
				{
					TasksToComplete &= ~EWorkerTask::MaterialCreate;
					UpdateMaterials(MaterialElementSet, AssetDataList);

					break;
				}

				ensure(DirectLink::InvalidId == ActionTask.GetAssetId());
				ActionTask.Execute(FAssetData::EmptyAsset);
				ActionCounter.Increment();
			}
		}

		bContinue = FPlatformTime::Seconds() < EndTime;

		if (bContinue && !ActionQueues[TEXTURE_QUEUE].IsEmpty())
		{
			ProcessQueue(TEXTURE_QUEUE, EndTime, EWorkerTask::TextureLoad);
		}

		bContinue = FPlatformTime::Seconds() < EndTime;

		if (bContinue && !ActionQueues[NONASYNC_QUEUE].IsEmpty())
		{
			FActionTask ActionTask;
			while (FPlatformTime::Seconds() < EndTime)
			{
				if (!ActionQueues[NONASYNC_QUEUE].Dequeue(ActionTask))
				{
					TasksToComplete &= ~EWorkerTask::NonAsyncTasks;

					break;
				}

				if (DirectLink::InvalidId == ActionTask.GetAssetId())
				{
					ActionTask.Execute(FAssetData::EmptyAsset);
					ActionCounter.Increment();
				}
				else
				{
					if (ActionTask.Execute(AssetDataList[ActionTask.GetAssetId()]) == EActionResult::Retry)
					{
						ActionQueues[NONASYNC_QUEUE].Enqueue(MoveTemp(ActionTask));
						continue;
					}

					ActionCounter.Increment();
				}
			}
		}

		bContinue = FPlatformTime::Seconds() < EndTime;

		// Flag used to avoid deleting components and associated assets in the same frame
		bool bHasComponentToDelete = ActionQueues[DELETE_QUEUE_C].IsEmpty();

		if (bContinue && !ActionQueues[DELETE_QUEUE_C].IsEmpty())
		{
			FActionTask ActionTask;
			while (FPlatformTime::Seconds() < EndTime)
			{
				if (!ActionQueues[DELETE_QUEUE_C].Dequeue(ActionTask))
				{
					TasksToComplete &= ~EWorkerTask::DeleteComponent;
					break;
				}

				if (ActionTask.Execute(FAssetData::EmptyAsset))
				{
					TasksToComplete |= EWorkerTask::GarbageCollect;
				}
			}
		}

		// Force a garbage collection if we are done with the components
		if (ActionQueues[DELETE_QUEUE_C].IsEmpty() && EnumHasAnyFlags(TasksToComplete, EWorkerTask::GarbageCollect))
		{
			if (!IsGarbageCollecting())
			{
				CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
				TasksToComplete &= ~EWorkerTask::GarbageCollect;
			}
		}

		// Do not continue if there are still components to garbage collect
		bContinue = FPlatformTime::Seconds() < EndTime && !(TasksToComplete & EWorkerTask::GarbageCollect);

		if (bContinue && !ActionQueues[DELETE_QUEUE_A].IsEmpty())
		{
			FActionTask ActionTask;
			while (FPlatformTime::Seconds() < EndTime)
			{
				if (!ActionQueues[DELETE_QUEUE_A].Dequeue(ActionTask))
				{
					TasksToComplete &= ~EWorkerTask::DeleteAsset;
					break;
				}

				ActionTask.Execute(FAssetData::EmptyAsset);
			}
		}

		if (TasksToComplete == EWorkerTask::NoTask && SceneElement.IsValid())
		{
			// Delete assets which has not been reused on the last processing
			if (FAssetRegistry::CleanUp())
			{
				if (!IsGarbageCollecting())
				{
					CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
				}
				else
				{
					// Garbage collection has not been performed. Do it on next frame
					TasksToComplete = EWorkerTask::GarbageCollect;
					return;
				}
			}

			TRACE_BOOKMARK(TEXT("Load complete - %s"), *SceneElement->GetName());

			OnGoingTasks.Empty();

			LastSceneGuid = SceneElement->GetSharedState()->GetGuid();

			Cast<ADatasmithRuntimeActor>(RootComponent->GetOwner())->OnImportEnd();
#ifdef LIVEUPDATE_TIME_LOGGING
			double ElapsedSeconds = FPlatformTime::Seconds() - GlobalStartTime;

			int ElapsedMin = int(ElapsedSeconds / 60.0);
			ElapsedSeconds -= 60.0 * (double)ElapsedMin;

			UE_LOG(LogDatasmithRuntime, Log, TEXT("Total load time is [%d min %.3f s]"), ElapsedMin, ElapsedSeconds);
#endif

			// Return if async tasks are not completed
			for (TFuture<bool>& OnGoingTask : OnGoingTasks)
			{
				if (!OnGoingTask.IsReady() && TasksToComplete != EWorkerTask::NoTask)
				{
					ensure(false);
					break;
				}
			}
		}
	}

	void FSceneImporter::Reset(bool bIsNewScene)
	{
		bIncrementalUpdate = !bIsNewScene;

		TasksToComplete = EWorkerTask::NoTask;

		// Clear all cached data if it is a new scene
		if (bIsNewScene)
		{
			SceneElement.Reset();
			LastSceneGuid = FGuid();

			TasksToComplete = EWorkerTask::ResetScene;
		}
		// Clean up referencer of all assets as it may change on an update
		else
		{
			for (TPair< FSceneGraphId, FAssetData >& Entry : AssetDataList)
			{
				Entry.Value.Referencers.Empty();
			}
		}

		// Reset counters
		QueuedTaskCount = 0;

		// Empty tracking arrays and sets
		MeshElementSet.Empty();
		TextureElementSet.Empty();
		MaterialElementSet.Empty();
		// #ue_datasmithruntime: What about lightmap weights on incremental update?
		LightmapWeights.Empty();

		// Empty tasks queues
		for (int32 Index = 0; Index < MAX_QUEUES; ++Index)
		{
			ActionQueues[Index].Empty();
		}
	}

	bool FSceneImporter::IncrementalUpdate(TSharedRef< IDatasmithScene > InSceneElement, FUpdateContext& UpdateContext)
	{
#ifdef LIVEUPDATE_TIME_LOGGING
		GlobalStartTime = FPlatformTime::Seconds();
#endif
		UE_LOG(LogDatasmithRuntime, Log, TEXT("Incremental update..."));

		Reset(false);

		// Update elements map with new pointers
		SceneElement = InSceneElement;

		for (int32 Index = 0; Index < SceneElement->GetTexturesCount(); ++Index)
		{
			FSceneGraphId ElementId = SceneElement->GetTexture(Index)->GetNodeId();
			if (this->Elements.Contains(ElementId))
			{
				TSharedPtr<IDatasmithElement> TextureElement = SceneElement->GetTexture(Index);

				AssetElementMapping.FindOrAdd(TexturePrefix + TextureElement->GetName()) = TextureElement->GetNodeId();
				if (FCString::Strcmp(Elements[ElementId]->GetName(), TextureElement->GetName()))
				{
					AssetElementMapping.Remove(TexturePrefix + TextureElement->GetName());
				}

				Elements[ElementId] = TextureElement;
			}
		}

		for (int32 Index = 0; Index < SceneElement->GetMaterialsCount(); ++Index)
		{
			FSceneGraphId ElementId = SceneElement->GetMaterial(Index)->GetNodeId();
			if (this->Elements.Contains(ElementId))
			{
				TSharedPtr<IDatasmithElement> MaterialElement = SceneElement->GetMaterial(Index);

				AssetElementMapping.FindOrAdd( MaterialPrefix + MaterialElement->GetName()) = MaterialElement->GetNodeId();
				if (FCString::Strcmp(Elements[ElementId]->GetName(), MaterialElement->GetName()))
				{
					AssetElementMapping.Remove(MaterialPrefix + MaterialElement->GetName());
				}

				this->Elements[ElementId] = SceneElement->GetMaterial(Index);
			}
		}

		for (int32 Index = 0; Index < SceneElement->GetMeshesCount(); ++Index)
		{
			FSceneGraphId ElementId = SceneElement->GetMesh(Index)->GetNodeId();
			if (this->Elements.Contains(ElementId))
			{
				TSharedPtr<IDatasmithElement> MeshElement = SceneElement->GetMesh(Index);

				AssetElementMapping.FindOrAdd( MeshPrefix + MeshElement->GetName()) = MeshElement->GetNodeId();
				if (FCString::Strcmp(Elements[ElementId]->GetName(), MeshElement->GetName()))
				{
					AssetElementMapping.Remove(MeshPrefix + MeshElement->GetName());
				}

				this->Elements[ElementId] = SceneElement->GetMesh(Index);
			}
		}

		for (int32 Index = 0; Index < SceneElement->GetActorsCount(); ++Index)
		{
			ParseScene(SceneElement->GetActor(Index), DirectLink::InvalidId,
				[this](const TSharedPtr<IDatasmithActorElement>& ActorElement, FSceneGraphId ParentId) -> void
				{
					FSceneGraphId ElementId = ActorElement->GetNodeId();
					if (this->Elements.Contains(ElementId))
					{
						this->Elements[ElementId] = ActorElement;
					}
				}
			);
		}

		// Mark assets which are about to be deleted
		for (DirectLink::FSceneGraphId& ElementId : UpdateContext.Deletions)
		{
			if (AssetDataList.Contains(ElementId))
			{
				AssetDataList[ElementId].AddState(EAssetState::PendingDelete);
			}
			else if (ActorDataList.Contains(ElementId))
			{
				ActorDataList[ElementId].AddState(EAssetState::PendingDelete);
			}
		}

		if (UpdateContext.Additions.Num() > 0)
		{
			const int32 AdditionCount = UpdateContext.Additions.Num();

			// Collect set of materials and meshes used in scene
			// Collect set of textures used in scene
			TextureElementSet.Empty(AdditionCount);
			MeshElementSet.Empty(AdditionCount);
			MaterialElementSet.Empty(AdditionCount);

			Elements.Reserve( Elements.Num() + AdditionCount );
			AssetDataList.Reserve( AssetDataList.Num() + AdditionCount );

			for (TSharedPtr<IDatasmithElement>& ElementPtr : UpdateContext.Additions)
			{
				if (ElementPtr->IsA(EDatasmithElementType::BaseMaterial))
				{
					AddAsset(MoveTemp(ElementPtr), MaterialPrefix, EDataType::Material);
				}
				else if (ElementPtr->IsA(EDatasmithElementType::StaticMesh))
				{
					if (IDatasmithMeshElement* MeshElement = static_cast<IDatasmithMeshElement*>(ElementPtr.Get()))
					{
						// If resource file does not exist, add scene's resource path if valid
						if (!FPaths::FileExists(MeshElement->GetFile()) && FPaths::DirectoryExists(SceneElement->GetResourcePath()))
						{
							MeshElement->SetFile( *FPaths::Combine(SceneElement->GetResourcePath(), MeshElement->GetFile()) );
						}

						if (FPaths::FileExists(MeshElement->GetFile()))
						{
							AddAsset(MoveTemp(ElementPtr), MeshPrefix, EDataType::Mesh);
						}
					}
				}
				else if (ElementPtr->IsA(EDatasmithElementType::Texture))
				{
					if (IDatasmithTextureElement* TextureElement = static_cast<IDatasmithTextureElement*>(ElementPtr.Get()))
					{
						// If resource file does not exist, add scene's resource path if valid
						if (!FPaths::FileExists(TextureElement->GetFile()) && FPaths::DirectoryExists(SceneElement->GetResourcePath()))
						{
							TextureElement->SetFile( *FPaths::Combine(SceneElement->GetResourcePath(), TextureElement->GetFile()) );
						}

						if (FPaths::FileExists(TextureElement->GetFile()))
						{
							AddAsset(MoveTemp(ElementPtr), TexturePrefix, EDataType::Texture);
						}
					}
				}
			}

			// Parse scene to process newly added actors
			for (int32 Index = 0; Index < SceneElement->GetActorsCount(); ++Index)
			{
				ParseScene(SceneElement->GetActor(Index), DirectLink::InvalidId,
					[this](const TSharedPtr<IDatasmithActorElement>& ActorElement, FSceneGraphId ParentId) -> void
					{
						this->ProcessActorElement(ActorElement, ParentId);
					}
				);
			}

			TasksToComplete |= EWorkerTask::SetupTasks;
		}

		if (UpdateContext.Updates.Num() > 0)
		{
			for (TSharedPtr<IDatasmithElement>& ElementPtr : UpdateContext.Updates)
			{
				if (Elements.Contains(ElementPtr->GetNodeId()))
				{
					FSceneGraphId ElementId = ElementPtr->GetNodeId();
					Elements[ElementId] = ElementPtr;

					FActionTaskFunction TaskFunc;

					if (ElementPtr->IsA(EDatasmithElementType::BaseMaterial))
					{
						TaskFunc = [this, ElementId](UObject*, const FReferencer&) -> EActionResult::Type
						{
							FAssetData& MaterialData = this->AssetDataList[ElementId];

							MaterialData.SetState(EAssetState::Unknown);

							this->ProcessMaterialData(MaterialData);

							return EActionResult::Succeeded;
						};
					}
					else if (ElementPtr->IsA(EDatasmithElementType::StaticMesh))
					{
						TaskFunc = [this, ElementId](UObject*, const FReferencer&) -> EActionResult::Type
						{
							FAssetData& MeshData = this->AssetDataList[ElementId];

							MeshData.SetState(EAssetState::Unknown);

							this->ProcessMeshData(MeshData);

							return EActionResult::Succeeded;
						};
					}
					else if (ElementPtr->IsA(EDatasmithElementType::Texture))
					{
						ensure(TextureDataList.Contains(ElementId));

						TaskFunc = [this, ElementId](UObject*, const FReferencer&) -> EActionResult::Type
						{
							FAssetData& TextureData = this->AssetDataList[ElementId];

							TextureData.SetState(EAssetState::Unknown);

							this->ProcessTextureData(ElementId);

							return EActionResult::Succeeded;
						};
					}
					else if (ElementPtr->IsA(EDatasmithElementType::Actor))
					{
						ensure(ActorDataList.Contains(ElementId));
						FActorData& ActorData = ActorDataList[ElementId];

						ActorData.SetState(EAssetState::Unknown);

						TaskFunc = [this, ElementId, ParentId = ActorData.ParentId](UObject*, const FReferencer&) -> EActionResult::Type
						{
							TSharedPtr<IDatasmithActorElement> ActorElement = StaticCastSharedPtr<IDatasmithActorElement>(this->Elements[ElementId]);

							this->ParseScene(ActorElement, ParentId,
								[this](const TSharedPtr<IDatasmithActorElement>& ActorElement, FSceneGraphId ParentId) -> void
								{
									FSceneGraphId ElementId = ActorElement->GetNodeId();

									if (Elements.Contains(ElementId))
									{
										ActorDataList[ElementId].SetState(EAssetState::Unknown);
									}
								}
							);

							this->ParseScene(ActorElement, ParentId,
								[this](const TSharedPtr<IDatasmithActorElement>& ActorElement, FSceneGraphId ParentId) -> void
								{
									this->ProcessActorElement(ActorElement, ParentId);
								}
							);

							return EActionResult::Succeeded;
						};
					}

					AddToQueue(UPDATE_QUEUE, { MoveTemp(TaskFunc), FReferencer() } );

					TasksToComplete |= EWorkerTask::SetupTasks;
				}
			}
		}

		if (UpdateContext.Deletions.Num() > 0)
		{
			FActionTaskFunction TaskFunc = [this](UObject*, const FReferencer& Referencer) -> EActionResult::Type
			{
				return this->DeleteElement(Referencer.GetId());
			};

			for (DirectLink::FSceneGraphId& ElementId : UpdateContext.Deletions)
			{
				if (Elements.Contains(ElementId))
				{
					if (AssetDataList.Contains(ElementId))
					{
						if (!AssetDataList[ElementId].HasState(EAssetState::PendingDelete))
						{
							continue;
						}

						AddToQueue(DELETE_QUEUE_A, { TaskFunc, FReferencer(ElementId) } );
						TasksToComplete |= EWorkerTask::DeleteAsset;
					}
					else if (ActorDataList.Contains(ElementId))
					{
						AddToQueue(DELETE_QUEUE_C, { TaskFunc, FReferencer(ElementId) } );
						TasksToComplete |= EWorkerTask::DeleteComponent;
					}
					else
					{
						TSharedPtr<IDatasmithElement> Element = Elements[ElementId];
						UE_LOG(LogDatasmithRuntime, Error, TEXT("Element %d (%s) was not found"), ElementId, Element->GetName());
						ensure(false);
					}
				}
			}
		}

		return true;
	}


	bool FSceneImporter::DeleteData()
	{
		bool bGarbageCollect = false;

		for (TPair< FSceneGraphId, FActorData >& Pair : ActorDataList)
		{
			bGarbageCollect |= DeleteComponent(Pair.Value);
		}

		for (TPair< FSceneGraphId, FAssetData >& Entry : AssetDataList)
		{
			bGarbageCollect |= DeleteAsset(Entry.Value);
		}

		return bGarbageCollect;
	}

	EActionResult::Type FSceneImporter::DeleteElement(FSceneGraphId ElementId)
	{
		TSharedPtr<IDatasmithElement> ElementPtr;
		if (!Elements.RemoveAndCopyValue(ElementId, ElementPtr))
		{
			ensure(false);
			return EActionResult::Failed;
		}

		if (AssetDataList.Contains(ElementId))
		{
			FAssetData AssetData(DirectLink::InvalidId);
			if (!AssetDataList.RemoveAndCopyValue(ElementId, AssetData))
			{
				ensure(false);
				return EActionResult::Failed;
			}

			FString AssetPrefixedName;

			if (ElementPtr->IsA(EDatasmithElementType::Texture))
			{
				AssetPrefixedName = TexturePrefix + ElementPtr->GetName();
				int32 Index = TextureDataList.Remove(ElementId);
				if (Index == 0)
				{
					ensure(false);
					return EActionResult::Failed;
				}
			}
			else if (ElementPtr->IsA(EDatasmithElementType::BaseMaterial))
			{
				AssetPrefixedName = MaterialPrefix + ElementPtr->GetName();
			}
			else if (ElementPtr->IsA(EDatasmithElementType::StaticMesh))
			{
				AssetPrefixedName = MeshPrefix + ElementPtr->GetName();
			}

			ensure(AssetElementMapping.Contains(AssetPrefixedName));
				
			// ElementId may mismatch if new object of same name but new id was added
			if (AssetElementMapping[AssetPrefixedName] == ElementId)
			{
				AssetElementMapping.Remove(AssetPrefixedName);
			}

			return DeleteAsset(AssetData) ? EActionResult::Succeeded : EActionResult::Failed;
		}

		ensure(ActorDataList.Contains(ElementId));

		FActorData ActorData(DirectLink::InvalidId);
		if (!ActorDataList.RemoveAndCopyValue(ElementId, ActorData))
		{
			return EActionResult::Failed;
		}

		return DeleteComponent(ActorData) ? EActionResult::Succeeded : EActionResult::Failed;
	}

	bool FSceneImporter::DeleteComponent(FActorData& ActorData)
	{
		if (USceneComponent* SceneComponent = ActorData.GetObject<USceneComponent>())
		{
			if (SceneComponent->GetAttachmentRoot() == RootComponent.Get())
			{
				SceneComponent->UnregisterComponent();

				SceneComponent->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);

				if (UStaticMeshComponent* MeshComponent = Cast< UStaticMeshComponent >(SceneComponent))
				{
					MeshComponent->OverrideMaterials.Reset();
					MeshComponent->SetStaticMesh(nullptr);
				}

				SceneComponent->ClearFlags(RF_AllFlags);
				SceneComponent->SetFlags(RF_Transient);
				SceneComponent->Rename(nullptr, nullptr, REN_NonTransactional | REN_DontCreateRedirectors);
				SceneComponent->MarkPendingKill();
			}

			ActorData.Object.Reset();

			return true;
		}

		return false;
	}

	bool FSceneImporter::DeleteAsset(FAssetData& AssetData)
	{
		if (UObject* Asset = AssetData.Object.Get())
		{
			AssetData.Object.Reset();
			FAssetRegistry::UnregisterAssetData(Asset, SceneKey, AssetData.ElementId);

			return true;
		}

		return false;
	}

	bool FSceneImporter::ProcessCameraActorData(FActorData& ActorData, IDatasmithCameraActorElement* CameraElement)
	{
		if (ActorData.HasState(EAssetState::Processed))
		{
			return true;
		}
		
		// Check to see if the camera must be updated or not
		// Update if only the current actor is the only one with a valid source and the source has changed
		bool bUpdateCamera = true;

		TArray<AActor*> Actors;
		UGameplayStatics::GetAllActorsOfClass(RootComponent->GetOwner()->GetWorld(), ADatasmithRuntimeActor::StaticClass(), Actors);

		if (Actors.Num() > 1)
		{
			for (AActor* Actor : Actors)
			{
				if (Actor == RootComponent->GetOwner())
				{
					continue;
				}

				if (ADatasmithRuntimeActor* RuntimeActor = Cast<ADatasmithRuntimeActor>(Actor))
				{
					bUpdateCamera &= RuntimeActor->GetSourceName() == TEXT("None");
				}
			}
		}

		bUpdateCamera &= LastSceneGuid != SceneElement->GetSharedState()->GetGuid();

		if (bUpdateCamera)
		{
			if (APlayerController* PlayerController = UGameplayStatics::GetPlayerController(RootComponent->GetOwner()->GetWorld(), 0))
			{
				PlayerController->SetControlRotation(ActorData.WorldTransform.GetRotation().Rotator());
				if (APawn* Pawn = PlayerController->GetPawn())
				{
					Pawn->SetActorLocationAndRotation(ActorData.WorldTransform.GetLocation(), ActorData.WorldTransform.GetRotation(), false);
				}
			}
		}

#if 0
		UCineCameraComponent* CameraComponent = ActorData.GetObject<UCineCameraComponent>();

		if (CameraComponent == nullptr)
		{
			UWorld* World = RootComponent->GetOwner()->GetWorld();

			ACineCameraActor* CameraActor = Cast< ACineCameraActor >( World->SpawnActor( ACineCameraActor::StaticClass(), nullptr, nullptr ) );
#if WITH_EDITOR
			CameraActor->SetActorLabel(CameraElement->GetLabel());
#endif
			CameraComponent = CameraActor->GetCineCameraComponent();

			ActorData.Object = TStrongObjectPtr<UObject>(CameraComponent);

			CameraActor->GetRootComponent()->SetMobility(EComponentMobility::Movable);
		}
		else
		{
			CameraComponent->GetOwner()->UpdateComponentTransforms();
			CameraComponent->GetOwner()->MarkComponentsRenderStateDirty();
		}

		CameraComponent->GetOwner()->GetRootComponent()->SetRelativeTransform(ActorData.WorldTransform);

		CameraComponent->Filmback.SensorWidth = CameraElement->GetSensorWidth();
		CameraComponent->Filmback.SensorHeight = CameraElement->GetSensorWidth() / CameraElement->GetSensorAspectRatio();
		CameraComponent->LensSettings.MaxFStop = 32.0f;
		CameraComponent->CurrentFocalLength = CameraElement->GetFocalLength();
		CameraComponent->CurrentAperture = CameraElement->GetFStop();

		CameraComponent->FocusSettings.FocusMethod = CameraElement->GetEnableDepthOfField() ? ECameraFocusMethod::Manual : ECameraFocusMethod::DoNotOverride;
		CameraComponent->FocusSettings.ManualFocusDistance = CameraElement->GetFocusDistance();

		if (const IDatasmithPostProcessElement* PostProcess = CameraElement->GetPostProcess().Get())
		{
			FPostProcessSettings& PostProcessSettings = CameraComponent->PostProcessSettings;

			if ( !FMath::IsNearlyEqual( PostProcess->GetTemperature(), 6500.f ) )
			{
				PostProcessSettings.bOverride_WhiteTemp = true;
				PostProcessSettings.WhiteTemp = PostProcess->GetTemperature();
			}

			if ( PostProcess->GetVignette() > 0.f )
			{
				PostProcessSettings.bOverride_VignetteIntensity = true;
				PostProcessSettings.VignetteIntensity = PostProcess->GetVignette();
			}

			if (PostProcess->GetColorFilter() != FLinearColor::Black && PostProcess->GetColorFilter() != FLinearColor::White )
			{
				PostProcessSettings.bOverride_FilmWhitePoint = true;
				PostProcessSettings.FilmWhitePoint = PostProcess->GetColorFilter();
			}

			if ( !FMath::IsNearlyEqual( PostProcess->GetSaturation(), 1.f ) )
			{
				PostProcessSettings.bOverride_ColorSaturation = true;
				PostProcessSettings.ColorSaturation.W = PostProcess->GetSaturation();
			}

			if ( PostProcess->GetCameraISO() > 0.f || PostProcess->GetCameraShutterSpeed() > 0.f || PostProcess->GetDepthOfFieldFstop() > 0.f )
			{
				PostProcessSettings.bOverride_AutoExposureMethod = true;
				PostProcessSettings.AutoExposureMethod = EAutoExposureMethod::AEM_Manual;

				if ( PostProcess->GetCameraISO() > 0.f )
				{
					PostProcessSettings.bOverride_CameraISO = true;
					PostProcessSettings.CameraISO = PostProcess->GetCameraISO();
				}

				if ( PostProcess->GetCameraShutterSpeed() > 0.f )
				{
					PostProcessSettings.bOverride_CameraShutterSpeed = true;
					PostProcessSettings.CameraShutterSpeed = PostProcess->GetCameraShutterSpeed();
				}

				if ( PostProcess->GetDepthOfFieldFstop() > 0.f )
				{
					PostProcessSettings.bOverride_DepthOfFieldFstop = true;
					PostProcessSettings.DepthOfFieldFstop = PostProcess->GetDepthOfFieldFstop();
				}
			}
		}
#endif
		ActorData.SetState(EAssetState::Processed | EAssetState::Completed);

		return true;
	}

} // End of namespace DatasmithRuntime