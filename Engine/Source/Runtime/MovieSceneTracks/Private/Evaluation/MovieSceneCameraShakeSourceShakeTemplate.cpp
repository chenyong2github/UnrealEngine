// Copyright Epic Games, Inc. All Rights Reserved.

#include "Evaluation/MovieSceneCameraShakeSourceShakeTemplate.h"
#include "Evaluation/MovieSceneEvaluation.h"
#include "Camera/CameraShakeSourceComponent.h"

#if WITH_EDITOR

#include "LevelEditorViewport.h"
#include "Camera/CameraModifier_CameraShake.h"

#endif

#if WITH_EDITOR

/**
 * A class that owns a gameplay camera shake manager, so that we can us it to preview shakes in editor.
 */
class FCameraShakePreviewer : public FGCObject
{
public:
	FCameraShakePreviewer();
	~FCameraShakePreviewer();

	void RegisterViewModifier();
	void UnRegisterViewModifier();
	void Update(float DeltaTime, bool bIsPlaying);

	UCameraModifier_CameraShake* GetCameraShake() { return PreviewCameraShake; }

private:
	// FGCObject interface
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override { Collector.AddReferencedObject(PreviewCameraShake); }
	virtual FString GetReferencerName() const override { return TEXT("SCameraShakePreviewer"); }

private:
	void OnModifyView(FMinimalViewInfo& InOutPOV);
	void OnLevelViewportClientListChanged();

private:
	UCameraModifier_CameraShake* PreviewCameraShake;
	TArray<FLevelEditorViewportClient*> RegisteredViewportClients;

	TOptional<float> LastDeltaTime;
	FVector LastLocationModifier;
	FRotator LastRotationModifier;
	float LastFOVModifier;
};

FCameraShakePreviewer::FCameraShakePreviewer()
	: PreviewCameraShake(NewObject<UCameraModifier_CameraShake>())
	, LastLocationModifier(FVector::ZeroVector)
	, LastRotationModifier(FRotator::ZeroRotator)
	, LastFOVModifier(0.f)
{
}

FCameraShakePreviewer::~FCameraShakePreviewer()
{
	PreviewCameraShake = nullptr;
}

void FCameraShakePreviewer::Update(float DeltaTime, bool bIsPlaying)
{
	LastDeltaTime = DeltaTime;

	if (!bIsPlaying)
	{
		LastLocationModifier = FVector::ZeroVector;
		LastRotationModifier = FRotator::ZeroRotator;
		LastFOVModifier = 0.f;
	}
}

void FCameraShakePreviewer::OnModifyView(FMinimalViewInfo& InOutPOV)
{
	const float DeltaTime = LastDeltaTime.Get(-1.f);
	if (DeltaTime > 0.f)
	{
		FMinimalViewInfo InPOV(InOutPOV);
		PreviewCameraShake->ModifyCamera(DeltaTime, InOutPOV);

		LastLocationModifier = InOutPOV.Location - InPOV.Location;
		LastRotationModifier = InOutPOV.Rotation - InPOV.Rotation;
		LastFOVModifier = InOutPOV.FOV - InPOV.FOV;

		LastDeltaTime.Reset();
	}
	else
	{
		InOutPOV.Location += LastLocationModifier;
		InOutPOV.Rotation += LastRotationModifier;
		InOutPOV.FOV += LastFOVModifier;
	}
}

void FCameraShakePreviewer::RegisterViewModifier()
{
	if (GEditor == nullptr)
	{
		return;
	}

	// Register our view modifier on all appropriate viewports, and remember which viewports we did that on.
	// We will later make sure to unregister on the same list, except for any viewport that somehow disappeared since,
	// which we will be notified about with the OnLevelViewportClientListChanged event.
	RegisteredViewportClients.Reset();
	for (FLevelEditorViewportClient* LevelVC : GEditor->GetLevelViewportClients())
	{		
		if (LevelVC && LevelVC->AllowsCinematicControl() && LevelVC->GetViewMode() != VMI_Unknown)
		{
			RegisteredViewportClients.Add(LevelVC);
			LevelVC->ViewModifiers.AddRaw(this, &FCameraShakePreviewer::OnModifyView);
		}
	}

	GEditor->OnLevelViewportClientListChanged().AddRaw(this, &FCameraShakePreviewer::OnLevelViewportClientListChanged);
}

void FCameraShakePreviewer::UnRegisterViewModifier()
{
	GEditor->OnLevelViewportClientListChanged().RemoveAll(this);

	for (FLevelEditorViewportClient* ViewportClient : RegisteredViewportClients)
	{
		ViewportClient->ViewModifiers.RemoveAll(this);
	}
}

void FCameraShakePreviewer::OnLevelViewportClientListChanged()
{
	if (GEditor != nullptr)
	{
		// If any viewports were removed while we were playing, simply get rid of them from our list of
		// registered viewports.
		TSet<FLevelEditorViewportClient*> PreviousViewportClients(RegisteredViewportClients);
		TSet<FLevelEditorViewportClient*> NewViewportClients(GEditor->GetLevelViewportClients());
		RegisteredViewportClients = PreviousViewportClients.Intersect(NewViewportClients).Array();
	}
}

#endif

struct FCameraShakeSourceShakeSectionInstanceData : IPersistentEvaluationData
{
	bool bStarted = false;

#if WITH_EDITOR
	FCameraShakePreviewer Previewer;
#endif
};

struct FPreAnimatedCameraShakeSourceShakeTokenProducer : IMovieScenePreAnimatedGlobalTokenProducer
{
	FMovieSceneEvaluationOperand Operand;

	FPreAnimatedCameraShakeSourceShakeTokenProducer(FMovieSceneEvaluationOperand InOperand)
		: Operand(InOperand)
	{
	}

	virtual IMovieScenePreAnimatedGlobalTokenPtr CacheExistingState() const override
	{
		struct FRestoreToken : IMovieScenePreAnimatedGlobalToken
		{
			FMovieSceneEvaluationOperand Operand;

			FRestoreToken(FMovieSceneEvaluationOperand InOperand) : Operand(InOperand) {}

			virtual void RestoreState(IMovieScenePlayer& Player) override
			{
				for (TWeakObjectPtr<> BoundObject : Player.FindBoundObjects(Operand))
				{
					if (BoundObject.IsValid())
					{
						if (UCameraShakeSourceComponent* ShakeSourceComponent = CastChecked<UCameraShakeSourceComponent>(BoundObject.Get()))
						{
							ShakeSourceComponent->StopAllCameraShakes();
						}
					}
				}
			}
		};

		return FRestoreToken(Operand);
	}
};

struct FCameraShakeSourceShakeExecutionToken : IMovieSceneExecutionToken
{
	const FMovieSceneCameraShakeSectionData& SourceData;

	FCameraShakeSourceShakeExecutionToken(const FMovieSceneCameraShakeSectionData& InSourceData)
		: SourceData(InSourceData)
	{
	}

	virtual void Execute(const FMovieSceneContext& Context, const FMovieSceneEvaluationOperand& Operand, FPersistentEvaluationData& PersistentData, IMovieScenePlayer& Player) override
	{
#if WITH_EDITOR
		FCameraShakeSourceShakeSectionInstanceData& InstanceData = PersistentData.GetSectionData<FCameraShakeSourceShakeSectionInstanceData>();
		UCameraModifier_CameraShake* const PreviewCameraShake = InstanceData.Previewer.GetCameraShake();
#endif

		for (TWeakObjectPtr<> BoundObject : Player.FindBoundObjects(Operand))
		{
			if (BoundObject.IsValid())
			{
				if (UCameraShakeSourceComponent* ShakeSourceComponent = CastChecked<UCameraShakeSourceComponent>(BoundObject.Get()))
				{
					static FMovieSceneAnimTypeID ShakeTypeID = TMovieSceneAnimTypeID<FCameraShakeSourceShakeExecutionToken, 0>();
					Player.SavePreAnimatedState(ShakeTypeID, FPreAnimatedCameraShakeSourceShakeTokenProducer(Operand));

					if (SourceData.ShakeClass)
					{
						ShakeSourceComponent->PlayCameraShake(SourceData.ShakeClass);

#if WITH_EDITOR
						FAddCameraShakeParams Params;
						Params.SourceComponent = ShakeSourceComponent;
						PreviewCameraShake->AddCameraShake(SourceData.ShakeClass, Params);
#endif
					}
					else
					{
						ShakeSourceComponent->Play();

#if WITH_EDITOR
						if (ShakeSourceComponent->CameraShake.Get() != nullptr)
						{
							FAddCameraShakeParams Params;
							Params.SourceComponent = ShakeSourceComponent;
							PreviewCameraShake->AddCameraShake(ShakeSourceComponent->CameraShake, Params);
						}
#endif
					}
				}
			}
		}
	}
};

FMovieSceneCameraShakeSourceShakeSectionTemplate::FMovieSceneCameraShakeSourceShakeSectionTemplate()
{
}

FMovieSceneCameraShakeSourceShakeSectionTemplate::FMovieSceneCameraShakeSourceShakeSectionTemplate(const UMovieSceneCameraShakeSourceShakeSection& Section)
	: SourceData(Section.ShakeData)
	, SectionStartTime(Section.HasStartFrame() ? Section.GetInclusiveStartFrame() : 0)
{
}

void FMovieSceneCameraShakeSourceShakeSectionTemplate::SetupOverrides()
{
	EnableOverrides(RequiresSetupFlag | RequiresTearDownFlag);
}

void FMovieSceneCameraShakeSourceShakeSectionTemplate::Setup(FPersistentEvaluationData& PersistentData, IMovieScenePlayer& Player) const
{
	FCameraShakeSourceShakeSectionInstanceData& InstanceData = PersistentData.AddSectionData<FCameraShakeSourceShakeSectionInstanceData>();
	InstanceData.bStarted = false;

#if WITH_EDITOR
	InstanceData.Previewer.RegisterViewModifier();
#endif
}

void FMovieSceneCameraShakeSourceShakeSectionTemplate::Evaluate(const FMovieSceneEvaluationOperand& Operand, const FMovieSceneContext& Context, const FPersistentEvaluationData& PersistentData, FMovieSceneExecutionTokens& ExecutionTokens) const
{
	FCameraShakeSourceShakeSectionInstanceData& InstanceData = PersistentData.GetSectionData<FCameraShakeSourceShakeSectionInstanceData>();
	if (!InstanceData.bStarted)
	{
		ExecutionTokens.Add(FCameraShakeSourceShakeExecutionToken(SourceData));
		InstanceData.bStarted = true;
	}

#if WITH_EDITOR
	const float DeltaTime = Context.GetFrameRate().AsSeconds(Context.GetDelta());
	const bool bIsPlaying = Context.GetStatus() == EMovieScenePlayerStatus::Playing;
	InstanceData.Previewer.Update(DeltaTime, bIsPlaying);
#endif
}

void FMovieSceneCameraShakeSourceShakeSectionTemplate::TearDown(FPersistentEvaluationData& PersistentData, IMovieScenePlayer& Player) const
{
	FCameraShakeSourceShakeSectionInstanceData& InstanceData = PersistentData.GetSectionData<FCameraShakeSourceShakeSectionInstanceData>();

#if WITH_EDITOR
	InstanceData.Previewer.UnRegisterViewModifier();
#endif
}
