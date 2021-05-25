// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Engine/World.h"
#include "UObject/StrongObjectPtr.h"

class ACameraActor;
class ACompositingElement;
class UMediaPlayer;
class UMediaTexture;
class UCameraNodalOffsetAlgo;
class UCompositingElementMaterialPass;
class ULensDistortionModelHandlerBase;
class ULensFile;
class ULiveLinkCameraController;
class UTextureRenderTarget2D;

struct FGeometry;
struct FLensFileEvalData;
struct FPointerEvent;

/**
 * FNodalOffsetTool is the controller for the nodal offset tool panel.
 * It has the logic to bridge user input like selection of nodal offset algorithm or CG camera
 * with the actions that follow. It houses convenience functions used to generate the data
 * of what is presented to the user, and holds pointers to the relevant objects and structures.
 */
class FNodalOffsetTool : public TSharedFromThis<FNodalOffsetTool>
{
public:

	FNodalOffsetTool(ULensFile* InLensFile);
	~FNodalOffsetTool();

	/** Creates composite with CG and selected media source */
	void CreateComp();

	/** Returns the render target of the Comp */
	UTextureRenderTarget2D* GetRenderTarget() const;

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

	/** Selects the nodal offset algorithm by name */
	void SetNodalOffsetAlgo(const FName& AlgoName);

	/** Returns the currently selected nodal offset algorithm */
	UCameraNodalOffsetAlgo* GetNodalOffsetAlgo() const;

	/** Returns true if the media player is paused */
	bool IsPaused() const;

	/** Returns the latest data used when evaluating the lens */
	const FLensFileEvalData* GetLensFileEvalData() const;

	/** Returns the LensFile that this nodal offset tool is using */
	const ULensFile* GetLensFile();

	/** Returns the distortion handler used to distort the CG being displayed in the simulcam viewport */
	const ULensDistortionModelHandlerBase* GetDistortionHandler() const;

	/** Finds the LiveLinkCameraController used in the given CameraActor that is also using the given LensFile */
	ULiveLinkCameraController* FindLiveLinkCameraControllerWithLens(const ACameraActor* CameraActor, const ULensFile* InLensFile) const;

	/** Sets the media source url to be played. Returns true if the url is a valid media source */
	bool SetMediaSourceUrl(const FString& InMediaSourceUrl);
	
	/** Finds available media sources and adds their urls to the given array */
	void FindMediaSourceUrls(TArray<TSharedPtr<FString>>& OutMediaSourceUrls) const;

	/** Gets the current media source url being played. Empty if None */
	FString GetMediaSourceUrl() const;

public:

	/** Called by the UI when the Simulcam Viewport is clicked */
	void OnSimulcamViewportClicked(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent);

	/** Called by the UI when the user wants to save the nodal offset that the current algorithm is providing */
	void OnSaveCurrentNodalOffset();

private:

	/** Returns a namespaced version of the given name. Useful to generate names unique to this lens file */
	FString NamespacedName(const FString&& Name) const;

	/** Finds the world being used by the tool for finding and spawning objects */
	UWorld* GetWorld() const;

	/** Finds an existing comp element based on its name. */
	ACompositingElement* FindElement(const FString& Name) const;

	/** Spawns a new compositing element of the given class and parent element */
	TWeakObjectPtr<ACompositingElement> AddElement(ACompositingElement* Parent, FString& ClassPath, FString& ElementName) const;

	/** Releases resources used by the nodal offset tool, including the transient comp elements */
	void Cleanup();

	/** Convenience function that returns the first camera it finds that is using the lens associated with this object. */
	ACameraActor* FindFirstCameraWithCurrentLens() const;

	/** Enables distortion in the CG comp */
	void EnableDistortionInCG();

	/** Called by the core ticker */
	bool OnTick(float DeltaTime);

private:

	/** Strong pointer to the LensFile used by this object */
	TStrongObjectPtr<class ULensFile> LensFile;

	/** The parent comp element */
	TWeakObjectPtr<ACompositingElement> Comp;

	/** The CG layer comp element */
	TWeakObjectPtr<ACompositingElement> CGLayer;

	/** The MediaPlate comp element*/
	TWeakObjectPtr<ACompositingElement> MediaPlate;

	/** The output render target of the comp */
	TWeakObjectPtr<UTextureRenderTarget2D> RenderTarget;

	/** The media texture used by the media plate */
	TWeakObjectPtr<UMediaTexture> MediaTexture;

	/** The media player that is playing the selected media source */
	TStrongObjectPtr<UMediaPlayer> MediaPlayer;

	/** The material pass the does the CG + MediaPlate composite with a wiper weight */
	TWeakObjectPtr<UCompositingElementMaterialPass> MaterialPass;

	/** The currently selected camera */
	TWeakObjectPtr<ACameraActor> Camera;

	/** The currently selected nodal offset algorithm */
	TWeakObjectPtr<UCameraNodalOffsetAlgo> NodalOffsetAlgo;

	/** The delegate for the core ticker callback */
	FDelegateHandle TickerHandle;

	/** Pointer to the LensFileEvalData used in the current frame. Only valid during the current frame. */
	const FLensFileEvalData* LensFileEvalData;
};
