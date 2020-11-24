// Copyright Epic Games, Inc. All Rights Reserved.

#include "MatineeCameraShake.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraAnim.h"
#include "Camera/CameraAnimInst.h"
#include "Camera/CameraComponent.h"
#include "Camera/CameraShake.h"
#include "Evaluation/MovieSceneCameraAnimTemplate.h"

UMovieSceneMatineeCameraShakeEvaluator::UMovieSceneMatineeCameraShakeEvaluator(const FObjectInitializer& ObjInit)
	: Super(ObjInit)
{
	if (HasAnyFlags(EObjectFlags::RF_ClassDefaultObject))
	{
		FMovieSceneCameraShakeEvaluatorRegistry::RegisterShakeEvaluatorBuilder(
			FMovieSceneBuildShakeEvaluator::CreateStatic(&UMovieSceneMatineeCameraShakeEvaluator::BuildMatineeShakeEvaluator));
	}
}

UMovieSceneCameraShakeEvaluator* UMovieSceneMatineeCameraShakeEvaluator::BuildMatineeShakeEvaluator(UCameraShakeBase* ShakeInstance)
{
	if (UMatineeCameraShake* MatineeShakeInstance = Cast<UMatineeCameraShake>(ShakeInstance))
	{
		return NewObject<UMovieSceneMatineeCameraShakeEvaluator>();
	}
	return nullptr;
}

bool UMovieSceneMatineeCameraShakeEvaluator::Setup(const FMovieSceneEvaluationOperand& Operand, FPersistentEvaluationData& PersistentData, IMovieScenePlayer& Player, UCameraShakeBase* ShakeInstance)
{
	UMatineeCameraShake* MatineeShakeInstance = CastChecked<UMatineeCameraShake>(ShakeInstance);

	// We use the global temp actor from the shared data (shared across all additive camera effects for this operand)
	ACameraActor* TempCameraActor = FMovieSceneMatineeCameraData::Get(Operand, PersistentData).GetTempCameraActor(Player);
	MatineeShakeInstance->SetTempCameraAnimActor(TempCameraActor);

	if (MatineeShakeInstance->AnimInst)
	{
		MatineeShakeInstance->AnimInst->SetStopAutomatically(false);
	}

	return true;
}

bool UMovieSceneMatineeCameraShakeEvaluator::Evaluate(const FMovieSceneContext& Context, const FMovieSceneEvaluationOperand& Operand, FPersistentEvaluationData& PersistentData, IMovieScenePlayer& Player, UCameraShakeBase* ShakeInstance)
{
	UMatineeCameraShake* MatineeShakeInstance = CastChecked<UMatineeCameraShake>(ShakeInstance);

	FMovieSceneMatineeCameraData& MatineeSharedData = FMovieSceneMatineeCameraData::Get(Operand, PersistentData);
	ACameraActor* TempCameraActor = MatineeSharedData.GetTempCameraActor(Player);
	
	// prepare temp camera actor by resetting it
	TempCameraActor->SetActorLocationAndRotation(FVector::ZeroVector, FRotator::ZeroRotator);

	ACameraActor const* const DefaultCamActor = GetDefault<ACameraActor>();
	if (DefaultCamActor)
	{
		TempCameraActor->GetCameraComponent()->AspectRatio = DefaultCamActor->GetCameraComponent()->AspectRatio;

		UCameraAnim* CamAnim = MatineeShakeInstance->AnimInst ? MatineeShakeInstance->AnimInst->CamAnim : nullptr;

		TempCameraActor->GetCameraComponent()->PostProcessSettings = CamAnim ? CamAnim->BasePostProcessSettings : FPostProcessSettings();
		TempCameraActor->GetCameraComponent()->PostProcessBlendWeight = CamAnim ? MatineeShakeInstance->AnimInst->CamAnim->BasePostProcessBlendWeight : 0.f;
	}

	return true;
}

