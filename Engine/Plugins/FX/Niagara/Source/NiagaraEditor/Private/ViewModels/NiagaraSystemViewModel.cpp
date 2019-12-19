// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "ViewModels/NiagaraSystemViewModel.h"
#include "NiagaraSystem.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraEmitter.h"
#include "NiagaraEmitterEditorData.h"
#include "NiagaraScriptSource.h"
#include "NiagaraEditorUtilities.h"
#include "ViewModels/NiagaraEmitterHandleViewModel.h"
#include "ViewModels/NiagaraEmitterViewModel.h"
#include "ViewModels/NiagaraSystemSelectionViewModel.h"
#include "ViewModels/Stack/NiagaraStackViewModel.h"
#include "ViewModels/Stack/NiagaraStackGraphUtilities.h"
#include "ViewModels/Stack/NiagaraStackEntry.h"
#include "NiagaraSystemScriptViewModel.h"
#include "NiagaraScriptGraphViewModel.h"
#include "NiagaraSequence.h"
#include "MovieSceneNiagaraEmitterTrack.h"
#include "NiagaraSystemInstance.h"
#include "NiagaraEditorSettings.h"
#include "NiagaraSystemEditorData.h"
#include "NiagaraGraph.h"
#include "NiagaraNodeInput.h"
#include "NiagaraEditorModule.h"
#include "NiagaraNodeFunctionCall.h"
#include "EdGraphSchema_NiagaraSystemOverview.h"

#include "Editor.h"

#include "ScopedTransaction.h"
#include "MovieScene.h"
#include "ISequencerModule.h"
#include "EditorSupportDelegates.h"
#include "Modules/ModuleManager.h"
#include "ViewModels/TNiagaraViewModelManager.h"
#include "Math/NumericLimits.h"
#include "NiagaraComponent.h"
#include "MovieSceneFolder.h"
#include "Channels/MovieSceneChannelProxy.h"
#include "Framework/Commands/UICommandList.h"
#include "NiagaraMessageManager.h"
#include "ViewModels/NiagaraOverviewGraphViewModel.h"
#include "Framework/Application/SlateApplication.h"

DECLARE_CYCLE_STAT(TEXT("Niagara - SystemViewModel - CompileSystem"), STAT_NiagaraEditor_SystemViewModel_CompileSystem, STATGROUP_NiagaraEditor);

#define LOCTEXT_NAMESPACE "NiagaraSystemViewModel"

template<> TMap<UNiagaraSystem*, TArray<FNiagaraSystemViewModel*>> TNiagaraViewModelManager<UNiagaraSystem, FNiagaraSystemViewModel>::ObjectsToViewModels{};

FNiagaraSystemViewModelOptions::FNiagaraSystemViewModelOptions()
	: bCanAutoCompile(true)
	, bCanSimulate(true)
{
}

FNiagaraSystemViewModel::FNiagaraSystemViewModel()
	: System(nullptr)
	, PreviewComponent(nullptr)
	, SystemInstance(nullptr)
	, NiagaraSequence(nullptr)
	, bSettingSequencerTimeDirectly(false)
	, bForceAutoCompileOnce(false)
	, bUpdatingEmittersFromSequencerDataChange(false)
	, bUpdatingSequencerFromEmitterDataChange(false)
	, bUpdatingSystemSelectionFromSequencer(false)
	, bUpdatingSequencerSelectionFromSystem(false)
	, EditorSettings(GetMutableDefault<UNiagaraEditorSettings>())
	, bResetRequestPending(false)
	, bCompilePendingCompletion(false)
	, SystemStackViewModel(nullptr)
	, SelectionViewModel(nullptr)
{
	GEditor->RegisterForUndo(this);
}

void FNiagaraSystemViewModel::Initialize(UNiagaraSystem& InSystem, FNiagaraSystemViewModelOptions InOptions)
{
	System = &InSystem;
	RegisteredHandle = RegisterViewModelWithMap(System, this);

	bCanModifyEmittersFromTimeline = InOptions.bCanModifyEmittersFromTimeline;
	bCanAutoCompile = InOptions.bCanAutoCompile;
	bCanSimulate = InOptions.bCanSimulate;
	EditMode = InOptions.EditMode;
	OnGetSequencerAddMenuContent = InOptions.OnGetSequencerAddMenuContent;
	SystemMessageLogGuidKey = InOptions.MessageLogGuid;	
	
	SelectionViewModel = NewObject<UNiagaraSystemSelectionViewModel>(GetTransientPackage());
	SelectionViewModel->Initialize(this->AsShared());
	SelectionViewModel->OnEmitterHandleIdSelectionChanged().AddSP(this, &FNiagaraSystemViewModel::SystemSelectionChanged);

	SystemScriptViewModel = MakeShared<FNiagaraSystemScriptViewModel>();
	SystemScriptViewModel->Initialize(GetSystem());

	OverviewGraphViewModel = MakeShared<FNiagaraOverviewGraphViewModel>();
	OverviewGraphViewModel->Initialize(this->AsShared());

	SystemStackViewModel = NewObject<UNiagaraStackViewModel>(GetTransientPackage());
	SystemStackViewModel->InitializeWithViewModels(this->AsShared(), TSharedPtr<FNiagaraEmitterHandleViewModel>(), FNiagaraStackViewModelOptions(true, false));
	SystemStackViewModel->OnStructureChanged().AddSP(this, &FNiagaraSystemViewModel::StackViewModelStructureChanged);

	SetupPreviewComponentAndInstance();
	SetupSequencer();
	RefreshAll();
	AddSystemEventHandlers();
	SendLastCompileMessageJobs();
}

void FNiagaraSystemViewModel::DumpToText(FString& ExportText)
{
	TSet<UObject*> ExportObjs;
	ExportObjs.Add(System);
	FEdGraphUtilities::ExportNodesToText(ExportObjs, ExportText);
}

void FNiagaraSystemViewModel::Cleanup()
{
	UE_LOG(LogNiagaraEditor, Warning, TEXT("Cleanup System view model %p"), this);

	if (SystemInstance)
	{
		SystemInstance->OnInitialized().RemoveAll(this);
		SystemInstance->OnReset().RemoveAll(this);
		SystemInstance = nullptr;
	}

	if (PreviewComponent)
	{
		PreviewComponent->OnSystemInstanceChanged().RemoveAll(this);
		PreviewComponent->DeactivateImmediate();
		PreviewComponent = nullptr;
	}

	CurveOwner.EmptyCurves();

	GEditor->UnregisterForUndo(this);

	// Make sure that we clear out all of our event handlers
	UnregisterViewModelWithMap(RegisteredHandle);

	for (TSharedRef<FNiagaraEmitterHandleViewModel>& HandleRef : EmitterHandleViewModels)
	{
		HandleRef->OnPropertyChanged().RemoveAll(this);
		HandleRef->OnNameChanged().RemoveAll(this);
		HandleRef->GetEmitterViewModel()->OnPropertyChanged().RemoveAll(this);
		HandleRef->GetEmitterViewModel()->OnScriptCompiled().RemoveAll(this);
		HandleRef->GetEmitterViewModel()->OnScriptGraphChanged().RemoveAll(this);
		HandleRef->GetEmitterViewModel()->OnScriptParameterStoreChanged().RemoveAll(this);
		HandleRef->GetEmitterStackViewModel()->OnStructureChanged().RemoveAll(this);
		HandleRef->Cleanup();
	}
	EmitterHandleViewModels.Empty();

	if (Sequencer.IsValid())
	{
		Sequencer->OnMovieSceneDataChanged().RemoveAll(this);
		Sequencer->OnGlobalTimeChanged().RemoveAll(this);
		Sequencer->GetSelectionChangedTracks().RemoveAll(this);
		Sequencer->GetSelectionChangedSections().RemoveAll(this);
		Sequencer.Reset();
	}

	RemoveSystemEventHandlers();
	SystemScriptViewModel.Reset();

	if (SystemStackViewModel != nullptr)
	{
		SystemStackViewModel->OnStructureChanged().RemoveAll(this);
		SystemStackViewModel->Finalize();
		SystemStackViewModel = nullptr;
	}

	if (SelectionViewModel != nullptr)
	{
		SelectionViewModel->OnEmitterHandleIdSelectionChanged().RemoveAll(this);
		SelectionViewModel->Finalize();
		SelectionViewModel = nullptr;
	}
}

FNiagaraSystemViewModel::~FNiagaraSystemViewModel()
{
	Cleanup();

	UE_LOG(LogNiagaraEditor, Warning, TEXT("Deleting System view model %p"), this);
}

FText FNiagaraSystemViewModel::GetDisplayName() const
{
	return FText::FromString(System->GetName());
}

const TArray<TSharedRef<FNiagaraEmitterHandleViewModel>>& FNiagaraSystemViewModel::GetEmitterHandleViewModels()
{
	return EmitterHandleViewModels;
}

TSharedPtr<FNiagaraEmitterHandleViewModel> FNiagaraSystemViewModel::GetEmitterHandleViewModelById(FGuid InEmitterHandleId)
{
	for (TSharedRef<FNiagaraEmitterHandleViewModel> EmitterHandleViewModel : EmitterHandleViewModels)
	{
		if (EmitterHandleViewModel->GetId() == InEmitterHandleId)
		{
			return EmitterHandleViewModel;
		}
	}
	return TSharedPtr<FNiagaraEmitterHandleViewModel>();
}

TSharedPtr<FNiagaraEmitterHandleViewModel> FNiagaraSystemViewModel::GetEmitterHandleViewModelForEmitter(UNiagaraEmitter* InEmitter) const
{
	for (TSharedRef<FNiagaraEmitterHandleViewModel> EmitterHandleViewModel : EmitterHandleViewModels)
	{
		if (EmitterHandleViewModel->GetEmitterViewModel()->GetEmitter() == InEmitter)
		{
			return EmitterHandleViewModel;
		}
	}
	return TSharedPtr<FNiagaraEmitterHandleViewModel>();
}

TSharedPtr<FNiagaraSystemScriptViewModel> FNiagaraSystemViewModel::GetSystemScriptViewModel()
{
	return SystemScriptViewModel;
}

void FNiagaraSystemViewModel::CompileSystem(bool bForce)
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraEditor_SystemViewModel_CompileSystem);
	FNiagaraEditorUtilities::KillSystemInstances(GetSystem());
	check(SystemScriptViewModel.IsValid());
	SystemScriptViewModel->CompileSystem(bForce);
	bCompilePendingCompletion = true;
}

ENiagaraScriptCompileStatus FNiagaraSystemViewModel::GetLatestCompileStatus() const
{
	check(SystemScriptViewModel.IsValid());
	return SystemScriptViewModel->GetLatestCompileStatus();
}

UNiagaraSystemEditorData& FNiagaraSystemViewModel::GetEditorData() const
{
	return *CastChecked<UNiagaraSystemEditorData>(GetSystem().GetEditorData(), ECastCheckedType::NullChecked);
}

UNiagaraComponent* FNiagaraSystemViewModel::GetPreviewComponent()
{
	return PreviewComponent;
}

TSharedPtr<ISequencer> FNiagaraSystemViewModel::GetSequencer()
{
	return Sequencer;
}

FNiagaraCurveOwner& FNiagaraSystemViewModel::GetCurveOwner()
{
	return CurveOwner;
}

UNiagaraSystem& FNiagaraSystemViewModel::GetSystem() const
{
	checkf(System != nullptr, TEXT("System view model not initialized before use."));
	return *System;
}

bool FNiagaraSystemViewModel::GetCanModifyEmittersFromTimeline() const
{
	return bCanModifyEmittersFromTimeline;
}

/** Gets the current editing mode for this system view model. */
ENiagaraSystemViewModelEditMode FNiagaraSystemViewModel::GetEditMode() const
{
	return EditMode;
}

TSharedPtr<FNiagaraEmitterHandleViewModel> FNiagaraSystemViewModel::AddEmitterFromAssetData(const FAssetData& AssetData)
{
	UNiagaraEmitter* Emitter = Cast<UNiagaraEmitter>(AssetData.GetAsset());
	if (Emitter != nullptr)
	{
		return AddEmitter(*Emitter);
	}

	return nullptr;
}

TSharedPtr<FNiagaraEmitterHandleViewModel> FNiagaraSystemViewModel::AddEmitter(UNiagaraEmitter& Emitter)
{
	// Reset view models before modifying the emitter handle list to prevent accessing deleted data.
	ResetEmitterHandleViewModelsAndTracks();

	// When editing an emitter asset the system is a placeholder and we don't want to make adding an emitter to it undoable.
	bool bSystemIsPlaceholder = EditMode == ENiagaraSystemViewModelEditMode::EmitterAsset;
	if (false == bSystemIsPlaceholder)
	{
		GEditor->BeginTransaction(LOCTEXT("AddEmitter", "Add emitter"));
	}

	const FGuid NewEmitterHandleId = FNiagaraEditorUtilities::AddEmitterToSystem(GetSystem(), Emitter);

	if (false == bSystemIsPlaceholder)
	{
		GEditor->EndTransaction();
	}

	if (GetSystem().GetNumEmitters() == 1 && EditorSettings->GetAutoPlay())
	{
		// When adding a new emitter to an empty system start playing.
		Sequencer->SetPlaybackStatus(EMovieScenePlayerStatus::Playing);
	}

	RefreshAll();

	TRange<float> SystemPlaybackRange = GetEditorData().GetPlaybackRange();
	TRange<float> EmitterPlaybackRange = GetEmitterHandleViewModelById(NewEmitterHandleId)->GetEmitterViewModel()->GetEditorData().GetPlaybackRange();
	TRange<float> NewSystemPlaybackRange = TRange<float>(
		FMath::Min(SystemPlaybackRange.GetLowerBoundValue(), EmitterPlaybackRange.GetLowerBoundValue()),
		FMath::Max(SystemPlaybackRange.GetUpperBoundValue(), EmitterPlaybackRange.GetUpperBoundValue()));

	GetEditorData().SetPlaybackRange(NewSystemPlaybackRange);

	TGuardValue<bool> UpdateGuard(bUpdatingSequencerFromEmitterDataChange, true);

	FFrameTime NewStartFrame = NewSystemPlaybackRange.GetLowerBoundValue() * NiagaraSequence->GetMovieScene()->GetTickResolution();
	int32 NewDuration = (NewSystemPlaybackRange.Size<float>() * NiagaraSequence->GetMovieScene()->GetTickResolution()).FrameNumber.Value;

	NiagaraSequence->GetMovieScene()->SetPlaybackRange(NewStartFrame.RoundToFrame(), NewDuration);

	TSharedPtr<FNiagaraEmitterHandleViewModel> NewEmitterHandleViewModel = GetEmitterHandleViewModelById(NewEmitterHandleId);
	TArray<UNiagaraStackEntry*> SelectedStackEntries;
	SelectedStackEntries.Add(NewEmitterHandleViewModel->GetEmitterStackViewModel()->GetRootEntry());
	SelectionViewModel->UpdateSelectedEntries(SelectedStackEntries, TArray<UNiagaraStackEntry*>(), true);

	bForceAutoCompileOnce = true;

	return NewEmitterHandleViewModel;
}

void FNiagaraSystemViewModel::DuplicateEmitters(TArray<FEmitterHandleToDuplicate> EmitterHandlesToDuplicate)
{
	if (EmitterHandlesToDuplicate.Num() <= 0)
	{
		return;
	}

	// Kill all system instances and reset view models before modifying the emitter handle list to prevent accessing deleted data.
	ResetEmitterHandleViewModelsAndTracks();
	FNiagaraEditorUtilities::KillSystemInstances(GetSystem());
	const FScopedTransaction DeleteTransaction(EmitterHandlesToDuplicate.Num() == 1
		? LOCTEXT("DuplicateEmitter", "Duplicate emitter")
		: LOCTEXT("DuplicateEmitters", "Duplicate emitters"));

	TSet<FName> EmitterHandleNames;
	for (const FNiagaraEmitterHandle& EmitterHandle : GetSystem().GetEmitterHandles())
	{
		EmitterHandleNames.Add(EmitterHandle.GetName());
	}
	
	GetSystem().Modify();
	for (FEmitterHandleToDuplicate& EmitterHandleToDuplicate : EmitterHandlesToDuplicate)
	{
		FNiagaraEmitterHandle HandleToDuplicate;
		for (TObjectIterator<UNiagaraSystem> OtherSystemIt; OtherSystemIt; ++OtherSystemIt)
		{
			UNiagaraSystem* OtherSystem = *OtherSystemIt;
			if (OtherSystem->GetPathName() == EmitterHandleToDuplicate.SystemPath)
			{
				for (const FNiagaraEmitterHandle& EmitterHandle : OtherSystem->GetEmitterHandles())
				{
					if (EmitterHandle.GetId() == EmitterHandleToDuplicate.EmitterHandleId)
					{
						HandleToDuplicate = EmitterHandle;
						break;
					}
				}
			}

			if (HandleToDuplicate.IsValid())
			{
				break;
			}
		}

		if (HandleToDuplicate.IsValid())
		{
			const FNiagaraEmitterHandle& EmitterHandle = GetSystem().DuplicateEmitterHandle(HandleToDuplicate, FNiagaraUtilities::GetUniqueName(HandleToDuplicate.GetName(), EmitterHandleNames));
			EmitterHandleNames.Add(EmitterHandle.GetName());
		}
	}

	FNiagaraStackGraphUtilities::RebuildEmitterNodes(GetSystem());
	GetEditorData().SynchronizeOverviewGraphWithSystem(GetSystem());
	RefreshAll();
	bForceAutoCompileOnce = true;
}

void FNiagaraSystemViewModel::DeleteEmitters(TSet<FGuid> EmitterHandleIdsToDelete)
{
	if (EmitterHandleIdsToDelete.Num() > 0)
	{
		// Reset view models before modifying the emitter handle list to prevent accessing deleted data.
		ResetEmitterHandleViewModelsAndTracks();
		FNiagaraEditorUtilities::RemoveEmittersFromSystemByEmitterHandleId(GetSystem(), EmitterHandleIdsToDelete);
		RefreshAll();
		bForceAutoCompileOnce = true;
	}
}

TArray<TSharedRef<FNiagaraEmitterHandleViewModel>> FNiagaraSystemViewModel::GetPinnedEmitterHandles()
{
	return PinnedEmitterHandles;
}

void FNiagaraSystemViewModel::SetEmitterPinnedState(TSharedRef<FNiagaraEmitterHandleViewModel> EmitterHandleModel, bool bPinnedState)
{
	if (bPinnedState)
	{
		PinnedEmitterHandles.AddUnique(EmitterHandleModel);
	}
	else
	{
		PinnedEmitterHandles.Remove(EmitterHandleModel);
	}
	OnPinnedChangedDelegate.Broadcast();
}

FNiagaraSystemViewModel::FOnPinnedEmittersChanged& FNiagaraSystemViewModel::GetOnPinnedEmittersChanged()
{
	return OnPinnedChangedDelegate;
}

bool FNiagaraSystemViewModel::GetIsEmitterPinned(TSharedRef<FNiagaraEmitterHandleViewModel> EmitterHandleModel)
{
	return PinnedEmitterHandles.ContainsByPredicate([=](TSharedRef<FNiagaraEmitterHandleViewModel> Model) {return Model==EmitterHandleModel;});
}


FNiagaraSystemViewModel::FOnEmitterHandleViewModelsChanged& FNiagaraSystemViewModel::OnEmitterHandleViewModelsChanged()
{
	return OnEmitterHandleViewModelsChangedDelegate;
}

FNiagaraSystemViewModel::FOnCurveOwnerChanged& FNiagaraSystemViewModel::OnCurveOwnerChanged()
{
	return OnCurveOwnerChangedDelegate;
}

FNiagaraSystemViewModel::FOnPostSequencerTimeChange& FNiagaraSystemViewModel::OnPostSequencerTimeChanged()
{
	return OnPostSequencerTimeChangeDelegate;
}

FNiagaraSystemViewModel::FOnSystemCompiled& FNiagaraSystemViewModel::OnSystemCompiled()
{
	return OnSystemCompiledDelegate;
}

void FNiagaraSystemViewModel::AddReferencedObjects(FReferenceCollector& Collector)
{
	if (PreviewComponent != nullptr)
	{
		Collector.AddReferencedObject(PreviewComponent);
	}
	if (NiagaraSequence != nullptr)
	{
		Collector.AddReferencedObject(NiagaraSequence);
	}
	if (SystemStackViewModel != nullptr)
	{
		Collector.AddReferencedObject(SystemStackViewModel);
	}
	if (SelectionViewModel != nullptr)
	{
		Collector.AddReferencedObject(SelectionViewModel);
	}
}

void FNiagaraSystemViewModel::PostUndo(bool bSuccess)
{
	// Reset emitter handle view models to prevent accessing invalid handles if they were in the undo operation.
	ResetEmitterHandleViewModelsAndTracks();
	RefreshAll();
	GetDefault<UEdGraphSchema_NiagaraSystemOverview>()->ForceVisualizationCacheClear();
}

void FNiagaraSystemViewModel::Tick(float DeltaTime)
{
	if (bCompilePendingCompletion && GetSystem().HasOutstandingCompilationRequests() == false)
	{
		bCompilePendingCompletion = false;
		OnSystemCompiled().Broadcast();
		SendLastCompileMessageJobs();
	}

	if (bForceAutoCompileOnce || (GetDefault<UNiagaraEditorSettings>()->GetAutoCompile() && bCanAutoCompile))
	{
		bool bRecompile = false;

		check(SystemScriptViewModel.IsValid());
		if (SystemScriptViewModel->GetLatestCompileStatus() == ENiagaraScriptCompileStatus::NCS_Dirty)
		{
			//SystemScriptViewModel->CompileSystem();
			//UE_LOG(LogNiagaraEditor, Log, TEXT("Compiling %s due to dirty scripts."), *GetSystem().GetName());
			bRecompile |= true;
		}

		for (TSharedRef<FNiagaraEmitterHandleViewModel> EmitterHandleViewModel : EmitterHandleViewModels)
		{
			if (EmitterHandleViewModel->GetIsEnabled() && EmitterHandleViewModel->GetEmitterViewModel()->GetLatestCompileStatus() == ENiagaraScriptCompileStatus::NCS_Dirty)
			{
				bRecompile |= true;
				//EmitterHandleViewModel->GetEmitterViewModel()->CompileScripts();
				//UE_LOG(LogNiagaraEditor, Log, TEXT("Compiling %s - %s due to dirty scripts."), *GetSystem().GetName(), *EmitterHandleViewModel->GetName().ToString());
			}
		}

		if (GetSystem().HasOutstandingCompilationRequests() == false)
		{
			if (bRecompile || bForceAutoCompileOnce)
			{
				CompileSystem(false);
				bForceAutoCompileOnce = false;
			}

			if (bResetRequestPending)
			{
				ResetSystem(ETimeResetMode::AllowResetTime, EMultiResetMode::AllowResetAllInstances, EReinitMode::ReinitializeSystem);
			}
		}
	}

	if (EmitterIdsRequiringSequencerTrackUpdate.Num() > 0)
	{
		UpdateSequencerTracksForEmitters(EmitterIdsRequiringSequencerTrackUpdate);
		EmitterIdsRequiringSequencerTrackUpdate.Empty();
	}

	if (SystemStackViewModel != nullptr)
	{
		SystemStackViewModel->Tick();
	}

	for (TSharedRef<FNiagaraEmitterHandleViewModel> EmitterHandleViewModel : EmitterHandleViewModels)
	{
		EmitterHandleViewModel->GetEmitterStackViewModel()->Tick();
	}

	if (SelectionViewModel != nullptr)
	{
		SelectionViewModel->Tick();
	}
}

void FNiagaraSystemViewModel::NotifyPreSave()
{
	if (GetSystem().HasOutstandingCompilationRequests())
	{
		UE_LOG(LogNiagaraEditor, Log, TEXT("System %s has pending compile jobs. Waiting for that code to complete before Saving.."), *GetSystem().GetName());
		GetSystem().WaitForCompilationComplete();
	}
}

void FNiagaraSystemViewModel::NotifyPreClose()
{
	if (GetSystem().HasOutstandingCompilationRequests())
	{
		UE_LOG(LogNiagaraEditor, Log, TEXT("System %s has pending compile jobs. Waiting for that code to complete before Closing.."), *GetSystem().GetName());
		GetSystem().WaitForCompilationComplete();
	}
	OnPreCloseDelegate.Broadcast();
}

FNiagaraSystemViewModel::FOnPreClose& FNiagaraSystemViewModel::OnPreClose()
{
	return OnPreCloseDelegate;
}

TSharedPtr<FUICommandList> FNiagaraSystemViewModel::GetToolkitCommands()
{
	return ToolkitCommands.Pin();
}

FNiagaraSystemViewModel::FOnPinnedCurvesChanged& FNiagaraSystemViewModel::GetOnPinnedCurvesChanged()
{
	return OnPinnedCurvesChangedDelegate;
}

void FNiagaraSystemViewModel::SetToolkitCommands(const TSharedRef<FUICommandList>& InToolkitCommands)
{
	ToolkitCommands = InToolkitCommands;
}

const TArray<FNiagaraStackModuleData>& FNiagaraSystemViewModel::GetStackModuleData(UNiagaraStackEntry* ModuleEntry)
{
	FGuid StackDataId;
	UNiagaraEmitter* Emitter = nullptr;
	if (ModuleEntry->GetEmitterViewModel().IsValid())
	{
		TSharedRef<FNiagaraEmitterHandleViewModel>* FoundModel = EmitterHandleViewModels.FindByPredicate([ModuleEntry](TSharedRef<FNiagaraEmitterHandleViewModel> CurrentViewModel)
		{
			return CurrentViewModel->GetEmitterViewModel() == ModuleEntry->GetEmitterViewModel(); 
		});
		checkf(FoundModel != nullptr, TEXT("Couldn't get stack module data for emitter"));
		StackDataId = (*FoundModel)->GetEmitterHandle()->GetId();
		Emitter = ModuleEntry->GetEmitterViewModel()->GetEmitter();
	}
	else
	{
		StackDataId = FGuid();
	}

	TArray<FNiagaraStackModuleData>* StackModuleData = GuidToCachedStackModuleData.Find(StackDataId);
	if (StackModuleData == nullptr)
	{
		// If not cached, rebuild
		StackModuleData = &GuidToCachedStackModuleData.Add(StackDataId);
		BuildStackModuleData(GetSystem().GetSystemSpawnScript(), StackDataId, *StackModuleData);
		BuildStackModuleData(GetSystem().GetSystemUpdateScript(), StackDataId, *StackModuleData);
		if (StackDataId.IsValid() && Emitter != nullptr)
		{
			BuildStackModuleData(Emitter->EmitterSpawnScriptProps.Script, StackDataId, *StackModuleData);
			BuildStackModuleData(Emitter->EmitterUpdateScriptProps.Script, StackDataId, *StackModuleData);
			BuildStackModuleData(Emitter->SpawnScriptProps.Script, StackDataId, *StackModuleData);
			BuildStackModuleData(Emitter->UpdateScriptProps.Script, StackDataId, *StackModuleData);
		}
	}
	return *StackModuleData;
}

UNiagaraStackViewModel* FNiagaraSystemViewModel::GetSystemStackViewModel()
{
	return SystemStackViewModel;
}

UNiagaraSystemSelectionViewModel* FNiagaraSystemViewModel::GetSelectionViewModel()
{
	return SelectionViewModel;
}

TStatId FNiagaraSystemViewModel::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(FNiagaraSystemViewModel, STATGROUP_Tickables);
}

void FNiagaraSystemViewModel::SendLastCompileMessageJobs() const
{
	if (SystemMessageLogGuidKey.IsSet() == false)
	{
		return;
	}

	struct FNiagaraScriptAndOwningScriptNameString
	{
		FNiagaraScriptAndOwningScriptNameString(const UNiagaraScript* InScript, const FString& InOwningScriptNameString)
			: Script(InScript)
			, OwningScriptNameString(InOwningScriptNameString)
		{
		}

		const UNiagaraScript* Script;
		const FString OwningScriptNameString;
	};

	int32 ErrorCount = 0;
	int32 WarningCount = 0;

	TArray<FNiagaraScriptAndOwningScriptNameString> ScriptsToGetCompileEventsFrom;
	ScriptsToGetCompileEventsFrom.Add(FNiagaraScriptAndOwningScriptNameString(GetSystem().GetSystemSpawnScript(), GetSystem().GetName()));
	ScriptsToGetCompileEventsFrom.Add(FNiagaraScriptAndOwningScriptNameString(GetSystem().GetSystemUpdateScript(), GetSystem().GetName()));
	const TArray<FNiagaraEmitterHandle> EmitterHandles = GetSystem().GetEmitterHandles();
	for (const FNiagaraEmitterHandle& Handle : EmitterHandles)
	{
		const UNiagaraEmitter* EmitterInSystem = Handle.GetInstance();
		TArray<UNiagaraScript*> EmitterScripts;
		EmitterInSystem->GetScripts(EmitterScripts);
		for (UNiagaraScript* EmitterScript : EmitterScripts)
		{
			ScriptsToGetCompileEventsFrom.Add(FNiagaraScriptAndOwningScriptNameString(EmitterScript, EmitterInSystem->GetUniqueEmitterName()));
		}
	}

	TArray<TSharedPtr<const INiagaraMessageJob>> JobBatchToQueue;
	// Iterate from back to front to avoid reordering the events when they are queued
	for (int i = ScriptsToGetCompileEventsFrom.Num()-1; i >=0; --i)
	{ 
		const FNiagaraScriptAndOwningScriptNameString& ScriptInfo = ScriptsToGetCompileEventsFrom[i];
		const TArray<FNiagaraCompileEvent>& CurrentCompileEvents = ScriptInfo.Script->GetVMExecutableData().LastCompileEvents;
		for (int j = CurrentCompileEvents.Num() - 1; j >= 0; --j)
		{
			const FNiagaraCompileEvent& CompileEvent = CurrentCompileEvents[j];
			if (CompileEvent.Severity == FNiagaraCompileEventSeverity::Error)
			{
				ErrorCount++;
			}
			else if (CompileEvent.Severity == FNiagaraCompileEventSeverity::Warning)
			{
				WarningCount++;
			}

			JobBatchToQueue.Add(MakeShared<FNiagaraMessageJobCompileEvent>(CompileEvent, MakeWeakObjectPtr(const_cast<UNiagaraScript*>(ScriptInfo.Script)), ScriptInfo.OwningScriptNameString));
		}
	}
	JobBatchToQueue.Insert(MakeShared<FNiagaraMessageJobPostCompileSummary>(ErrorCount, WarningCount, GetLatestCompileStatus(), FText::FromString("System")), 0);
	FNiagaraMessageManager::Get()->RefreshMessagesForAssetKeyAndMessageJobType(SystemMessageLogGuidKey.GetValue(), ENiagaraMessageJobType::CompileEventMessageJob);
	FNiagaraMessageManager::Get()->QueueMessageJobBatch(JobBatchToQueue, SystemMessageLogGuidKey.GetValue());
}

void FNiagaraSystemViewModel::SetupPreviewComponentAndInstance()
{
	if (bCanSimulate)
	{
		PreviewComponent = NewObject<UNiagaraComponent>(GetTransientPackage(), NAME_None, RF_Transient);
		PreviewComponent->CastShadow = 1;
		PreviewComponent->bCastDynamicShadow = 1;
		PreviewComponent->SetAsset(System);
		PreviewComponent->SetForceSolo(true);
		PreviewComponent->SetAgeUpdateMode(ENiagaraAgeUpdateMode::DesiredAge);
		PreviewComponent->SetCanRenderWhileSeeking(false);
		PreviewComponent->Activate(true);

		FTransform OwnerTransform = GetEditorData().GetOwnerTransform();
		PreviewComponent->SetRelativeTransform(OwnerTransform);

		PreviewComponent->OnSystemInstanceChanged().AddRaw(this, &FNiagaraSystemViewModel::PreviewComponentSystemInstanceChanged);
		PreviewComponentSystemInstanceChanged();
	}
}

void FNiagaraSystemViewModel::RefreshAll()
{
	ResetSystem(ETimeResetMode::AllowResetTime, EMultiResetMode::ResetThisInstance, EReinitMode::ReinitializeSystem);
	RefreshEmitterHandleViewModels();
	RefreshSequencerTracks();
	ResetCurveData();
}

void FNiagaraSystemViewModel::NotifyDataObjectChanged(UObject* ChangedObject)
{
	UNiagaraDataInterface* ChangedDataInterface = Cast<UNiagaraDataInterface>(ChangedObject);
	if (ChangedDataInterface)
	{
		UpdateCompiledDataInterfaces(ChangedDataInterface);
	}

	UNiagaraDataInterfaceCurveBase* ChangedDataInterfaceCurve = Cast<UNiagaraDataInterfaceCurveBase>(ChangedDataInterface);
	if (ChangedDataInterfaceCurve || ChangedObject == nullptr)
	{
		TArray<UNiagaraDataInterfaceCurveBase*> OldShownCurveDataInterfaces = ShownCurveDataInterfaces;
		ResetCurveData();
		if(ChangedDataInterfaceCurve != nullptr && ChangedDataInterfaceCurve->ShowInCurveEditor && OldShownCurveDataInterfaces.Contains(ChangedDataInterfaceCurve) == false)
		{
			NotifyPinnedCurvesChanged();
		}
	}

	ResetSystem(ETimeResetMode::AllowResetTime, EMultiResetMode::AllowResetAllInstances, EReinitMode::ReinitializeSystem);
}

void FNiagaraSystemViewModel::IsolateEmitters(TArray<FGuid> EmitterHandlesIdsToIsolate)
{
	for (TSharedRef<FNiagaraEmitterHandleViewModel> EmitterHandle : EmitterHandleViewModels)
	{
		EmitterHandle->GetEmitterHandle()->SetIsolated(false);
	}

	bool bAnyEmitterIsolated = false;
	for (TSharedRef<FNiagaraEmitterHandleViewModel> EmitterHandle : EmitterHandleViewModels)
	{
		if (EmitterHandlesIdsToIsolate.Contains(EmitterHandle->GetId()))
		{
			bAnyEmitterIsolated = true;
			EmitterHandle->GetEmitterHandle()->SetIsolated(true);
		}
	}

	GetSystem().SetIsolateEnabled(bAnyEmitterIsolated);
}

void FNiagaraSystemViewModel::ToggleEmitterIsolation(TSharedRef<FNiagaraEmitterHandleViewModel> InEmitterHandle)
{
	InEmitterHandle->GetEmitterHandle()->SetIsolated(!InEmitterHandle->GetEmitterHandle()->IsIsolated());
	
	bool bAnyEmitterIsolated = false;
	for (TSharedRef<FNiagaraEmitterHandleViewModel> EmitterHandle : EmitterHandleViewModels)
	{
		if (EmitterHandle->GetEmitterHandle()->IsIsolated())
		{
			bAnyEmitterIsolated = true;
			break;
		}
	}

	GetSystem().SetIsolateEnabled(bAnyEmitterIsolated);
}

void FNiagaraSystemViewModel::ResetEmitterHandleViewModelsAndTracks()
{
	for (TSharedRef<FNiagaraEmitterHandleViewModel> EmitterHandleViewModel : EmitterHandleViewModels)
	{
		EmitterHandleViewModel->Reset();
	}

	TArray<UMovieSceneTrack*> MasterTracks = NiagaraSequence->GetMovieScene()->GetMasterTracks();
	for (UMovieSceneTrack* MasterTrack : MasterTracks)
	{
		if (MasterTrack != nullptr)
		{
			NiagaraSequence->GetMovieScene()->RemoveMasterTrack(*MasterTrack);
		}
	}
}

void FNiagaraSystemViewModel::RefreshEmitterHandleViewModels()
{
	TArray<TSharedRef<FNiagaraEmitterHandleViewModel>> OldViewModels = EmitterHandleViewModels;
	EmitterHandleViewModels.Empty();
	GuidToCachedStackModuleData.Empty();

	// Map existing view models to the real instances that now exist. Reuse if we can. Create a new one if we cannot.
	TArray<FGuid> ValidEmitterHandleIds;
	int32 i;
	for (i = 0; i < GetSystem().GetNumEmitters(); ++i)
	{
		FNiagaraEmitterHandle* EmitterHandle = &GetSystem().GetEmitterHandle(i);
		TSharedPtr<FNiagaraEmitterInstance, ESPMode::ThreadSafe> Simulation = SystemInstance ? SystemInstance->GetSimulationForHandle(*EmitterHandle) : nullptr;
		ValidEmitterHandleIds.Add(EmitterHandle->GetId());

		TSharedPtr<FNiagaraEmitterHandleViewModel> ViewModel;
		bool bAdd = OldViewModels.Num() <= i;
		if (bAdd)
		{
			ViewModel = MakeShared<FNiagaraEmitterHandleViewModel>();
			ViewModel->OnPropertyChanged().AddRaw(this, &FNiagaraSystemViewModel::EmitterHandlePropertyChanged, EmitterHandle->GetId());
			ViewModel->OnNameChanged().AddRaw(this, &FNiagaraSystemViewModel::EmitterHandleNameChanged);
			ViewModel->GetEmitterViewModel()->OnPropertyChanged().AddRaw(this, &FNiagaraSystemViewModel::EmitterPropertyChanged);
			ViewModel->GetEmitterViewModel()->OnScriptCompiled().AddRaw(this, &FNiagaraSystemViewModel::ScriptCompiled);
			ViewModel->GetEmitterViewModel()->OnScriptGraphChanged().AddRaw(this, &FNiagaraSystemViewModel::EmitterScriptGraphChanged, EmitterHandle->GetId());
			ViewModel->GetEmitterViewModel()->OnScriptParameterStoreChanged().AddRaw(this, &FNiagaraSystemViewModel::EmitterParameterStoreChanged);
			ViewModel->GetEmitterStackViewModel()->OnStructureChanged().AddRaw(this, &FNiagaraSystemViewModel::StackViewModelStructureChanged);
		}
		else
		{
			ViewModel = OldViewModels[i];
		}

		EmitterHandleViewModels.Add(ViewModel.ToSharedRef());
		ViewModel->Initialize(this->AsShared(), EmitterHandle, Simulation);
	}

	check(EmitterHandleViewModels.Num() == GetSystem().GetNumEmitters());

	// Clear out any old view models that may still be left around.
	for (; i < OldViewModels.Num(); i++)
	{
		TSharedRef<FNiagaraEmitterHandleViewModel> ViewModel = OldViewModels[i];
		ViewModel->OnPropertyChanged().RemoveAll(this);
		ViewModel->OnNameChanged().RemoveAll(this);
		ViewModel->GetEmitterViewModel()->OnPropertyChanged().RemoveAll(this);
		ViewModel->GetEmitterViewModel()->OnScriptCompiled().RemoveAll(this);
		ViewModel->GetEmitterViewModel()->OnScriptGraphChanged().RemoveAll(this);
		ViewModel->GetEmitterViewModel()->OnScriptParameterStoreChanged().RemoveAll(this);
		ViewModel->GetEmitterStackViewModel()->OnStructureChanged().RemoveAll(this);
		ViewModel->Reset();
	}

	bool bAnyEmitterIsolated = false;
	for (TSharedRef<FNiagaraEmitterHandleViewModel> EmitterHandle : EmitterHandleViewModels)
	{
		if (EmitterHandle->GetIsIsolated())
		{
			bAnyEmitterIsolated = true;
		}
	}
	GetSystem().SetIsolateEnabled(bAnyEmitterIsolated);

	if (SelectionViewModel != nullptr)
	{
		SelectionViewModel->Refresh();
	}

	OnEmitterHandleViewModelsChangedDelegate.Broadcast();
}

void PopulateChildMovieSceneFoldersFromNiagaraFolders(const UNiagaraSystemEditorFolder* NiagaraFolder, UMovieSceneFolder* MovieSceneFolder, const TMap<FGuid, UMovieSceneNiagaraEmitterTrack*>& EmitterHandleIdToTrackMap)
{
	for (const UNiagaraSystemEditorFolder* ChildNiagaraFolder : NiagaraFolder->GetChildFolders())
	{
		UMovieSceneFolder* MatchingMovieSceneFolder = nullptr;
		for (UMovieSceneFolder* ChildMovieSceneFolder : MovieSceneFolder->GetChildFolders())
		{
			if (ChildMovieSceneFolder->GetFolderName() == ChildNiagaraFolder->GetFolderName())
			{
				MatchingMovieSceneFolder = ChildMovieSceneFolder;
			}
		}

		if (MatchingMovieSceneFolder == nullptr)
		{
			MatchingMovieSceneFolder = NewObject<UMovieSceneFolder>(MovieSceneFolder, ChildNiagaraFolder->GetFolderName(), RF_Transactional);
			MatchingMovieSceneFolder->SetFolderName(ChildNiagaraFolder->GetFolderName());
			MovieSceneFolder->AddChildFolder(MatchingMovieSceneFolder);
		}

		PopulateChildMovieSceneFoldersFromNiagaraFolders(ChildNiagaraFolder, MatchingMovieSceneFolder, EmitterHandleIdToTrackMap);
	}

	for (const FGuid& ChildEmitterHandleId : NiagaraFolder->GetChildEmitterHandleIds())
	{
		UMovieSceneNiagaraEmitterTrack* const* TrackPtr = EmitterHandleIdToTrackMap.Find(ChildEmitterHandleId);
		if (TrackPtr != nullptr && MovieSceneFolder->GetChildMasterTracks().Contains(*TrackPtr) == false)
		{
			MovieSceneFolder->AddChildMasterTrack(*TrackPtr);
		}
	}
}

void FNiagaraSystemViewModel::RefreshSequencerTracks()
{
	TGuardValue<bool> UpdateGuard(bUpdatingSequencerFromEmitterDataChange, true);

	TArray<UMovieSceneTrack*> MasterTracks = NiagaraSequence->GetMovieScene()->GetMasterTracks();
	for (UMovieSceneTrack* MasterTrack : MasterTracks)
	{
		if (MasterTrack != nullptr)
		{
			NiagaraSequence->GetMovieScene()->RemoveMasterTrack(*MasterTrack);
		}
	}

	TMap<FGuid, UMovieSceneNiagaraEmitterTrack*> EmitterHandleIdToTrackMap;
	for (TSharedRef<FNiagaraEmitterHandleViewModel> EmitterHandleViewModel : EmitterHandleViewModels)
	{
		UMovieSceneNiagaraEmitterTrack* EmitterTrack = Cast<UMovieSceneNiagaraEmitterTrack>(NiagaraSequence->GetMovieScene()->AddMasterTrack(UMovieSceneNiagaraEmitterTrack::StaticClass()));
		EmitterTrack->Initialize(*this, EmitterHandleViewModel, NiagaraSequence->GetMovieScene()->GetTickResolution());
		EmitterHandleIdToTrackMap.Add(EmitterHandleViewModel->GetId(), EmitterTrack);
	}

	TArray<UMovieSceneFolder*>& MovieSceneRootFolders = NiagaraSequence->GetMovieScene()->GetRootFolders();
	MovieSceneRootFolders.Empty();

	const UNiagaraSystemEditorData& SystemEditorData = GetEditorData();
	UNiagaraSystemEditorFolder& RootFolder = SystemEditorData.GetRootFolder();
	for (const UNiagaraSystemEditorFolder* RootChildFolder : RootFolder.GetChildFolders())
	{
		UMovieSceneFolder* MovieSceneRootFolder = NewObject<UMovieSceneFolder>(NiagaraSequence->GetMovieScene(), RootChildFolder->GetFolderName(), RF_Transactional);
		MovieSceneRootFolder->SetFolderName(RootChildFolder->GetFolderName());
		MovieSceneRootFolders.Add(MovieSceneRootFolder);
		PopulateChildMovieSceneFoldersFromNiagaraFolders(RootChildFolder, MovieSceneRootFolder, EmitterHandleIdToTrackMap);
	}

	Sequencer->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::MovieSceneStructureItemsChanged);

	// Since we just rebuilt all of the sequencer tracks, these updates don't need to be done.
	EmitterIdsRequiringSequencerTrackUpdate.Empty();
}

void FNiagaraSystemViewModel::UpdateSequencerTracksForEmitters(const TArray<FGuid>& EmitterIdsRequiringUpdate)
{
	TGuardValue<bool> UpdateGuard(bUpdatingSequencerFromEmitterDataChange, true);
	for (UMovieSceneTrack* Track : NiagaraSequence->GetMovieScene()->GetMasterTracks())
	{
		UMovieSceneNiagaraEmitterTrack* EmitterTrack = CastChecked<UMovieSceneNiagaraEmitterTrack>(Track);
		if (EmitterIdsRequiringUpdate.Contains(EmitterTrack->GetEmitterHandleViewModel()->GetId()))
		{
			EmitterTrack->UpdateTrackFromEmitterGraphChange(NiagaraSequence->GetMovieScene()->GetTickResolution());
		}
	}
	Sequencer->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::MovieSceneStructureItemsChanged);
}

UMovieSceneNiagaraEmitterTrack* FNiagaraSystemViewModel::GetTrackForHandleViewModel(TSharedRef<FNiagaraEmitterHandleViewModel> EmitterHandleViewModel)
{
	for (UMovieSceneTrack* Track : NiagaraSequence->GetMovieScene()->GetMasterTracks())
	{
		UMovieSceneNiagaraEmitterTrack* EmitterTrack = CastChecked<UMovieSceneNiagaraEmitterTrack>(Track);
		if (EmitterTrack->GetEmitterHandleViewModel() == EmitterHandleViewModel)
		{
			return EmitterTrack;
		}
	}
	return nullptr;
}

void FNiagaraSystemViewModel::SetupSequencer()
{
	NiagaraSequence = NewObject<UNiagaraSequence>(GetTransientPackage());
	UMovieScene* MovieScene = NewObject<UMovieScene>(NiagaraSequence, FName("Niagara System MovieScene"), RF_Transactional);
	MovieScene->SetDisplayRate(FFrameRate(240, 1));

	NiagaraSequence->Initialize(this, MovieScene);

	FFrameTime StartTime = GetEditorData().GetPlaybackRange().GetLowerBoundValue() * MovieScene->GetTickResolution();
	int32      Duration  = (GetEditorData().GetPlaybackRange().Size<float>() * MovieScene->GetTickResolution()).FrameNumber.Value;

	MovieScene->SetPlaybackRange(StartTime.RoundToFrame(), Duration);

	FMovieSceneEditorData& EditorData = NiagaraSequence->GetMovieScene()->GetEditorData();
	float ViewTimeOffset = .1f;
	EditorData.WorkStart = GetEditorData().GetPlaybackRange().GetLowerBoundValue() - ViewTimeOffset;
	EditorData.WorkEnd = GetEditorData().GetPlaybackRange().GetUpperBoundValue() + ViewTimeOffset;
	EditorData.ViewStart = EditorData.WorkStart;
	EditorData.ViewEnd = EditorData.WorkEnd;

	FSequencerViewParams ViewParams(TEXT("NiagaraSequencerSettings"));
	{
		ViewParams.UniqueName = "NiagaraSequenceEditor";
		ViewParams.OnGetAddMenuContent = OnGetSequencerAddMenuContent;
	}

	FSequencerInitParams SequencerInitParams;
	{
		SequencerInitParams.ViewParams = ViewParams;
		SequencerInitParams.RootSequence = NiagaraSequence;
		SequencerInitParams.bEditWithinLevelEditor = false;
		SequencerInitParams.ToolkitHost = nullptr;
	}

	ISequencerModule &SequencerModule = FModuleManager::LoadModuleChecked< ISequencerModule >("Sequencer");
	Sequencer = SequencerModule.CreateSequencer(SequencerInitParams);

	Sequencer->OnMovieSceneDataChanged().AddRaw(this, &FNiagaraSystemViewModel::SequencerDataChanged);
	Sequencer->OnGlobalTimeChanged().AddRaw(this, &FNiagaraSystemViewModel::SequencerTimeChanged);
	Sequencer->GetSelectionChangedTracks().AddRaw(this, &FNiagaraSystemViewModel::SequencerTrackSelectionChanged);
	Sequencer->GetSelectionChangedSections().AddRaw(this, &FNiagaraSystemViewModel::SequencerSectionSelectionChanged);
	Sequencer->SetPlaybackStatus(GetSystem().GetNumEmitters() > 0 && EditorSettings->GetAutoPlay()
		? EMovieScenePlayerStatus::Playing
		: EMovieScenePlayerStatus::Stopped);
}

void FNiagaraSystemViewModel::ResetSystem()
{
	ResetSystem(ETimeResetMode::AllowResetTime, EMultiResetMode::ResetThisInstance, EReinitMode::ResetSystem);
}

void FNiagaraSystemViewModel::ResetSystem(ETimeResetMode TimeResetMode, EMultiResetMode MultiResetMode, EReinitMode ReinitMode)
{
	bool bResetAge = TimeResetMode == ETimeResetMode::AllowResetTime && (Sequencer->GetPlaybackStatus() == EMovieScenePlayerStatus::Playing || EditorSettings->GetResimulateOnChangeWhilePaused() == false);
	if (bResetAge)
	{
		TGuardValue<bool> Guard(bSettingSequencerTimeDirectly, true);
		if (Sequencer->GetPlaybackStatus() == EMovieScenePlayerStatus::Playing)
		{
			Sequencer->SetPlaybackStatus(EMovieScenePlayerStatus::Paused);
			Sequencer->SetGlobalTime(0);
			Sequencer->SetPlaybackStatus(EMovieScenePlayerStatus::Playing);
		}
		else
		{
			Sequencer->SetGlobalTime(0);
		}
	}

	TArray<UNiagaraComponent*> ReferencingComponents;
	if (MultiResetMode == EMultiResetMode::ResetThisInstance)
	{
		if (PreviewComponent != nullptr)
		{
			ReferencingComponents.Add(PreviewComponent);
		}
	}
	else
	{
		ReferencingComponents = FNiagaraEditorUtilities::GetComponentsThatReferenceSystem(GetSystem());
	}

	for (auto Component : ReferencingComponents)
	{
		if (ReinitMode == EReinitMode::ResetSystem)
		{
			Component->ResetSystem();
			if (bResetAge && Component->GetAgeUpdateMode() == ENiagaraAgeUpdateMode::DesiredAge)
			{
				Component->SetDesiredAge(0);
			}
		}

		else if (ReinitMode == EReinitMode::ReinitializeSystem)
		{
			Component->ReinitializeSystem();
		}
	}

	if (EditMode == ENiagaraSystemViewModelEditMode::EmitterAsset && MultiResetMode == EMultiResetMode::AllowResetAllInstances && EditorSettings->GetResetDependentSystemsWhenEditingEmitters())
	{
		FNiagaraEditorUtilities::ResetSystemsThatReferenceSystemViewModel(*this);
	}

	FEditorSupportDelegates::RedrawAllViewports.Broadcast();
	bResetRequestPending = false;
}

void FNiagaraSystemViewModel::RequestResetSystem()
{
	bResetRequestPending = true;
}

struct FNiagaraSystemCurveData
{
	FRichCurve* Curve;
	FName Name;
	FLinearColor Color;
	UObject* Owner;
};

void GetCurveDataFromInterface(UNiagaraDataInterfaceCurveBase* CurveDataInterface, FString CurveSource, FString DefaultName,  
	TArray<FNiagaraSystemCurveData>& OutCurveData, TArray<UNiagaraDataInterfaceCurveBase*>& OutCurveDataInterfaces)
{
	if (!CurveDataInterface->ShowInCurveEditor)
	{
		return;
	}
	OutCurveDataInterfaces.Add(CurveDataInterface);
	TArray<UNiagaraDataInterfaceCurveBase::FCurveData> CurveData;
	CurveDataInterface->GetCurveData(CurveData);
	for (UNiagaraDataInterfaceCurveBase::FCurveData CurveDataItem : CurveData)
	{
		FNiagaraSystemCurveData SystemCurveData;
		SystemCurveData.Curve = CurveDataItem.Curve;
		SystemCurveData.Color = CurveDataItem.Color;
		SystemCurveData.Owner = CurveDataInterface;
		FString ParameterName = CurveDataItem.Name == NAME_None
			? DefaultName
			: DefaultName + ".";
		FString DataName = CurveDataItem.Name == NAME_None
			? FString()
			: CurveDataItem.Name.ToString();
		SystemCurveData.Name = *(CurveSource + ParameterName + DataName);
		OutCurveData.Add(SystemCurveData);
	}
}

void GetCurveData(FString CurveSource, UNiagaraGraph* SourceGraph, TArray<FNiagaraSystemCurveData>& OutCurveData, TArray<UNiagaraDataInterfaceCurveBase*>& OutCurveDataInterfaces)
{
	TArray<UNiagaraNodeInput*> InputNodes;
	SourceGraph->GetNodesOfClass<UNiagaraNodeInput>(InputNodes);
	TSet<FName> HandledInputs;
	for (UNiagaraNodeInput* InputNode : InputNodes)
	{
		if (HandledInputs.Contains(InputNode->Input.GetName()) == false)
		{
			if (InputNode->Usage == ENiagaraInputNodeUsage::Parameter)
			{
				UNiagaraDataInterfaceCurveBase* CurveDataInterface = Cast<UNiagaraDataInterfaceCurveBase>(InputNode->GetDataInterface());
				if (CurveDataInterface != nullptr)
				{
					FString DefaultName = InputNode->Input.GetName().ToString();
					GetCurveDataFromInterface(CurveDataInterface, CurveSource, DefaultName, OutCurveData, OutCurveDataInterfaces);
				}
			}
			HandledInputs.Add(InputNode->Input.GetName());
		}
	}
}

void FNiagaraSystemViewModel::ResetCurveData()
{
	CurveOwner.EmptyCurves();
	ShownCurveDataInterfaces.Empty();

	TArray<FNiagaraSystemCurveData> CurveData;


	check(SystemScriptViewModel.IsValid()); 
	GetCurveData(
		TEXT("System"),
		SystemScriptViewModel->GetGraphViewModel()->GetGraph(),
		CurveData,
		ShownCurveDataInterfaces);
	// Get curves from user variables
	for (UNiagaraDataInterface* DataInterface : GetSystem().GetExposedParameters().GetDataInterfaces())
	{
		UNiagaraDataInterfaceCurveBase* CurveDataInterface = Cast<UNiagaraDataInterfaceCurveBase>(DataInterface);
		if (CurveDataInterface != nullptr)
		{
			GetCurveDataFromInterface(CurveDataInterface, TEXT("System"), TEXT("User"), CurveData, ShownCurveDataInterfaces);
		}
	}
	
	for (TSharedRef<FNiagaraEmitterHandleViewModel> EmitterHandleViewModel : EmitterHandleViewModels)
	{
		GetCurveData(
			EmitterHandleViewModel->GetName().ToString(),
			EmitterHandleViewModel->GetEmitterViewModel()->GetSharedScriptViewModel()->GetGraphViewModel()->GetGraph(),
			CurveData, 
			ShownCurveDataInterfaces);
	}

	for (FNiagaraSystemCurveData& CurveDataItem : CurveData)
	{
		CurveOwner.AddCurve(
			*CurveDataItem.Curve,
			CurveDataItem.Name,
			CurveDataItem.Color,
			*CurveDataItem.Owner,
			FNiagaraCurveOwner::FNotifyCurveChanged::CreateRaw(this, &FNiagaraSystemViewModel::CurveChanged));
	}

	OnCurveOwnerChangedDelegate.Broadcast();
}

void UpdateCompiledDataInterfacesForScript(UNiagaraScript& TargetScript, FName TargetDataInterfaceName, UNiagaraDataInterface& SourceDataInterface)
{
	for (FNiagaraScriptDataInterfaceInfo& DataInterfaceInfo : TargetScript.GetCachedDefaultDataInterfaces())
	{
		if (DataInterfaceInfo.Name == TargetDataInterfaceName)
		{
			SourceDataInterface.CopyTo(DataInterfaceInfo.DataInterface);
			break;
		}
	}
}

void FNiagaraSystemViewModel::UpdateCompiledDataInterfaces(UNiagaraDataInterface* ChangedDataInterface)
{
	UNiagaraNodeInput* OuterInputNode = ChangedDataInterface->GetTypedOuter<UNiagaraNodeInput>();
	if (OuterInputNode != nullptr)
	{
		// If the data interface's owning node has been removed from it's graph then it's not valid so early out here.
		bool bIsValidInputNode = OuterInputNode->GetGraph()->Nodes.Contains(OuterInputNode);
		if (bIsValidInputNode == false)
		{
			return;
		}

		// If the data interface was owned by an input node, then we need to try to update the compiled version.
		UNiagaraEmitter* OwningEmitter;
		UNiagaraScript* OwningScript;
		FNiagaraStackGraphUtilities::GetOwningEmitterAndScriptForStackNode(*OuterInputNode, GetSystem(), OwningEmitter, OwningScript);
		if (ensureMsgf(OwningScript != nullptr, TEXT("Could not find owning script for data interface input node.")))
		{
			switch (OwningScript->GetUsage())
			{
			case ENiagaraScriptUsage::SystemSpawnScript:
			case ENiagaraScriptUsage::SystemUpdateScript:
			case ENiagaraScriptUsage::ParticleSpawnScript:
			case ENiagaraScriptUsage::ParticleSpawnScriptInterpolated:
			case ENiagaraScriptUsage::ParticleUpdateScript:
			case ENiagaraScriptUsage::ParticleEventScript:
				UpdateCompiledDataInterfacesForScript(*OwningScript, OuterInputNode->Input.GetName(), *ChangedDataInterface);
				break;
			case ENiagaraScriptUsage::EmitterSpawnScript:
			case ENiagaraScriptUsage::EmitterUpdateScript:
				if (ensureMsgf(OwningEmitter != nullptr, TEXT("Could not find owning emitter for data interface input node.")))
				{
					UNiagaraScript& TargetScript = OwningScript->GetUsage() == ENiagaraScriptUsage::EmitterSpawnScript ? *GetSystem().GetSystemSpawnScript() : *GetSystem().GetSystemUpdateScript();
					FName AliasedInputNodeName = FNiagaraParameterMapHistory::ResolveEmitterAlias(OuterInputNode->Input.GetName(), OwningEmitter->GetUniqueEmitterName());
					UpdateCompiledDataInterfacesForScript(TargetScript, AliasedInputNodeName, *ChangedDataInterface);
				}
				break;
			}
		}
	}
	else
	{
		// If the data interface wasn't owned by a script, try to find it in the exposed parameter data interfaces.
		const FNiagaraVariableBase* FoundExposedDataInterface = GetSystem().GetExposedParameters().FindVariable(ChangedDataInterface);
		if (FoundExposedDataInterface != nullptr)
		{
			GetSystem().GetExposedParameters().OnInterfaceChange();
		}
	}
}

void FNiagaraSystemViewModel::EmitterHandlePropertyChanged(FGuid OwningEmitterHandleId)
{
	// When the emitter handle changes, refresh the System scripts emitter nodes and the sequencer tracks just in case the
	// property that changed was the handles emitter.
	if (bUpdatingEmittersFromSequencerDataChange == false)
	{
		TGuardValue<bool> UpdateGuard(bUpdatingSequencerFromEmitterDataChange, true);
		for (UMovieSceneTrack* Track : NiagaraSequence->GetMovieScene()->GetMasterTracks())
		{
			UMovieSceneNiagaraEmitterTrack* EmitterTrack = CastChecked<UMovieSceneNiagaraEmitterTrack>(Track);
			if (EmitterTrack->GetEmitterHandleViewModel()->GetId() == OwningEmitterHandleId)
			{
				EmitterTrack->UpdateTrackFromEmitterGraphChange(NiagaraSequence->GetMovieScene()->GetTickResolution());
			}
		}
		Sequencer->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::MovieSceneStructureItemsChanged);
	}

	// Refresh the overview nodes and the emitter stacks just in case the emitter enabled state changed.
	GetEditorData().SynchronizeOverviewGraphWithSystem(GetSystem());
	TSharedPtr<FNiagaraEmitterHandleViewModel> EmitterHandleViewModel = GetEmitterHandleViewModelById(OwningEmitterHandleId);
	if (EmitterHandleViewModel.IsValid())
	{
		EmitterHandleViewModel->GetEmitterStackViewModel()->GetRootEntry()->RefreshChildren();
	}

	ResetSystem(ETimeResetMode::AllowResetTime, EMultiResetMode::ResetThisInstance, EReinitMode::ReinitializeSystem);
}

void FNiagaraSystemViewModel::EmitterHandleNameChanged()
{
	GetDefault<UEdGraphSchema_NiagaraSystemOverview>()->ForceVisualizationCacheClear();
	CompileSystem(false);
}

void FNiagaraSystemViewModel::EmitterPropertyChanged()
{
	ResetSystem(ETimeResetMode::AllowResetTime, EMultiResetMode::ResetThisInstance, EReinitMode::ReinitializeSystem);
}

void FNiagaraSystemViewModel::ScriptCompiled()
{
	bCompilePendingCompletion = true;
	//ReInitializeSystemInstances();
}

void FNiagaraSystemViewModel::SystemParameterStoreChanged(const FNiagaraParameterStore& ChangedParameterStore, const UNiagaraScript* OwningScript)
{
	UpdateSimulationFromParameterChange();
}

void FNiagaraSystemViewModel::EmitterScriptGraphChanged(const FEdGraphEditAction& InAction, const UNiagaraScript& OwningScript, FGuid OwningEmitterHandleId)
{
	if (bUpdatingEmittersFromSequencerDataChange == false)
	{
		EmitterIdsRequiringSequencerTrackUpdate.AddUnique(OwningEmitterHandleId);
	}
	// Remove from cache on graph change 
	GuidToCachedStackModuleData.Remove(OwningEmitterHandleId);
}

void FNiagaraSystemViewModel::SystemScriptGraphChanged(const FEdGraphEditAction& InAction)
{
	GuidToCachedStackModuleData.Empty();
}

void FNiagaraSystemViewModel::EmitterParameterStoreChanged(const FNiagaraParameterStore& ChangedParameterStore, const UNiagaraScript& OwningScript)
{
	if (bUpdatingEmittersFromSequencerDataChange == false)
	{
		TGuardValue<bool> UpdateGuard(bUpdatingSequencerFromEmitterDataChange, true);
		for (UMovieSceneTrack* Track : NiagaraSequence->GetMovieScene()->GetMasterTracks())
		{
			UMovieSceneNiagaraEmitterTrack* EmitterTrack = CastChecked<UMovieSceneNiagaraEmitterTrack>(Track);
			EmitterTrack->UpdateTrackFromEmitterParameterChange(NiagaraSequence->GetMovieScene()->GetTickResolution());
		}
		Sequencer->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::TrackValueChanged);
	}
	UpdateSimulationFromParameterChange();
}

void FNiagaraSystemViewModel::UpdateSimulationFromParameterChange()
{
	if (EditorSettings->GetResetSimulationOnChange())
	{
		/* Calling RequestResetSystem here avoids reentrancy into ResetSystem() when we edit the system parameter store on 
		** UNiagaraComponent::Activate() as we always call PrepareRapidIterationParameters().  */
		RequestResetSystem();
	}
	else
	{
		if (Sequencer->GetPlaybackStatus() == EMovieScenePlayerStatus::Stopped)
		{
			// TODO: Update the view when paused and reset on change is turned off.
		}
	}
}

void FNiagaraSystemViewModel::CurveChanged(FRichCurve* ChangedCurve, UObject* InCurveOwner)
{
	UNiagaraDataInterfaceCurveBase* CurveDataInterface = Cast<UNiagaraDataInterfaceCurveBase>(InCurveOwner);
	if (CurveDataInterface != nullptr)
	{
		CurveDataInterface->UpdateLUT();
		UpdateCompiledDataInterfaces(CurveDataInterface);
	}
	ResetSystem();
}

void PopulateNiagaraFoldersFromMovieSceneFolders(const TArray<UMovieSceneFolder*>& MovieSceneFolders, const TArray<UMovieSceneTrack*>& MovieSceneTracks, UNiagaraSystemEditorFolder* ParentFolder)
{
	TArray<FName> ValidFolderNames;
	for (UMovieSceneFolder* MovieSceneFolder : MovieSceneFolders)
	{
		ValidFolderNames.Add(MovieSceneFolder->GetFolderName());
		UNiagaraSystemEditorFolder* MatchingNiagaraFolder = nullptr;
		for (UNiagaraSystemEditorFolder* ChildNiagaraFolder : ParentFolder->GetChildFolders())
		{
			CA_ASSUME(ChildNiagaraFolder != nullptr);
			if (ChildNiagaraFolder->GetFolderName() == MovieSceneFolder->GetFolderName())
			{
				MatchingNiagaraFolder = ChildNiagaraFolder;
				break;
			}
		}

		if (MatchingNiagaraFolder == nullptr)
		{
			MatchingNiagaraFolder = NewObject<UNiagaraSystemEditorFolder>(ParentFolder, MovieSceneFolder->GetFolderName(), RF_Transactional);
			MatchingNiagaraFolder->SetFolderName(MovieSceneFolder->GetFolderName());
			ParentFolder->AddChildFolder(MatchingNiagaraFolder);
		}

		PopulateNiagaraFoldersFromMovieSceneFolders(MovieSceneFolder->GetChildFolders(), MovieSceneFolder->GetChildMasterTracks(), MatchingNiagaraFolder);
	}

	TArray<UNiagaraSystemEditorFolder*> ChildNiagaraFolders = ParentFolder->GetChildFolders();
	for (UNiagaraSystemEditorFolder* ChildNiagaraFolder : ChildNiagaraFolders)
	{
		if (ValidFolderNames.Contains(ChildNiagaraFolder->GetFolderName()) == false)
		{
			ParentFolder->RemoveChildFolder(ChildNiagaraFolder);
		}
	}

	TArray<FGuid> ValidEmitterHandleIds;
	for (UMovieSceneTrack* MovieSceneTrack : MovieSceneTracks)
	{
		UMovieSceneNiagaraEmitterTrack* NiagaraEmitterTrack = Cast<UMovieSceneNiagaraEmitterTrack>(MovieSceneTrack);
		if (NiagaraEmitterTrack != nullptr)
		{
			FGuid EmitterHandleId = NiagaraEmitterTrack->GetEmitterHandleViewModel()->GetId();
			ValidEmitterHandleIds.Add(EmitterHandleId);
			if (ParentFolder->GetChildEmitterHandleIds().Contains(EmitterHandleId) == false)
			{
				ParentFolder->AddChildEmitterHandleId(EmitterHandleId);
			}
		}
	}

	TArray<FGuid> ChildEmitterHandleIds = ParentFolder->GetChildEmitterHandleIds();
	for (FGuid& ChildEmitterHandleId : ChildEmitterHandleIds)
	{
		if (ValidEmitterHandleIds.Contains(ChildEmitterHandleId) == false)
		{
			ParentFolder->RemoveChildEmitterHandleId(ChildEmitterHandleId);
		}
	}
}

void FNiagaraSystemViewModel::SequencerDataChanged(EMovieSceneDataChangeType DataChangeType)
{
	if (bUpdatingSequencerFromEmitterDataChange == false && GIsTransacting == false)
	{
		TGuardValue<bool> UpdateGuard(bUpdatingEmittersFromSequencerDataChange, true);

		GetEditorData().Modify();
		TRange<FFrameNumber> FramePlaybackRange = NiagaraSequence->GetMovieScene()->GetPlaybackRange();
		float StartTimeSeconds = NiagaraSequence->GetMovieScene()->GetTickResolution().AsSeconds(FramePlaybackRange.GetLowerBoundValue());
		float EndTimeSeconds = NiagaraSequence->GetMovieScene()->GetTickResolution().AsSeconds(FramePlaybackRange.GetUpperBoundValue());
		GetEditorData().SetPlaybackRange(TRange<float>(StartTimeSeconds, EndTimeSeconds));

		TSet<FGuid> VaildTrackEmitterHandleIds;
		TArray<FEmitterHandleToDuplicate> EmittersToDuplicate;
		TArray<TTuple<TSharedPtr<FNiagaraEmitterHandleViewModel>, FName>> EmitterHandlesToRename;

		bool bRefreshAllTracks = false;
		for (UMovieSceneTrack* Track : NiagaraSequence->GetMovieScene()->GetMasterTracks())
		{
			UMovieSceneNiagaraEmitterTrack* EmitterTrack = CastChecked<UMovieSceneNiagaraEmitterTrack>(Track);
			if (EmitterTrack->GetEmitterHandleViewModel().IsValid())
			{
				if (EmitterTrack->GetAllSections().Num() == 0 && EmitterTrack->GetSectionsWereModified())
				{
					// If there are no sections and the section collection was modified, the section was deleted so skip adding
					// it's handle to the valid collection so that it's emitter is deleted.
					continue;
				}

				VaildTrackEmitterHandleIds.Add(EmitterTrack->GetEmitterHandleViewModel()->GetId());
				EmitterTrack->UpdateEmitterHandleFromTrackChange(NiagaraSequence->GetMovieScene()->GetTickResolution());
				EmitterTrack->GetEmitterHandleViewModel()->GetEmitterViewModel()->GetOrCreateEditorData().Modify();
				EmitterTrack->GetEmitterHandleViewModel()->GetEmitterViewModel()->GetOrCreateEditorData().SetPlaybackRange(GetEditorData().GetPlaybackRange());
				if (EmitterTrack->GetDisplayName().ToString() != EmitterTrack->GetEmitterHandleViewModel()->GetNameText().ToString())
				{
					EmitterHandlesToRename.Add(TTuple<TSharedPtr<FNiagaraEmitterHandleViewModel>, FName>(EmitterTrack->GetEmitterHandleViewModel(), *EmitterTrack->GetDisplayName().ToString()));
				}

				if (EmitterTrack->GetAllSections().Num() > 1)
				{
					// If a section was duplicated, force a refresh to remove the duplicated section since that's not currently supported.
					// TODO: Detect duplicated sections and create a duplicate emitter with the correct values.
					bRefreshAllTracks = true;
				}
			}
			else
			{
				if (EmitterTrack->GetEmitterHandleId().IsValid())
				{
					// The emitter handle is invalid, but the track has a valid Id, most probably because of a copy/paste event
					FEmitterHandleToDuplicate EmitterHandleToDuplicate;
					EmitterHandleToDuplicate.SystemPath = EmitterTrack->GetSystemPath();
					EmitterHandleToDuplicate.EmitterHandleId = EmitterTrack->GetEmitterHandleId();
					EmittersToDuplicate.AddUnique(EmitterHandleToDuplicate);
				}
			}
		}

		bRefreshAllTracks |= EmitterHandlesToRename.Num() > 0;

		for (TTuple<TSharedPtr<FNiagaraEmitterHandleViewModel>, FName>& EmitterHandletoRename : EmitterHandlesToRename)
		{
			EmitterHandletoRename.Get<0>()->SetName(EmitterHandletoRename.Get<1>());
		}

		TSet<FGuid> AllEmitterHandleIds;
		for (TSharedRef<FNiagaraEmitterHandleViewModel> EmitterHandleViewModel : EmitterHandleViewModels)
		{
			AllEmitterHandleIds.Add(EmitterHandleViewModel->GetId());
		}

		TSet<FGuid> RemovedEmitterHandleIds = AllEmitterHandleIds.Difference(VaildTrackEmitterHandleIds);
		if (RemovedEmitterHandleIds.Num() > 0)
		{
			if (bCanModifyEmittersFromTimeline)
			{
				DeleteEmitters(RemovedEmitterHandleIds);
			}
			else
			{
				bRefreshAllTracks = true;
			}
		}

		if (EmittersToDuplicate.Num() > 0)
		{
			if (bCanModifyEmittersFromTimeline)
			{
				DuplicateEmitters(EmittersToDuplicate);
			}
			else
			{
				bRefreshAllTracks = true;
			}
		}

		TArray<UMovieSceneTrack*> RootTracks;
		TArray<UMovieSceneFolder*> RootFolders = NiagaraSequence->GetMovieScene()->GetRootFolders();
		if (RootFolders.Num() != 0 || GetEditorData().GetRootFolder().GetChildFolders().Num() != 0)
		{
			PopulateNiagaraFoldersFromMovieSceneFolders(RootFolders, RootTracks, &GetEditorData().GetRootFolder());
		}

		if (bRefreshAllTracks)
		{
			RefreshSequencerTracks();
		}
	}
}

void FNiagaraSystemViewModel::SequencerTimeChanged()
{
	if (!PreviewComponent || !PreviewComponent->GetSystemInstance() || !PreviewComponent->GetSystemInstance()->GetAreDataInterfacesInitialized())
	{
		return;
	}
	EMovieScenePlayerStatus::Type CurrentStatus = Sequencer->GetPlaybackStatus();
	float CurrentSequencerTime = Sequencer->GetGlobalTime().AsSeconds();
	if (SystemInstance != nullptr)
	{
		// Avoid reentrancy if we're setting the time directly.
		if (bSettingSequencerTimeDirectly == false && CurrentSequencerTime != PreviousSequencerTime)
		{
			// Skip the first update after going from stopped to playing or from playing to stopped because snapping in sequencer may have made
			// the time reverse by a small amount, and sending that update to the System will reset it unnecessarily.
			bool bStartedPlaying = CurrentStatus == EMovieScenePlayerStatus::Playing && PreviousSequencerStatus != EMovieScenePlayerStatus::Playing;
			bool bEndedPlaying = CurrentStatus != EMovieScenePlayerStatus::Playing && PreviousSequencerStatus == EMovieScenePlayerStatus::Playing;

			bool bUpdateDesiredAge = bStartedPlaying == false;
			bool bResetSystemInstance = SystemInstance->IsComplete();

			if (bUpdateDesiredAge)
			{
				if (CurrentStatus == EMovieScenePlayerStatus::Playing)
				{
					PreviewComponent->SetDesiredAge(FMath::Max(CurrentSequencerTime, 0.0f));
				}
				else
				{
					PreviewComponent->SeekToDesiredAge(FMath::Max(CurrentSequencerTime, 0.0f));
				}
			}

			if (bResetSystemInstance)
			{
				// We don't want to reset the current time if we're scrubbing.
				if (CurrentStatus == EMovieScenePlayerStatus::Playing)
				{
					ResetSystem(ETimeResetMode::AllowResetTime, EMultiResetMode::ResetThisInstance, EReinitMode::ResetSystem);
				}
				else
				{
					ResetSystem(ETimeResetMode::KeepCurrentTime, EMultiResetMode::ResetThisInstance, EReinitMode::ResetSystem);
				}
			}
		}
	}

	PreviousSequencerStatus = CurrentStatus;
	PreviousSequencerTime = CurrentSequencerTime;

	OnPostSequencerTimeChangeDelegate.Broadcast();
}

void FNiagaraSystemViewModel::SystemSelectionChanged()
{
	if (bUpdatingSystemSelectionFromSequencer == false)
	{
		UpdateSequencerFromEmitterHandleSelection();
	}
}

void FNiagaraSystemViewModel::SequencerTrackSelectionChanged(TArray<UMovieSceneTrack*> SelectedTracks)
{
	if (bUpdatingSequencerSelectionFromSystem == false)
	{
		UpdateEmitterHandleSelectionFromSequencer();
	}
}

void FNiagaraSystemViewModel::SequencerSectionSelectionChanged(TArray<UMovieSceneSection*> SelectedSections)
{
	if (bUpdatingSequencerSelectionFromSystem == false)
	{
		UpdateEmitterHandleSelectionFromSequencer();
	}
}

void FNiagaraSystemViewModel::UpdateEmitterHandleSelectionFromSequencer()
{
	TArray<FGuid> NewSelectedEmitterHandleIds;

	TArray<UMovieSceneTrack*> SelectedTracks;
	Sequencer->GetSelectedTracks(SelectedTracks);
	for (UMovieSceneTrack* SelectedTrack : SelectedTracks)
	{
		UMovieSceneNiagaraEmitterTrack* SelectedEmitterTrack = Cast<UMovieSceneNiagaraEmitterTrack>(SelectedTrack);
		if (SelectedEmitterTrack != nullptr && SelectedEmitterTrack->GetEmitterHandleViewModel().IsValid())
		{
			NewSelectedEmitterHandleIds.AddUnique(SelectedEmitterTrack->GetEmitterHandleViewModel()->GetId());
		}
	}

	TArray<UMovieSceneSection*> SelectedSections;
	Sequencer->GetSelectedSections(SelectedSections);
	for (UMovieSceneSection* SelectedSection : SelectedSections)
	{
		UMovieSceneNiagaraEmitterSectionBase* SelectedEmitterSection = Cast<UMovieSceneNiagaraEmitterSectionBase>(SelectedSection);
		if (SelectedEmitterSection != nullptr && SelectedEmitterSection->GetEmitterHandleViewModel().IsValid())
		{
			NewSelectedEmitterHandleIds.AddUnique(SelectedEmitterSection->GetEmitterHandleViewModel()->GetId());
		}
	}

	TGuardValue<bool> UpdateGuard(bUpdatingSystemSelectionFromSequencer, true);
	if (SelectionViewModel != nullptr)
	{
		TArray<UNiagaraStackEntry*> EntriesToSelect;
		TArray<UNiagaraStackEntry*> EntriesToDeselect;
		UNiagaraStackEntry* SystemRootEntry = SystemStackViewModel->GetRootEntry();
		if (NewSelectedEmitterHandleIds.Num() > 0 && GetEditorData().GetOwningSystemIsPlaceholder() == false)
		{
			EntriesToSelect.Add(SystemRootEntry);
		}
		else
		{
			EntriesToDeselect.Add(SystemRootEntry);
		}
		for (TSharedRef<FNiagaraEmitterHandleViewModel> EmitterHandleViewModel : EmitterHandleViewModels)
		{
			UNiagaraStackEntry* EmitterRootEntry = EmitterHandleViewModel->GetEmitterStackViewModel()->GetRootEntry();
			if (NewSelectedEmitterHandleIds.Contains(EmitterHandleViewModel->GetId()))
			{
				EntriesToSelect.Add(EmitterRootEntry);
			}
			else
			{
				EntriesToDeselect.Add(EmitterRootEntry);
			}
		}
		bool bClearCurrentSelection = FSlateApplication::Get().GetModifierKeys().IsControlDown() == false;
		SelectionViewModel->UpdateSelectedEntries(EntriesToSelect, EntriesToDeselect, bClearCurrentSelection);
	}
}

void FNiagaraSystemViewModel::UpdateSequencerFromEmitterHandleSelection()
{
	TGuardValue<bool> UpdateGuard(bUpdatingSequencerSelectionFromSystem, true);
	Sequencer->EmptySelection();
	for (FGuid SelectedEmitterHandleId : SelectionViewModel->GetSelectedEmitterHandleIds())
	{
		for (UMovieSceneTrack* MasterTrack : NiagaraSequence->GetMovieScene()->GetMasterTracks())
		{
			UMovieSceneNiagaraEmitterTrack* EmitterTrack = Cast<UMovieSceneNiagaraEmitterTrack>(MasterTrack);
			if (EmitterTrack != nullptr && EmitterTrack->GetEmitterHandleViewModel()->GetId() == SelectedEmitterHandleId)
			{
				Sequencer->SelectTrack(EmitterTrack);
			}
		}
	}
}

void FNiagaraSystemViewModel::SystemInstanceReset()
{
	SystemInstanceInitialized();
}

void FNiagaraSystemViewModel::PreviewComponentSystemInstanceChanged()
{
	FNiagaraSystemInstance* OldSystemInstance = SystemInstance;
	SystemInstance = PreviewComponent->GetSystemInstance();
	if (SystemInstance != OldSystemInstance)
	{
		if (SystemInstance != nullptr)
		{
			SystemInstance->OnInitialized().AddRaw(this, &FNiagaraSystemViewModel::SystemInstanceInitialized);
			SystemInstance->OnReset().AddRaw(this, &FNiagaraSystemViewModel::SystemInstanceReset);
		}
		else
		{
			for (TSharedRef<FNiagaraEmitterHandleViewModel> EmitterHandleViewModel : EmitterHandleViewModels)
			{
				if (EmitterHandleViewModel->GetEmitterHandle())
				{
					EmitterHandleViewModel->SetSimulation(nullptr);
				}
			}
		}
	}
}

void FNiagaraSystemViewModel::SystemInstanceInitialized()
{
	for (TSharedRef<FNiagaraEmitterHandleViewModel> EmitterHandleViewModel : EmitterHandleViewModels)
	{
		if (EmitterHandleViewModel->IsValid())
		{
			EmitterHandleViewModel->SetSimulation(SystemInstance->GetSimulationForHandle(*EmitterHandleViewModel->GetEmitterHandle()));
		}
	}
}

void FNiagaraSystemViewModel::UpdateEmitterFixedBounds()
{
	for (TSharedRef<FNiagaraEmitterHandleViewModel>& EmitterHandleViewModel : EmitterHandleViewModels)
	{
		if (SelectionViewModel->GetSelectedEmitterHandleIds().Contains(EmitterHandleViewModel->GetId()) == false)
		{
			continue;
		}
		FNiagaraEmitterHandle* SelectedEmitterHandle = EmitterHandleViewModel->GetEmitterHandle();
		check(SelectedEmitterHandle);
		UNiagaraEmitter* Emitter = SelectedEmitterHandle->GetInstance();
		for (TSharedRef<FNiagaraEmitterInstance, ESPMode::ThreadSafe>& EmitterInst : PreviewComponent->GetSystemInstance()->GetEmitters())
		{
			if (&EmitterInst->GetEmitterHandle() == SelectedEmitterHandle && !EmitterInst->IsComplete())
			{
				EmitterInst->CalculateFixedBounds(PreviewComponent->GetComponentToWorld().Inverse());
			}
		}
	}
	PreviewComponent->MarkRenderTransformDirty();
}

void FNiagaraSystemViewModel::AddSystemEventHandlers()
{
	if (GetSystem().IsValid())
	{
		TArray<UNiagaraScript*> Scripts;
		Scripts.Add(GetSystem().GetSystemSpawnScript());
		Scripts.Add(GetSystem().GetSystemUpdateScript());
		
		for (UNiagaraScript* Script : Scripts)
		{
			FDelegateHandle OnParameterStoreChangedHandle = Script->RapidIterationParameters.AddOnChangedHandler(
				FNiagaraParameterStore::FOnChanged::FDelegate::CreateRaw<FNiagaraSystemViewModel, const FNiagaraParameterStore&, const UNiagaraScript*>(
					this, &FNiagaraSystemViewModel::SystemParameterStoreChanged, Script->RapidIterationParameters, Script));
			ScriptToOnParameterStoreChangedHandleMap.Add(FObjectKey(Script), OnParameterStoreChangedHandle);
		}

		UserParameterStoreChangedHandle = GetSystem().GetExposedParameters().AddOnChangedHandler(
			FNiagaraParameterStore::FOnChanged::FDelegate::CreateRaw<FNiagaraSystemViewModel, const FNiagaraParameterStore&, const UNiagaraScript*>(
				this, &FNiagaraSystemViewModel::SystemParameterStoreChanged, GetSystem().GetExposedParameters(), nullptr));

		SystemScriptGraphChangedHandler = SystemScriptViewModel->GetGraphViewModel()->GetGraph()->AddOnGraphChangedHandler(FOnGraphChanged::FDelegate::CreateRaw(this, &FNiagaraSystemViewModel::SystemScriptGraphChanged));
	}
}

void FNiagaraSystemViewModel::RemoveSystemEventHandlers()
{
	if (GetSystem().IsValid())
	{
		TArray<UNiagaraScript*> Scripts;
		Scripts.Add(GetSystem().GetSystemSpawnScript());
		Scripts.Add(GetSystem().GetSystemUpdateScript());

		for (UNiagaraScript* Script : Scripts)
		{
			FDelegateHandle* OnParameterStoreChangedHandle = ScriptToOnParameterStoreChangedHandleMap.Find(FObjectKey(Script));
			if (OnParameterStoreChangedHandle != nullptr)
			{
				Script->RapidIterationParameters.RemoveOnChangedHandler(*OnParameterStoreChangedHandle);
			}
		}

		GetSystem().GetExposedParameters().RemoveOnChangedHandler(UserParameterStoreChangedHandle);
		if (SystemScriptViewModel.IsValid())
		{
			SystemScriptViewModel->GetGraphViewModel()->GetGraph()->RemoveOnGraphChangedHandler(SystemScriptGraphChangedHandler);
		}
	}

	ScriptToOnParameterStoreChangedHandleMap.Empty();
	UserParameterStoreChangedHandle.Reset();
}

void FNiagaraSystemViewModel::NotifyPinnedCurvesChanged()
{
	OnPinnedCurvesChangedDelegate.Broadcast();
}

void FNiagaraSystemViewModel::BuildStackModuleData(UNiagaraScript* Script, FGuid InEmitterHandleId, TArray<FNiagaraStackModuleData>& OutStackModuleData)
{
	UNiagaraNodeOutput* OutputNode = FNiagaraEditorUtilities::GetScriptOutputNode(*Script);
	TArray<FNiagaraStackGraphUtilities::FStackNodeGroup> StackGroups;
	FNiagaraStackGraphUtilities::GetStackNodeGroups((UNiagaraNode&)*OutputNode, StackGroups);

	int StackIndex = 0;
	if (StackGroups.Num() > 2)
	{
		for (int i = 1; i < StackGroups.Num() - 1; i++)
		{
			FNiagaraStackGraphUtilities::FStackNodeGroup& StackGroup = StackGroups[i];
			StackIndex = i - 1;
			TArray<UNiagaraNode*> GroupNodes;
			StackGroup.GetAllNodesInGroup(GroupNodes);
			UNiagaraNodeFunctionCall * ModuleNode = Cast<UNiagaraNodeFunctionCall>(StackGroup.EndNode);
			if (ModuleNode != nullptr)
			{
				ENiagaraScriptUsage Usage = Script->GetUsage();
				FGuid UsageId = Script->GetUsageId();
				int32 Index = StackIndex;
				FNiagaraStackModuleData ModuleData = { ModuleNode, Usage, UsageId, Index, InEmitterHandleId };
				OutStackModuleData.Add(ModuleData);
			}
		}
	}

}

void FNiagaraSystemViewModel::StackViewModelStructureChanged()
{
	if (SelectionViewModel != nullptr)
	{
		SelectionViewModel->RefreshDeferred();
	}
}

TSharedPtr<FNiagaraOverviewGraphViewModel> FNiagaraSystemViewModel::GetOverviewGraphViewModel() const
{
	return OverviewGraphViewModel;
}

#undef LOCTEXT_NAMESPACE // NiagaraSystemViewModel
