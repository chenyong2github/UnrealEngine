// Copyright Epic Games, Inc. All Rights Reserved.

#include "CameraAnimToTemplateSequenceConverter.h"
#include "AssetToolsModule.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraAnim.h"
#include "CameraAnimationSequence.h"
#include "ContentBrowserMenuContexts.h"
#include "Framework/Docking/TabManager.h"
#include "Framework/Notifications/NotificationManager.h"
#include "IAssetTools.h"
#include "ToolMenuContext.h"
#include "MatineeConverter.h"
#include "Sections/MovieSceneSpawnSection.h"
#include "Tracks/MovieSceneSpawnTrack.h"
#include "Widgets/Notifications/SNotificationList.h"

#define LOCTEXT_NAMESPACE "CameraAnimToTemplateSequenceConverter"

FCameraAnimToTemplateSequenceConverter::FCameraAnimToTemplateSequenceConverter(const FMatineeConverter* InMatineeConverter)
	: MatineeConverter(InMatineeConverter)
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
	bool bConvertSuccess = false;
	IAssetTools& AssetTools = FModuleManager::GetModuleChecked<FAssetToolsModule>("AssetTools").Get();
	for (UCameraAnim* CameraAnimToConvert : CameraAnimsToConvert)
	{
		bConvertSuccess = ConvertSingleCameraAnimToTemplateSequence(
				CameraAnimToConvert, AssetTools, CameraAnimationSequenceFactoryNew, true, NumWarnings) 
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

UObject* FCameraAnimToTemplateSequenceConverter::ConvertCameraAnim(IAssetTools& AssetTools, UFactory* CameraAnimationSequenceFactoryNew, UCameraAnim* CameraAnim, int32& NumWarnings)
{
	using namespace UE::MovieScene;

	if (!ensure(CameraAnim))
	{
		return nullptr;
	}

	ensure(CameraAnimationSequenceFactoryNew != nullptr);
	UObject* NewAsset = ConvertSingleCameraAnimToTemplateSequence(
			CameraAnim, AssetTools, CameraAnimationSequenceFactoryNew, false, NumWarnings);
	return NewAsset;
}

UObject* FCameraAnimToTemplateSequenceConverter::ConvertSingleCameraAnimToTemplateSequence(UCameraAnim* CameraAnimToConvert, IAssetTools& AssetTools, UFactory* CameraAnimationSequenceFactoryNew, bool bPromptCreateAsset, int32& NumWarnings)
{
	// Ask user for the new asset's name and folder.
	UPackage* AssetPackage = CameraAnimToConvert->GetOutermost();
	FString NewCameraAnimSequenceName = CameraAnimToConvert->GetName() + FString("Sequence");
	FString NewCameraAnimSequencePath = FPaths::GetPath(AssetPackage->GetName());

	UObject* NewAsset = nullptr;
	if (bPromptCreateAsset)
	{
		NewAsset = AssetTools.CreateAssetWithDialog(NewCameraAnimSequenceName, NewCameraAnimSequencePath, UCameraAnimationSequence::StaticClass(), CameraAnimationSequenceFactoryNew);
	}
	else
	{
		NewAsset = AssetTools.CreateAsset(NewCameraAnimSequenceName, AssetPackage->GetPathName(), UCameraAnimationSequence::StaticClass(), CameraAnimationSequenceFactoryNew);
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
		MatineeConverter->ConvertInterpGroup(
				CameraAnimToConvert->CameraInterpGroup, SpawnableGuid, nullptr, 
				NewSequence, NewMovieScene, NumWarnings);
	}

	return NewAsset;
}

#undef LOCTEXT_NAMESPACE
