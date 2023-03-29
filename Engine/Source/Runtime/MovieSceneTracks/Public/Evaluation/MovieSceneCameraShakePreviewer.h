// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_EDITOR

#include "CoreMinimal.h"
#include "UObject/GCObject.h"
#include "Camera/PlayerCameraManager.h"

#include "MovieSceneCameraShakePreviewer.generated.h"

class FLevelEditorViewportClient;
class UCameraModifier_CameraShake;
struct FEditorViewportViewModifierParams;

/**
 * A mock player camera manager, used to store camera shake post-process settings for previewing.
 */
UCLASS()
class APreviewPlayerCameraManager : public APlayerCameraManager
{
	GENERATED_BODY()

public:
	void ResetPostProcessSettings()
	{
		ClearCachedPPBlends();
	}

	void MergePostProcessSettings(TArray<FPostProcessSettings>& InSettings, TArray<float>& InBlendWeights)
	{
		InSettings.Append(PostProcessBlendCache);
		InBlendWeights.Append(PostProcessBlendCacheWeights);
	}
};

/**
 * A class that owns a gameplay camera shake manager, so that we can us it to preview shakes in editor.
 */
class MOVIESCENETRACKS_API FCameraShakePreviewer : public FGCObject
{
public:
	FCameraShakePreviewer();
	~FCameraShakePreviewer();

	void Initialize(UWorld* InWorld);
	bool IsInitialized() const { return PreviewCameraShake != nullptr; }
	void Teardown();

	APlayerCameraManager* GetCameraManager() const { return PreviewCamera; }
	UCameraModifier_CameraShake* GetCameraModifier() const { return PreviewCameraShake; }

	void ModifyView(FEditorViewportViewModifierParams& Params);

	void RegisterViewModifier();
	void UnRegisterViewModifier();
	void Update(float DeltaTime, bool bIsPlaying);

private:
	// FGCObject interface
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override { Collector.AddReferencedObject(PreviewCameraShake); }
	virtual FString GetReferencerName() const override { return TEXT("SCameraShakePreviewer"); }

private:
	void OnModifyView(FEditorViewportViewModifierParams& Params);
	void OnLevelViewportClientListChanged();

private:
	APreviewPlayerCameraManager* PreviewCamera;
	UCameraModifier_CameraShake* PreviewCameraShake;
	TArray<FLevelEditorViewportClient*> RegisteredViewportClients;

	TOptional<float> LastDeltaTime;
	FVector LastLocationModifier;
	FRotator LastRotationModifier;
	float LastFOVModifier;

	TArray<FPostProcessSettings> LastPostProcessSettings;
	TArray<float> LastPostProcessBlendWeights;
};

#endif

