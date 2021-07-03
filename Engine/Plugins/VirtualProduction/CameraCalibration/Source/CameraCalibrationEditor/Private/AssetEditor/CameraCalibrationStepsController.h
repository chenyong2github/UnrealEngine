// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "UObject/StrongObjectPtr.h"


class ACameraActor;
class ACompositingElement;
class FCameraCalibrationToolkit;
class SWidget;
class UMediaPlayer;
class UMediaTexture;
class UCameraCalibrationStep;
class UCompositingElementMaterialPass;
class ULensDistortionModelHandlerBase;
class ULensFile;
class ULiveLinkCameraController;

struct FGeometry;
struct FLensFileEvalData;
struct FPointerEvent;


/**
 * Controller for SCameraCalibrationSteps, where the calibration steps are hosted in.
 */
class FCameraCalibrationStepsController : public TSharedFromThis<FCameraCalibrationStepsController>
{
public:

	FCameraCalibrationStepsController(TWeakPtr<FCameraCalibrationToolkit> InCameraCalibrationToolkit, ULensFile* InLensFile);
	~FCameraCalibrationStepsController();

	/** Initialize resources. */
	void Initialize();

	/** Returns the UI that this object controls */
	TSharedPtr<SWidget> BuildUI();

	/** Creates composite with CG and selected media source */
	void CreateComp();

	/** Returns the render target of the Comp */
	UTextureRenderTarget2D* GetRenderTarget() const;

	/** Returns the render target of the Media Plate */
	UTextureRenderTarget2D* GetMediaPlateRenderTarget() const;

	/** Creates a way to read the media plate pixels for processing by any calibration step */
	void CreateMediaPlateOutput();

	/** Returns the CG weight that is composited on top of the media */
	float GetWiperWeight() const;

	/** Sets the weight/alpha for that CG that is composited on top of the media. 0 means invisible. */
	void SetWiperWeight(float InWeight);

	/** Sets the camera used for the CG */
	void SetCamera(ACameraActor* InCamera);

	/** Returns the camera used for the CG */
	ACameraActor* GetCamera() const;

	/** Toggles the play/stop state of the media player */
	void TogglePlay();

	/** Returns true if the media player is paused */
	bool IsPaused() const;

	/** Set play on the media player */
	void Play();

	/** Set pause on the media player */
	void Pause();

	/** Returns the latest data used when evaluating the lens */
	const FLensFileEvalData* GetLensFileEvalData() const;

	/** Returns the LensFile that this tool is using */
	ULensFile* GetLensFile() const;

	/** Returns the distortion handler used to distort the CG being displayed in the simulcam viewport */
	const ULensDistortionModelHandlerBase* GetDistortionHandler() const;

	/** Finds the LiveLinkCameraController used in the given CameraActor that is also using the given LensFile */
	ULiveLinkCameraController* FindLiveLinkCameraController() const;

	/** Sets the media source url to be played. Returns true if the url is a valid media source */
	bool SetMediaSourceUrl(const FString& InMediaSourceUrl);

	/** Finds available media sources and adds their urls to the given array */
	void FindMediaSourceUrls(TArray<TSharedPtr<FString>>& OutMediaSourceUrls) const;

	/** Gets the current media source url being played. Empty if None */
	FString GetMediaSourceUrl() const;

	/** Gets the lens eval data for this frame that was cached in Tick */
	const FLensFileEvalData* GetLensFileEvalData();

	/** Returns the calibration steps */
	const TConstArrayView<TStrongObjectPtr<UCameraCalibrationStep>> GetCalibrationSteps() const;

	/** Returns the calibration steps */
	void SelectStep(const FName& Name);

	/** Calculates the normalized (0~1) coordinates in the simulcam viewport of the given mouse click */
	bool CalculateNormalizedMouseClickPosition(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent, FVector2D& OutPosition) const;

	/** Finds the world being used by the tool for finding and spawning objects */
	UWorld* GetWorld() const;

	/** Reads the pixels in the media plate */
	bool ReadMediaPixels(TArray<FColor>& Pixels, FIntPoint& Size, ETextureRenderTargetFormat& PixelFormat, FText& OutErrorMessage) const;

public:

	/** Called by the UI when the Simulcam Viewport is clicked */
	void OnSimulcamViewportClicked(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent);

private:

	/** Finds the LiveLinkCameraController used in the given CameraActor that is also using the given LensFile */
	ULiveLinkCameraController* FindLiveLinkCameraControllerWithLens(const ACameraActor* CameraActor, const ULensFile* InLensFile) const;

	/** Returns a namespaced version of the given name. Useful to generate names unique to this lens file */
	FString NamespacedName(const FString&& Name) const;

	/** Finds an existing comp element based on its name. */
	ACompositingElement* FindElement(const FString& Name) const;

	/** Spawns a new compositing element of the given class and parent element */
	TWeakObjectPtr<ACompositingElement> AddElement(ACompositingElement* Parent, FString& ClassPath, FString& ElementName) const;

	/** Releases resources used by the tool */
	void Cleanup();

	/** Convenience function that returns the first camera it finds that is using the lens associated with this object. */
	ACameraActor* FindFirstCameraWithCurrentLens() const;

	/** Enables distortion in the CG comp */
	void EnableDistortionInCG();

	/** Called by the core ticker */
	bool OnTick(float DeltaTime);

	/** Finds and creates the available calibration steps */
	void CreateSteps();

private:

	/** Pointer to the camera calibration toolkit */
	TWeakPtr<FCameraCalibrationToolkit> CameraCalibrationToolkit;

	/** Array of the calibration steps that this controller is managing */
	TArray<TStrongObjectPtr<UCameraCalibrationStep>> CalibrationSteps;

	/** The lens asset */
	TWeakObjectPtr<class ULensFile> LensFile;

	/** The parent comp element */
	TWeakObjectPtr<ACompositingElement> Comp;

	/** The CG layer comp element */
	TWeakObjectPtr<ACompositingElement> CGLayer;

	/** The MediaPlate comp element*/
	TWeakObjectPtr<ACompositingElement> MediaPlate;

	/** The output render target of the comp */
	TWeakObjectPtr<UTextureRenderTarget2D> RenderTarget;

	/** The output render target of the media plate */
	TWeakObjectPtr<UTextureRenderTarget2D> MediaPlateRenderTarget;

	/** The media texture used by the media plate */
	TWeakObjectPtr<UMediaTexture> MediaTexture;

	/** The media player that is playing the selected media source */
	TStrongObjectPtr<UMediaPlayer> MediaPlayer;

	/** The material pass the does the CG + MediaPlate composite with a wiper weight */
	TWeakObjectPtr<UCompositingElementMaterialPass> MaterialPass;

	/** The currently selected camera */
	TWeakObjectPtr<ACameraActor> Camera;

	/** The delegate for the core ticker callback */
	FDelegateHandle TickerHandle;

	/** Pointer to the LensFileEvalData used in the current frame. Only valid during the current frame. */
	const FLensFileEvalData* LensFileEvalData;

};
