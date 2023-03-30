// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosClothAsset/ClothTrainingTool.h"
#include "CoreMinimal.h"

#include "Animation/AttributesRuntime.h"
#include "BonePose.h"
#include "Chaos/ChaosCache.h"
#include "Chaos/CacheCollection.h"
#include "ChaosCloth/ChaosClothingSimulationConfig.h"
#include "ChaosClothAsset/ClothAsset.h"
#include "ChaosClothAsset/ClothComponent.h"
#include "ChaosClothAsset/ClothComponentToolTarget.h"
#include "ChaosClothAsset/ClothCollection.h"
#include "ChaosClothAsset/ClothSimulationProxy.h"
#include "ClothingSystemRuntimeTypes.h"
#include "ComponentReregisterContext.h"
#include "ContextObjectStore.h"
#include "InteractiveToolManager.h"
#include "Internationalization/Regex.h"
#include "ModelingOperators.h"
#include "Misc/ScopedSlowTask.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "ToolTargetManager.h"
#include "UObject/SavePackage.h"

#define LOCTEXT_NAMESPACE "ClothTrainingTool"
DEFINE_LOG_CATEGORY_STATIC(LogClothTrainingTool, Log, All);

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

namespace UE::ClothTrainingTool::Private
{
	UChaosCache* GetCache(UChaosCacheCollection* CacheCollection)
	{
		static const FName CacheName = FName("SimulatedCache");
		return CacheCollection ? CacheCollection->FindOrAddCache(CacheName) : nullptr;
	}

	TArray<int32> ParseFrames(const FString& FramesString)
	{
		TArray<int32> Result;
		static const FRegexPattern AllowedCharsPattern(TEXT("^[-,0-9\\s]+$"));

		if (!FRegexMatcher(AllowedCharsPattern, FramesString).FindNext())
		{
		    UE_LOG(LogClothTrainingTool, Error, TEXT("Input contains invalid characters."));
		    return Result;
		}

		static const FRegexPattern SingleNumberPattern(TEXT("^\\s*(\\d+)\\s*$"));
		static const FRegexPattern RangePattern(TEXT("^\\s*(\\d+)\\s*-\\s*(\\d+)\\s*$"));

		TArray<FString> Segments;
	    FramesString.ParseIntoArray(Segments, TEXT(","), true);
	    for (const FString& Segment : Segments)
	    {
	    	bool bSegmentValid = false;

	    	FRegexMatcher SingleNumberMatcher(SingleNumberPattern, Segment);
	    	if (SingleNumberMatcher.FindNext())
	    	{
	    	    const int32 SingleNumber = FCString::Atoi(*SingleNumberMatcher.GetCaptureGroup(1));
	    	    Result.Add(SingleNumber);
	    	    bSegmentValid = true;
	    	}
	    	else
	    	{
	    		FRegexMatcher RangeMatcher(RangePattern, Segment);
	    		if (RangeMatcher.FindNext())
	    		{
	    		    const int32 RangeStart = FCString::Atoi(*RangeMatcher.GetCaptureGroup(1));
	    		    const int32 RangeEnd = FCString::Atoi(*RangeMatcher.GetCaptureGroup(2));

	    		    for (int32 i = RangeStart; i <= RangeEnd; ++i)
	    		    {
	    		        Result.Add(i);
	    		    }
	    		    bSegmentValid = true;
	    		}
	    	}
	    	
	    	if (!bSegmentValid)
	    	{
	    	    UE_LOG(LogClothTrainingTool, Error, TEXT("Invalid format in segment: %s"), *Segment);
	    	}
	    }

		return Result;
	}

	TArray<int32> Range(int32 End)
	{
		TArray<int32> Result;
		Result.Reserve(End);
		for (int32 Index = 0; Index < End; Index++)
		{
			Result.Add(Index);
		}
		return Result;
	}
};

class UClothTrainingTool::FGenerateClothOp : public UE::Geometry::TGenericDataOperator<FSkinnedMeshVertices>
{
public: 
	FGenerateClothOp(TObjectPtr<UChaosClothComponent> InClothComponent, FClothSimulationDataGenerationProxy* InDataGenerationProxy, TObjectPtr<UClothTrainingToolProperties> InToolProperties)
		: ClothComponent(InClothComponent)
		, DataGenerationProxy(InDataGenerationProxy)
		, ToolProperties(InToolProperties)
	{
	}

	virtual void CalculateResult(FProgressCancel* Progress) override;

private:
	void Simulate(float DeltaTime, int32 NumSteps, int32 PoseIndex, FProgressCancel* Progress, bool bDebug = false);
	void PrepareAnimationSequence();
	TArray<FTransform> GetBoneTransforms(int32 Frame) const;
	bool IsClothComponentValid() const;
	bool IsTransformsValid(const TArray<FTransform> &Transforms) const;
	bool GetSimPositions(TArray<FVector3f>& OutPositions) const;
	void AddToCache(UChaosCache* InCache, int32 Frame);
	void AddToCache(UChaosCache* InCache, int32 Frame, const TArray<FVector3f>& Positions) const;

	void BackupClothComponentState();
	void RestoreClothComponentState();

	TObjectPtr<UChaosClothComponent> ClothComponent = nullptr;
	FClothSimulationDataGenerationProxy *DataGenerationProxy = nullptr;
	TObjectPtr<UClothTrainingToolProperties> ToolProperties = nullptr;

	bool bIsSimulationSuspendedBackup = false;
	bool bTeleportBackup = false;
	bool bResetBackup = false;
	TArray<FTransform> ComponentSpaceTransformsBackup;

	inline static const FName PositionXName = TEXT("PositionX");
	inline static const FName PositionYName = TEXT("PositionY");
	inline static const FName PositionZName = TEXT("PositionZ");
};

void UClothTrainingTool::FGenerateClothOp::PrepareAnimationSequence()
{
	TObjectPtr<UAnimSequence> AnimationSequence = ToolProperties->AnimationSequence;
	if (AnimationSequence)
	{
		AnimationSequence->Interpolation = EAnimInterpolationType::Step;
	}
}

TArray<FTransform> UClothTrainingTool::FGenerateClothOp::GetBoneTransforms(int32 Frame) const
{
	TObjectPtr<UAnimSequence> AnimationSequence = ToolProperties->AnimationSequence;

	const double Time = AnimationSequence->GetTimeAtFrame(Frame);
	FAnimExtractContext ExtractionContext(Time);

	UChaosClothAsset* const ClothAsset = ClothComponent->GetClothAsset();
	const FReferenceSkeleton* const ReferenceSkeleton = ClothAsset ? &ClothAsset->GetRefSkeleton() : nullptr;
	USkeleton* const Skeleton = ClothAsset ? ClothAsset->GetSkeleton() : nullptr;
	const int32 NumBones = ReferenceSkeleton ? ReferenceSkeleton->GetNum() : 0;

	TArray<uint16> BoneIndices;
	BoneIndices.SetNumUninitialized(NumBones);
	for (int32 Index = 0; Index < NumBones; ++Index)
	{
		BoneIndices[Index] = (uint16)Index;
	}

	FBoneContainer BoneContainer;
	BoneContainer.SetUseRAWData(true);
	BoneContainer.InitializeTo(BoneIndices, UE::Anim::FCurveFilterSettings(), *Skeleton);

	FCompactPose OutPose;
	OutPose.SetBoneContainer(&BoneContainer);
	FBlendedCurve OutCurve;
	OutCurve.InitFrom(BoneContainer);
	UE::Anim::FStackAttributeContainer TempAttributes;

	FAnimationPoseData AnimationPoseData(OutPose, OutCurve, TempAttributes);
	AnimationSequence->GetAnimationPose(AnimationPoseData, ExtractionContext);

	TArray<FTransform> ComponentSpaceTransforms;
	ComponentSpaceTransforms.SetNumUninitialized(NumBones);
	for (int32 Index = 0; Index < NumBones; ++Index)
	{
		const FCompactPoseBoneIndex CompactIndex = BoneContainer.MakeCompactPoseIndex(FMeshPoseBoneIndex(Index));
		const int32 ParentIndex = ReferenceSkeleton->GetParentIndex(Index);
		ComponentSpaceTransforms[Index] = 
			ComponentSpaceTransforms.IsValidIndex(ParentIndex) && ParentIndex < Index ? 
			AnimationPoseData.GetPose()[CompactIndex] * ComponentSpaceTransforms[ParentIndex] : 
			ReferenceSkeleton->GetRefBonePose()[Index];
	}

	return ComponentSpaceTransforms;
}

void UClothTrainingTool::FGenerateClothOp::AddToCache(UChaosCache* InCache, int32 Frame)
{
	TArray<FVector3f> SimPositions;
	if (!GetSimPositions(SimPositions))
	{
		return;
	}

	AddToCache(InCache, Frame, SimPositions);
}

void UClothTrainingTool::FGenerateClothOp::AddToCache(UChaosCache* InCache, int32 Frame, const TArray<FVector3f>& Positions) const
{
	constexpr float CacheFPS = 30;
	const float Time = Frame / CacheFPS;
	FPendingFrameWrite NewFrame;
	NewFrame.Time = Time;

	const int32 NumParticles = Positions.Num();
	TArray<int32>& PendingID = NewFrame.PendingChannelsIndices;
	TArray<float> PendingPX, PendingPY, PendingPZ;
	TArray<float> PendingVX, PendingVY, PendingVZ;
	PendingID.SetNum(NumParticles);
	PendingPX.SetNum(NumParticles);
	PendingPY.SetNum(NumParticles);
	PendingPZ.SetNum(NumParticles);

	for (int32 ParticleIndex = 0; ParticleIndex < NumParticles; ++ParticleIndex)
	{
		const FVector3f& Position = Positions[ParticleIndex];
		PendingID[ParticleIndex] = ParticleIndex;
		PendingPX[ParticleIndex] = Position.X;
		PendingPY[ParticleIndex] = Position.Y;
		PendingPZ[ParticleIndex] = Position.Z;
	}

	NewFrame.PendingChannelsData.Add(PositionXName, PendingPX);
	NewFrame.PendingChannelsData.Add(PositionYName, PendingPY);
	NewFrame.PendingChannelsData.Add(PositionZName, PendingPZ);

	InCache->AddFrame_Concurrent(MoveTemp(NewFrame));
}

void UClothTrainingTool::FGenerateClothOp::CalculateResult(FProgressCancel* Progress)
{
	if (ClothComponent == nullptr || DataGenerationProxy == nullptr || !IsClothComponentValid())
	{
		return;
	}
	
	using UE::ClothTrainingTool::Private::ParseFrames;
	using UE::ClothTrainingTool::Private::Range;

	bool bCancelled = false;
	constexpr float DeltaTime = 1e-3;
	const TArray<int32> FramesToSimulate = ToolProperties->FramesToSimulate.Len() > 0
		? ParseFrames(ToolProperties->FramesToSimulate) 
		: Range(ToolProperties->AnimationSequence->GetNumberOfSampledKeys());

	const int32 NumFrames = ToolProperties->bDebug ? 1 : FramesToSimulate.Num();
	if (NumFrames == 0)
	{
		return;
	}

	PrepareAnimationSequence();
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

			const int32 FrameToSimulate = ToolProperties->bDebug ? ToolProperties->DebugFrame : FramesToSimulate[Frame];
			const TArray<FTransform> BoneTransforms = GetBoneTransforms(FrameToSimulate);
			if (!IsTransformsValid(BoneTransforms))
			{
				break;
			}

			ClothComponent->Pose(BoneTransforms);
			Simulate(ToolProperties->TimeStep, ToolProperties->NumSteps, FrameToSimulate, Progress, ToolProperties->bDebug);

			Progress->AdvanceCurrentScopeProgressBy(1.f / NumFrames);
		}
	}

	RestoreClothComponentState();

	if (!bCancelled)
	{
		UE_LOG(LogClothTrainingTool, Display, TEXT("Data generation complete."));
	}
}

void UClothTrainingTool::FGenerateClothOp::Simulate(float DeltaTime, int32 NumSteps, int32 Frame, FProgressCancel* Progress, bool bDebug)
{
	using UE::ClothTrainingTool::Private::GetCache;
	UChaosCache* Cache = ToolProperties->bDebug ? GetCache(ToolProperties->DebugCacheCollection) : GetCache(ToolProperties->CacheCollection);
	if (Cache == nullptr)
	{
		return;
	}

	ClothComponent->ForceNextUpdateTeleportAndReset();
	DataGenerationProxy->FillSimulationContext(DeltaTime);	
	DataGenerationProxy->InitializeConfigs();
	bool bCancelled = false;
	for (int32 Step = 0; Step < NumSteps; ++Step)
	{
		if (Progress)
		{
			if (Progress->Cancelled())
			{
				bCancelled = true;
				break;
			}

			DataGenerationProxy->Tick();

			// Clear any reset flags at the end of the first step
			if (Step == 0 && NumSteps > 1)
			{
				ClothComponent->ResetTeleportMode();
				DataGenerationProxy->FillSimulationContext(DeltaTime);	
			}

			if (bDebug)
			{
				DataGenerationProxy->WriteSimulationData();
				AddToCache(Cache, Step);
			}
		}
	}

	if (!bDebug && !bCancelled)
	{
		DataGenerationProxy->WriteSimulationData();
		AddToCache(Cache, Frame);
	}
}

bool UClothTrainingTool::FGenerateClothOp::GetSimPositions(TArray<FVector3f> &OutPositions) const
{
	const TMap<int32, FClothSimulData> &SimulDataMap = DataGenerationProxy->GetCurrentSimulationData_AnyThread();
	const FClothSimulData* const SimulData = SimulDataMap.Find(0);
	if (SimulDataMap.Num() > 1)
	{
		ensureMsgf(false, TEXT("Multiple cloth is not yet supported."));
		return false;
	}
	if (SimulData == nullptr)
	{
		ensureMsgf(false, TEXT("ClothSimulData is nullptr"));
		return false;
	}

	const TArray<FVector3f>& SimPositions = SimulData->Positions;
	OutPositions.SetNum(SimPositions.Num());
	for (int32 Index = 0; Index < SimPositions.Num(); ++Index)
	{
		OutPositions[Index] = FVector3f(SimulData->ComponentRelativeTransform.TransformPosition(FVector(SimPositions[Index])));
	}
	return true;
}

void UClothTrainingTool::FGenerateClothOp::BackupClothComponentState()
{
	bIsSimulationSuspendedBackup = ClothComponent->IsSimulationSuspended();
	bTeleportBackup = ClothComponent->NeedsTeleport();
	bResetBackup = ClothComponent->NeedsReset();
	ComponentSpaceTransformsBackup = ClothComponent->GetComponentSpaceTransforms();
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
	ClothComponent->Pose(ComponentSpaceTransformsBackup);
}

bool UClothTrainingTool::FGenerateClothOp::IsClothComponentValid() const
{
	if (USkinnedMeshComponent* const LeaderComponent = ClothComponent->LeaderPoseComponent.Get())
	{
		UE_LOG(LogClothTrainingTool, Error, TEXT("Leader pose component is not supported yet."));
		return false;
	}
	else
	{
		return true;
	}
}

bool UClothTrainingTool::FGenerateClothOp::IsTransformsValid(const TArray<FTransform>& Transforms) const
{
	if (Transforms.Num() != ComponentSpaceTransformsBackup.Num())
	{
		UE_LOG(LogClothTrainingTool, Error, TEXT("The number of bones in the AnimSequence does not match the number of bones in the ClothAsset. Try an AnimSequence with the same skeleton as the ClothAsset."));
		return false;
	}
	else
	{
		return true;
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
	ToolProperties->RestoreProperties(this);

	ActionProperties = NewObject<UClothTrainingToolActionProperties>(this);
	ActionProperties->ParentTool = this;
	AddToolPropertySource(ActionProperties);
}

void UClothTrainingTool::RunTraining()
{
	if (ClothComponent == nullptr)
	{
		return;
	}


	const FText DefaultMessage(LOCTEXT("ClothTrainingMessage", "Generate training data..."));
	
	using FTaskType = UE::Geometry::TModelingOpTask<FGenerateClothOp>;
	using FExecuterType = UE::Geometry::FAsyncTaskExecuterWithProgressCancel<FTaskType>;
	using UE::ClothTrainingTool::Private::GetCache;

	if (!DataGenerationProxy.IsValid())
	{
		DataGenerationProxy = MakeUnique<FClothSimulationDataGenerationProxy>(*ClothComponent);
	}
	UChaosCacheCollection* const CacheCollection = ToolProperties->bDebug ? ToolProperties->DebugCacheCollection : ToolProperties->CacheCollection;
	if (CacheCollection == nullptr)
	{
		if (ToolProperties->bDebug)
		{
			UE_LOG(LogClothTrainingTool, Error, TEXT("Debug cache is None. Please select a valid cache for output."));
		}
		else
		{
			UE_LOG(LogClothTrainingTool, Error, TEXT("Generated Cache is None. Please select a valid cache for output."));
		}
		return;
	}
	UChaosCache* const Cache = GetCache(CacheCollection);

	FCacheUserToken CacheUserToken = Cache->BeginRecord(ClothComponent, FGuid(), FTransform::Identity);

	TUniquePtr<FGenerateClothOp> NewOp = MakeUnique<FGenerateClothOp>(ClothComponent, DataGenerationProxy.Get(), ToolProperties);

	TUniquePtr<FExecuterType> BackgroundTaskExecuter = MakeUnique<FExecuterType>(MoveTemp(NewOp));
	BackgroundTaskExecuter->StartBackgroundTask();

	FScopedSlowTask SlowTask(1, DefaultMessage);
	SlowTask.MakeDialog(true);

	bool bSuccess = false;
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
		const bool bSaveSucced = UPackage::SavePackage(Package, ToolProperties->CacheCollection, *SavePath, SaveArgs);
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

void UClothTrainingTool::Shutdown(EToolShutdownType ShutdownType)
{
	Super::Shutdown(ShutdownType);
	ToolProperties->SaveProperties(this);
}

#undef LOCTEXT_NAMESPACE
