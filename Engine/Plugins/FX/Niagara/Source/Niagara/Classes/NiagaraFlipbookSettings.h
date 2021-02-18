// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Camera/CameraTypes.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "Engine/TextureRenderTarget2D.h"

#include "NiagaraFlipbookSettings.generated.h"

UENUM()
enum class ENiagaraFlipbookViewMode
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
struct FNiagaraFlipbookTextureSource
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Source")
	FName SourceName;
};

USTRUCT()
struct FNiagaraFlipbookTextureSettings
{
	GENERATED_BODY()

	/** Optional output name, if left empty a name will be auto generated using the index of the texture/ */
	UPROPERTY(EditAnywhere, Category = "Texture")
	FName OutputName;

	/** Source visualization we should capture, i.e. Scene Color, World Normal, etc */
	UPROPERTY(EditAnywhere, Category = "Texture")
	FNiagaraFlipbookTextureSource SourceBinding;

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
};

UCLASS()
class NIAGARA_API UNiagaraFlipbookSettings : public UObject
{
	GENERATED_BODY()

public:
	UNiagaraFlipbookSettings(const FObjectInitializer& Init);

	/** Time we start the capture at. */
	UPROPERTY(EditAnywhere, Category="Timeline")
	float StartSeconds = 0.0f;

	/** Duration in seconds to take the capture over. */
	UPROPERTY(EditAnywhere, Category = "Timeline")
	float DurationSeconds = 4.0f;

	/** Number of frames in each dimension. */
	UPROPERTY(EditAnywhere, Category = "Texture")
	FIntPoint FramesPerDimension = FIntPoint(8, 8);

	/** List of output textures we will generated. */
	UPROPERTY(EditAnywhere, Category = "Texture")
	TArray<FNiagaraFlipbookTextureSettings> OutputTextures;

	/** Current active viewport we will render the flipbook from. */
	UPROPERTY(EditAnywhere, Category = "Camera")
	ENiagaraFlipbookViewMode CameraViewportMode = ENiagaraFlipbookViewMode::Perspective;

	/** Per viewport camera position.. */
	UPROPERTY(EditAnywhere, Category = "Camera")
	FVector CameraViewportLocation[(int)ENiagaraFlipbookViewMode::Num];

	/** Per viewport camera rotation.. */
	UPROPERTY(EditAnywhere, Category = "Camera")
	FRotator CameraViewportRotation[(int)ENiagaraFlipbookViewMode::Num];

	/** Camera FOV to use when in perspective mode. */
	UPROPERTY(EditAnywhere, Category = "Camera", meta=(EditCondition="CameraViewportMode == ENiagaraFlipbookViewMode::Perspective"))
	float CameraFOV = 90.0f;

	/** Camera Orthographic size to use with in orthographic mode. */
	UPROPERTY(EditAnywhere, Category = "Camera", meta=(EditCondition="CameraViewportMode != ENiagaraFlipbookViewMode::Perspective"))
	FVector2D CameraOrthoSize = FVector2D(512.0f, 512.0f);

	/** Should we render just the component or the whole scene . */
	UPROPERTY(EditAnywhere, Category = "Environment")
	uint8 bRenderComponentOnly : 1;

	/** Type of level setup to use. */
	UPROPERTY(EditAnywhere, Category = "Environment", meta = (PinHiddenByDefault, InlineEditConditionToggle))
	uint8 bLoadLevel : 1;

	/** Used to determine type of level setup. */
	UPROPERTY(EditAnywhere, Category = "Environment", meta = (EditCondition = "bLoadLevel"))
	TSoftObjectPtr<class ULevel> LevelEnvironment;

	FVector GetCameraLocation() const;
	FMatrix GetViewMatrix() const;
	FMatrix GetProjectionMatrixForTexture(int32 iOutputTextureIndex) const;

	virtual void PostLoad() override;

#if WITH_EDITORONLY_DATA
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
};
