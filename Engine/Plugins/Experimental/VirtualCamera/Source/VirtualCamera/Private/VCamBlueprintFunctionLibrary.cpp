// Copyright Epic Games, Inc. All Rights Reserved.

#include "VCamBlueprintFunctionLibrary.h"
#include "LevelSequence.h"
#include "LevelSequenceEditor/Private/LevelSequenceEditorBlueprintLibrary.h"
#include "VirtualCameraClipsMetaData.h"
#include "VPUtilitiesEditor/Public/VPUtilitiesEditorBlueprintLibrary.h"
#include "Components/SceneCaptureComponent2D.h"
#include "AssetData.h"
#include "VirtualCameraUserSettings.h"


#if WITH_EDITOR
#include "Editor.h"
#include "EditorAssetLibrary.h"
#include "EditorLevelLibrary.h"
#endif

bool UVCamBlueprintFunctionLibrary::IsGameRunning()
{
#if WITH_EDITOR
	return (GEditor && GEditor->IsPlaySessionInProgress());
#endif
	return true;
}

UVirtualCameraUserSettings* UVCamBlueprintFunctionLibrary::GetUserSettings()
{
	return GetMutableDefault<UVirtualCameraUserSettings>();
}

ULevelSequence* UVCamBlueprintFunctionLibrary::GetCurrentLevelSequence()
{
#if WITH_EDITOR
	return ULevelSequenceEditorBlueprintLibrary::GetCurrentLevelSequence();
#else
	return nullptr;
#endif
}

void UVCamBlueprintFunctionLibrary::PlayCurrentLevelSequence()
{
#if WITH_EDITOR
	ULevelSequenceEditorBlueprintLibrary::Play();
#endif
}

void UVCamBlueprintFunctionLibrary::PauseCurrentLevelSequence()
{
#if WITH_EDITOR
	ULevelSequenceEditorBlueprintLibrary::Pause();
#endif
}

void UVCamBlueprintFunctionLibrary::SetCurrentLevelSequenceCurrentFrame(int32 NewFrame)
{
#if WITH_EDITOR
	ULevelSequenceEditorBlueprintLibrary::SetCurrentTime(NewFrame);
#endif

}

int32 UVCamBlueprintFunctionLibrary::GetCurrentLevelSequenceCurrentFrame()
{
#if WITH_EDITOR
	return ULevelSequenceEditorBlueprintLibrary::GetCurrentTime();
#else
	return 0;
#endif
}

int32 UVCamBlueprintFunctionLibrary::GetLevelSequenceLengthInFrames(const ULevelSequence* LevelSequence)
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

FTimecode UVCamBlueprintFunctionLibrary::GetLevelSequenceFrameAsTimecode(const ULevelSequence* LevelSequence, int32 InFrame)
{
	if (LevelSequence)
	{
		return FTimecode::FromFrameNumber(InFrame, LevelSequence->GetMovieScene()->GetDisplayRate());
	}

	return FTimecode();
}

FTimecode UVCamBlueprintFunctionLibrary::GetLevelSequenceFrameAsTimecodeWithoutObject(const FFrameRate DisplayRate, int32 InFrame)
{
	return FTimecode::FromFrameNumber(InFrame, DisplayRate);
}

bool UVCamBlueprintFunctionLibrary::IsCurrentLevelSequencePlaying()
{
#if WITH_EDITOR
	return ULevelSequenceEditorBlueprintLibrary::IsPlaying();
#else
	return false;
#endif
}


UTexture* UVCamBlueprintFunctionLibrary::ImportSnapshotTexture(FString FileName, FString SubFolderName, FString AbsolutePathPackage)
{
#if WITH_EDITOR
	return UVPUtilitiesEditorBlueprintLibrary::ImportSnapshotTexture(FileName, SubFolderName, AbsolutePathPackage);
#else
	return nullptr;
#endif
}


bool UVCamBlueprintFunctionLibrary::ModifyLevelSequenceMetadata(UVirtualCameraClipsMetaData* LevelSequenceMetaData)
{
#if WITH_EDITOR
	if (LevelSequenceMetaData)
	{
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

bool UVCamBlueprintFunctionLibrary::ModifyLevelSequenceMetadataForSelects(UVirtualCameraClipsMetaData* LevelSequenceMetaData, bool bIsSelected)
{
#if WITH_EDITOR
	if (LevelSequenceMetaData)
	{
		LevelSequenceMetaData->SetSelected(bIsSelected);
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

bool UVCamBlueprintFunctionLibrary::EditorSaveAsset(FString AssetPath)
{
#if WITH_EDITOR
	return UEditorAssetLibrary::SaveAsset(AssetPath, true);
#endif
	return false;

}


UObject* UVCamBlueprintFunctionLibrary::EditorLoadAsset(FString AssetPath)
{
#if WITH_EDITOR
	return UEditorAssetLibrary::LoadAsset(AssetPath);
#endif
	return nullptr;
}



void UVCamBlueprintFunctionLibrary::ModifyObjectMetadataTags(UObject* InObject, FName InTag, FString InValue)
{
#if WITH_EDITOR
	if (InObject) 
	{
		UEditorAssetLibrary::SetMetadataTag(InObject, InTag, InValue); 

	}
#endif
}

TMap<FName, FString> UVCamBlueprintFunctionLibrary::GetObjectMetadataTags(UObject* InObject)
{

#if WITH_EDITOR
	if (InObject)
	{
		return UEditorAssetLibrary::GetMetadataTagValues(InObject); 
	}
#endif
return TMap<FName, FString>();

}

TArray<FAssetData> UVCamBlueprintFunctionLibrary::SortAssetsByTimecodeAssetData(TArray<FAssetData> LevelSequenceAssetData)
{
	TArray<FAssetData> SortedTimecodeArray = LevelSequenceAssetData;


	//Sort an array of AssetData by their timecode contained in TakeMetaData inteded for use with LevelSequences
	SortedTimecodeArray.Sort([](const FAssetData& LevelSequence1AssetData, const FAssetData& LevelSequence2AssetData)
	{
		FString LevelSequence1TimestampTagValue;
		bool bFoundLevelSequence1Tag = LevelSequence1AssetData.GetTagValue("TakeMetaData_Timestamp", LevelSequence1TimestampTagValue);

		FString LevelSequence2TimestampTagValue;
		bool bFoundLevelSequence2Tag = LevelSequence2AssetData.GetTagValue("TakeMetaData_Timestamp", LevelSequence2TimestampTagValue);


		if (bFoundLevelSequence1Tag && bFoundLevelSequence2Tag)
		{
			FDateTime LevelSequence1TimeStamp;
			FDateTime LevelSequence2TimeStamp;
			FDateTime::Parse(LevelSequence1TimestampTagValue, LevelSequence1TimeStamp);
			FDateTime::Parse(LevelSequence2TimestampTagValue, LevelSequence2TimeStamp);
			return LevelSequence1TimeStamp > LevelSequence2TimeStamp;
		}

		//Handle cases in which valid metadata is found on one but not the another. 
		else if (bFoundLevelSequence1Tag == false && bFoundLevelSequence2Tag == true)
		{
			return false;
		}

		else if (bFoundLevelSequence1Tag == true && bFoundLevelSequence2Tag == false)
		{
			return true;

		}
		return false;

	});

	return SortedTimecodeArray;
}

void UVCamBlueprintFunctionLibrary::PilotActor(AActor* SelectedActor)
{
#if WITH_EDITOR
	if (SelectedActor)
	{
		UEditorLevelLibrary::PilotLevelActor(SelectedActor);
	}
#endif
}

bool UVCamBlueprintFunctionLibrary::UpdatePostProcessSettingsForCapture(USceneCaptureComponent2D* CaptureComponent, float DepthOfField, float FStopValue)
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

FFrameRate UVCamBlueprintFunctionLibrary::GetDisplayRate(ULevelSequence* LevelSequence)
{
	if (LevelSequence)
	{
		return LevelSequence->GetMovieScene()->GetDisplayRate();
	}
	return FFrameRate();
}

FFrameRate UVCamBlueprintFunctionLibrary::ConvertStringToFrameRate(FString InFrameRateString)
{
	TValueOrError<FFrameRate, FExpressionError> ParseResult = ParseFrameRate(*InFrameRateString);
	if (ParseResult.IsValid())
	{
		return ParseResult.GetValue();
	}
	else {
		return FFrameRate();
	}
}

bool UVCamBlueprintFunctionLibrary::CallFunctionByName(UObject* ObjPtr, FName FunctionName)
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

void UVCamBlueprintFunctionLibrary::EditorSetGameView(bool bIsToggled)
{
#if WITH_EDITOR
	//Only set Game view when streaming in editor mode (so not on PIE, SIE or standalone) 
	if (!GEditor || GEditor->IsPlaySessionInProgress())
	{
		return;
	}
	UEditorLevelLibrary::EditorSetGameView(bIsToggled);
#endif
}
