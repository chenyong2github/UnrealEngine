// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Misc/Guid.h"
#include "Engine/Blueprint.h"
#include "Misc/Crc.h"
#include "ControlRigDefines.h"
#include "Rigs/RigHierarchyContainer.h"
#include "Rigs/RigCurveContainer.h"
#include "Interfaces/Interface_PreviewMeshProvider.h"
#include "ControlRigGizmoLibrary.h"
#include "RigVMCore/RigVM.h"
#include "RigVMCore/RigVMStatistics.h"
#include "RigVMModel/RigVMController.h"
#include "RigVMCompiler/RigVMCompiler.h"
#include "ControlRigHierarchyModifier.h"
#include "ControlRigValidationPass.h"
#include "Drawing/ControlRigDrawContainer.h"
#include "ControlRigBlueprint.generated.h"

class UControlRigBlueprintGeneratedClass;
class USkeletalMesh;
class UControlRigGraph;

DECLARE_EVENT_TwoParams(UControlRigBlueprint, FOnVMCompiledEvent, UBlueprint*, URigVM*);
DECLARE_EVENT_OneParam(UControlRigBlueprint, FOnRefreshEditorEvent, UControlRigBlueprint*);
DECLARE_EVENT_FourParams(UControlRigBlueprint, FOnVariableDroppedEvent, UObject*, FProperty*, const FVector2D&, const FVector2D&);
DECLARE_EVENT_OneParam(UControlRigBlueprint, FOnExternalVariablesChanged, const TArray<FRigVMExternalVariable>&);

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

#if WITH_EDITOR

	// UBlueprint interface
	virtual UClass* GetBlueprintClass() const override;
	virtual bool SupportedByDefaultBlueprintFactory() const override { return false; }
	virtual bool IsValidForBytecodeOnlyRecompile() const override { return false; }
	virtual void LoadModulesRequiredForCompilation() override;
	virtual void GetTypeActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const override;
	virtual void GetInstanceActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const override;
	virtual void SetObjectBeingDebugged(UObject* NewObject) override;
	virtual void PreSave(const class ITargetPlatform* TargetPlatform) override;
	virtual void PostLoad() override;
	virtual bool IsPostLoadThreadSafe() const override { return false; }
	virtual void PostTransacted(const FTransactionObjectEvent& TransactionEvent) override;

	virtual bool SupportsGlobalVariables() const override { return true; }
	virtual bool SupportsLocalVariables() const override { return false; }
	virtual bool SupportsFunctions() const override { return false; }
	virtual bool SupportsMacros() const override { return false; }
	virtual bool SupportsDelegates() const override { return false; }
	virtual bool SupportsEventGraphs() const override { return false; }
	virtual bool SupportsAnimLayers() const override { return false; }


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

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VM")
	FRigVMCompileSettings VMCompileSettings;

	UPROPERTY(BlueprintReadOnly, Category = "VM")
	URigVMGraph* Model;

	UPROPERTY(BlueprintReadOnly, transient, Category = "VM")
	URigVMController* Controller;

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
	static TArray<UStruct*> GetAvailableRigUnits();

	UFUNCTION(BlueprintCallable, Category = "Hierarchy")
	UControlRigHierarchyModifier* GetHierarchyModifier();

#if WITH_EDITORONLY_DATA
	UPROPERTY(EditAnywhere, config, Category = DefaultGizmo)
	TAssetPtr<UControlRigGizmoLibrary> GizmoLibrary;

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

#endif

	UPROPERTY(EditAnywhere, Category = "Influence Map")
	FRigInfluenceMapPerEvent Influences;

public:
	UPROPERTY()
	FRigHierarchyContainer HierarchyContainer;

private:

	UPROPERTY()
	FRigBoneHierarchy Hierarchy_DEPRECATED;

	UPROPERTY()
	FRigCurveContainer CurveContainer_DEPRECATED;

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
	int32 VMRecompilationBracket;

	UPROPERTY(transient)
	UControlRigHierarchyModifier* HierarchyModifier;

	FRigVMGraphModifiedEvent ModifiedEvent;
	void Notify(ERigVMGraphNotifType InNotifType, UObject* InSubject);
	void HandleModifiedEvent(ERigVMGraphNotifType InNotifType, URigVMGraph* InGraph, UObject* InSubject);

#if WITH_EDITOR

	UFUNCTION(BlueprintCallable, Category = "Control Rig Blueprint")
	void SuspendNotifications(bool bSuspendNotifs);

	FOnRefreshEditorEvent RefreshEditorEvent;
	FOnVariableDroppedEvent VariableDroppedEvent;

public:

	FOnRefreshEditorEvent& OnRefreshEditor() { return RefreshEditorEvent; }
	FOnVariableDroppedEvent& OnVariableDropped() { return VariableDroppedEvent; }

private:

#endif

	FOnVMCompiledEvent VMCompiledEvent;

	static TArray<UControlRigBlueprint*> sCurrentlyOpenedRigBlueprints;

	void CleanupBoneHierarchyDeprecated();
	void CreateMemberVariablesOnLoad();
	void PatchVariableNodesOnLoad();

	TMap<FName, int32> AddedMemberVariableMap;
	TArray<FBPVariableDescription> LastNewVariables;

public:
	void PropagatePoseFromInstanceToBP(UControlRig* InControlRig);
	void PropagatePoseFromBPToInstances();
	void PropagateHierarchyFromBPToInstances(bool bInitializeContainer = true, bool bInitializeRigs = true);
	void PropagateDrawInstructionsFromBPToInstances();
	void PropagatePropertyFromBPToInstances(FRigElementKey InRigElement, const FProperty* InProperty);
	void PropagatePropertyFromInstanceToBP(FRigElementKey InRigElement, const FProperty* InProperty, UControlRig* InInstance);

private:

	UPROPERTY()
	UControlRigValidator* Validator;

#if WITH_EDITOR

	void HandleOnElementAdded(FRigHierarchyContainer* InContainer, const FRigElementKey& InKey);
	void HandleOnElementRemoved(FRigHierarchyContainer* InContainer, const FRigElementKey& InKey);
	void HandleOnElementRenamed(FRigHierarchyContainer* InContainer, ERigElementType InElementType, const FName& InOldName, const FName& InNewName);
	void HandleOnElementReparented(FRigHierarchyContainer* InContainer, const FRigElementKey& InKey, const FName& InOldParentName, const FName& InNewParentName);
	void HandleOnElementSelected(FRigHierarchyContainer* InContainer, const FRigElementKey& InKey, bool bSelected);
#endif

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

private:

	FOnExternalVariablesChanged ExternalVariablesChangedEvent;
	void BroadcastExternalVariablesChangedEvent();

#endif

	bool bDirtyDuringLoad;

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
};
