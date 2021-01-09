// Copyright Epic Games, Inc. All Rights Reserved.

#include "CameraAnimToTemplateSequenceConverter.h"
#include "AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraAnim.h"
#include "CameraAnimationSequence.h"
#include "ContentBrowserMenuContexts.h"
#include "EntitySystem/Interrogation/MovieSceneInterrogationLinker.h"
#include "EntitySystem/Interrogation/MovieSceneInterrogatedPropertyInstantiator.h"
#include "Framework/Docking/TabManager.h"
#include "Framework/Notifications/NotificationManager.h"
#include "GenericPlatform/GenericPlatformMisc.h"
#include "IAssetRegistry.h"
#include "IAssetTools.h"
#include "LevelEditorViewport.h"
#include "Matinee/InterpData.h"
#include "Matinee/InterpGroupCamera.h"
#include "Matinee/MatineeActorCameraAnim.h"
#include "MatineeConverter.h"
#include "Misc/MessageDialog.h"
#include "Sections/MovieSceneSpawnSection.h"
#include "ToolMenuContext.h"
#include "Tracks/MovieScene3DTransformTrack.h"
#include "Tracks/MovieSceneFloatTrack.h"
#include "Tracks/MovieSceneSpawnTrack.h"
#include "Widgets/Notifications/SNotificationList.h"

#define LOCTEXT_NAMESPACE "CameraAnimToTemplateSequenceConverter"

FCameraAnimToTemplateSequenceConverter::FCameraAnimToTemplateSequenceConverter(const FMatineeConverter* InMatineeConverter)
	: MatineeConverter(InMatineeConverter)
	, PreviewMatineeActor(nullptr)
{
}

void FCameraAnimToTemplateSequenceConverter::ConvertCameraAnim(const FToolMenuContext& MenuContext)
{
	using namespace UE::MovieScene;

	UContentBrowserAssetContextMenuContext* Context = MenuContext.FindContext<UContentBrowserAssetContextMenuContext>();
	if (Context == nullptr)
	{
		return;
	}

	// Get the assets to convert.
	TArray<UCameraAnim*> CameraAnimsToConvert;
	for (TWeakObjectPtr<UObject> SelectedObject : Context->SelectedObjects)
	{
		if (UCameraAnim* CameraAnimToConvert = CastChecked<UCameraAnim>(SelectedObject.Get(), ECastCheckedType::NullAllowed))
		{
			CameraAnimsToConvert.Add(CameraAnimToConvert);
		}
	}
	if (CameraAnimsToConvert.Num() == 0)
	{
		return;
	}

	// Find the factory class.
	UFactory* CameraAnimationSequenceFactoryNew = FindFactoryForClass(UCameraAnimationSequence::StaticClass());
	ensure(CameraAnimationSequenceFactoryNew != nullptr);

	// Convert all selected camera anims.
	int32 NumWarnings = 0;
	TOptional<bool> bAutoReuseExistingAsset;
	bool bAssetCreated = false;
	bool bConvertSuccess = false;
	IAssetTools& AssetTools = FModuleManager::GetModuleChecked<FAssetToolsModule>("AssetTools").Get();
	IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
	for (UCameraAnim* CameraAnimToConvert : CameraAnimsToConvert)
	{
		bConvertSuccess = ConvertSingleCameraAnimToTemplateSequence(
				CameraAnimToConvert, AssetTools, AssetRegistry, CameraAnimationSequenceFactoryNew, true, bAutoReuseExistingAsset, NumWarnings, bAssetCreated)
			|| bConvertSuccess;
	}

	if (bConvertSuccess)
	{
		FText NotificationText = FText::Format(
				LOCTEXT("CameraAnim_ConvertToSequence_Notification", "Converted {0} assets with {1} warnings"),
				FText::AsNumber(CameraAnimsToConvert.Num()), FText::AsNumber(NumWarnings));
		FNotificationInfo NotificationInfo(NotificationText);
		NotificationInfo.ExpireDuration = 5.f;
		NotificationInfo.Hyperlink = FSimpleDelegate::CreateStatic([](){ FGlobalTabmanager::Get()->TryInvokeTab(FName("OutputLog")); });
		NotificationInfo.HyperlinkText = LOCTEXT("ShowMessageLogHyperlink", "Show Output Log");
		FSlateNotificationManager::Get().AddNotification(NotificationInfo);
	}
}

UObject* FCameraAnimToTemplateSequenceConverter::ConvertCameraAnim(IAssetTools& AssetTools, IAssetRegistry& AssetRegistry, UFactory* CameraAnimationSequenceFactoryNew, UCameraAnim* CameraAnim, TOptional<bool>& bAutoReuseExistingAsset, int32& NumWarnings, bool& bAssetCreated)
{
	using namespace UE::MovieScene;

	if (!ensure(CameraAnim))
	{
		return nullptr;
	}

	ensure(CameraAnimationSequenceFactoryNew != nullptr);
	UObject* NewAsset = ConvertSingleCameraAnimToTemplateSequence(
			CameraAnim, AssetTools, AssetRegistry, CameraAnimationSequenceFactoryNew, false, bAutoReuseExistingAsset, NumWarnings, bAssetCreated);
	return NewAsset;
}

UObject* FCameraAnimToTemplateSequenceConverter::ConvertSingleCameraAnimToTemplateSequence(UCameraAnim* CameraAnimToConvert, IAssetTools& AssetTools, IAssetRegistry& AssetRegistry, UFactory* CameraAnimationSequenceFactoryNew, bool bPromptCreateAsset, TOptional<bool>& bAutoReuseExistingAsset, int32& NumWarnings, bool& bAssetCreated)
{
	UPackage* AssetPackage = CameraAnimToConvert->GetOutermost();

	// See if the converted asset already exists.
	UObject* ExistingAsset = nullptr;
	TArray<FAssetData> ExistingCameraAnimSequences;
	FString NewCameraAnimSequencePackageName = AssetPackage->GetName() + FString("Sequence");
	AssetRegistry.GetAssetsByPackageName(FName(NewCameraAnimSequencePackageName), ExistingCameraAnimSequences, false);
	if (ExistingCameraAnimSequences.Num() > 0)
	{
		// Find if there's any camera animation sequence in there. If there is, we can be somewhat confident
		// that it's going to be what we want.
		for (const FAssetData& ExistingCameraAnimSequence : ExistingCameraAnimSequences)
		{
			if (ExistingCameraAnimSequence.AssetClass == UCameraAnimationSequence::StaticClass()->GetFName())
			{
				ExistingAsset = ExistingCameraAnimSequence.GetAsset();
				if (ExistingAsset != nullptr)
				{
					break;
				}
			}
		}
	}
	if (ExistingAsset != nullptr)
	{
		bool bUseExistingAsset = true;

		if (!bAutoReuseExistingAsset.IsSet())
		{
			// Ask whether we should reuse the existing asset or make a new one.
			const EAppReturnType::Type DialogAnswer = FMessageDialog::Open(
					EAppMsgType::YesNoYesAllNoAll, 
					FText::Format(LOCTEXT("AskReuseExistingAsset", "Found camera animation sequence {0} while trying to convert Matinee camera anim {1}. Do you want to re-use the existing camera animation sequence and skip the conversion process?"), FText::FromName(ExistingAsset->GetPackage()->GetFName()), FText::FromName(CameraAnimToConvert->GetPackage()->GetFName())));
			switch (DialogAnswer)
			{
				case EAppReturnType::YesAll:
				default:
					bAutoReuseExistingAsset = true;
					bUseExistingAsset = true;
					break;
				case EAppReturnType::Yes:
					bUseExistingAsset = true;
					break;
				case EAppReturnType::NoAll:
					bAutoReuseExistingAsset = false;
					bUseExistingAsset = false;
					break;
				case EAppReturnType::No:
					bUseExistingAsset = false;
					break;
			}
		}

		if (bUseExistingAsset)
		{
			bAssetCreated = false;
			return ExistingAsset;
		}
	}

	// No existing asset, let's create our own.
	UObject* NewAsset = nullptr;
	FString NewCameraAnimSequenceName = CameraAnimToConvert->GetName() + FString("Sequence");
	FString AssetPackagePath = FPaths::GetPath(AssetPackage->GetPathName());
	if (bPromptCreateAsset)
	{
		NewAsset = AssetTools.CreateAssetWithDialog(NewCameraAnimSequenceName, AssetPackagePath, UCameraAnimationSequence::StaticClass(), CameraAnimationSequenceFactoryNew);
	}
	else
	{
		NewAsset = AssetTools.CreateAsset(NewCameraAnimSequenceName, AssetPackagePath, UCameraAnimationSequence::StaticClass(), CameraAnimationSequenceFactoryNew);
	}
	if (NewAsset == nullptr)
	{
		return nullptr;
	}

	// Create the new sequence.
	UCameraAnimationSequence* NewSequence = Cast<UCameraAnimationSequence>(NewAsset);
	NewSequence->BoundActorClass = ACameraActor::StaticClass();

	UMovieScene* NewMovieScene = NewSequence->GetMovieScene();

	// Add the spawnable for the camera.
	ACameraActor* CameraTemplate = NewObject<ACameraActor>(NewMovieScene, ACameraActor::StaticClass());
	FGuid SpawnableGuid = NewMovieScene->AddSpawnable("CameraActor", *CameraTemplate);
	
	// Set sequence length.
	const int32 LengthInFrames = (CameraAnimToConvert->AnimLength * NewMovieScene->GetTickResolution()).FrameNumber.Value;
	NewMovieScene->SetPlaybackRange(FFrameNumber(0), LengthInFrames + 1);

	// Add spawning track for the camera.
	UMovieSceneSpawnTrack* NewSpawnTrack = NewMovieScene->AddTrack<UMovieSceneSpawnTrack>(SpawnableGuid);
	UMovieSceneSpawnSection* NewSpawnSection = CastChecked<UMovieSceneSpawnSection>(NewSpawnTrack->CreateNewSection());
	NewSpawnSection->GetChannel().SetDefault(true);
	NewSpawnSection->SetStartFrame(TRangeBound<FFrameNumber>());
	NewSpawnSection->SetEndFrame(TRangeBound<FFrameNumber>());
	NewSpawnTrack->AddSection(*NewSpawnSection);

	// Add camera animation data.
	if (CameraAnimToConvert->CameraInterpGroup != nullptr)
	{
		// Construct a temporary matinee actor
		CreateMatineeActorForCameraAnim(CameraAnimToConvert);

		// Changed the actor type, but don't want to lose any properties from previous
		// so duplicate from old, but with new class
		check(CameraAnimToConvert->CameraInterpGroup);
		if (!CameraAnimToConvert->CameraInterpGroup->IsA(UInterpGroupCamera::StaticClass()))
		{
			CameraAnimToConvert->CameraInterpGroup = CastChecked<UInterpGroupCamera>(StaticDuplicateObject(CameraAnimToConvert->CameraInterpGroup, CameraAnimToConvert, TEXT("CameraAnimation"), RF_NoFlags, UInterpGroupCamera::StaticClass()));
		}

		UInterpGroupCamera* NewInterpGroup = CastChecked<UInterpGroupCamera>(CameraAnimToConvert->CameraInterpGroup);
		check(NewInterpGroup);

		if (PreviewMatineeActor.Get()->MatineeData)
		{
			PreviewMatineeActor.Get()->MatineeData->SetFlags(RF_Transient);
			PreviewMatineeActor.Get()->MatineeData->InterpLength = CameraAnimToConvert->AnimLength;

			if (NewInterpGroup)
			{
				PreviewMatineeActor.Get()->MatineeData->InterpGroups.Add(NewInterpGroup);
			}
		}

		// Create a CameraActor and connect it to the Interp. will create this at the perspective viewport's location and rotation
		CreateCameraActorForCameraAnim(CameraAnimToConvert);

		// Set up the group actor
		PreviewMatineeActor.Get()->InitGroupActorForGroup(NewInterpGroup, PreviewCamera.Get());

		// This will create the instances for everything
		PreviewMatineeActor.Get()->InitInterp();

		// Actually do the conversion!
		MatineeConverter->ConvertInterpGroup(
				CameraAnimToConvert->CameraInterpGroup, SpawnableGuid, PreviewCamera.Get(), 
				NewSequence, NewMovieScene, NumWarnings);

		// Clean up all the temp stuff Matinee forced us to create.
		CleanUpActors();

		// Fix-up the transform blend type for camera anims that have a "relative to initial transform" option enabled.
		const FMovieScenePossessable* CameraComponentPossessable = NewMovieScene->FindPossessable(
				[](FMovieScenePossessable& InPossessable) { return InPossessable.GetName() == TEXT("CameraComponent"); });
		if (CameraAnimToConvert->bRelativeToInitialTransform)
		{
			// We find the transform track on the spawnable itself (the camera actor) because the scene component is treated 
			// in a bit of a special way.
			UMovieScene3DTransformTrack* TransformTrack = NewMovieScene->FindTrack<UMovieScene3DTransformTrack>(SpawnableGuid);
			if (TransformTrack)
			{
				if (ensureMsgf(TransformTrack->GetSupportedBlendTypes().Contains(EMovieSceneBlendType::AdditiveFromBase),
							TEXT("Camera animation with relative transform has a transform track that doesn't support AdditiveFromBase blend type"))
				   )
				{
					for (UMovieSceneSection* TransformSection : TransformTrack->GetAllSections())
					{
						TransformSection->SetBlendType(EMovieSceneBlendType::AdditiveFromBase);
					}
				}
			}
		}

		// Camera animations do some kind of relative animation for FOVs regardless of whether "relative to initial FOV"
		// is set or not... the only difference is that they will get their initial FOV value from evaluating the FOV track vs.
		// from the "initial FOV" property value.
		// We are going to make our FOV track "additive from base" either way, but report a warning if the values don't match.
		// This warning can probably be ignored because the previous asset was probably wrong in that case, unless it was 
		// support to make the FOV "pop" on purpose on the first frame.
		if (CameraComponentPossessable)
		{
			UMovieSceneFloatTrack* FOVTrack = NewMovieScene->FindTrack<UMovieSceneFloatTrack>(CameraComponentPossessable->GetGuid(), "FieldOfView");
			if (
					FOVTrack &&
					ensureMsgf(FOVTrack->GetSupportedBlendTypes().Contains(EMovieSceneBlendType::AdditiveFromBase),
						TEXT("Camera animation has an FOV track that doesn't support AdditiveFromBase blend type"))
					)
			{
				for (UMovieSceneSection* FOVSection : FOVTrack->GetAllSections())
				{
					FOVSection->SetBlendType(EMovieSceneBlendType::AdditiveFromBase);
				}
			}

			if (!CameraAnimToConvert->bRelativeToInitialFOV)
			{
				using namespace UE::MovieScene;

				FSystemInterrogator Interrogator;
				TGuardValue<FEntityManager*> DebugVizGuard(GEntityManagerForDebuggingVisualizers, &Interrogator.GetLinker()->EntityManager);

				Interrogator.ImportTrack(FOVTrack, FInterrogationChannel::Default());
				Interrogator.AddInterrogation(NewMovieScene->GetPlaybackRange().HasLowerBound() ?
						NewMovieScene->GetPlaybackRange().GetLowerBoundValue() : FFrameNumber(0));
				Interrogator.Update();

				TArray<float> AnimatedFOVs;
				FMovieSceneTracksComponentTypes* TracksComponents = FMovieSceneTracksComponentTypes::Get();
				Interrogator.QueryPropertyValues(TracksComponents->Float, AnimatedFOVs);

				if (ensure(AnimatedFOVs.Num() == 1))
				{
					if (!FMath::IsNearlyEqual(AnimatedFOVs[0], CameraAnimToConvert->BaseFOV))
					{
						UE_LOG(LogMatineeToLevelSequence, Warning, 
								TEXT("Camera animation '%s' has an FOV track that starts with a value of '%f' but the base FOV value is '%f'. The converted animation will use 'AdditiveFromBase' and animate values relative to '%f'."), 
								*CameraAnimToConvert->GetName(), AnimatedFOVs[0], CameraAnimToConvert->BaseFOV, AnimatedFOVs[0]);
						++NumWarnings;
					}
				}
			}
		}
	}

	bAssetCreated = true;
	return NewAsset;
}

void FCameraAnimToTemplateSequenceConverter::CreateMatineeActorForCameraAnim(UCameraAnim* InCameraAnim)
{
	check(InCameraAnim);

	FActorSpawnParameters ActorSpawnParameters;
	ActorSpawnParameters.Name = InCameraAnim->GetFName();
	PreviewMatineeActor = GEditor->GetEditorWorldContext().World()->SpawnActor<AMatineeActorCameraAnim>(ActorSpawnParameters);
	check(PreviewMatineeActor.IsValid());
	UInterpData* NewData = NewObject<UInterpData>(GetTransientPackage(), NAME_None, RF_Transactional);
	PreviewMatineeActor.Get()->MatineeData = NewData;
	PreviewMatineeActor.Get()->CameraAnim = InCameraAnim;
}

void FCameraAnimToTemplateSequenceConverter::CreateCameraActorForCameraAnim(UCameraAnim* InCameraAnim)
{
	check(InCameraAnim);

	FVector ViewportCamLocation(FVector::ZeroVector);
	FRotator ViewportCamRotation(FRotator::ZeroRotator);

	for (FLevelEditorViewportClient* ViewportClient : GEditor->GetLevelViewportClients())
	{
		if (ViewportClient != NULL && ViewportClient->ViewportType == LVT_Perspective)
		{
			ViewportCamLocation = ViewportClient->ViewTransformPerspective.GetLocation();
			ViewportCamRotation = ViewportClient->ViewTransformPerspective.GetRotation();
			break;
		}
	}

	PreviewCamera = GEditor->GetEditorWorldContext().World()->SpawnActor<ACameraActor>(ViewportCamLocation, ViewportCamRotation);
	check(PreviewCamera.IsValid());
	PreviewCamera.Get()->SetFlags(RF_Transient);
	PreviewCamera.Get()->SetActorLabel(FText::Format(LOCTEXT("CamerAnimPreviewCameraName", "Preview Camera - {0}"), FText::FromName(InCameraAnim->GetFName())).ToString());

	// copy data from the CamAnim to the CameraActor
	check(PreviewCamera.Get()->GetCameraComponent());
	PreviewCamera.Get()->PreviewedCameraAnim = InCameraAnim;
	PreviewCamera.Get()->GetCameraComponent()->FieldOfView = InCameraAnim->BaseFOV;
	PreviewCamera.Get()->GetCameraComponent()->PostProcessSettings = InCameraAnim->BasePostProcessSettings;
}

void FCameraAnimToTemplateSequenceConverter::CleanUpActors()
{
	// clean up our preview actors if they are still present
	if(PreviewCamera.IsValid())
	{
		GEditor->GetEditorWorldContext().World()->DestroyActor(PreviewCamera.Get(), false, false);
		PreviewCamera.Reset();
	}

	if (PreviewMatineeActor.IsValid())
	{
		GEditor->GetEditorWorldContext().World()->DestroyActor(PreviewMatineeActor.Get(), false, false);
		PreviewMatineeActor.Reset();
	}
}

#undef LOCTEXT_NAMESPACE
