// Copyright Epic Games, Inc. All Rights Reserved.

#include "MovieSceneLiveLinkCameraControllerSection.h"

#include "Evaluation/MovieScenePreAnimatedState.h"
#include "IMovieScenePlayer.h"
#include "LiveLinkCameraController.h"
#include "LiveLinkComponentController.h"
#include "LiveLinkControllerBase.h"
#include "MovieSceneExecutionToken.h"
#include "Roles/LiveLinkCameraRole.h"

struct FPreAnimatedLensFileTokenProducer : IMovieScenePreAnimatedTokenProducer
{
	virtual IMovieScenePreAnimatedTokenPtr CacheExistingState(UObject& Object) const
	{
		struct FLensFileToken : IMovieScenePreAnimatedToken
		{
			FLensFileToken(ULiveLinkCameraController* LiveLinkCameraController)
			{
				// Save the original values of these settings
				bOriginalUseDefaultLensFile = LiveLinkCameraController->LensFilePicker.bUseDefaultLensFile;
				OriginalLensFile = LiveLinkCameraController->LensFilePicker.GetLensFile();
			}

			virtual void RestoreState(UObject& Object, const UE::MovieScene::FRestoreStateParams& Params)
			{
				if (ULiveLinkCameraController* CameraController = CastChecked<ULiveLinkCameraController>(&Object))
				{
					// Restore the original values of these settings
					CameraController->LensFilePicker.bUseDefaultLensFile = bOriginalUseDefaultLensFile;
					CameraController->LensFilePicker.LensFile = OriginalLensFile.Get();
				}
			}
			
			// Original settings of the LiveLinkCameraController to save/restore
			bool bOriginalUseDefaultLensFile;
			TWeakObjectPtr<ULensFile> OriginalLensFile;
		};

		return FLensFileToken(CastChecked<ULiveLinkCameraController>(&Object));
	}
	static FMovieSceneAnimTypeID GetAnimTypeID()
	{
		return TMovieSceneAnimTypeID<FPreAnimatedLensFileTokenProducer>();
	}
};

void UMovieSceneLiveLinkCameraControllerSection::Initialize(ULiveLinkControllerBase* InLiveLinkController)
{
	// Duplicate the recorded LensFile asset and save the duplicate in this level sequence section
	if (ULiveLinkCameraController* CameraController = Cast<ULiveLinkCameraController>(InLiveLinkController))
	{
		if (ULensFile* LensFile = CameraController->LensFilePicker.GetLensFile())
		{
			const FName DuplicateName = FName(*FString::Format(TEXT("{0}_Cached"), { LensFile->GetFName().ToString() }));
			CachedLensFile = Cast<ULensFile>(StaticDuplicateObject(LensFile, this, DuplicateName, RF_AllFlags & ~RF_Transient));
		}
	}
}

void UMovieSceneLiveLinkCameraControllerSection::Begin(IMovieScenePlayer* Player, const UE::MovieScene::FEvaluationHookParams& Params) const
{
	FScopedPreAnimatedCaptureSource CaptureSource(&Player->PreAnimatedState, this, Params.SequenceID, EvalOptions.CompletionMode == EMovieSceneCompletionMode::RestoreState);

	for (TWeakObjectPtr<> BoundObject : Player->FindBoundObjects(Params.ObjectBindingID, Params.SequenceID))
	{
		if (ULiveLinkComponentController* LiveLinkComponent = Cast<ULiveLinkComponentController>(BoundObject.Get()))
		{
			// Find the LL camera controller in the component's controller map			
			if (ULiveLinkControllerBase** Controller = LiveLinkComponent->ControllerMap.Find(ULiveLinkCameraRole::StaticClass()))
			{
				if (ULiveLinkCameraController* CameraController = Cast<ULiveLinkCameraController>(*Controller))
				{
					Player->SavePreAnimatedState(*CameraController, FPreAnimatedLensFileTokenProducer::GetAnimTypeID(), FPreAnimatedLensFileTokenProducer());

					// Override the existing lens file settings of the camera controller to use the cached, duplicate lens file
					CameraController->LensFilePicker.bUseDefaultLensFile = false;
					CameraController->LensFilePicker.LensFile = CachedLensFile;
				}
			}
		}
	}
}

void UMovieSceneLiveLinkCameraControllerSection::End(IMovieScenePlayer* Player, const UE::MovieScene::FEvaluationHookParams& Params) const
{
	Player->RestorePreAnimatedState();
}
