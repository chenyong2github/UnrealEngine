// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosClothAsset/ClothTrainingTool.h"
#include "CoreMinimal.h"
#include "ChaosCloth/ChaosClothingSimulationConfig.h"
#include "ChaosClothAsset/ClothComponent.h"
#include "ChaosClothAsset/ClothComponentToolTarget.h"
#include "ChaosClothAsset/ClothCollection.h"
#include "ChaosClothAsset/ClothSimulationProxy.h"
#include "ClothingSystemRuntimeTypes.h"
#include "Chaos/ChaosCache.h"
#include "Chaos/CacheCollection.h"
#include "ComponentReregisterContext.h"
#include "ModelingOperators.h"
#include "Misc/ScopedSlowTask.h"
#include "ContextObjectStore.h"
#include "InteractiveToolManager.h"
#include "ToolTargetManager.h"
#include "UObject/SavePackage.h"

#define LOCTEXT_NAMESPACE "ClothTrainingTool"

class UClothTrainingTool::FClothSimulationDataGenerationProxy : public UE::Chaos::ClothAsset::FClothSimulationProxy
{
public:
	explicit FClothSimulationDataGenerationProxy(const UChaosClothComponent& InClothComponent);
	~FClothSimulationDataGenerationProxy();

	using UE::Chaos::ClothAsset::FClothSimulationProxy::Tick;
	using UE::Chaos::ClothAsset::FClothSimulationProxy::FillSimulationContext;
	using UE::Chaos::ClothAsset::FClothSimulationProxy::InitializeConfigs;
	using UE::Chaos::ClothAsset::FClothSimulationProxy::WriteSimulationData;
};

UClothTrainingTool::FClothSimulationDataGenerationProxy::FClothSimulationDataGenerationProxy(const UChaosClothComponent& InClothComponent)
	: FClothSimulationProxy(InClothComponent)
{	
}

UClothTrainingTool::FClothSimulationDataGenerationProxy::~FClothSimulationDataGenerationProxy()
{
}

class UClothTrainingTool::FGenerateClothOp : public UE::Geometry::TGenericDataOperator<FSkinnedMeshVertices>
{
public: 
	FGenerateClothOp(TObjectPtr<UAnimSequence> InAnimSequence, TObjectPtr<UChaosCache> InCache, TObjectPtr<UChaosClothComponent> InClothComponent, FClothSimulationDataGenerationProxy* InDataGenerationProxy)
		: AnimSequence(InAnimSequence)
		, Cache(InCache)
		, ClothComponent(InClothComponent)
		, DataGenerationProxy(InDataGenerationProxy)
	{
	}

	virtual void CalculateResult(FProgressCancel* Progress) override;

private:
	void Simulate(float DeltaTime, int32 NumSteps);
	void BackupClothComponentState();
	void RestoreClothComponentState();
	const TObjectPtr<UAnimSequence> AnimSequence;
	TObjectPtr<UChaosCache> Cache;
	TObjectPtr<UChaosClothComponent> ClothComponent;
	FClothSimulationDataGenerationProxy *DataGenerationProxy = nullptr;

	bool bIsSimulationSuspendedBackup = false;
	bool bTeleportBackup = false;
	bool bResetBackup = false;
};

void UClothTrainingTool::FGenerateClothOp::CalculateResult(FProgressCancel* Progress)
{
	if (ClothComponent == nullptr || Cache == nullptr || DataGenerationProxy == nullptr)
	{
		return;
	}

	static const FName PositionXName = TEXT("PositionX");
	static const FName PositionYName = TEXT("PositionY");
	static const FName PositionZName = TEXT("PositionZ");
	
	bool bCancelled = false;
	// TODO: change this part to data from AnimSequence
	constexpr float DeltaTime = 1e-3;
	constexpr float CacheFPS = 30;
	const int32 NumFrames = 10;

	BackupClothComponentState();

	ClothComponent->ResumeSimulation();
	for (int32 Frame = 0; Frame < NumFrames; Frame++)
	{
		if (Progress)
		{
			if (Progress->Cancelled())
			{
				bCancelled = true;
				break;
			}

			FPendingFrameWrite NewFrame;
			const float TotalTime = Frame * DeltaTime * 5;
			Simulate(DeltaTime, (Frame + 1) * 5);

			const TMap<int32, FClothSimulData> &SimulDataMap = DataGenerationProxy->GetCurrentSimulationData_AnyThread();
			const FClothSimulData* const SimulData = SimulDataMap.Find(0);
			if (SimulDataMap.Num() > 1)
			{
				ensureMsgf(false, TEXT("Only support single cloth for now."));
				continue;
			}
			if (SimulData == nullptr)
			{
				ensureMsgf(false, TEXT("ClothSimulData is nullptr"));
				continue;
			}

			const TArray<FVector3f>& SimPositions = SimulData->Positions;
			const int32 NumParticles = SimPositions.Num();

			const float Time = Frame / CacheFPS;
			NewFrame.Time = Time;

			TArray<int32>& PendingID = NewFrame.PendingChannelsIndices;
			TArray<float> PendingPX, PendingPY, PendingPZ;
			TArray<float> PendingVX, PendingVY, PendingVZ;
			PendingID.SetNum(NumParticles);
			PendingPX.SetNum(NumParticles);
			PendingPY.SetNum(NumParticles);
			PendingPZ.SetNum(NumParticles);

			for (int32 ParticleIndex = 0; ParticleIndex < NumParticles; ++ParticleIndex)
			{
				const FVector3f& Position = SimPositions[ParticleIndex];
				PendingID[ParticleIndex] = ParticleIndex;
				PendingPX[ParticleIndex] = Position.X;
				PendingPY[ParticleIndex] = Position.Y;
				PendingPZ[ParticleIndex] = Position.Z + Frame;
			}

			NewFrame.PendingChannelsData.Add(PositionXName, PendingPX);
			NewFrame.PendingChannelsData.Add(PositionYName, PendingPY);
			NewFrame.PendingChannelsData.Add(PositionZName, PendingPZ);

			Cache->AddFrame_Concurrent(MoveTemp(NewFrame));
			Progress->AdvanceCurrentScopeProgressBy(1.f / NumFrames);
		}
	}
	RestoreClothComponentState();

	if (!bCancelled)
	{
		UE_LOG(LogTemp, Display, TEXT("Data generation complete."));
	}
}

void UClothTrainingTool::FGenerateClothOp::Simulate(float DeltaTime, int32 NumSteps)
{
	ClothComponent->ForceNextUpdateTeleportAndReset();
	DataGenerationProxy->FillSimulationContext(DeltaTime);	
	DataGenerationProxy->InitializeConfigs();
	for (int32 Step = 0; Step < NumSteps; ++Step)
	{
		DataGenerationProxy->Tick();

		// Clear any reset flags at the end of the first step
		if (Step == 0 && NumSteps > 1)
		{
			ClothComponent->ResetTeleportMode();
			DataGenerationProxy->FillSimulationContext(DeltaTime);	
		}
	}
	DataGenerationProxy->WriteSimulationData();
}

void UClothTrainingTool::FGenerateClothOp::BackupClothComponentState()
{
	bIsSimulationSuspendedBackup = ClothComponent->IsSimulationSuspended();
	bTeleportBackup = ClothComponent->NeedsTeleport();
	bResetBackup = ClothComponent->NeedsReset();
}

void UClothTrainingTool::FGenerateClothOp::RestoreClothComponentState()
{
	bIsSimulationSuspendedBackup ? ClothComponent->SuspendSimulation() : ClothComponent->ResumeSimulation();
	if (bResetBackup)
	{
		ClothComponent->ForceNextUpdateTeleportAndReset();
	}
	else if (bTeleportBackup)
	{
		ClothComponent->ForceNextUpdateTeleport();
	}
	else
	{
		ClothComponent->ResetTeleportMode();
	}
}


// ------------------- Properties -------------------

void UClothTrainingToolActionProperties::PostAction(EClothTrainingToolActions Action)
{
	if (ParentTool.IsValid())
	{
		ParentTool->RequestAction(Action);
	}
}


// ------------------- Builder -------------------

const FToolTargetTypeRequirements& UClothTrainingToolBuilder::GetTargetRequirements() const
{
	static FToolTargetTypeRequirements TypeRequirements({ 
		UPrimitiveComponentBackedTarget::StaticClass(),
		UClothAssetBackedTarget::StaticClass()
		});
	return TypeRequirements;
}

bool UClothTrainingToolBuilder::CanBuildTool(const FToolBuilderState& SceneState) const
{
	return (SceneState.TargetManager->CountSelectedAndTargetable(SceneState, GetTargetRequirements()) == 1);
}

UInteractiveTool* UClothTrainingToolBuilder::BuildTool(const FToolBuilderState& SceneState) const
{
	UClothTrainingTool* NewTool = NewObject<UClothTrainingTool>();
	
	UToolTarget* Target = SceneState.TargetManager->BuildFirstSelectedTargetable(SceneState, GetTargetRequirements());
	NewTool->SetTarget(Target);

	return NewTool;
}

// ------------------- Tool -------------------
UClothTrainingTool::UClothTrainingTool() = default;

UClothTrainingTool::UClothTrainingTool(FVTableHelper& Helper)
	: Super(Helper)
{
}

UClothTrainingTool::~UClothTrainingTool() = default;


void UClothTrainingTool::Setup()
{
	UInteractiveTool::Setup();

	if (UClothComponentToolTarget* ClothComponentTarget = Cast<UClothComponentToolTarget>(Target))
	{
		UChaosClothComponent* TargetClothComponent = ClothComponentTarget->GetClothComponent();
		ClothComponent = TargetClothComponent;
	}

	ToolProperties = NewObject<UClothTrainingToolProperties>(this);
	AddToolPropertySource(ToolProperties);

	ActionProperties = NewObject<UClothTrainingToolActionProperties>(this);
	ActionProperties->ParentTool = this;
	AddToolPropertySource(ActionProperties);
}

void UClothTrainingTool::RunTraining()
{
	if (ClothComponent == nullptr || ToolProperties->CacheCollection == nullptr)
	{
		return;
	}

	static const FName CacheName = FName("SimulatedCache");
	UChaosCacheCollection* CacheCollection = ToolProperties->CacheCollection;
	UChaosCache* Cache = CacheCollection->FindOrAddCache(CacheName);

	const FText DefaultMessage(LOCTEXT("ClothTrainingMessage", "Generate training data..."));
	
	using FTaskType = UE::Geometry::TModelingOpTask<FGenerateClothOp>;
	using FExecuterType = UE::Geometry::FAsyncTaskExecuterWithProgressCancel<FTaskType>;

	if (!DataGenerationProxy.IsValid())
	{
		DataGenerationProxy = MakeUnique<FClothSimulationDataGenerationProxy>(*ClothComponent);
	}

	TUniquePtr<FGenerateClothOp> NewOp = MakeUnique<FGenerateClothOp>(ToolProperties->AnimationSequence, Cache, ClothComponent, DataGenerationProxy.Get());

	TUniquePtr<FExecuterType> BackgroundTaskExecuter = MakeUnique<FExecuterType>(MoveTemp(NewOp));
	BackgroundTaskExecuter->StartBackgroundTask();

	FScopedSlowTask SlowTask(1, DefaultMessage);
	SlowTask.MakeDialog(true);

	bool bSuccess = false;
	FCacheUserToken CacheUserToken = Cache->BeginRecord(ClothComponent, FGuid(), FTransform::Identity);
	while (true)
	{
		if (SlowTask.ShouldCancel())
		{
			// Release ownership to the TDeleterTask that is spawned by CancelAndDelete()
			BackgroundTaskExecuter.Release()->CancelAndDelete();
			break;
		}
		if (BackgroundTaskExecuter->IsDone())
		{
			bSuccess = !BackgroundTaskExecuter->GetTask().IsAborted();
			break;
		}
		FPlatformProcess::Sleep(.2); // SlowTask::ShouldCancel will throttle any updates faster than .2 seconds
		float ProgressFrac;
		FText ProgressMessage;
		bool bMadeProgress = BackgroundTaskExecuter->PollProgress(ProgressFrac, ProgressMessage);
		if (bMadeProgress)
		{
			// SlowTask expects progress to be reported before it happens; we work around this by directly updating the progress amount
			SlowTask.CompletedWork = ProgressFrac;
			SlowTask.EnterProgressFrame(0, ProgressMessage);
		}
		else
		{
			SlowTask.TickProgress(); // Still tick the UI when we don't get new progress frames
		}
	}

	ClothComponent->SuspendSimulation();
	Cache->bCompressChannels = true;
	Cache->EndRecord(CacheUserToken);

	if (bSuccess)
	{
		UPackage* const Package = CacheCollection->GetPackage();

		const FString SavePath = Package->GetFName().ToString();
		UE_LOG(LogTemp, Display, TEXT("Save to %s"), *SavePath);
		if (Package == nullptr)
		{
			UE_LOG(LogTemp, Error, TEXT("Failed to get package for %s"), *SavePath);
			return;
		}
		FSavePackageArgs SaveArgs;
		SaveArgs.SaveFlags = SAVE_NoError;
		const bool bSaveSucced = UPackage::SavePackage(Package, CacheCollection, *SavePath, SaveArgs);
		if (!bSaveSucced)
		{
			UE_LOG(LogTemp, Display, TEXT("Failed to save cache collection"));
		}
	}

}


void UClothTrainingTool::OnTick(float DeltaTime)
{
	if (PendingAction != EClothTrainingToolActions::NoAction)
	{
		if (PendingAction == EClothTrainingToolActions::Train)
		{
			RunTraining();
		}
		PendingAction = EClothTrainingToolActions::NoAction;
	}
}


void UClothTrainingTool::RequestAction(EClothTrainingToolActions ActionType)
{
	if (PendingAction != EClothTrainingToolActions::NoAction)
	{
		return;
	}
	PendingAction = ActionType;
}

#undef LOCTEXT_NAMESPACE
