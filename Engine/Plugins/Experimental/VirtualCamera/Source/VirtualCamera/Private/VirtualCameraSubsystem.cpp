// Copyright Epic Games, Inc. All Rights Reserved.

#include "VirtualCameraSubsystem.h"

#include "LevelSequence.h"
#include "LevelSequenceEditor/Private/LevelSequenceEditorBlueprintLibrary.h"
#include "LevelSequencePlaybackController.h"
#include "Misc/Timecode.h"
#include "UObject/Script.h"
#include "VirtualCameraUserSettings.h"
#include "VPUtilitiesEditor/Public/VPUtilitiesEditorBlueprintLibrary.h"
#include "TakeMetaData.h"
#include "VirtualCameraClipsMetaData.h"
#include "Components/SceneCaptureComponent2D.h"

#if WITH_EDITOR
#include "Editor.h"
#include "EditorAssetLibrary.h"
#include "FileHelpers.h"
#include "EditorLevelLibrary.h"
#endif


UVirtualCameraSubsystem::UVirtualCameraSubsystem()
	: bIsStreaming(false)
{
	SequencePlaybackController = CreateDefaultSubobject<ULevelSequencePlaybackController>("SequencePlaybackController");
}

bool UVirtualCameraSubsystem::StartStreaming()
{
	if (bIsStreaming)
	{
		return false;
	}

	if (ActiveCameraController)
	{
		bIsStreaming = ActiveCameraController->StartStreaming();
	}

	if (bIsStreaming)
	{
		FEditorScriptExecutionGuard ScriptGuard;
		OnStreamStartedDelegate.Broadcast();
	}

	return bIsStreaming;
}

bool UVirtualCameraSubsystem::StopStreaming()
{
	if (!bIsStreaming)
	{
		return false;
	}

	if (ActiveCameraController)
	{
		bIsStreaming = !(ActiveCameraController->StopStreaming());
	}

	if (!bIsStreaming)
	{
		FEditorScriptExecutionGuard ScriptGuard;
		OnStreamStoppedDelegate.Broadcast();
	}

	return bIsStreaming;
}

bool UVirtualCameraSubsystem::IsStreaming() const
{
	return bIsStreaming;
}

TScriptInterface<IVirtualCameraController> UVirtualCameraSubsystem::GetVirtualCameraController() const
{
	return ActiveCameraController;
}

void UVirtualCameraSubsystem::SetVirtualCameraController(TScriptInterface<IVirtualCameraController> VirtualCamera)
{
	ActiveCameraController = VirtualCamera;
	//todo deactive the last current, initialize the new active, call back
}

UVirtualCameraUserSettings* UVirtualCameraSubsystem::GetUserSettings()
{ 
	return GetMutableDefault<UVirtualCameraUserSettings>(); 
}

ULevelSequence* UVirtualCameraSubsystem::GetCurrentLevelSequence()
{
#if WITH_EDITOR
	return ULevelSequenceEditorBlueprintLibrary::GetCurrentLevelSequence();
#else
	return nullptr;
#endif
}

void UVirtualCameraSubsystem::PlayCurrentLevelSequence()
{
#if WITH_EDITOR
	ULevelSequenceEditorBlueprintLibrary::Play();
#endif
}

void UVirtualCameraSubsystem::PauseCurrentLevelSequence()
{
#if WITH_EDITOR
	ULevelSequenceEditorBlueprintLibrary::Pause();
#endif
}

void UVirtualCameraSubsystem::SetCurrentLevelSequenceCurrentFrame(int32 NewFrame)
{
#if WITH_EDITOR
	ULevelSequenceEditorBlueprintLibrary::SetCurrentTime(NewFrame);
#endif
}

int32 UVirtualCameraSubsystem::GetCurrentLevelSequenceCurrentFrame()
{
#if WITH_EDITOR
	return ULevelSequenceEditorBlueprintLibrary::GetCurrentTime();
#else
	return 0;
#endif
}

int32 UVirtualCameraSubsystem::GetLevelSequenceLengthInFrames(const ULevelSequence * LevelSequence)
{
	if (LevelSequence)
	{
		int32 SequenceLowerBound = LevelSequence->GetMovieScene()->GetPlaybackRange().GetLowerBoundValue().Value;
		int32 SequenceUpperBound = LevelSequence->GetMovieScene()->GetPlaybackRange().GetUpperBoundValue().Value;
		int32 Length = SequenceUpperBound - SequenceLowerBound;
		return ConvertFrameTime(Length, LevelSequence->GetMovieScene()->GetTickResolution(), LevelSequence->GetMovieScene()->GetDisplayRate()).FloorToFrame().Value;
	}

	return 0;
}

FTimecode UVirtualCameraSubsystem::GetLevelSequenceFrameAsTimecode(const ULevelSequence * LevelSequence, int32 InFrame)
{
	if (LevelSequence)
	{
		return FTimecode::FromFrameNumber(InFrame, LevelSequence->GetMovieScene()->GetDisplayRate());
	}

	return FTimecode();
}

bool UVirtualCameraSubsystem::IsCurrentLevelSequencePlaying()
{
#if WITH_EDITOR
	return ULevelSequenceEditorBlueprintLibrary::IsPlaying();
#else
	return false;
#endif
}

UTexture* UVirtualCameraSubsystem::ImportSnapshotTexture(FString FileName, FString SubFolderName, FString AbsolutePathPackage)
{
#if WITH_EDITOR
	return UVPUtilitiesEditorBlueprintLibrary::ImportSnapshotTexture(FileName, SubFolderName, AbsolutePathPackage);
#else
	return nullptr;
#endif
}

bool UVirtualCameraSubsystem::IsGameRunning()
{
#if WITH_EDITOR
	return (GEditor && GEditor->IsPlaySessionInProgress());
#endif
	return true;
}



bool UVirtualCameraSubsystem::ModifyLevelSequenceMetadataForSelects(UVirtualCameraClipsMetaData* LevelSequenceMetaData, bool IsSelected)
{
	#if WITH_EDITOR
	if (LevelSequenceMetaData) 
	{
		LevelSequenceMetaData->SetSelected(IsSelected);
		LevelSequenceMetaData->MarkPackageDirty(); 
		
		return UEditorAssetLibrary::SaveAsset(LevelSequenceMetaData->GetPathName()); 
	}
	else 
	{
		return false; 
	}

	#endif
	return false; 

}

bool UVirtualCameraSubsystem::SaveThumbnailAsset(FString AssetPath)
{
#if WITH_EDITOR
	return UEditorAssetLibrary::SaveAsset(AssetPath, true);
#endif
	return false; 

}

UObject* UVirtualCameraSubsystem::LoadThumbnailAsset(FString AssetPath)
{
#if WITH_EDITOR
	return UEditorAssetLibrary::LoadAsset(AssetPath);
#endif
	return nullptr;
}


TArray<ULevelSequence*> UVirtualCameraSubsystem::SortAssetsByTimecode(TArray<ULevelSequence*> LevelSequenceAssets)
{
	TArray<ULevelSequence*> SortedTimecodeArray = LevelSequenceAssets;

	//Sort an array of LevelSequences by their timecode contained in TakeMetaData
	SortedTimecodeArray.Sort([this](ULevelSequence& LevelSequence1, ULevelSequence& LevelSequence2) 
	{
		UTakeMetaData* LevelSequence1Metadata = Cast<UTakeMetaData>(LevelSequence1.FindMetaDataByClass(UTakeMetaData::StaticClass()));
		UTakeMetaData* LevelSequence2Metadata = Cast<UTakeMetaData>(LevelSequence2.FindMetaDataByClass(UTakeMetaData::StaticClass()));
		if (LevelSequence1Metadata != nullptr && LevelSequence2Metadata != nullptr)
		{
			return LevelSequence1Metadata->GetTimestamp() > LevelSequence2Metadata->GetTimestamp();
		}

		//Handle cases in which valid metadata is found on one but not the another. 
		else if (LevelSequence1Metadata == nullptr && LevelSequence2Metadata != nullptr) 
		{
			return false;
		}
		else if (LevelSequence1Metadata != nullptr && LevelSequence2Metadata == nullptr) 
		{
			return true;

		}
		return false; 
	});

	return SortedTimecodeArray;
}

void UVirtualCameraSubsystem::PilotActor(AActor* SelectedActor)
{
#if WITH_EDITOR
	if (SelectedActor) 
	{
		UEditorLevelLibrary::PilotLevelActor(SelectedActor);
	}
#endif
	
}

bool UVirtualCameraSubsystem::UpdatePostProcessSettingsForCapture(USceneCaptureComponent2D* CaptureComponent, float DepthOfField, float FStopValue)
{
	if (CaptureComponent)
	{
		FPostProcessSettings NewCapturePostProcessSettings = CaptureComponent->PostProcessSettings;
		NewCapturePostProcessSettings.bOverride_DepthOfFieldFstop = true;
		NewCapturePostProcessSettings.bOverride_DepthOfFieldFocalDistance = true;

		NewCapturePostProcessSettings.DepthOfFieldFstop = FStopValue;
		NewCapturePostProcessSettings.DepthOfFieldFocalDistance = DepthOfField;
		
		CaptureComponent->PostProcessSettings = NewCapturePostProcessSettings;
		return true; 
	}
	return false;
}

bool UVirtualCameraSubsystem::CallFunctionByName(UObject* ObjPtr, FName FunctionName)
{
	if (ObjPtr) 
	{
		if (UFunction* Function = ObjPtr->FindFunction(FunctionName))
		{
			ObjPtr->ProcessEvent(Function, nullptr);
			return true;
		}
		return false;
	}
	return false;  
}
