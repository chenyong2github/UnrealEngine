// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IMovieScenePlayer.h"

#include "EditorUndoClient.h"
#include "UObject/GCObject.h"
#include "NiagaraCurveOwner.h"
#include "ViewModels/TNiagaraViewModelManager.h"
#include "ISequencer.h"

#include "TickableEditorObject.h"
#include "ISequencerModule.h"
#include "UObject/ObjectKey.h"

class UNiagaraSystem;
class UNiagaraComponent;
class UNiagaraSequence;
class UMovieSceneNiagaraEmitterTrack;
struct FNiagaraVariable;
struct FNiagaraEmitterHandle;
class FNiagaraEmitterHandleViewModel;
class FNiagaraSystemScriptViewModel;
class UNiagaraStackViewModel;
class UNiagaraStackEntry;
class UNiagaraSystemSelectionViewModel;
class FNiagaraSystemInstance;
class ISequencer;
struct FAssetData;
class UNiagaraSystemEditorData;
struct FRichCurve;
class UNiagaraEditorSettings;
struct FNiagaraParameterStore;
struct FEdGraphEditAction;
class UNiagaraNodeFunctionCall;
class FNiagaraEmitterViewModel;
class FNiagaraOverviewGraphViewModel;

/** Defines different editing modes for this system view model. */
enum class NIAGARAEDITOR_API ENiagaraSystemViewModelEditMode
{
	/** A system asset is being edited.  This assumes that emitters should be inheriting from a base version and that emitter editing will be restricted. */
	SystemAsset,
	/** An emitter asset is being edited.  In this mode the system scripts will not be editable and all emitter editing options are available. */
	EmitterAsset,
};

/** Defines options for the niagara System view model */
struct FNiagaraSystemViewModelOptions
{
	FNiagaraSystemViewModelOptions();

	/** Whether or not the user can edit emitters from the timeline. */
	bool bCanModifyEmittersFromTimeline;

	/** A delegate which is used to generate the content for the add menu in sequencer. */
	FOnGetAddMenuContent OnGetSequencerAddMenuContent;

	/** Whether or not the system represented by this view model can be automatically compiled.  True by default. */
	bool bCanAutoCompile;

	/** Whether or not the system represented by this view model can be simulated. True by default. */
	bool bCanSimulate;

	/** An optional unique id which associates this view model with messages in the message manager. */
	TOptional<const FGuid> MessageLogGuid;

	/** Gets the current editing mode for this system. */
	ENiagaraSystemViewModelEditMode EditMode;
};

struct FNiagaraStackModuleData
{
	UNiagaraNodeFunctionCall* ModuleNode;
	ENiagaraScriptUsage Usage;
	FGuid UsageId;
	int32 Index;
	FGuid EmitterHandleId;
};

/** A view model for viewing and editing a UNiagaraSystem. */
class FNiagaraSystemViewModel 
	: public TSharedFromThis<FNiagaraSystemViewModel>
	, public FGCObject
	, public FEditorUndoClient
	, public FTickableEditorObject
	, public TNiagaraViewModelManager<UNiagaraSystem, FNiagaraSystemViewModel>
{
public:
	DECLARE_MULTICAST_DELEGATE(FOnEmitterHandleViewModelsChanged);

	DECLARE_MULTICAST_DELEGATE(FOnCurveOwnerChanged);

	DECLARE_MULTICAST_DELEGATE(FOnPostSequencerTimeChange)

	DECLARE_MULTICAST_DELEGATE(FOnSystemCompiled);

	DECLARE_MULTICAST_DELEGATE(FOnPinnedEmittersChanged);

	DECLARE_MULTICAST_DELEGATE(FOnPinnedCurvesChanged);

	DECLARE_MULTICAST_DELEGATE(FOnPreClose);

public:
	struct FEmitterHandleToDuplicate
	{
		FString SystemPath;
		FGuid EmitterHandleId;
		bool operator==(const FEmitterHandleToDuplicate& Other) const
		{
			return SystemPath == Other.SystemPath && EmitterHandleId == Other.EmitterHandleId;
		}
	};
	
	/** Defines different multi-system reset modes for this system view model */
	enum class EMultiResetMode
	{
		/** Reset this instance. Then, if reset dependent systems is enabled in the editor through NiagaraEditorCommands, find all components that share this system and reset those components' system instances. */
		AllowResetAllInstances,
		/** Reset just this instance. */
		ResetThisInstance,
	};

	/** Defines different time reset modes for this system view model */
	enum class ETimeResetMode
	{
		/** If the current sequencer state and user settings allow, reset this system's time. */
		AllowResetTime,
		/** Keep this system's current time. */
		KeepCurrentTime,
	};

	/** Defines different initialization modes when resetting for this system view model */
	enum class EReinitMode
	{
		/** Reinitialize this system (pull in all changes) */
		ReinitializeSystem,
		/** Reset this system (do not pull in changes) */
		ResetSystem,
	};
	/** Creates a new view model with the supplied System and System instance. */
	FNiagaraSystemViewModel();
	
	/** Initializes this system view model with the supplied system and options. */
	void Initialize(UNiagaraSystem& InSystem, FNiagaraSystemViewModelOptions InOptions);

	~FNiagaraSystemViewModel();

	NIAGARAEDITOR_API FText GetDisplayName() const;

	/** Gets an array of the view models for the emitter handles owned by this System. */
	NIAGARAEDITOR_API const TArray<TSharedRef<FNiagaraEmitterHandleViewModel>>& GetEmitterHandleViewModels();

	/** Gets an emitter handle view model by ID. Returns an invalid shared ptr if it can't be found. */
	NIAGARAEDITOR_API TSharedPtr<FNiagaraEmitterHandleViewModel> GetEmitterHandleViewModelById(FGuid InEmitterHandleId);

	/** Gets an emitter handle view model for the given emitter. Returns an invalid shared ptr if it can't be found. */
	NIAGARAEDITOR_API TSharedPtr<FNiagaraEmitterHandleViewModel> GetEmitterHandleViewModelForEmitter(UNiagaraEmitter* InEmitter) const;

	/** Gets the view model for the System script. */
	TSharedPtr<FNiagaraSystemScriptViewModel> GetSystemScriptViewModel();

	/** Gets a niagara component for previewing the simulated System. */
	UNiagaraComponent* GetPreviewComponent();

	/** Gets the sequencer for this System for displaying the timeline. */
	TSharedPtr<ISequencer> GetSequencer();

	/** Gets the curve owner for the System represented by this view model, for use with the curve editor widget. */
	FNiagaraCurveOwner& GetCurveOwner();

	/** Get access to the underlying system*/
	NIAGARAEDITOR_API UNiagaraSystem& GetSystem() const;

	/** Gets whether or not emitters can be added from the timeline. */
	bool GetCanModifyEmittersFromTimeline() const;

	/** Gets the current editing mode for this system view model. */
	NIAGARAEDITOR_API ENiagaraSystemViewModelEditMode GetEditMode() const;

	/** Adds a new emitter to the System from an emitter asset data. */
	NIAGARAEDITOR_API void AddEmitterFromAssetData(const FAssetData& AssetData);

	/** Adds a new emitter to the System. */
	void AddEmitter(UNiagaraEmitter& Emitter);

	/** Deletes the emitters with the supplied ids from the system */
	void DeleteEmitters(TSet<FGuid> EmitterHandleIdsToDelete);

	/** Gets a multicast delegate which is called any time the array of emitter handle view models changes. */
	NIAGARAEDITOR_API FOnEmitterHandleViewModelsChanged& OnEmitterHandleViewModelsChanged();

	/** Gets a delegate which is called any time the data in the curve owner is changed internally by this view model. */
	FOnCurveOwnerChanged& OnCurveOwnerChanged();
	
	/** Gets a multicast delegate which is called whenever we've received and handled a sequencer time update.*/
	FOnPostSequencerTimeChange& OnPostSequencerTimeChanged();

	/** Gets a multicast delegate which is called whenever the system has been compiled. */
	FOnSystemCompiled& OnSystemCompiled();

	//~ FGCObject interface
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;

	//~ FEditorUndoClient interface
	virtual void PostUndo(bool bSuccess) override;
	virtual void PostRedo(bool bSuccess) override { PostUndo(bSuccess); }

	// ~ FTickableEditorObject
	virtual void Tick(float DeltaTime) override;
	virtual bool IsTickable() const override { return true; }
	virtual TStatId GetStatId() const override;

	/** Resets the System instance to initial conditions. Tries to resets system simulation time. Does not reset all systems that share its emitters.
	 * Does not reinitialize the system to pull in changes. Calls into overloaded ResetSystem(). */
	void ResetSystem();

	/** Resets the System instance to initial conditions on the next tick. Tries to resets system simulation time. Does not reset all systems that share its emitters.
	 * Does not reinitialize the system to pull in changes. Calls into overloaded ResetSystem(). */
	void RequestResetSystem();

	/** Resets the system instance to initial conditions. Optionally resets system simulation time. Optionally resets all systems that share its emitters. 
	 * Optionally reinitializes the system to pull in changes.
	 * @param TimeResetMode Defines whether the system being reset should try to reset its time to 0 or keep its current time.
	 * @param MultiResetMode Defines whether the system being reset should try to reset all other systems with which it shares emitters along with resetting itself.
	 * @param ReinitMode Defines whether the system should reinitialize and pull in changes or reset and keep its current state.
	 */
	void ResetSystem(ETimeResetMode TimeResetMode, EMultiResetMode MultiResetMode, EReinitMode ReinitMode);

	/** Compiles the spawn and update scripts. */
	void CompileSystem(bool bForce);

	/* Get the latest status of this view-model's script compilation.*/
	ENiagaraScriptCompileStatus GetLatestCompileStatus() const;

	/** Gets editor specific data which can be stored per system.  If this data hasn't been created the default version will be returned. */
	NIAGARAEDITOR_API UNiagaraSystemEditorData& GetEditorData() const;
	
	/** Act as if the system has been fully destroyed although references might persist.*/
	void Cleanup();

	/** Reinitializes all System instances, and rebuilds emitter handle view models and tracks. */
	void RefreshAll();

	/** Called to notify the system view model that one of the data objects in the system was modified. */
	void NotifyDataObjectChanged(UObject* ChangedObject);

	/** Updates all selected emitter's fixed bounds with their current dynamic bounds. */
	void UpdateEmitterFixedBounds();

	/** Isolates the supplied emitters.  This will remove all other emitters from isolation. */
	NIAGARAEDITOR_API void IsolateEmitters(TArray<FGuid> EmitterHandlesIdsToIsolate);

	/** Toggles the isolation state of a single emitter. */
	void ToggleEmitterIsolation(TSharedRef<FNiagaraEmitterHandleViewModel> InEmitterHandle);

	/** Export the current state of the system to text.*/
	void DumpToText(FString& ExportText);

	/** Keeps track of the emitters that are pinned in the stack UI, for persistence*/
	NIAGARAEDITOR_API TArray<TSharedRef<FNiagaraEmitterHandleViewModel>> GetPinnedEmitterHandles();

	/** Sets an internal state when an emitter was pinned/unpinned in the stack UI */
	NIAGARAEDITOR_API void SetEmitterPinnedState(TSharedRef<FNiagaraEmitterHandleViewModel> EmitterHandleModel, bool bPinnedState);

	/** Returns a multicast delegate that is broadcast when an emitter pinned state has changed */
	NIAGARAEDITOR_API FOnPinnedEmittersChanged& GetOnPinnedEmittersChanged();

	/** Checks whether or not an emitter is pinned in the stack UI*/
	NIAGARAEDITOR_API bool GetIsEmitterPinned(TSharedRef<FNiagaraEmitterHandleViewModel> EmitterHandleModel);

	NIAGARAEDITOR_API void NotifyPreSave();

	NIAGARAEDITOR_API void NotifyPreClose();

	NIAGARAEDITOR_API FOnPreClose& OnPreClose();
	
	/** Gets the system toolkit command list. */
	NIAGARAEDITOR_API TSharedPtr<FUICommandList> GetToolkitCommands();
	
	/** Returns a multicast delegate that is broadcast when pinned curves (shown in curve editor) state has changed*/
	NIAGARAEDITOR_API FOnPinnedCurvesChanged& GetOnPinnedCurvesChanged();

	/** Set the system toolkit command list. */
	void SetToolkitCommands(const TSharedRef<FUICommandList>& InToolkitCommands);

	/** Gets the stack module data for the provided module, for use in determining dependencies. */
	const TArray<FNiagaraStackModuleData>& GetStackModuleData(UNiagaraStackEntry* ModuleEntry);

	/** Gets the ViewModel for the system overview graph. */
	NIAGARAEDITOR_API TSharedPtr<FNiagaraOverviewGraphViewModel> GetOverviewGraphViewModel() const;

	/** Gets the stack view model representing this system. */
	NIAGARAEDITOR_API UNiagaraStackViewModel* GetSystemStackViewModel();

	/** Gets the a view model representing the selected entries in the overview. */
	NIAGARAEDITOR_API UNiagaraSystemSelectionViewModel* GetSelectionViewModel();

	/** Duplicates a set of emitters and refreshes everything.*/
	void DuplicateEmitters(TArray<FEmitterHandleToDuplicate> EmitterHandlesToDuplicate);

private:

	/** Sends message jobs to FNiagaraMessageManager for all compile events from the last compile. */
	void SendLastCompileMessageJobs() const;

	/** Sets up the preview component and System instance. */
	void SetupPreviewComponentAndInstance();

	/** Resets the emitter handle view models and tracks to remove data from them.  This must be called before modifying the emitter handle collection to prevent accessing invalid data. */
	void ResetEmitterHandleViewModelsAndTracks();

	/** Rebuilds the emitter handle view models. */
	void RefreshEmitterHandleViewModels();

	/** Rebuilds the sequencer tracks. */
	void RefreshSequencerTracks();

	/** Updates the data in the sequencer tracks for the specified emitter ids. */
	void UpdateSequencerTracksForEmitters(const TArray<FGuid>& EmitterIdsRequiringUpdate);

	/** Gets the sequencer emitter track for the supplied emitter handle view model. */
	UMovieSceneNiagaraEmitterTrack* GetTrackForHandleViewModel(TSharedRef<FNiagaraEmitterHandleViewModel> EmitterHandleViewModel);

	/** Sets up the sequencer for this emitter. */
	void SetupSequencer();

	/** Gets the content for the add menu in sequencer. */
	void GetSequencerAddMenuContent(FMenuBuilder& MenuBuilder, TSharedRef<ISequencer> Sequencer);

	/** Resets and rebuilds the data in the curve owner. */
	void ResetCurveData();

	/** Updates the compiled versions of data interfaces when their sources change. */
	void UpdateCompiledDataInterfaces(UNiagaraDataInterface* ChangedDataInterface);

	/** Called whenever a property on the emitter handle changes. */
	void EmitterHandlePropertyChanged(FGuid OwningEmitterHandleId);

	/** Called whenever the name on an emitter handle changes. */
	void EmitterHandleNameChanged();

	/** Called whenever a property on the emitter changes. */
	void EmitterPropertyChanged();

	/** 
	 * Called whenever a parameter store owned by the system changes.
	 * @param ChangedParameterStore The parameter store that changed.
	 * @param OwningScript The script that owns the parameter store, if there is one.
	 */
	void SystemParameterStoreChanged(const FNiagaraParameterStore& ChangedParameterStore, const UNiagaraScript* OwningScript);

	/** Called whenever an emitter's script graph changes. */
	void EmitterScriptGraphChanged(const FEdGraphEditAction& InAction, const UNiagaraScript& OwningScript, FGuid OwningEmitterHandleId);

	/** Called whenever the system script graph changes. */
	void SystemScriptGraphChanged(const FEdGraphEditAction& InAction);

	/**
	* Called whenever an emitter's parameter store owned by the system changes.
	* @param ChangedParameterStore The parameter store that changed.
	* @param OwningScript The script that owns the parameter store, if there is one.
	*/
	void EmitterParameterStoreChanged(const FNiagaraParameterStore& ChangedParameterStore, const UNiagaraScript& OwningScript);

	/** Updates the current simulation for a parameter changing, based on the current simulation options. */
	void UpdateSimulationFromParameterChange();

	/** Called when a script is compiled */
	void ScriptCompiled();

	/** Handles when a curve in the curve owner is changed by the curve editor. */
	void CurveChanged(FRichCurve* ChangedCurve, UObject* CurveOwnerObject);

	/** Called whenever the data in the sequence is changed. */
	void SequencerDataChanged(EMovieSceneDataChangeType DataChangeType);

	/** Called whenever the global time in the sequencer is changed. */
	void SequencerTimeChanged();

	/** Called whenever the current selection in the system changes. */
	void SystemSelectionChanged();

	/** Called whenever the track selection in sequencer changes. */
	void SequencerTrackSelectionChanged(TArray<UMovieSceneTrack*> SelectedTracks);

	/** Called whenever the section selection in sequencer changes. */
	void SequencerSectionSelectionChanged(TArray<UMovieSceneSection*> SelectedSections);

	/** Updates the current emitter handle selection base on the sequencer selection. */
	void UpdateEmitterHandleSelectionFromSequencer();

	/** Updates the sequencer selection based on the current emitter handle selection. */
	void UpdateSequencerFromEmitterHandleSelection();

	/** Called when the system instance on the preview component changes. */
	void PreviewComponentSystemInstanceChanged();

	/** Called whenever the System instance is initialized. */
	void SystemInstanceInitialized();

	/** Called whenever the System instance is reset.*/
	void SystemInstanceReset();

	/** Adds event handler for the system's scripts. */
	void AddSystemEventHandlers();

	/** Removes event handlers for the system's scripts. */
	void RemoveSystemEventHandlers();
	
	/** Adds event handler for the system's scripts. */
	void AddCurveEventHandlers();

	/** Removes event handlers for the system's scripts. */
	void RemoveCurveEventHandlers();

	/** sends a notification that pinned curves have changed status */
	void NotifyPinnedCurvesChanged();

	/** builds stack module data for use in module dependencies */
	void BuildStackModuleData(UNiagaraScript* Script, FGuid InEmitterHandleId, TArray<FNiagaraStackModuleData>& OutStackModuleData);

	/** Called whenever one of the owned stack viewmodels structure changes. */
	void StackViewModelStructureChanged();

private:
	/** The System being viewed and edited by this view model. */
	UNiagaraSystem* System;

	/** The component used for previewing the System in a viewport. */
	UNiagaraComponent* PreviewComponent;

	/** The system instance currently simulating this system if available. */
	FNiagaraSystemInstance* SystemInstance;

	/** The view models for the emitter handles owned by the System. */
	TArray<TSharedRef<FNiagaraEmitterHandleViewModel>> EmitterHandleViewModels;

	/** The view model for the System script. */
	TSharedPtr<FNiagaraSystemScriptViewModel> SystemScriptViewModel;

	/** A niagara sequence for displaying this System in the sequencer timeline. */
	UNiagaraSequence *NiagaraSequence;

	/** The sequencer instance viewing and editing the niagara sequence. */
	TSharedPtr<ISequencer> Sequencer;

	/** Flag which indicates we are setting the sequencer time directly in an internal operation. */
	bool bSettingSequencerTimeDirectly;

	/** The previous play status for sequencer timeline. */
	EMovieScenePlayerStatus::Type PreviousSequencerStatus;

	/** The previous time for the sequencer timeline. */
	float PreviousSequencerTime;

	/** Whether or not the user can edit emitters from the timeline. */
	bool bCanModifyEmittersFromTimeline;

	/** Whether or not the system represented by this view model can be automatically compiled. */
	bool bCanAutoCompile;

	/** Whether or not the system requires a compilation*/
	bool bForceAutoCompileOnce;

	/** Whether or not the system represented by this view model can be simulated. */
	bool bCanSimulate;

	/** The current editing mode for this view model. */
	ENiagaraSystemViewModelEditMode EditMode;

	/** A delegate which is used to generate the content for the add menu in sequencer. */
	FOnGetAddMenuContent OnGetSequencerAddMenuContent;

	/** A multicast delegate which is called any time the array of emitter handle view models changes. */
	FOnEmitterHandleViewModelsChanged OnEmitterHandleViewModelsChangedDelegate;

	/** A multicast delegate which is called when the contents of the curve owner is changed internally by this view model. */
	FOnCurveOwnerChanged OnCurveOwnerChangedDelegate;

	/** A multicast delegate which is called whenever we've received and handled a sequencer time update.*/
	FOnPostSequencerTimeChange OnPostSequencerTimeChangeDelegate;

	/** A multicast delegate which is called whenever the system has been compiled. */
	FOnSystemCompiled OnSystemCompiledDelegate;

	/** A multicast delegate which is called whenever this has been notified it's owner will be closing. */
	FOnPreClose OnPreCloseDelegate;

	/** A flag for preventing reentrancy when syncrhonizing sequencer data. */
	bool bUpdatingEmittersFromSequencerDataChange;

	/** A flag for preventing reentrancy when syncrhonizing sequencer data. */
	bool bUpdatingSequencerFromEmitterDataChange;

	/** A flag for preventing reentrancy when synchronizing system selection with sequencer selection */
	bool bUpdatingSystemSelectionFromSequencer;

	/** A flag for preventing reentrancy when synchronizing sequencer selection with system selection */
	bool bUpdatingSequencerSelectionFromSystem;

	/** A curve owner implementation for curves in a niagara System. */
	FNiagaraCurveOwner CurveOwner;

	TNiagaraViewModelManager<UNiagaraSystem, FNiagaraSystemViewModel>::Handle RegisteredHandle;

	/** Array of FNiagaraEmitterHandleViewModel representing emitters that are pinned in the stack UI*/
	TArray<TSharedRef<FNiagaraEmitterHandleViewModel>> PinnedEmitterHandles;

	/** A single param delegate which is called when the pinned status is changed. */
	FOnPinnedEmittersChanged OnPinnedChangedDelegate;

	/** A cached reference to the niagara editor settings .*/
	UNiagaraEditorSettings* EditorSettings;

	/** A mapping of script to the delegate handle for it's on parameter map changed delegate. */
	TMap<FObjectKey, FDelegateHandle> ScriptToOnParameterStoreChangedHandleMap;

	/** A handle to the on parameter changed delegate for the user parameter store. */
	FDelegateHandle UserParameterStoreChangedHandle;

	/** A flag indicating that a reset has been request on the next tick */
	bool bResetRequestPending;

	/** The system toolkit commands. */
	TWeakPtr<class FUICommandList> ToolkitCommands;

	/*A multicast delegate which is called any time the pinned status of the curves changes*/
	FOnPinnedCurvesChanged OnPinnedCurvesChangedDelegate;

	/*An array of data interfaces used to keep track of how curves are changed by the user*/
	TArray<UNiagaraDataInterfaceCurveBase*> ShownCurveDataInterfaces;

	/** A flag which indicates that a compile has been requested, but has not completed. */
	bool bCompilePendingCompletion;

	/** The cache of stack module data for each emitter and for the system */
	TMap<FGuid, TArray<FNiagaraStackModuleData>> GuidToCachedStackModuleData;
	
	/** A handle to the on graph changed delegate for the system script. */
	FDelegateHandle SystemScriptGraphChangedHandler;

	/** An array of emitter handle ids which need their sequencer tracks refreshed next frame. */
	TArray<FGuid> EmitterIdsRequiringSequencerTrackUpdate;

	/** GUID used when sending message jobs to FNiagaraMessageManager for notifying the FNiagaraMessageLogViewModel with the same GUID key */
	TOptional<const FGuid> SystemMessageLogGuidKey;

	/** ViewModel for the system overview graph */
	TSharedPtr<FNiagaraOverviewGraphViewModel> OverviewGraphViewModel;

	UNiagaraStackViewModel* SystemStackViewModel;

	UNiagaraSystemSelectionViewModel* SelectionViewModel;
};