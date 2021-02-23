// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Modules/ModuleManager.h"
#include "MovieSceneCaptureDialogModule.h"
#include "Channels/MovieSceneEvent.h"
#include "SequencerBindingProxy.h"
#include "SequencerScriptingRange.h"
#include "SequencerTools.generated.h"

class UFbxExportOption;
class UAnimSequenceExportOption;
class UAnimSequence;
class UPoseAsset;
class UMovieSceneSequencePlayer;
class UTemplateSequence;

class UMovieSceneEventSectionBase;
class UK2Node_CustomEvent;

class UAnimSeqExportOption;
class UMovieSceneUserImportFBXControlRigSettings;
DECLARE_DYNAMIC_DELEGATE_OneParam(FOnRenderMovieStopped, bool, bSuccess);

USTRUCT(BlueprintType)
struct SEQUENCERSCRIPTINGEDITOR_API FSequencerBoundObjects
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

/** Wrapper around result of quick binding for event track in sequencer. */
USTRUCT(BlueprintType)
struct FSequencerQuickBindingResult
{
	GENERATED_BODY()

	FSequencerQuickBindingResult() : EventEndpoint(nullptr) {}

	/** Actual endpoint wrapped by this structure.  */
	UPROPERTY()
	UK2Node_CustomEvent* EventEndpoint;

	/** Names of the payload variables of the event. */
	UPROPERTY(BlueprintReadOnly, Category = Data)
	TArray<FString> PayloadNames;

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
	static bool ExportLevelSequenceFBX(UWorld* InWorld, ULevelSequence* InSequence, const TArray<FSequencerBindingProxy>& InBindings, UFbxExportOption* OverrideOptions, const FString& InFBXFileName);

	UFUNCTION(BlueprintCallable, Category = "Editor Scripting | Sequencer Tools | FBX")
	static bool ExportTemplateSequenceFBX(UWorld* InWorld, UTemplateSequence* InSequence, const TArray<FSequencerBindingProxy>& InBindings, UFbxExportOption* OverrideOptions, const FString& InFBXFileName);

	UE_DEPRECATED(4.27, "Please use ExportLevelSequenceFBX instead")
	static bool ExportFBX(UWorld* InWorld, ULevelSequence* InSequence, const TArray<FSequencerBindingProxy>& InBindings, UFbxExportOption* OverrideOptions,const FString& InFBXFileName) { return ExportLevelSequenceFBX(InWorld, InSequence, InBindings, OverrideOptions, InFBXFileName); }

	/*
	 * Export Passed in Binding as an Anim Seqquence.
	 *
	 * @InWorld World to export
	 * @InSequence Sequence to export
	 * @AnimSequence The AnimSequence to save into.
	 * @ExportOption The export options for the sequence.
	 * @InBinding Binding to export that has a skelmesh component on it
	 * @InAnimSequenceFilename File to create
	 */
	UFUNCTION(BlueprintCallable, Category = "Editor Scripting | Sequencer Tools | Animation")
	static bool ExportAnimSequence(UWorld* World, ULevelSequence*  Sequence, UAnimSequence* AnimSequence, UAnimSeqExportOption* ExportOption, const FSequencerBindingProxy& Binding);

	/*
	 * Import FBX onto Passed in Bindings
	 *
	 * @InWorld World to import to
	 * @InSequence InSequence to import
	 * @InBindings InBindings to import
	 * @InImportFBXSettings Settings to control import.
	 * @InImportFileName Path to fbx file to import from
	 * @InPlayer Player to bind to
	 */
	UFUNCTION(BlueprintCallable, Category = "Editor Scripting | Sequencer Tools | FBX")
	static bool ImportLevelSequenceFBX(UWorld* InWorld, ULevelSequence* InSequence, const TArray<FSequencerBindingProxy>& InBindings, UMovieSceneUserImportFBXSettings* InImportFBXSettings, const FString& InImportFilename);

	UFUNCTION(BlueprintCallable, Category = "Editor Scripting | Sequencer Tools | FBX")
	static bool ImportTemplateSequenceFBX(UWorld* InWorld, UTemplateSequence* InSequence, const TArray<FSequencerBindingProxy>& InBindings, UMovieSceneUserImportFBXSettings* InImportFBXSettings, const FString& InImportFilename);

	UE_DEPRECATED(4.27, "Please use ImportLevelSequenceFBX instead")
	static bool ImportFBX(UWorld* InWorld, ULevelSequence* InSequence, const TArray<FSequencerBindingProxy>& InBindings, UMovieSceneUserImportFBXSettings* InImportFBXSettings, const FString& InImportFilename) { return ImportLevelSequenceFBX(InWorld, InSequence, InBindings, InImportFBXSettings, InImportFilename); }

public:
	/**
	 * Create an event from a previously created blueprint endpoint and a payload. The resulting event should be added only
	 * to a channel of the section that was given as a parameter.
	 * @param InSequence Main level sequence that holds the event track and to which the resulting event should be added.
	 * @param InSection Section of the event track of the main sequence.
	 * @param InEndpoint Previously created endpoint.
	 * @param InPayload Values passed as payload to event, count must match the numbers of payload variable names in @InEndpoint.
	 * @return The created movie event.
	 * @see CreateQuickBinding
	 */
	UFUNCTION(BlueprintCallable, Category = "Editor Scripting | Sequencer Tools | Animation")
	static FMovieSceneEvent CreateEvent(UMovieSceneSequence* InSequence, UMovieSceneEventSectionBase* InSection, const FSequencerQuickBindingResult& InEndpoint, const TArray<FString>& InPayload);

	/**
	 * Check if an endpoint is valid and can be used to create movie scene event.
	 * @param InEndpoint Endpoint to check.
	 */
	UFUNCTION(BlueprintCallable, Category = "Editor Scripting | Sequencer Tools | Animation")
	static bool IsEventEndpointValid(const FSequencerQuickBindingResult& InEndpoint);

	/**
	 * Create a quick binding to an actor's member method to be used in an event sequence.
	 * @param InActor Actor that will be bound
	 * @param InFunctionName Name of the method, as it is displayed in the Blueprint Editor. eg. "Set Actor Scale 3D"
	 * @param bCallInEditor Should the event be callable in editor.
	 * @return The created binding.
	 */
	UFUNCTION(BlueprintCallable, Category = "Editor Scripting | Sequencer Tools | Animation")
	static FSequencerQuickBindingResult CreateQuickBinding(UMovieSceneSequence* InSequence, UObject* InObject, const FString& InFunctionName, bool bCallInEditor);

	/*
	 * Import FBX onto a control rig with the specified track name
	 *
	 * @InWorld World to import to
	 * @InSequence InSequence to import
	 * @ActorWithControlRigTrack ActorWithControlRigTrack The name of the actor with the control rig track we are importing onto
	 * @SelectedControlRigNames  List of selected control rig names. Will use them if  ImportFBXControlRigSettings->bImportOntoSelectedControls is true
	 * @ImportFBXControlRigSettings Settings to control import.
	 * @InImportFileName Path to fbx file to create
	 */
	UFUNCTION(BlueprintCallable, Category = "Editor Scripting | Sequencer Tools | FBX")
	static bool ImportFBXToControlRig(UWorld* World, ULevelSequence* InSequence, const FString& ActorWithControlRigTrack, const TArray<FString>& SelectedControlRigNames,
		UMovieSceneUserImportFBXControlRigSettings* ImportFBXControlRigSettings,
		const FString& ImportFilename);
};