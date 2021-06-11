// Copyright Epic Games, Inc. All Rights Reserved.

#include "ViewModels/NiagaraEmitterViewModel.h"
#include "NiagaraEmitter.h"
#include "NiagaraEmitterInstance.h"
#include "NiagaraScriptSourceBase.h"
#include "ViewModels/NiagaraSystemViewModel.h"
#include "ViewModels/NiagaraScriptViewModel.h"
#include "NiagaraScriptGraphViewModel.h"
#include "NiagaraObjectSelection.h"
#include "NiagaraNodeOutput.h"
#include "NiagaraScriptSource.h"
#include "NiagaraEmitterEditorData.h"
#include "ViewModels/NiagaraParameterEditMode.h"
#include "NiagaraGraph.h"
#include "NiagaraSystem.h"
#include "NiagaraSystemInstance.h"
#include "NiagaraParameterDefinitions.h"

#include "ScopedTransaction.h"
#include "IContentBrowserSingleton.h"
#include "Framework/Application/SlateApplication.h"
#include "ContentBrowserModule.h"
#include "Framework/Docking/TabManager.h"
#include "Widgets/SWindow.h"
#include "ViewModels/NiagaraEmitterHandleViewModel.h"

#define LOCTEXT_NAMESPACE "EmitterEditorViewModel"

template<> TMap<UNiagaraEmitter*, TArray<FNiagaraEmitterViewModel*>> TNiagaraViewModelManager<UNiagaraEmitter, FNiagaraEmitterViewModel>::ObjectsToViewModels{};

const FText FNiagaraEmitterViewModel::StatsFormat = NSLOCTEXT("NiagaraEmitterViewModel", "StatsFormat", "{0} Particles | {1} ms | {2} MB | {3}");
const FText FNiagaraEmitterViewModel::StatsParticleCountFormat = NSLOCTEXT("NiagaraEmitterViewModel", "StatsParticleCountFormat", "{0} Particles");
const FText FNiagaraEmitterViewModel::ParticleDisabledDueToScalability = NSLOCTEXT("NiagaraEmitterViewModel", "ParticleDisabledDueToScalability", "Disabled due to scalability settings (Current Effects Quality: {0}). See Scalability options for valid range. ");

namespace NiagaraCommands
{
	static FAutoConsoleVariable EmitterStatsFormat(TEXT("Niagara.EmitterStatsFormat"), 1, TEXT("0 shows the particles count, ms, mb and state. 1 shows particles count."));
}

FNiagaraEmitterViewModel::FNiagaraEmitterViewModel(bool bInIsForDataProcessingOnly)
	: SharedScriptViewModel(MakeShareable(new FNiagaraScriptViewModel(LOCTEXT("SharedDisplayName", "Graph"), ENiagaraParameterEditMode::EditAll, bInIsForDataProcessingOnly)))
	, bUpdatingSelectionInternally(false)
	, ExecutionStateEnum(StaticEnum<ENiagaraExecutionState>())
{	
}

void FNiagaraEmitterViewModel::Cleanup()
{
	if (NewParentWindow.IsValid())
	{
		NewParentWindow.Reset();
	}

	if (Emitter.IsValid())
	{
		Emitter->OnEmitterVMCompiled().RemoveAll(this);
		Emitter->OnPropertiesChanged().RemoveAll(this);
	}

	if (SharedScriptViewModel.IsValid())
	{
		SharedScriptViewModel->GetGraphViewModel()->GetNodeSelection()->OnSelectedObjectsChanged().RemoveAll(this);
		SharedScriptViewModel.Reset();
	}

	RemoveScriptEventHandlers();
}

bool FNiagaraEmitterViewModel::Initialize(UNiagaraEmitter* InEmitter, TWeakPtr<FNiagaraEmitterInstance, ESPMode::ThreadSafe> InSimulation)
{
	SetEmitter(InEmitter);
	SetSimulation(InSimulation);

	return true;
}

void FNiagaraEmitterViewModel::Reset()
{
	SetEmitter(nullptr);
	SetSimulation(nullptr);
}

INiagaraParameterDefinitionsSubscriber* FNiagaraEmitterViewModel::GetParameterDefinitionsSubscriber()
{
	return GetEmitter();
}

FNiagaraEmitterViewModel::~FNiagaraEmitterViewModel()
{
	Cleanup();
	UnregisterViewModelWithMap(RegisteredHandle);

	//UE_LOG(LogNiagaraEditor, Warning, TEXT("Deleting Emitter view model %p"), this);
}

void FNiagaraEmitterViewModel::SetEmitter(UNiagaraEmitter* InEmitter)
{
	if (Emitter.IsValid())
	{
		Emitter->OnEmitterVMCompiled().RemoveAll(this);
		Emitter->OnEmitterGPUCompiled().RemoveAll(this);
		Emitter->OnPropertiesChanged().RemoveAll(this);
		UnregisterViewModelWithMap(RegisteredHandle);
	}

	UnregisterViewModelWithMap(RegisteredHandle);

	RemoveScriptEventHandlers();

	Emitter = InEmitter;

	if (Emitter.IsValid())
	{
		Emitter->OnEmitterVMCompiled().AddSP(this, &FNiagaraEmitterViewModel::OnVMCompiled);
		Emitter->OnPropertiesChanged().AddSP(this, &FNiagaraEmitterViewModel::OnEmitterPropertiesChanged);
		Emitter->OnEmitterGPUCompiled().AddSP(this, &FNiagaraEmitterViewModel::OnGPUCompiled);
	}

	AddScriptEventHandlers();

	RegisteredHandle = RegisterViewModelWithMap(InEmitter, this);

	check(SharedScriptViewModel.IsValid());
	SharedScriptViewModel->SetScripts(InEmitter);

	OnEmitterChanged().Broadcast();
}

TWeakPtr<FNiagaraEmitterInstance, ESPMode::ThreadSafe> FNiagaraEmitterViewModel::GetSimulation() const
{
	return Simulation;
}

void FNiagaraEmitterViewModel::SetSimulation(TWeakPtr<FNiagaraEmitterInstance, ESPMode::ThreadSafe> InSimulation)
{
	Simulation = InSimulation;
}

UNiagaraEmitter* FNiagaraEmitterViewModel::GetEmitter()
{
	return Emitter.Get();
}

bool FNiagaraEmitterViewModel::HasParentEmitter() const
{
	return Emitter.IsValid() && Emitter->GetParent() != nullptr;
}

const UNiagaraEmitter* FNiagaraEmitterViewModel::GetParentEmitter() const
{
	return Emitter.IsValid() ? Emitter->GetParent() : nullptr;
}

FText FNiagaraEmitterViewModel::GetParentNameText() const
{
	if (Emitter.IsValid() && Emitter->GetParent() != nullptr)
	{
		return FText::FromString(Emitter->GetParent()->GetName());
	}
	return FText();
}

FText FNiagaraEmitterViewModel::GetParentPathNameText() const
{
	if (Emitter.IsValid() && Emitter->GetParent() != nullptr)
	{
		return FText::FromString(Emitter->GetParent()->GetPathName());
	}
	return FText();
}

void FNiagaraEmitterViewModel::CreateNewParentWindow(TSharedRef<FNiagaraEmitterHandleViewModel> EmitterHandleViewModel)
{
	FAssetPickerConfig AssetPickerConfig;
	AssetPickerConfig.SelectionMode = ESelectionMode::SingleToggle;
	AssetPickerConfig.InitialAssetViewType = EAssetViewType::List;
	AssetPickerConfig.Filter.ClassNames.Add(UNiagaraEmitter::StaticClass()->GetFName());
	AssetPickerConfig.OnAssetsActivated.BindSP(this, &FNiagaraEmitterViewModel::UpdateParentEmitter, EmitterHandleViewModel);

	FContentBrowserModule& ContentBrowserModule = FModuleManager::Get().LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));
	TSharedRef<SWidget> AssetPicker = ContentBrowserModule.Get().CreateAssetPicker(AssetPickerConfig);

	const FText TitleText = LOCTEXT("NewParentEmitter", "New Parent Emitter");
	// Create the window to pick the class
	SAssignNew(NewParentWindow, SWindow)
		.Title(TitleText)
		.SizingRule(ESizingRule::UserSized)
		.ClientSize(FVector2D(700.f, 300.f))
		.AutoCenter(EAutoCenter::PreferredWorkArea)
		.SupportsMinimize(false);
	NewParentWindow->SetContent(AssetPicker);
	TSharedPtr<SWindow> RootWindow = FGlobalTabmanager::Get()->GetRootWindow();
	if (RootWindow.IsValid())
	{
		FSlateApplication::Get().AddWindowAsNativeChild(NewParentWindow.ToSharedRef(), RootWindow.ToSharedRef());
	}
	else
	{
		FSlateApplication::Get().AddWindow(NewParentWindow.ToSharedRef());
	}
}

void FNiagaraEmitterViewModel::UpdateParentEmitter(const TArray<FAssetData> & ActivatedAssets, EAssetTypeActivationMethod::Type ActivationMethod, TSharedRef<FNiagaraEmitterHandleViewModel> EmitterHandleViewModel)
{
	FScopedTransaction ScopedTransaction(LOCTEXT("RemoveParentEmitterTransaction", "Remove Parent Emitter"));
	if ((ActivationMethod == EAssetTypeActivationMethod::DoubleClicked || ActivationMethod == EAssetTypeActivationMethod::Opened) && ActivatedAssets.Num() == 1)
	{
		if (Emitter.IsValid())
		{
			FAssetData NewParentData = ActivatedAssets[0];
			UNiagaraEmitter* NewParentPtr = Cast<UNiagaraEmitter>(NewParentData.GetAsset());
			if (NewParentPtr && NewParentPtr != Emitter)
			{
				Emitter->PreEditChange(nullptr);
				Emitter->Reparent(*NewParentPtr);
				Emitter->MergeChangesFromParent();
				Emitter->PostEditChange();
				EmitterHandleViewModel->GetOwningSystemViewModel()->RefreshAll();
				NewParentWindow->RequestDestroyWindow();
			}
			
		}
		
	}
}

void FNiagaraEmitterViewModel::RemoveParentEmitter()
{
	FScopedTransaction ScopedTransaction(LOCTEXT("RemoveParentEmitterTransaction", "Remove Parent Emitter"));
	Emitter->Modify();
	Emitter->RemoveParent();
	OnParentRemovedDelegate.Broadcast();
}

FText FNiagaraEmitterViewModel::GetStatsText() const
{
	if (Simulation.IsValid())
	{
		TSharedPtr<FNiagaraEmitterInstance, ESPMode::ThreadSafe> SimInstance = Simulation.Pin();
		if (SimInstance.IsValid())
		{
			static const FNumberFormattingOptions FractionalFormatOptions = FNumberFormattingOptions()
				.SetMinimumFractionalDigits(3)
				.SetMaximumFractionalDigits(3);

			if (!SimInstance->IsReadyToRun() || SimInstance->GetParentSystemInstance()->GetSystem()->HasOutstandingCompilationRequests())
			{
				return LOCTEXT("PendingCompile", "Compilation in progress...");
			}

			const FNiagaraEmitterHandle& Handle = SimInstance->GetEmitterHandle();
			if (Handle.GetInstance())
			{
				if (Handle.IsValid() == false)
				{
					return LOCTEXT("InvalidHandle", "Invalid handle");
				}

				UNiagaraEmitter* HandleEmitter = Handle.GetInstance();
				if (HandleEmitter == nullptr)
				{
					return LOCTEXT("NullInstance", "Invalid instance");
				}
				
				if (Handle.GetIsEnabled() == false)
				{
					return LOCTEXT("DisabledSimulation", "Emitter is not enabled. Excluded from code.");
				}

				if (!HandleEmitter->IsValid())
				{
					return LOCTEXT("InvalidInstance", "Invalid Emitter! May have compile errors.");
				}

 				int32 QualityLevel = FNiagaraPlatformSet::GetQualityLevel();
  				if (!HandleEmitter->IsAllowedByScalability())
  				{
					return FText::Format(ParticleDisabledDueToScalability, FNiagaraPlatformSet::GetQualityLevelText(QualityLevel));
  				}

				if (NiagaraCommands::EmitterStatsFormat->GetInt() == 1)
				{
					return FText::Format(StatsParticleCountFormat, FText::AsNumber(SimInstance->GetNumParticles()));
				}
				else
				{
					const double Megabyte = 1024 * 1024;
					return FText::Format(StatsFormat,
						FText::AsNumber(SimInstance->GetNumParticles()),
						FText::AsNumber(SimInstance->GetTotalCPUTimeMS(), &FractionalFormatOptions),
						FText::AsNumber(double(SimInstance->GetTotalBytesUsed()) / Megabyte, &FractionalFormatOptions),
						ExecutionStateEnum->GetDisplayNameTextByValue((int32)SimInstance->GetExecutionState()));
				}
			}
		}
	}
	else if(!Emitter->IsReadyToRun())
	{
		return LOCTEXT("SimulationNotReady", "Preparing simulation...");
	}
	
	return LOCTEXT("NoActivePreview", "No running preview...");
}

TSharedRef<FNiagaraScriptViewModel> FNiagaraEmitterViewModel::GetSharedScriptViewModel()
{
	return SharedScriptViewModel.ToSharedRef();
}

const UNiagaraEmitterEditorData& FNiagaraEmitterViewModel::GetEditorData() const
{
	check(Emitter.IsValid());

	const UNiagaraEmitterEditorData* EditorData = Cast<UNiagaraEmitterEditorData>(Emitter->GetEditorData());
	if (EditorData == nullptr)
	{
		EditorData = GetDefault<UNiagaraEmitterEditorData>();
	}
	return *EditorData;
}

UNiagaraEmitterEditorData& FNiagaraEmitterViewModel::GetOrCreateEditorData()
{
	check(Emitter.IsValid());
	UNiagaraEmitterEditorData* EditorData = Cast<UNiagaraEmitterEditorData>(Emitter->GetEditorData());
	if (EditorData == nullptr)
	{
		EditorData = NewObject<UNiagaraEmitterEditorData>(Emitter.Get(), NAME_None, RF_Transactional);
		Emitter->Modify(); 
		Emitter->SetEditorData(EditorData);
	}
	return *EditorData;
}

void FNiagaraEmitterViewModel::AddEventHandler(FNiagaraEventScriptProperties& EventScriptProperties, bool bResetGraphForOutput /*= false*/)
{
	EventScriptProperties.Script = NewObject<UNiagaraScript>(GetEmitter(), MakeUniqueObjectName(GetEmitter(), UNiagaraScript::StaticClass(), "EventScript"), EObjectFlags::RF_Transactional);
	EventScriptProperties.Script->SetUsage(ENiagaraScriptUsage::ParticleEventScript);
	EventScriptProperties.Script->SetUsageId(FGuid::NewGuid());
	EventScriptProperties.Script->SetLatestSource(GetSharedScriptViewModel()->GetGraphViewModel()->GetScriptSource());
	Emitter->AddEventHandler(EventScriptProperties);
	if (bResetGraphForOutput)
	{
		FNiagaraStackGraphUtilities::ResetGraphForOutput(*GetSharedScriptViewModel()->GetGraphViewModel()->GetGraph(), ENiagaraScriptUsage::ParticleEventScript, EventScriptProperties.Script->GetUsageId());
	}
}

void FNiagaraEmitterViewModel::OnGPUCompiled(UNiagaraEmitter* InEmitter)
{
	// Flush compilation logs just like the CPU script compiled.
	OnVMCompiled(InEmitter);
}

void FNiagaraEmitterViewModel::OnVMCompiled(UNiagaraEmitter* InEmitter)
{
	if (Emitter.IsValid() && InEmitter == Emitter.Get())
	{
		TArray<ENiagaraScriptCompileStatus> CompileStatuses;
		TArray<FString> CompileErrors;
		TArray<FString> CompilePaths;
		TArray<TPair<ENiagaraScriptUsage, int32> > Usages;

		ENiagaraScriptCompileStatus AggregateStatus = ENiagaraScriptCompileStatus::NCS_UpToDate;
		FString AggregateErrors;

		TArray<UNiagaraScript*> Scripts;
		Emitter->GetScripts(Scripts, true);

		int32 EventsFound = 0;
		for (int32 i = 0; i < Scripts.Num(); i++)
		{
			UNiagaraScript* Script = Scripts[i];
			if (Script != nullptr && Script->GetVMExecutableData().IsValid())
			{
				CompileStatuses.Add(Script->GetVMExecutableData().LastCompileStatus);
				CompileErrors.Add(Script->GetVMExecutableData().ErrorMsg);
				CompilePaths.Add(Script->GetPathName());

				if (Script->GetUsage() == ENiagaraScriptUsage::ParticleEventScript)
				{
					Usages.Add(TPair<ENiagaraScriptUsage, int32>(Script->GetUsage(), EventsFound));
					EventsFound++;
				}
				else
				{
					Usages.Add(TPair<ENiagaraScriptUsage, int32>(Script->GetUsage(), 0));
				}
			}
			else
			{
				CompileStatuses.Add(ENiagaraScriptCompileStatus::NCS_Unknown);
				CompileErrors.Add(TEXT("Invalid script pointer!"));
				CompilePaths.Add(TEXT("Unknown..."));
				Usages.Add(TPair<ENiagaraScriptUsage, int32>(ENiagaraScriptUsage::Function, 0));
			}

		}

		// Now handle any GPU sims..
		TArray<UNiagaraScript*> GPUScripts;
		
		// Going ahead and making an array for future proofing..
		{
			UNiagaraScript* GPUScript = Emitter->GetGPUComputeScript();
			GPUScripts.Add(GPUScript);
		}

		// Push in GPU script errors into the other scripts
		for (UNiagaraScript* GPUScript : GPUScripts)
		{
			if (GPUScript)
			{
				const FNiagaraShaderScript* ShaderScript = GPUScript->GetRenderThreadScript();
				if (!ShaderScript)
					return;

				for (int32 i = 0; i < Scripts.Num(); i++)
				{
					if (Scripts[i] != nullptr && CompileStatuses.Num() > i && CompileErrors.Num() > i && GPUScript->ContainsUsage(Scripts[i]->Usage))
					{
						const TArray<FString>& NewErrors = ShaderScript->GetCompileErrors();
						bool bErrors = false;
						for (const FString& ErrorStr : NewErrors)
						{
							if (ErrorStr.Contains(TEXT("err0r")))
							{
								CompileErrors[i] += ErrorStr + TEXT("\n");
								bErrors = true;
							}
						}

						if (bErrors)
						{
							CompileStatuses[i] = ENiagaraScriptCompileStatus::NCS_Error;
						}
					}
				}

			}
		}

		for (int32 i = 0; i < CompileStatuses.Num(); i++)
		{
			AggregateStatus = FNiagaraEditorUtilities::UnionCompileStatus(AggregateStatus, CompileStatuses[i]);
			AggregateErrors += CompilePaths[i] + TEXT(" ") + FNiagaraEditorUtilities::StatusToText(CompileStatuses[i]).ToString() + TEXT("\n");
			AggregateErrors += CompileErrors[i] + TEXT("\n");
		}
		
		if (SharedScriptViewModel.IsValid())
		{
			SharedScriptViewModel->UpdateCompileStatus(AggregateStatus, AggregateErrors, CompileStatuses, CompileErrors, CompilePaths, Scripts);
		}
	}
	OnScriptCompiled().Broadcast(nullptr, FGuid());
}

ENiagaraScriptCompileStatus FNiagaraEmitterViewModel::GetLatestCompileStatus()
{
	check(SharedScriptViewModel.IsValid());
	ENiagaraScriptCompileStatus UnionStatus = SharedScriptViewModel->GetLatestCompileStatus();

	if (Emitter->SimTarget == ENiagaraSimTarget::GPUComputeSim)
	{
		if (UnionStatus != ENiagaraScriptCompileStatus::NCS_Dirty)
		{
			if (!Emitter->GetGPUComputeScript()->AreScriptAndSourceSynchronized())
			{
				UnionStatus = ENiagaraScriptCompileStatus::NCS_Dirty;
			}
		}
	}
	return UnionStatus;
}


FNiagaraEmitterViewModel::FOnEmitterChanged& FNiagaraEmitterViewModel::OnEmitterChanged()
{
	return OnEmitterChangedDelegate;
}

FNiagaraEmitterViewModel::FOnPropertyChanged& FNiagaraEmitterViewModel::OnPropertyChanged()
{
	return OnPropertyChangedDelegate;
}

FNiagaraEmitterViewModel::FOnScriptCompiled& FNiagaraEmitterViewModel::OnScriptCompiled()
{
	return OnScriptCompiledDelegate;
}

FNiagaraEmitterViewModel::FOnParentRemoved& FNiagaraEmitterViewModel::OnParentRemoved()
{
	return OnParentRemovedDelegate;
}

FNiagaraEmitterViewModel::FOnScriptGraphChanged& FNiagaraEmitterViewModel::OnScriptGraphChanged()
{
	return OnScriptGraphChangedDelegate;
}

FNiagaraEmitterViewModel::FOnScriptParameterStoreChanged& FNiagaraEmitterViewModel::OnScriptParameterStoreChanged()
{
	return OnScriptParameterStoreChangedDelegate;
}

void FNiagaraEmitterViewModel::AddScriptEventHandlers()
{
	if (Emitter.IsValid())
	{
		TArray<UNiagaraScript*> Scripts;
		Emitter->GetScripts(Scripts, false);
		for (UNiagaraScript* Script : Scripts)
		{
			UNiagaraScriptSource* ScriptSource = CastChecked<UNiagaraScriptSource>(Script->GetLatestSource());
			FDelegateHandle OnGraphChangedHandle = ScriptSource->NodeGraph->AddOnGraphChangedHandler(
				FOnGraphChanged::FDelegate::CreateSP<FNiagaraEmitterViewModel, const UNiagaraScript&>(this->AsShared(), &FNiagaraEmitterViewModel::ScriptGraphChanged, *Script));
			FDelegateHandle OnGraphNeedRecompileHandle = ScriptSource->NodeGraph->AddOnGraphNeedsRecompileHandler(
				FOnGraphChanged::FDelegate::CreateSP<FNiagaraEmitterViewModel, const UNiagaraScript&>(this->AsShared(), &FNiagaraEmitterViewModel::ScriptGraphChanged, *Script));

			ScriptToOnGraphChangedHandleMap.Add(FObjectKey(Script), OnGraphChangedHandle);
			ScriptToRecompileHandleMap.Add(FObjectKey(Script), OnGraphNeedRecompileHandle);

			FDelegateHandle OnParameterStoreChangedHandle = Script->RapidIterationParameters.AddOnChangedHandler(
				FNiagaraParameterStore::FOnChanged::FDelegate::CreateSP<FNiagaraEmitterViewModel, const FNiagaraParameterStore&, const UNiagaraScript&>(
					this->AsShared(), &FNiagaraEmitterViewModel::ScriptParameterStoreChanged, Script->RapidIterationParameters, *Script));
			ScriptToOnParameterStoreChangedHandleMap.Add(FObjectKey(Script), OnParameterStoreChangedHandle);
		}
	}
}

void FNiagaraEmitterViewModel::RemoveScriptEventHandlers()
{
	if (Emitter.IsValid())
	{
		TArray<UNiagaraScript*> Scripts;
		Emitter->GetScripts(Scripts, false);
		for (UNiagaraScript* Script : Scripts)
		{
			FDelegateHandle* OnGraphChangedHandle = ScriptToOnGraphChangedHandleMap.Find(FObjectKey(Script));
			if (OnGraphChangedHandle != nullptr)
			{
				UNiagaraScriptSource* ScriptSource = CastChecked<UNiagaraScriptSource>(Script->GetLatestSource());
				ScriptSource->NodeGraph->RemoveOnGraphChangedHandler(*OnGraphChangedHandle);
			}

			FDelegateHandle* OnGraphRecompileHandle = ScriptToRecompileHandleMap.Find(FObjectKey(Script));
			if (OnGraphRecompileHandle != nullptr)
			{
				UNiagaraScriptSource* ScriptSource = CastChecked<UNiagaraScriptSource>(Script->GetLatestSource());
				ScriptSource->NodeGraph->RemoveOnGraphNeedsRecompileHandler(*OnGraphRecompileHandle);
			}

			FDelegateHandle* OnParameterStoreChangedHandle = ScriptToOnParameterStoreChangedHandleMap.Find(FObjectKey(Script));
			if (OnParameterStoreChangedHandle != nullptr)
			{
				Script->RapidIterationParameters.RemoveOnChangedHandler(*OnParameterStoreChangedHandle);
			}
		}
	}

	ScriptToOnGraphChangedHandleMap.Empty();
	ScriptToRecompileHandleMap.Empty();
	ScriptToOnParameterStoreChangedHandleMap.Empty();
}

void FNiagaraEmitterViewModel::ScriptGraphChanged(const FEdGraphEditAction& InAction, const UNiagaraScript& OwningScript)
{
	OnScriptGraphChangedDelegate.Broadcast(InAction, OwningScript);
}

void FNiagaraEmitterViewModel::ScriptParameterStoreChanged(const FNiagaraParameterStore& ChangedParameterStore, const UNiagaraScript& OwningScript)
{
	OnScriptParameterStoreChangedDelegate.Broadcast(ChangedParameterStore, OwningScript);
}

void FNiagaraEmitterViewModel::OnEmitterPropertiesChanged()
{
	// Check that these are valid since post edit changed is called on objects even when they've been deleted as a result of undo/redo.
	if (SharedScriptViewModel.IsValid() && Emitter.IsValid())
	{
		// When the properties change we reset the scripts on the script view model because gpu/cpu or interpolation may have changed.
		SharedScriptViewModel->SetScripts(Emitter.Get());
		OnPropertyChangedDelegate.Broadcast();
	}
}

#undef LOCTEXT_NAMESPACE
