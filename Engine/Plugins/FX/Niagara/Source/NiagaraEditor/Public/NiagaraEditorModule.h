// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
#include "Toolkits/AssetEditorToolkit.h"
#include "NiagaraTypes.h"
#include "INiagaraCompiler.h"
#include "AssetTypeCategories.h"
#include "NiagaraPerfBaseline.h"
#include "NiagaraDebuggerCommon.h"
#include "NiagaraEditorModule.generated.h"

class IAssetTools;
class IAssetTypeActions;
class INiagaraEditorTypeUtilities;
class UNiagaraSettings;
class USequencerSettings;
class UNiagaraStackViewModel;
class UNiagaraStackEntry;
class UNiagaraStackIssue;
class FNiagaraSystemViewModel;
class FNiagaraScriptMergeManager;
class FNiagaraCompileOptions;
class FNiagaraCompileRequestDataBase;
class UMovieSceneNiagaraParameterTrack;
struct IConsoleCommand;
class INiagaraEditorOnlyDataUtilities;
class FNiagaraEditorCommands;
struct FNiagaraScriptHighlight;
class FNiagaraClipboard;
class UNiagaraScratchPadViewModel;
class FHlslNiagaraCompiler;
class FNiagaraComponentBroker;
class INiagaraStackObjectIssueGenerator;
class UNiagaraEffectType;
class SNiagaraBaselineViewport;
class FParticlePerfStatsListener_NiagaraBaselineComparisonRender;
class FNiagaraDebugger;
class UNiagaraParameterDefinitions;
class UNiagaraReservedParametersManager;

DECLARE_STATS_GROUP(TEXT("Niagara Editor"), STATGROUP_NiagaraEditor, STATCAT_Advanced);

extern NIAGARAEDITOR_API int32 GbShowNiagaraDeveloperWindows;

/* Defines methods for allowing external modules to supply widgets to the core editor module. */
class NIAGARAEDITOR_API INiagaraEditorWidgetProvider
{
public:
	virtual TSharedRef<SWidget> CreateStackView(UNiagaraStackViewModel& StackViewModel) const = 0;
	virtual TSharedRef<SWidget> CreateSystemOverview(TSharedRef<FNiagaraSystemViewModel> SystemViewModel) const = 0;
	virtual TSharedRef<SWidget> CreateStackIssueIcon(UNiagaraStackViewModel& StackViewModel, UNiagaraStackEntry& StackEntry) const = 0;
	virtual TSharedRef<SWidget> CreateScriptScratchPad(UNiagaraScratchPadViewModel& ScriptScratchPadViewModel) const = 0;
	virtual TSharedRef<SWidget> CreateCurveOverview(TSharedRef<FNiagaraSystemViewModel> SystemViewModel) const = 0;
	virtual FLinearColor GetColorForExecutionCategory(FName ExecutionCategory) const = 0;
};

/* Wrapper struct for tracking parameters that are reserved by parameter definitions assets. */
USTRUCT()
struct FReservedParameter
{
	GENERATED_BODY()

public:
	FReservedParameter()
		: Parameter(FNiagaraVariable())
		, ReservingDefinitionsAsset(nullptr)
	{};

	FReservedParameter(const FNiagaraVariableBase& InParameter, const UNiagaraParameterDefinitions* InReservingDefinitionsAsset)
		: Parameter(InParameter)
		, ReservingDefinitionsAsset(const_cast<UNiagaraParameterDefinitions*>(InReservingDefinitionsAsset))
	{};

	bool operator== (const FReservedParameter& Other) const { return Parameter == Other.GetParameter() && ReservingDefinitionsAsset == Other.GetReservingDefinitionsAsset(); };

	const FNiagaraVariableBase& GetParameter() const { return Parameter; };
	const UNiagaraParameterDefinitions* GetReservingDefinitionsAsset() const { return ReservingDefinitionsAsset; };

private:
	UPROPERTY(transient)
	FNiagaraVariableBase Parameter;
		 
	UPROPERTY(transient)
	UNiagaraParameterDefinitions* ReservingDefinitionsAsset;
};

FORCEINLINE uint32 GetTypeHash(const FReservedParameter& ReservedParameter) { return GetTypeHash(ReservedParameter.GetParameter().GetName()); };

/** Niagara Editor module */
class FNiagaraEditorModule : public IModuleInterface,
	public IHasMenuExtensibility, public IHasToolBarExtensibility, public FGCObject
{
public:
	DECLARE_DELEGATE_RetVal_OneParam(UMovieSceneNiagaraParameterTrack*, FOnCreateMovieSceneTrackForParameter, FNiagaraVariable);
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnCheckScriptToolkitsShouldFocusGraphElement, const FNiagaraScriptIDAndGraphFocusInfo*);

public:
	FNiagaraEditorModule();

	// IModuleInterface
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	/** Get the instance of this module. */
	NIAGARAEDITOR_API static FNiagaraEditorModule& Get();

	/** Start the compilation of the specified script. */
	virtual int32 CompileScript(const FNiagaraCompileRequestDataBase* InCompileRequest, const FNiagaraCompileOptions& InCompileOptions);
	virtual TSharedPtr<FNiagaraVMExecutableData> GetCompilationResult(int32 JobID, bool bWait);

	TSharedPtr<FNiagaraCompileRequestDataBase, ESPMode::ThreadSafe> Precompile(UObject* Obj, FGuid Version);

	/** Gets the extensibility managers for outside entities to extend static mesh editor's menus and toolbars */
	virtual TSharedPtr<FExtensibilityManager> GetMenuExtensibilityManager() override {return MenuExtensibilityManager;}
	virtual TSharedPtr<FExtensibilityManager> GetToolBarExtensibilityManager() override {return ToolBarExtensibilityManager;}

	/** Registers niagara editor type utilities for a specific type. */
	void RegisterTypeUtilities(FNiagaraTypeDefinition Type, TSharedRef<INiagaraEditorTypeUtilities, ESPMode::ThreadSafe> EditorUtilities);

	/** Register/unregister niagara editor settings. */
	void RegisterSettings();
	void UnregisterSettings();

	/** Gets Niagara editor type utilities for a specific type if there are any registered. */
	TSharedPtr<INiagaraEditorTypeUtilities, ESPMode::ThreadSafe> NIAGARAEDITOR_API GetTypeUtilities(const FNiagaraTypeDefinition& Type);

	static EAssetTypeCategories::Type GetAssetCategory() { return NiagaraAssetCategory; }

	NIAGARAEDITOR_API void RegisterWidgetProvider(TSharedRef<INiagaraEditorWidgetProvider> InWidgetProvider);
	NIAGARAEDITOR_API void UnregisterWidgetProvider(TSharedRef<INiagaraEditorWidgetProvider> InWidgetProvider);

	TSharedRef<INiagaraEditorWidgetProvider> GetWidgetProvider() const;

	TSharedRef<FNiagaraScriptMergeManager> GetScriptMergeManager() const;

	void RegisterParameterTrackCreatorForType(const UScriptStruct& StructType, FOnCreateMovieSceneTrackForParameter CreateTrack);
	void UnregisterParameterTrackCreatorForType(const UScriptStruct& StructType);
	bool CanCreateParameterTrackForType(const UScriptStruct& StructType);
	UMovieSceneNiagaraParameterTrack* CreateParameterTrackForType(const UScriptStruct& StructType, FNiagaraVariable Parameter);

	/** Niagara Editor app identifier string */
	static const FName NiagaraEditorAppIdentifier;

	/** The tab color scale for niagara editors. */
	static const FLinearColor WorldCentricTabColorScale;

	/** Get the niagara UI commands. */
	NIAGARAEDITOR_API const class FNiagaraEditorCommands& Commands();

	FOnCheckScriptToolkitsShouldFocusGraphElement& GetOnScriptToolkitsShouldFocusGraphElement() { return OnCheckScriptToolkitsShouldFocusGraphElement; };

	NIAGARAEDITOR_API TSharedPtr<FNiagaraSystemViewModel> GetExistingViewModelForSystem(UNiagaraSystem* InSystem);

	NIAGARAEDITOR_API const FNiagaraEditorCommands& GetCommands() const;

	void InvalidateCachedScriptAssetData();

	const TArray<FNiagaraScriptHighlight>& GetCachedScriptAssetHighlights() const;

	void GetScriptAssetsMatchingHighlight(const FNiagaraScriptHighlight& InHighlight, TArray<FAssetData>& OutMatchingScriptAssets) const;

	NIAGARAEDITOR_API FNiagaraClipboard& GetClipboard() const;

	template<typename T>
	void EnqueueObjectForDeferredDestruction(TSharedRef<T> InObjectToDestruct)
	{
		TDeferredDestructionContainer<T>* ObjectInContainer = new TDeferredDestructionContainer<T>(InObjectToDestruct);
		EnqueueObjectForDeferredDestructionInternal(ObjectInContainer);
	}

	FORCEINLINE INiagaraStackObjectIssueGenerator* FindStackObjectIssueGenerator(FName StructName)
	{
		if (INiagaraStackObjectIssueGenerator** FoundGenerator = StackIssueGenerators.Find(StructName))
		{
			return *FoundGenerator;
		}
		return nullptr;
	}

#if WITH_NIAGARA_DEBUGGER
	TSharedPtr<FNiagaraDebugger> GetDebugger(){ return Debugger; }
#endif

	UNiagaraReservedParametersManager* GetReservedParametersManager();

	NIAGARAEDITOR_API void GetTargetSystemAndEmitterForDataInterface(UNiagaraDataInterface* InDataInterface, UNiagaraSystem*& OutOwningSystem, UNiagaraEmitter*& OutOwningEmitter);
	NIAGARAEDITOR_API void GetDataInterfaceFeedbackSafe(UNiagaraDataInterface* InDataInterface, TArray<FNiagaraDataInterfaceError>& OutErrors, TArray<FNiagaraDataInterfaceFeedback>& Warnings, TArray<FNiagaraDataInterfaceFeedback>& Info);

	TArray<UNiagaraParameterDefinitions*>& GetReservedDefinitions() { return ReservedDefinitions; };

private:
	class FDeferredDestructionContainerBase
	{
	public:
		virtual ~FDeferredDestructionContainerBase()
		{
		}
	};

	template<typename T>
	class TDeferredDestructionContainer : public FDeferredDestructionContainerBase
	{
	public:
		TDeferredDestructionContainer(TSharedRef<const T> InObjectToDestruct)
			: ObjectToDestuct(InObjectToDestruct)
		{
		}

		virtual ~TDeferredDestructionContainer()
		{
			ObjectToDestuct.Reset();
		}

		TSharedPtr<const T> ObjectToDestuct;
	};

	void RegisterAssetTypeAction(IAssetTools& AssetTools, TSharedRef<IAssetTypeActions> Action);
	void OnNiagaraSettingsChangedEvent(const FName& PropertyName, const UNiagaraSettings* Settings);
	void OnPreGarbageCollection();
	void OnExecParticleInvoked(const TCHAR* InStr);
	void OnPostEngineInit();
	void OnDeviceProfileManagerUpdated();
	void OnPreviewPlatformChanged();
	void OnPreExit();

	/** FGCObject interface */
	virtual void AddReferencedObjects( FReferenceCollector& Collector ) override;
	virtual FString GetReferencerName() const override
	{
		return "FNiagaraEditorModule";
	}

	void TestCompileScriptFromConsole(const TArray<FString>& Arguments);
	void ReinitializeStyle();

	void EnqueueObjectForDeferredDestructionInternal(FDeferredDestructionContainerBase* InObjectToDestruct);

	bool DeferredDestructObjects(float InDeltaTime);

	void RegisterStackIssueGenerator(FName StructName, INiagaraStackObjectIssueGenerator* Generator)
	{
		StackIssueGenerators.Add(StructName) = Generator;
	}

	void PreloadAllParameterDefinitions();

private:
	TSharedPtr<FExtensibilityManager> MenuExtensibilityManager;
	TSharedPtr<FExtensibilityManager> ToolBarExtensibilityManager;

	/** All created asset type actions.  Cached here so that we can unregister it during shutdown. */
	TArray< TSharedPtr<IAssetTypeActions> > CreatedAssetTypeActions;

	FCriticalSection TypeEditorsCS;
	TMap<FNiagaraTypeDefinition, TSharedRef<INiagaraEditorTypeUtilities, ESPMode::ThreadSafe>> TypeToEditorUtilitiesMap;
	TSharedPtr<INiagaraEditorTypeUtilities, ESPMode::ThreadSafe> EnumTypeUtilities;

	static EAssetTypeCategories::Type NiagaraAssetCategory;

	FDelegateHandle CreateEmitterTrackEditorHandle;
	FDelegateHandle CreateSystemTrackEditorHandle;

	FDelegateHandle CreateBoolParameterTrackEditorHandle;
	FDelegateHandle CreateFloatParameterTrackEditorHandle;
	FDelegateHandle CreateIntegerParameterTrackEditorHandle;
	FDelegateHandle CreateVectorParameterTrackEditorHandle;
	FDelegateHandle CreateColorParameterTrackEditorHandle;

	FDelegateHandle ScriptCompilerHandle;
	FDelegateHandle CompileResultHandle;
	FDelegateHandle PrecompilerHandle;

	FDelegateHandle DeviceProfileManagerUpdatedHandle;

	FDelegateHandle PreviewPlatformChangedHandle;

	USequencerSettings* SequencerSettings;

	TSharedPtr<INiagaraEditorWidgetProvider> WidgetProvider;

	TSharedPtr<FNiagaraScriptMergeManager> ScriptMergeManager;

	TSharedPtr<INiagaraEditorOnlyDataUtilities> EditorOnlyDataUtilities;

	TSharedPtr<FNiagaraComponentBroker> NiagaraComponentBroker;

	TMap<const UScriptStruct*, FOnCreateMovieSceneTrackForParameter> TypeToParameterTrackCreatorMap;

	IConsoleCommand* TestCompileScriptCommand;
	IConsoleCommand* DumpRapidIterationParametersForAsset;
	IConsoleCommand* PreventSystemRecompileCommand;
	IConsoleCommand* PreventAllSystemRecompilesCommand;
	IConsoleCommand* UpgradeAllNiagaraAssetsCommand;
	IConsoleCommand* DumpCompileIdDataForAssetCommand;

	FOnCheckScriptToolkitsShouldFocusGraphElement OnCheckScriptToolkitsShouldFocusGraphElement;

	mutable TOptional<TArray<FNiagaraScriptHighlight>> CachedScriptAssetHighlights;

	bool bThumbnailRenderersRegistered;

	TSharedRef<FNiagaraClipboard> Clipboard;

	IConsoleCommand* ReinitializeStyleCommand;

	TMap<int32, TSharedPtr<FHlslNiagaraCompiler>> ActiveCompilations;

	TArray<TSharedRef<const FDeferredDestructionContainerBase>> EnqueuedForDeferredDestruction;

	TMap<FName, INiagaraStackObjectIssueGenerator*> StackIssueGenerators;

#if NIAGARA_PERF_BASELINES
	void GeneratePerfBaselines(TArray<UNiagaraEffectType*>& BaselinesToGenerate);
	void OnPerfBaselineWindowClosed(const TSharedRef<SWindow>& ClosedWindow);

	/** Viewport used when generating the performance baselines for Niagara systems. */
	TSharedPtr<SNiagaraBaselineViewport> BaselineViewport;
#endif

#if WITH_NIAGARA_DEBUGGER
	TSharedPtr<FNiagaraDebugger> Debugger;
#endif

	UNiagaraReservedParametersManager* ReservedParametersManagerSingleton;

	// Set of Parameter Definitions assets to reconcile the unique Id for Definitions that are duplicated from each other.
	TArray<UNiagaraParameterDefinitions*> ReservedDefinitions;
};

USTRUCT()
struct FReservedParameterArray
{
	GENERATED_BODY()

public:
	UPROPERTY(Transient)
	TArray<FReservedParameter> Arr;
};

/** Manager singleton for tracking parameters that are reserved by parameter definitions assets.
 *  Implements UObject to support undo/redo transactions on the map of definitions asset ptrs to parameter names.
 */
UCLASS()
class UNiagaraReservedParametersManager : public UObject
{
	GENERATED_BODY()

public:
	// UNiagaraReservedParametersManager is lazy initialized inside FNiagaraEditorModule::GetReservedParametersByName(), so delegate this method as a friend so that it may call InitReservedParameters().
	friend UNiagaraReservedParametersManager* FNiagaraEditorModule::GetReservedParametersManager();

public:
	const TArray<FReservedParameter>* FindReservedParametersByName(const FName ParameterName) const;
	int32 GetNumReservedParametersByName(const FName ParameterName) const;
	EParameterDefinitionMatchState GetDefinitionMatchStateForParameter(const FNiagaraVariableBase& Parameter) const;
	void AddReservedParameter(const FNiagaraVariableBase& Parameter, const UNiagaraParameterDefinitions* ReservingDefinitionsAsset);
	void RemoveReservedParameter(const FNiagaraVariableBase& Parameter, const UNiagaraParameterDefinitions* ReservingDefinitionsAsset);

private:
	UPROPERTY(Transient)
	TMap<FName, FReservedParameterArray> ReservedParameters;

private:
	void InitReservedParameters();
};
