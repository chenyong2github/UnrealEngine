// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Delegates/DelegateCombinations.h"
#include "IVirtualCameraController.h"
#include "Subsystems/EngineSubsystem.h"

#include "VirtualCameraSubsystem.generated.h"

class UWorld;
class UVirtualCameraUserSettings;
class ULevelSequence;
class UTexture;
class UTakeMetaData;
class UVirtualCameraClipsMetaData; 
class USceneCaptureComponent2D;

struct FTimecode;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnStreamStarted);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnStreamStopped);

UCLASS(BlueprintType, Category = "VirtualCamera", DisplayName = "VirtualCameraSubsystem")
class VIRTUALCAMERA_API UVirtualCameraSubsystem : public UEngineSubsystem
{
	GENERATED_BODY()
	
public:

	UVirtualCameraSubsystem();

public:
	UFUNCTION(BlueprintCallable, Category = "VirtualCamera | Streaming")
	bool StartStreaming();

	UFUNCTION(BlueprintCallable, Category = "VirtualCamera | Streaming")
	bool StopStreaming();

	UFUNCTION(BlueprintCallable, Category = "VirtualCamera | Streaming")
	bool IsStreaming() const;

	UFUNCTION(BlueprintCallable, Category = "VirtualCamera")
	TScriptInterface<IVirtualCameraController> GetVirtualCameraController() const;
	
	UFUNCTION(BlueprintCallable, Category = "VirtualCamera")
	void SetVirtualCameraController(TScriptInterface<IVirtualCameraController> VirtualCamera);

	UPROPERTY(BlueprintReadOnly, Category = "VirtualCamera")
	ULevelSequencePlaybackController* SequencePlaybackController;

	UPROPERTY(BlueprintAssignable, Category = "VirtualCamera | Streaming")
	FOnStreamStarted OnStreamStartedDelegate;

	UPROPERTY(BlueprintAssignable, Category = "VirtualCamera | Streaming")
	FOnStreamStopped OnStreamStoppedDelegate;
	
	UFUNCTION(BlueprintPure, Category = "VirtualCamera")
	UVirtualCameraUserSettings* GetUserSettings();

	/** Get the currently opened level sequence asset */
	UFUNCTION(BlueprintPure, Category = "VirtualCamera")
	ULevelSequence* GetCurrentLevelSequence();

	/** Play the current level sequence */
	UFUNCTION(BlueprintCallable, Category = "VirtualCamera")
	void PlayCurrentLevelSequence();

	/** Pause the current level sequence */
	UFUNCTION(BlueprintCallable, Category = "VirtualCamera")
	void PauseCurrentLevelSequence();

	/** Set playback position for the current level sequence in frames */
	UFUNCTION(BlueprintCallable, Category = "VirtualCamera")
	void SetCurrentLevelSequenceCurrentFrame(int32 NewFrame);

	/** Get the current playback position in frames */
	UFUNCTION(BlueprintPure, Category = "VirtualCamera")
	int32 GetCurrentLevelSequenceCurrentFrame();

	/** Get length in frames of a level sequence */
	UFUNCTION(BlueprintCallable, Category = "VirtualCamera")
	int32 GetLevelSequenceLengthInFrames(const ULevelSequence* LevelSequence);

	/** Convert a frame from a level sequence to timecode */
	UFUNCTION(BlueprintCallable, Category = "VirtualCamera")
	FTimecode GetLevelSequenceFrameAsTimecode(const ULevelSequence* LevelSequence, int32 InFrame);

	/** Check whether the sequence is actively playing. */
	UFUNCTION(BlueprintPure, Category = "VirtualCamera")
	bool IsCurrentLevelSequencePlaying();

	/** Imports image as a uasset */
	UFUNCTION(BlueprintCallable, Category = "VirtualCamera")
	UTexture* ImportSnapshotTexture(FString FileName, FString SubFolderName, FString AbsolutePathPackage);

	/** Returns true if not in editor or if running the game in PIE or Simulate*/
	UFUNCTION(BlueprintPure, Category = "VirtualCamera")
	bool IsGameRunning();
	
	/** Returns true if the function was found & executed correctly. */
	UFUNCTION(BlueprintCallable, Category = "VirtualCamera")
	bool CallFunctionByName(UObject* ObjPtr, FName FunctionName);

	/** Saves UVirtualCameraClipsMetaData with updated selects information. */ 
	UFUNCTION(BlueprintCallable, Category = "VirtualCamera | Metadata")
	bool ModifyLevelSequenceMetadataForSelects(UVirtualCameraClipsMetaData* LevelSequenceMetaData, bool IsSelected);

	/** Save an asset through path. Used for saving thumbnails. Returns true on success. */
	UFUNCTION(BlueprintCallable, Category = "VirtualCamera | Clips")
	bool SaveThumbnailAsset(FString AssetPath);

	/** Load an asset through path. Used for saving thumbnails. */
	UFUNCTION(BlueprintCallable, Category = "VirtualCamera | Clips")
	UObject* LoadThumbnailAsset(FString AssetPath);

	/** Sort array by metadata timecode **/
	UFUNCTION(BlueprintCallable, Category = "VirtualCamera | Clips ")
	TArray<ULevelSequence*> SortAssetsByTimecode(TArray<ULevelSequence*> LevelSequenceAssets);

	/** Pilot the provided actor using editor scripting */
	UFUNCTION(BlueprintCallable, Category = "VirtualCamera | Streaming ")
	void PilotActor(AActor* SelectedActor); 

	/** Updates the provided USceneCaptureComponent2D's PostProcessingSettings. Returns true on success. */
	UFUNCTION(BlueprintCallable, Category = "VirtualCamera")
	bool UpdatePostProcessSettingsForCapture(USceneCaptureComponent2D* CaptureComponent, float DepthOfField, float FStopValue);

private:

	UPROPERTY(Transient) 
	TScriptInterface<IVirtualCameraController> ActiveCameraController;

	bool bIsStreaming;
};
