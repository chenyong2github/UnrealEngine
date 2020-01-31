// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Modules/ModuleManager.h"
#include "MovieSceneCaptureDialogModule.h"
#include "SequencerBindingProxy.h"
#include "SequencerScriptingRange.h"
#include "SequencerTools.generated.h"

class UFbxExportOption;
class UAnimSequence;
class UPoseAsset;

DECLARE_DYNAMIC_DELEGATE_OneParam(FOnRenderMovieStopped, bool, bSuccess);

USTRUCT(BlueprintType)
struct FSequencerBoundObjects
{
	GENERATED_BODY()

	FSequencerBoundObjects()
	{}

	FSequencerBoundObjects(FSequencerBindingProxy InBindingProxy, const TArray<UObject*>& InBoundObjects)
		: BindingProxy(InBindingProxy)
		, BoundObjects(InBoundObjects)
	{}

	UPROPERTY(BlueprintReadWrite, Category=Binding)
	FSequencerBindingProxy BindingProxy;

	UPROPERTY(BlueprintReadWrite, Category=Binding)
	TArray<UObject*> BoundObjects;
};

/** 
 * This is a set of helper functions to access various parts of the Sequencer API via Python. Because Sequencer itself is not suitable for exposing, most functionality
 * gets wrapped by UObjects that have an easier API to work with. This UObject provides access to these wrapper UObjects where needed. 
 */
UCLASS(Transient, meta=(ScriptName="SequencerTools"))
class SEQUENCERSCRIPTINGEDITOR_API USequencerToolsFunctionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:

	/**
	* Attempts to render a sequence to movie based on the specified settings. This will automatically detect
	* if we're rendering via a PIE instance or a new process based on the passed in settings. Will return false
	* if the state is not valid (ie: null or missing required parameters, capture in progress, etc.), true otherwise.
	*/
	UFUNCTION(BlueprintCallable, Category = "Editor Scripting | Sequencer Tools | Movie Rendering")
	static bool RenderMovie(class UMovieSceneCapture* InCaptureSettings, FOnRenderMovieStopped OnFinishedCallback);

	/** 
	* Returns if Render to Movie is currently in progress.
	*/
	UFUNCTION(BlueprintCallable, Category = "Editor Scripting | Sequencer Tools | Movie Rendering")
	static bool IsRenderingMovie()
	{
		IMovieSceneCaptureDialogModule& MovieSceneCaptureModule = FModuleManager::Get().LoadModuleChecked<IMovieSceneCaptureDialogModule>("MovieSceneCaptureDialog");
		return MovieSceneCaptureModule.GetCurrentCapture().IsValid();
	}

	/**
	* Attempts to cancel an in-progress Render to Movie. Does nothing if there is no render in progress.
	*/
	UFUNCTION(BlueprintCallable, Category = "Editor Scripting | Sequencer Tools | Movie Rendering")
	static void CancelMovieRender();

public:

	/*
	 * Retrieve all objects currently bound to the specified binding identifiers. The sequence will be evaluated in lower bound of the specified range, 
	 * which allows for retrieving spawnables in that period of time.
	 */
	UFUNCTION(BlueprintCallable, Category = "Editor Scripting | Sequencer Tools")
	static TArray<FSequencerBoundObjects> GetBoundObjects(UWorld* InWorld, ULevelSequence* InSequence, const TArray<FSequencerBindingProxy>& InBindings, const FSequencerScriptingRange& InRange);

	/*
	 * Get the object bindings for the requested object. The sequence will be evaluated in lower bound of the specified range, 
	 * which allows for retrieving spawnables in that period of time.
	 */
	UFUNCTION(BlueprintCallable, Category = "Editor Scripting | Sequencer Tools")
	static TArray<FSequencerBoundObjects> GetObjectBindings(UWorld* InWorld, ULevelSequence* InSequence, const TArray<UObject*>& InObject, const FSequencerScriptingRange& InRange);

public:
	/*
	 * Export Passed in Bindings to FBX
	 *
	 * @InWorld World to export
	 * @InSequence Sequence to export
	 * @InBindings Bindings to export
	 * @InFBXFileName File to create
	 */
	UFUNCTION(BlueprintCallable, Category = "Editor Scripting | Sequencer Tools | FBX")
	static bool ExportFBX(UWorld* InWorld, ULevelSequence* InSequence, const TArray<FSequencerBindingProxy>& InBindings, UFbxExportOption* OverrideOptions,const FString& InFBXFileName);

	/*
	 * Export Passed in Binding as an Anim Seqquence.
	 *
	 * @InWorld World to export
	 * @InSequence Sequence to export
	 * @AnimSequence The AnimSequence to save into.
	 * @InBinding Binding to export that has a skelmesh component on it
	 * @InAnimSequenceFilename File to create
	 */
	UFUNCTION(BlueprintCallable, Category = "Editor Scripting | Sequencer Tools | Animation")
	static bool ExportAnimSequence(UWorld* World, ULevelSequence*  Sequence, UAnimSequence* AnimSequence, const FSequencerBindingProxy& Binding);

	/*
	 * Import Passed in Bindings to FBX
	 *
	 * @InWorld World to import to
	 * @InSequence InSequence to import
	 * @InBindings InBindings to import
	 * @InImportFBXSettings Settings to control import.
	 * @InImportFileName Path to fbx file to create
	 */
	UFUNCTION(BlueprintCallable, Category = "Editor Scripting | Sequencer Tools | FBX")
	static bool ImportFBX(UWorld* InWorld, ULevelSequence* InSequence, const TArray<FSequencerBindingProxy>& InBindings, UMovieSceneUserImportFBXSettings* InImportFBXSettings, const FString&  InImportFilename);

};