// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Camera/CameraTypes.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "Engine/TextureRenderTarget2D.h"

#include "NiagaraBakerSettings.generated.h"

UENUM()
enum class ENiagaraBakerViewMode
{
	Perspective,
	OrthoFront,
	OrthoBack,
	OrthoLeft,
	OrthoRight,
	OrthoTop,
	OrthoBottom,
	Num
};

USTRUCT()
struct FNiagaraBakerTextureSource
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Source")
	FName SourceName;
};

USTRUCT()
struct FNiagaraBakerTextureSettings
{
	GENERATED_BODY()

	/** Optional output name, if left empty a name will be auto generated using the index of the texture/ */
	UPROPERTY(EditAnywhere, Category = "Texture")
	FName OutputName;
	
	/** Source visualization we should capture, i.e. Scene Color, World Normal, etc */
	UPROPERTY(EditAnywhere, Category = "Texture")
	FNiagaraBakerTextureSource SourceBinding;

	UPROPERTY(EditAnywhere, Category = "Texture", meta = (PinHiddenByDefault, InlineEditConditionToggle))
	uint8 bUseFrameSize : 1;

	/** Size of each frame generated. */
	UPROPERTY(EditAnywhere, Category = "Texture", meta = (EditCondition="bUseFrameSize"))
	FIntPoint FrameSize = FIntPoint(128, 128);

	/** Overall texture size that will be generated. */
	UPROPERTY(EditAnywhere, Category = "Texture", meta = (EditCondition="!bUseFrameSize"))
	FIntPoint TextureSize = FIntPoint(128 * 8, 128 * 8);

	//-TODO: Add property to control generated texture compression format
	//UPROPERTY(EditAnywhere, Category = "Texture")
	//TEnumAsByte<ETextureRenderTargetFormat> Format = RTF_RGBA16f;

	/** Final texture generated, an existing entry will be updated with new capture data. */
	UPROPERTY(EditAnywhere, Category = "Texture")
	UTexture2D* GeneratedTexture = nullptr;

	bool Equals(const FNiagaraBakerTextureSettings& Other) const;

	FNiagaraBakerTextureSettings() :
		bUseFrameSize(false)
	{}
};

UCLASS()
class NIAGARA_API UNiagaraBakerSettings : public UObject
{
	GENERATED_BODY()

public:
	struct FDisplayInfo
	{
		float NormalizedTime;
		int FrameIndexA;
		int FrameIndexB;
		float Interp;
	};

	UNiagaraBakerSettings(const FObjectInitializer& Init);

	/**
	This is the start time of the simultion where we being the capture.
	I.e. 2.0 would mean the simulation warms up by 2 seconds before we begin capturing.
	*/
	UPROPERTY(EditAnywhere, Category="Timeline")
	float StartSeconds = 0.0f;

	/** Duration in seconds to take the capture over. */
	UPROPERTY(EditAnywhere, Category = "Timeline")
	float DurationSeconds = 4.0f;

	/**
	The frame rate to run the simulation at during capturing.
	This is only used for the preview view and calculating the number of ticks to execute
	as we capture the generated texture.
	*/
	UPROPERTY(EditAnywhere, Category = "Timeline", meta = (ClampMin=1, ClampMax=480))
	int FramesPerSecond = 60;

	/** Should the preview playback as looping or not. */
	UPROPERTY(EditAnywhere, Category = "Preview")
	uint8 bPreviewLooping : 1;

	/** Number of frames in each dimension. */
	UPROPERTY(EditAnywhere, Category = "Texture")
	FIntPoint FramesPerDimension = FIntPoint(8, 8);

	/** List of output textures we will generated. */
	UPROPERTY(EditAnywhere, Category = "Texture")
	TArray<FNiagaraBakerTextureSettings> OutputTextures;

	/** Current active viewport we will render from. */
	UPROPERTY(EditAnywhere, Category = "Camera")
	ENiagaraBakerViewMode CameraViewportMode = ENiagaraBakerViewMode::Perspective;

	/** Per viewport camera position.. */
	UPROPERTY(EditAnywhere, Category = "Camera")
	FVector CameraViewportLocation[(int)ENiagaraBakerViewMode::Num];

	/** Per viewport camera rotation.. */
	UPROPERTY(EditAnywhere, Category = "Camera")
	FRotator CameraViewportRotation[(int)ENiagaraBakerViewMode::Num];

	/** Perspective camera orbit distance. */
	UPROPERTY(EditAnywhere, Category = "Camera", meta = (EditCondition = "CameraViewportMode == ENiagaraBakerViewMode::Perspective", ClampMin = "0.01"))
	float CameraOrbitDistance = 200.f;

	/** Camera FOV to use when in perspective mode. */
	UPROPERTY(EditAnywhere, Category = "Camera", meta=(EditCondition="CameraViewportMode == ENiagaraBakerViewMode::Perspective", ClampMin="1.0", ClampMax="180.0"))
	float CameraFOV = 90.0f;

	/** Camera Orthographic width to use with in orthographic mode. */
	UPROPERTY(EditAnywhere, Category = "Camera", meta = (EditCondition = "CameraViewportMode != ENiagaraBakerViewMode::Perspective", ClampMin="1.0"))
	float CameraOrthoWidth = 512.0f;

	UPROPERTY(EditAnywhere, Category = "Camera", meta = (PinHiddenByDefault, InlineEditConditionToggle))
	uint8 bUseCameraAspectRatio : 1;

	/** Custom aspect ratio to use rather than using the width & height to automatically calculate. */
	UPROPERTY(EditAnywhere, Category = "Camera", meta = (EditCondition = "bUseCameraAspectRatio", ClampMin="0.01"))
	float CameraAspectRatio = 1.0f;

	/** Should we render just the component or the whole scene. */
	UPROPERTY(EditAnywhere, Category = "Environment")
	uint8 bRenderComponentOnly : 1;

	///** Type of level setup to use. */
	//UPROPERTY(EditAnywhere, Category = "Environment", meta = (PinHiddenByDefault, InlineEditConditionToggle))
	//uint8 bLoadLevel : 1;

	///** Used to determine type of level setup. */
	//UPROPERTY(EditAnywhere, Category = "Environment", meta = (EditCondition = "bLoadLevel"))
	//TSoftObjectPtr<class ULevel> LevelEnvironment;

	bool Equals(const UNiagaraBakerSettings& Other) const;

	int GetNumFrames() const { return FramesPerDimension.X * FramesPerDimension.Y; }

	float GetSeekDelta() const { return 1.0f / float(FramesPerSecond); }

	float GetAspectRatio(int32 iOutputTextureIndex) const;
	FVector2D GetOrthoSize(int32 iOutputTextureIndex) const;
	FVector GetCameraLocation() const;
	FMatrix GetViewMatrix() const;
	FMatrix GetProjectionMatrixForTexture(int32 iOutputTextureIndex) const;

	bool IsOrthographic() const { return CameraViewportMode != ENiagaraBakerViewMode::Perspective; }
	bool IsPerspective() const { return CameraViewportMode == ENiagaraBakerViewMode::Perspective; }

	// Get display info, the input time is expected to tbe relative, i.e. StartDuration is not taking into account
	FDisplayInfo GetDisplayInfo(float Time, bool bLooping) const;

	virtual void PostLoad() override;

#if WITH_EDITORONLY_DATA
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
};
