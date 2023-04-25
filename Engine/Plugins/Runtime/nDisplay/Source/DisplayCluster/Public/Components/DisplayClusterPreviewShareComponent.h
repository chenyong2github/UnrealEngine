// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/ActorComponent.h"
#include "Containers/Map.h"

#include "DisplayClusterPreviewShareComponent.generated.h"


class UMediaCapture;
class UMediaOutput;
class UMediaPlayer;
class UMediaSource;
class UMediaTexture;
class UTexture;


/** Available sharing modes */
UENUM()
enum class EDisplayClusterPreviewShareMode
{
	/** Sharing disabled */
	None,

	/** Sends the viewport textures for sharing */
	Send,

	/** Receives textures to replace the viewport textures with */
	Receive,
};


/**
 * nDisplay Viewport preview share component
 * 
 * It shares using Shared Memory Media the viewport textures of the parent nDisplay Actor.
 * It should only be added to DisplayClusterRootActor instances, and only one component per instance.
 * The way it works is that the sender generates a unique name for each viewport and captures its texture
 * by getting a pointer to it from the corresponding Preview Component.
 * The receiver will read it using the corresponding media source, and use the Texture Replace functionality
 * in the nDisplay actor viewports to have them used and displayed.
 */
UCLASS(ClassGroup = (DisplayCluster), meta = (BlueprintSpawnableComponent), HideCategories = (Activation, Collision, Cooking))
class DISPLAYCLUSTER_API UDisplayClusterPreviewShareComponent
	: public UActorComponent
{
	GENERATED_BODY()

public:

	/** Constructor */
	UDisplayClusterPreviewShareComponent(const FObjectInitializer& ObjectInitializer);

#if WITH_EDITOR // Bulk wrap with WITH_EDITOR until preview is supported in other modes.

	//~ UActorComponent interface begin
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void DestroyComponent(bool bPromoteChildren = false) override;
	//~ UActorComponent interface end

#endif // WITH_EDITOR

	//~ UObject interface begin
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif // WITH_EDITOR
	//~ UObject interface end

	/** Sets the sharing mode */
	UFUNCTION(BlueprintCallable, Category = "Sharing")
	void SetMode(EDisplayClusterPreviewShareMode NewMode);

	/** Sets the unique name, which should match between sender and receiver of viewport textures */
	UFUNCTION(BlueprintCallable, Category = "Sharing")
	void SetUniqueName(const FString& NewUniqueName);

public:

#if WITH_EDITORONLY_DATA

	/** Current sharing mode of this component */
	UPROPERTY(Transient, EditAnywhere, Setter=SetMode, BlueprintSetter=SetMode, Category=Sharing)
	EDisplayClusterPreviewShareMode Mode = EDisplayClusterPreviewShareMode::None;

	/** Current unique name of this component, which should match between sender and receiver of viewport textures */
	UPROPERTY(EditAnywhere, Setter = SetUniqueName, BlueprintSetter = SetUniqueName, Category = Sharing)
	FString UniqueName;

#endif // WITH_EDITORONLY_DATA

private:

#if WITH_EDITOR

	/** Closes all media related objects (i.e. media captures and media players) */
	void CloseAllMedia();

	/** True if this component is valid to be actively used for sharing. E.g. CDOs are not considered active. */
	bool AllowedToShare() const;

	/** Called when the sharing mode was changed, so that it can update its internal state accordingly */
	void ModeChanged();

	/** Generates a string id for the viewport, used as a key to store data about it */
	FString GenerateViewportKey(const FString& ActorName, const FString& UniqueViewportName) const;

	/** Generates a Unique Name for the media being shared */
	FString GenerateMediaUniqueName(const FString& NodeName, const FString& ViewportName) const;

	/** Logic that should run every tick when in Send mode */
	void TickSend();

	/** Logic that should run every tick when in Receive mode */
	void TickReceive();

	/** Restores settings in the nDisplay actor that were altered by this component to achieve its intended purpose */
	void RestoreRootActorOriginalSettings();

	/** Enables/Disables component ticking */
	void SetTickEnable(const bool bEnable);

#endif // WITH_EDITOR

private:

#if WITH_EDITORONLY_DATA

	/** Media Outputs associated with the given viewport unique names */
	UPROPERTY(Transient)
	TMap<FString, TObjectPtr<UMediaOutput>> MediaOutputs;

	/** Media Captures associated with the given viewport unique names */
	UPROPERTY(Transient)
	TMap<FString, TObjectPtr<UMediaCapture>> MediaCaptures;

	/** Media Sources associated with the given viewport unique names */
	UPROPERTY(Transient)
	TMap<FString, TObjectPtr<UMediaSource>> MediaSources;

	/** Media Players associated with the given viewport unique names */
	UPROPERTY(Transient)
	TMap<FString, TObjectPtr<UMediaPlayer>> MediaPlayers;

	/** Media Textures associated with the given viewport unique names */
	UPROPERTY(Transient)
	TMap<FString, TObjectPtr<UMediaTexture>> MediaTextures;

	/** Cache of original Texture Replace Source Textures associated with the given viewport unique names. Used when restoring the original state */
	UPROPERTY(Transient)
	TMap<FString, TObjectPtr<UTexture>> OriginalSourceTextures;

	/** Cache of original Texture Replace enable boolean associated with the given viewport unique names. Used when restoring the original state */
	UPROPERTY(Transient)
	TMap<FString, bool> OriginalTextureReplaces;

#endif // WITH_EDITORONLY_DATA

};
