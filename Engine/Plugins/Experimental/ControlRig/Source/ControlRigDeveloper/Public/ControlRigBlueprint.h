// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Misc/Guid.h"
#include "Engine/Blueprint.h"
#include "Misc/Crc.h"
#include "ControlRigDefines.h"
#include "Rigs/RigHierarchyContainer.h"
#include "Rigs/RigHierarchy.h"
#include "Rigs/RigHierarchyController.h"
#include "Interfaces/Interface_PreviewMeshProvider.h"
#include "ControlRigGizmoLibrary.h"
#include "RigVMCore/RigVM.h"
#include "RigVMCore/RigVMStatistics.h"
#include "RigVMModel/RigVMController.h"
#include "RigVMCompiler/RigVMCompiler.h"
#include "ControlRigValidationPass.h"
#include "Drawing/ControlRigDrawContainer.h"

#if WITH_EDITOR
#include "Kismet2/Kismet2NameValidators.h"
#include "Kismet2/CompilerResultsLog.h"
#endif

#include "ControlRigBlueprint.generated.h"

class UControlRigBlueprintGeneratedClass;
class USkeletalMesh;
class UControlRigGraph;

DECLARE_EVENT_TwoParams(UControlRigBlueprint, FOnVMCompiledEvent, UBlueprint*, URigVM*);
DECLARE_EVENT_OneParam(UControlRigBlueprint, FOnRefreshEditorEvent, UControlRigBlueprint*);
DECLARE_EVENT_FourParams(UControlRigBlueprint, FOnVariableDroppedEvent, UObject*, FProperty*, const FVector2D&, const FVector2D&);
DECLARE_EVENT_OneParam(UControlRigBlueprint, FOnExternalVariablesChanged, const TArray<FRigVMExternalVariable>&);
DECLARE_EVENT_TwoParams(UControlRigBlueprint, FOnNodeDoubleClicked, UControlRigBlueprint*, URigVMNode*);
DECLARE_EVENT_OneParam(UControlRigBlueprint, FOnGraphImported, UEdGraph*);
DECLARE_EVENT_OneParam(UControlRigBlueprint, FOnPostEditChangeChainProperty, FPropertyChangedChainEvent&);
DECLARE_EVENT_ThreeParams(UControlRigBlueprint, FOnLocalizeFunctionDialogRequested, URigVMLibraryNode*, UControlRigBlueprint*, bool);
DECLARE_EVENT_ThreeParams(UControlRigBlueprint, FOnReportCompilerMessage, EMessageSeverity::Type, UObject*, const FString&);
DECLARE_DELEGATE_RetVal_FourParams(bool, FControlRigOnBulkEditDialogRequestedDelegate, UControlRigBlueprint*, URigVMController*, URigVMLibraryNode*, ERigVMControllerBulkEditType);
DECLARE_EVENT(UControlRigBlueprint, FOnBreakpointAdded);

USTRUCT()
struct CONTROLRIGDEVELOPER_API FControlRigPublicFunctionArg
{
	GENERATED_BODY();
	
	FControlRigPublicFunctionArg()
	: Name(NAME_None)
	, CPPType(NAME_None)
	, CPPTypeObjectPath(NAME_None)
	, bIsArray(false)
	, Direction(ERigVMPinDirection::Input)
	{}

	UPROPERTY()
	FName Name;

	UPROPERTY()
	FName CPPType;

	UPROPERTY()
	FName CPPTypeObjectPath;

	UPROPERTY()
	bool bIsArray;

	UPROPERTY()
	ERigVMPinDirection Direction;

	FEdGraphPinType GetPinType() const;
};

USTRUCT()
struct CONTROLRIGDEVELOPER_API FControlRigPublicFunctionData
{
	GENERATED_BODY();

	FControlRigPublicFunctionData()
		:Name(NAME_None)
	{}

	UPROPERTY()
	FName Name;

	UPROPERTY()
	FString DisplayName;

	UPROPERTY()
	FString Category;

	UPROPERTY()
	FString Keywords;

	UPROPERTY()
	FControlRigPublicFunctionArg ReturnValue;

	UPROPERTY()
	TArray<FControlRigPublicFunctionArg> Arguments;
};

USTRUCT()
struct CONTROLRIGDEVELOPER_API FRigGraphDisplaySettings
{
	GENERATED_BODY();

	FRigGraphDisplaySettings()
		: bShowNodeRunCounts(false)
		, NodeRunLowerBound(1)
		, NodeRunLimit(64)
	{
	}

	// When enabled shows the node counts both in the graph view as
	// we as in the execution stack window.
	// The number on each node represents how often the node has been run.
	// Keep in mind when looking at nodes in a function the count
	// represents the sum of all counts for each node based on all
	// references of the function currently running.
	UPROPERTY(EditAnywhere, Category = "Graph Display Settings")
	bool bShowNodeRunCounts;

	// A lower limit for counts for nodes used for debugging.
	// Any node lower than this count won't show the run count.
	UPROPERTY(EditAnywhere, Category = "Graph Display Settings")
	int32 NodeRunLowerBound;

	// A upper limit for counts for nodes used for debugging.
	// If a node reaches this count a warning will be issued for the
	// node and displayed both in the execution stack as well as in the
	// graph. Setting this to <= 1 disables the warning.
	// Note: The count limit doesn't apply to functions / collapse nodes.
	UPROPERTY(EditAnywhere, Category = "Graph Display Settings")
	int32 NodeRunLimit;
};

UCLASS(BlueprintType, meta=(IgnoreClassThumbnail))
class CONTROLRIGDEVELOPER_API UControlRigBlueprint : public UBlueprint, public IInterface_PreviewMeshProvider
{
	GENERATED_UCLASS_BODY()

public:
	UControlRigBlueprint();

	void InitializeModelIfRequired(bool bRecompileVM = true);

	/** Get the (full) generated class for this control rig blueprint */
	UControlRigBlueprintGeneratedClass* GetControlRigBlueprintGeneratedClass() const;

	/** Get the (skeleton) generated class for this control rig blueprint */
	UControlRigBlueprintGeneratedClass* GetControlRigBlueprintSkeletonClass() const;

	virtual void Serialize(FArchive& Ar) override;

#if WITH_EDITOR

	// UBlueprint interface
	virtual UClass* GetBlueprintClass() const override;
	virtual UClass* RegenerateClass(UClass* ClassToRegenerate, UObject* PreviousCDO) override;
	virtual bool SupportedByDefaultBlueprintFactory() const override { return false; }
	virtual bool IsValidForBytecodeOnlyRecompile() const override { return false; }
	virtual void LoadModulesRequiredForCompilation() override;
	virtual void GetTypeActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const override;
	virtual void GetInstanceActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const override;
	virtual void SetObjectBeingDebugged(UObject* NewObject) override;
	PRAGMA_DISABLE_DEPRECATION_WARNINGS // Suppress compiler warning on override of deprecated function
	UE_DEPRECATED(5.0, "Use version that takes FObjectPreSaveContext instead.")
	virtual void PreSave(const class ITargetPlatform* TargetPlatform) override;
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
	virtual void PreSave(FObjectPreSaveContext ObjectSaveContext) override;
	virtual void PostLoad() override;
	virtual bool IsPostLoadThreadSafe() const override { return false; }
	virtual void PostTransacted(const FTransactionObjectEvent& TransactionEvent) override;
	virtual void ReplaceDeprecatedNodes() override;

	virtual bool SupportsGlobalVariables() const override { return true; }
	virtual bool SupportsLocalVariables() const override { return false; }
	virtual bool SupportsFunctions() const override { return true; }
	virtual bool SupportsMacros() const override { return false; }
	virtual bool SupportsDelegates() const override { return false; }
	virtual bool SupportsEventGraphs() const override { return false; }
	virtual bool SupportsAnimLayers() const override { return false; }
	virtual bool ExportGraphToText(UEdGraph* InEdGraph, FString& OutText) override;
	virtual bool TryImportGraphFromText(const FString& InClipboardText, UEdGraph** OutGraphPtr = nullptr) override;
	virtual bool CanImportGraphFromText(const FString& InClipboardText) override;

	// UObject interface
	virtual void PostEditChangeChainProperty(struct FPropertyChangedChainEvent& PropertyChangedEvent) override;

#endif	// #if WITH_EDITOR

	virtual bool ShouldBeMarkedDirtyUponTransaction() const override { return false; }

	/** IInterface_PreviewMeshProvider interface */
	UFUNCTION(BlueprintCallable, Category = "Control Rig Blueprint")
	virtual void SetPreviewMesh(USkeletalMesh* PreviewMesh, bool bMarkAsDirty = true) override;
	
	UFUNCTION(BlueprintCallable, Category = "Control Rig Blueprint")
	virtual USkeletalMesh* GetPreviewMesh() const override;

	UFUNCTION(BlueprintCallable, Category = "Control Rig Blueprint")
	void RecompileVM();

	UFUNCTION(BlueprintCallable, Category = "Control Rig Blueprint")
	void RecompileVMIfRequired();
	
	UFUNCTION(BlueprintCallable, Category = "Control Rig Blueprint")
	void RequestAutoVMRecompilation();

	void IncrementVMRecompileBracket();
	void DecrementVMRecompileBracket();

	UFUNCTION(BlueprintCallable, Category = "Control Rig Blueprint")
	void RequestControlRigInit();

	UFUNCTION(BlueprintCallable, Category = "Control Rig Blueprint")
	URigVMGraph* GetModel(const UEdGraph* InEdGraph = nullptr) const;
	URigVMGraph* GetModel(const FString& InNodePath) const;

	UFUNCTION(BlueprintCallable, Category = "Control Rig Blueprint")
	TArray<URigVMGraph*> GetAllModels() const;

	UFUNCTION(BlueprintCallable, Category = "Control Rig Blueprint")
	URigVMFunctionLibrary* GetLocalFunctionLibrary() const;

	UFUNCTION(BlueprintCallable, Category = "Control Rig Blueprint")
	URigVMController* GetController(URigVMGraph* InGraph = nullptr) const;

	UFUNCTION(BlueprintCallable, Category = "Control Rig Blueprint")
	URigVMController* GetOrCreateController(URigVMGraph* InGraph = nullptr);

	URigVMController* GetController(const UEdGraph* InEdGraph) const;
	URigVMController* GetOrCreateController(const UEdGraph* InGraph);

	URigVMGraph* GetTemplateModel();
	URigVMController* GetTemplateController();

#if WITH_EDITOR
	UEdGraph* GetEdGraph(URigVMGraph* InModel = nullptr) const;
	UEdGraph* GetEdGraph(const FString& InNodePath) const;
#endif

#if WITH_EDITORONLY_DATA
	UPROPERTY()
	UControlRigGraph* FunctionLibraryEdGraph;
#endif

	bool IsFunctionPublic(const FName& InFunctionName) const;
	void MarkFunctionPublic(const FName& InFunctionName, bool bIsPublic = true);

	// Returns a list of dependencies of this blueprint.
	// Dependencies are blueprints that contain functions used in this blueprint
	TArray<UControlRigBlueprint*> GetDependencies(bool bRecursive = false) const;

	// Returns a list of dependents as unresolved soft object pointers.
	// A dependent is a blueprint which uses a function defined in this blueprint.
	// This function is not recursive, since it avoids opening the asset.
	// Use GetDependentBlueprints as an alternative.
	TArray<FAssetData> GetDependentAssets() const;

	// Returns a list of dependents as resolved blueprints.
	// A dependent is a blueprint which uses a function defined in this blueprint.
	// If bOnlyLoaded is false, this function loads the dependent assets and can introduce a large cost
	// depending on the size / count of assets in the project.
	TArray<UControlRigBlueprint*> GetDependentBlueprints(bool bRecursive = false, bool bOnlyLoaded = false) const;

	UPROPERTY(EditAnywhere, Category = "User Interface")
	FRigGraphDisplaySettings RigGraphDisplaySettings;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VM")
	FRigVMCompileSettings VMCompileSettings;

protected:

	UPROPERTY(BlueprintReadOnly, Category = "VM")
	URigVMGraph* Model;

	UPROPERTY(BlueprintReadOnly, Category = "VM")
	URigVMFunctionLibrary* FunctionLibrary;

	/** Asset searchable information about exposed public functions on this rig */
	UPROPERTY(AssetRegistrySearchable)
	TArray<FControlRigPublicFunctionData> PublicFunctions;

	UPROPERTY(BlueprintReadOnly, transient, Category = "VM")
	TMap<URigVMGraph*, URigVMController*> Controllers;

#if WITH_EDITORONLY_DATA

	UPROPERTY(transient)
	URigVMGraph* TemplateModel;

	UPROPERTY(transient)
	URigVMController* TemplateController;

#endif

public:

	UPROPERTY(transient)
	TMap<FString, FRigVMOperand> PinToOperandMap;

	bool bSuspendModelNotificationsForSelf;
	bool bSuspendModelNotificationsForOthers;
	bool bSuspendAllNotifications;

	void PopulateModelFromGraphForBackwardsCompatibility(UControlRigGraph* InGraph);
	void SetupPinRedirectorsForBackwardsCompatibility();
	void RebuildGraphFromModel();

	FRigVMGraphModifiedEvent& OnModified();
	FOnVMCompiledEvent& OnVMCompiled();

	UFUNCTION(BlueprintCallable, Category = "VM")
	static TArray<UControlRigBlueprint*> GetCurrentlyOpenRigBlueprints();

	UFUNCTION(BlueprintCallable, Category = "VM")
	UClass* GetControlRigClass();

	UFUNCTION(BlueprintCallable, Category = "VM")
	UControlRig* CreateControlRig();

	UFUNCTION(BlueprintCallable, Category = "VM")
	static TArray<UStruct*> GetAvailableRigUnits();

#if WITH_EDITOR
	UFUNCTION(BlueprintCallable, Category = "Variables")
	FName AddMemberVariable(const FName& InName, const FString& InCPPType, bool bIsPublic = false, bool bIsReadOnly = false, FString InDefaultValue = TEXT(""));
#endif

#if WITH_EDITORONLY_DATA
	UPROPERTY(EditAnywhere, config, Category = DefaultGizmo)
	TSoftObjectPtr<UControlRigGizmoLibrary> GizmoLibrary;
#endif

	UPROPERTY(transient, VisibleAnywhere, Category = "VM", meta = (DisplayName = "VM Statistics", DisplayAfter = "VMCompileSettings"))
	FRigVMStatistics Statistics;

	UPROPERTY(EditAnywhere, Category = "Drawing")
	FControlRigDrawContainer DrawContainer;

#if WITH_EDITOR
	/** Remove a transient / temporary control used to interact with a pin */
	FName AddTransientControl(URigVMPin* InPin);

	/** Remove a transient / temporary control used to interact with a pin */
	FName RemoveTransientControl(URigVMPin* InPin);

	/** Remove a transient / temporary control used to interact with a bone */
	FName AddTransientControl(const FRigElementKey& InElement);

	/** Remove a transient / temporary control used to interact with a bone */
	FName RemoveTransientControl(const FRigElementKey& InElement);

	/** Removes all  transient / temporary control used to interact with pins */
	void ClearTransientControls();

	/** update the value of the transient / temporary control to the current value of the rig element*/
	void SetTransientControlValue(const FRigElementKey& InElement);

#endif

	UPROPERTY(EditAnywhere, Category = "Influence Map")
	FRigInfluenceMapPerEvent Influences;

public:

	UPROPERTY()
	FRigHierarchyContainer HierarchyContainer_DEPRECATED;

	UPROPERTY(BlueprintReadOnly, Category = "Hierarchy")
	URigHierarchy* Hierarchy;

	UFUNCTION(BlueprintCallable, Category = "Hierarchy")
	URigHierarchyController* GetHierarchyController() { return HierarchyController; }

private:

	/** Whether or not this rig has an Inversion Event */
	UPROPERTY(AssetRegistrySearchable)
	bool bSupportsInversion;

	/** Whether or not this rig has Controls on It */
	UPROPERTY(AssetRegistrySearchable)
	bool bSupportsControls;

	/** The default skeletal mesh to use when previewing this asset */
	UPROPERTY(AssetRegistrySearchable)
	TSoftObjectPtr<USkeletalMesh> PreviewSkeletalMesh;

	/** The skeleton from import into a hierarchy */
	UPROPERTY(DuplicateTransient, AssetRegistrySearchable)
	TSoftObjectPtr<UObject> SourceHierarchyImport;

	/** The skeleton from import into a curve */
	UPROPERTY(DuplicateTransient, AssetRegistrySearchable)
	TSoftObjectPtr<UObject> SourceCurveImport;

	/** The event names this control rig blueprint contains */
	UPROPERTY(AssetRegistrySearchable)
	TArray<FName> SupportedEventNames;

	/** If set to true, this control rig has animatable controls */
	UPROPERTY(AssetRegistrySearchable)
	bool bExposesAnimatableControls;

	UPROPERTY(transient)
	bool bAutoRecompileVM;

	UPROPERTY(transient)
	bool bVMRecompilationRequired;

	UPROPERTY(transient)
	bool bIsCompiling;

	UPROPERTY(transient)
	int32 VMRecompilationBracket;

	UPROPERTY(transient)
	URigHierarchyController* HierarchyController;

	FRigVMGraphModifiedEvent ModifiedEvent;
	void Notify(ERigVMGraphNotifType InNotifType, UObject* InSubject);
	void HandleModifiedEvent(ERigVMGraphNotifType InNotifType, URigVMGraph* InGraph, UObject* InSubject);

#if WITH_EDITOR

	UFUNCTION(BlueprintCallable, Category = "Control Rig Blueprint")
	void SuspendNotifications(bool bSuspendNotifs);

	FOnRefreshEditorEvent RefreshEditorEvent;
	FOnVariableDroppedEvent VariableDroppedEvent;
	FOnBreakpointAdded BreakpointAddedEvent;

public:

	void BroadcastRefreshEditor() { return RefreshEditorEvent.Broadcast(this); }
	FOnRefreshEditorEvent& OnRefreshEditor() { return RefreshEditorEvent; }
	FOnVariableDroppedEvent& OnVariableDropped() { return VariableDroppedEvent; }
	FOnBreakpointAdded& OnBreakpointAdded() { return BreakpointAddedEvent; }

private:

#endif

	FOnVMCompiledEvent VMCompiledEvent;

	static TArray<UControlRigBlueprint*> sCurrentlyOpenedRigBlueprints;

	void CreateMemberVariablesOnLoad();
#if WITH_EDITOR
	static FName FindCRMemberVariableUniqueName(TSharedPtr<FKismetNameValidator> InNameValidator, const FString& InBaseName);
	static int32 AddCRMemberVariable(UControlRigBlueprint* InBlueprint, const FName& InVarName, FEdGraphPinType InVarType, bool bIsPublic, bool bIsReadOnly, FString InDefaultValue);
	FName AddCRMemberVariableFromExternal(FRigVMExternalVariable InVariableToCreate, FString InDefaultValue = FString());
#endif
	void PatchVariableNodesOnLoad();

	TMap<FName, int32> AddedMemberVariableMap;
	TArray<FBPVariableDescription> LastNewVariables;

public:
	void PropagatePoseFromInstanceToBP(UControlRig* InControlRig);
	void PropagatePoseFromBPToInstances();
	void PropagateHierarchyFromBPToInstances();
	void PropagateDrawInstructionsFromBPToInstances();
	void PropagatePropertyFromBPToInstances(FRigElementKey InRigElement, const FProperty* InProperty);
	void PropagatePropertyFromInstanceToBP(FRigElementKey InRigElement, const FProperty* InProperty, UControlRig* InInstance);

private:

	UPROPERTY()
	UControlRigValidator* Validator;

	void HandleHierarchyModified(ERigHierarchyNotification InNotification, URigHierarchy* InHierarchy, const FRigBaseElement* InElement);

	// Class used to temporarily cache all 
	// current control values and reapply them
	// on destruction
	class CONTROLRIGDEVELOPER_API FControlValueScope
	{
	public: 
		FControlValueScope(UControlRigBlueprint* InBlueprint);
		~FControlValueScope();

	private:

		UControlRigBlueprint* Blueprint;
		TMap<FName, FRigControlValue> ControlValues;
	};

#if WITH_EDITOR

public:

	FOnExternalVariablesChanged& OnExternalVariablesChanged() { return ExternalVariablesChangedEvent; }

	virtual void OnPreVariableChange(UObject* InObject);
	virtual void OnPostVariableChange(UBlueprint* InBlueprint);
	virtual void OnVariableAdded(const FName& InVarName);
	virtual void OnVariableRemoved(const FName& InVarName);
	virtual void OnVariableRenamed(const FName& InOldVarName, const FName& InNewVarName);
	virtual void OnVariableTypeChanged(const FName& InVarName, FEdGraphPinType InOldPinType, FEdGraphPinType InNewPinType);

	FOnNodeDoubleClicked& OnNodeDoubleClicked() { return NodeDoubleClickedEvent; }
	void BroadcastNodeDoubleClicked(URigVMNode* InNode);

	FOnGraphImported& OnGraphImported() { return GraphImportedEvent; }
	void BroadcastGraphImported(UEdGraph* InGraph);

	FOnPostEditChangeChainProperty& OnPostEditChangeChainProperty() { return PostEditChangeChainPropertyEvent; }
	void BroadcastPostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedChainEvent);

	FOnLocalizeFunctionDialogRequested& OnRequestLocalizeFunctionDialog() { return RequestLocalizeFunctionDialog; }
	void BroadcastRequestLocalizeFunctionDialog(URigVMLibraryNode* InFunction, bool bForce = false);

	FControlRigOnBulkEditDialogRequestedDelegate& OnRequestBulkEditDialog() { return RequestBulkEditDialog; }

	FOnReportCompilerMessage& OnReportCompilerMessage() { return ReportCompilerMessageEvent; }
	void BroadCastReportCompilerMessage(EMessageSeverity::Type InSeverity, UObject* InSubject, const FString& InMessage);

private:

	FOnExternalVariablesChanged ExternalVariablesChangedEvent;
	void BroadcastExternalVariablesChangedEvent();
	FCompilerResultsLog CompileLog;

	FOnNodeDoubleClicked NodeDoubleClickedEvent;
	FOnGraphImported GraphImportedEvent;
	FOnPostEditChangeChainProperty PostEditChangeChainPropertyEvent;
	FOnLocalizeFunctionDialogRequested RequestLocalizeFunctionDialog;
	FOnReportCompilerMessage ReportCompilerMessageEvent;
	FControlRigOnBulkEditDialogRequestedDelegate RequestBulkEditDialog;

#endif

	void CreateEdGraphForCollapseNodeIfNeeded(URigVMCollapseNode* InNode, bool bForce = false);
	bool RemoveEdGraphForCollapseNode(URigVMCollapseNode* InNode, bool bNotify = false);
	void HandleReportFromCompiler(EMessageSeverity::Type InSeverity, UObject* InSubject, const FString& InMessage);

#if WITH_EDITOR
private:
	bool bCompileInDebugMode;
	
	TArray<URigVMNode*> RigVMBreakpointNodes;

public:

	/** Sets the execution mode. In Release mode the rig will ignore all breakpoints. */
	FORCEINLINE void SetDebugMode(const bool bValue) { bCompileInDebugMode = bValue; }

	/** Removes all the breakpoints from the blueprint and the VM */
	void ClearBreakpoints();

	/** Adds a breakpoint to all loaded blueprints which use the node indicated by InBreakpointNodePath 
	  * If the node is inside a public function, it will add a breakpoint to all blueprints calling this function. */
	bool AddBreakpoint(const FString& InBreakpointNodePath);

	/** Adds a breakpoint to all loaded blueprints which use the InBreakpointNode. 
	  * If LibraryNode is not null, it indicates that the library uses the InBreapointNode, and the function will add
	  * breakpoints to any other loaded blueprint that references this library. */
	bool AddBreakpoint(URigVMNode* InBreakpointNode, URigVMLibraryNode* LibraryNode = nullptr);

	/** Adds a breakpoint to the first instruction of each callpath related to the InBreakpointNode */
	bool AddBreakpointToControlRig(URigVMNode* InBreakpointNode);

	/** Removes the given breakpoint from all the loaded blueprints that use this node, and recomputes all breakpoints
	  * in the VM  */
	bool RemoveBreakpoint(const FString& InBreakpointNodePath);
	bool RemoveBreakpoint(URigVMNode* InBreakpointNode);

	/** Recomputes the instruction breakpoints given the node breakpoints in the blueprint */
	void RefreshControlRigBreakpoints();

#endif

private:
	bool bDirtyDuringLoad;
	bool bErrorsDuringCompilation;

	friend class FControlRigBlueprintCompilerContext;
	friend class SRigHierarchy;
	friend class SRigCurveContainer;
	friend class FControlRigEditor;
	friend class UEngineTestControlRig;
	friend class FControlRigEditMode;
	friend class FControlRigBlueprintActions;
	friend class FControlRigDrawContainerDetails;
	friend class UDefaultControlRigManipulationLayer;
	friend struct FRigValidationTabSummoner;
	friend class UAnimGraphNode_ControlRig;
	friend class UControlRigThumbnailRenderer;
	friend class FControlRigGraphDetails;
	friend class FControlRigEditorModule;
};
