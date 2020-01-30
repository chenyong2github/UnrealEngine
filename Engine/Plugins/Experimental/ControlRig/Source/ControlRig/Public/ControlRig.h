// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "UObject/Object.h"
#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/ScriptMacros.h"
#include "Engine/EngineBaseTypes.h"
#include "Templates/SubclassOf.h"
#include "ControlRigDefines.h"
#include "Rigs/RigHierarchyContainer.h"
#include "Units/RigUnitContext.h"
#include "Animation/NodeMappingProviderInterface.h"
#include "Units/RigUnit.h"
#include "Units/Control/RigUnit_Control.h"
#include "Manipulatable/IControlRigManipulatable.h"
#include "RigVMCore/RigVM.h"

#if WITH_EDITOR
#include "RigVMModel/RigVMPin.h"
#endif

#if WITH_EDITOR
#include "AnimPreviewInstance.h"
#endif 

#include "ControlRig.generated.h"

class IControlRigObjectBinding;
class UScriptStruct;
struct FRigUnit;
struct FRigControl;

DECLARE_LOG_CATEGORY_EXTERN(LogControlRig, Log, All);

/** Runs logic for mapping input data to transforms (the "Rig") */
UCLASS(Blueprintable, Abstract, editinlinenew)
class CONTROLRIG_API UControlRig : public UObject, public INodeMappingProviderInterface, public IControlRigManipulatable
{
	GENERATED_UCLASS_BODY()

	friend class UControlRigComponent;
	friend class SControlRigStackView;

public:
	static const FName DeprecatedMetaName;
	static const FName InputMetaName;
	static const FName OutputMetaName;
	static const FName AbstractMetaName;
	static const FName CategoryMetaName;
	static const FName DisplayNameMetaName;
	static const FName MenuDescSuffixMetaName;
	static const FName ShowVariableNameInTitleMetaName;
	static const FName CustomWidgetMetaName;
	static const FName ConstantMetaName;
	static const FName TitleColorMetaName;
	static const FName NodeColorMetaName;
	static const FName KeywordsMetaName;
	static const FName PrototypeNameMetaName;
	static const FName ExpandPinByDefaultMetaName;
	static const FName DefaultArraySizeMetaName;

	static const FName OwnerComponent;


private:
	/** Current delta time */
	float DeltaTime;

public:
	UControlRig();

	virtual void Serialize(FArchive& Ar) override;
#if WITH_EDITOR
	virtual void PostEditUndo() override;
#endif
	
	/** Set the current delta time */
	void SetDeltaTime(float InDeltaTime);

#if WITH_EDITOR
	/** Get the category of this ControlRig (for display in menus) */
	virtual FText GetCategory() const;

	/** Get the tooltip text to display for this node (displayed in graphs and from context menus) */
	virtual FText GetToolTipText() const;
#endif

	/** UObject interface */
	virtual UWorld* GetWorld() const override;

	/** Initialize things for the ControlRig */
	virtual void Initialize(bool bInitRigUnits = true);

	/** Evaluate at Any Thread */
	virtual void Evaluate_AnyThread();

	/** input output handling */
	FORCEINLINE const TArray<FRigVMParameter>& GetParameters() const
	{
		return VM->GetParameters();
	}
	template<class T>
	FORCEINLINE T GetParameterValue(const FName& InParameterName)
	{
		return VM->GetParameterValue<T>(InParameterName);
	}
	template<class T>
	FORCEINLINE void SetParameterValue(const FName& InParameterName, const T& InValue)
	{
		VM->SetParameterValue<T>(InParameterName, InValue);
	}

	/** Setup bindings to a runtime object (or clear by passing in nullptr). */
	virtual void SetObjectBinding(TSharedPtr<IControlRigObjectBinding> InObjectBinding) override
	{
		ObjectBinding = InObjectBinding;
	}

	/** Get bindings to a runtime object */
	virtual TSharedPtr<IControlRigObjectBinding> GetObjectBinding() const override
	{
		return ObjectBinding;
	}
    /** Get OurName*/
	virtual FString GetName() const override
	{
		FString ObjectName = (GetClass()->GetName());
		ObjectName.RemoveFromEnd(TEXT("_C"));
		return ObjectName;
	}
	FRigHierarchyContainer* GetHierarchy()
	{
		return &Hierarchy;
	}

	FRigBoneHierarchy& GetBoneHierarchy()
	{
		return Hierarchy.BoneHierarchy;
	}

	FRigSpaceHierarchy& GetSpaceHierarchy()
	{
		return Hierarchy.SpaceHierarchy;
	}

	FRigControlHierarchy& GetControlHierarchy()
	{
		return Hierarchy.ControlHierarchy;
	}

	FRigCurveContainer& GetCurveContainer()
	{
		return Hierarchy.CurveContainer;
	}

	/** Evaluate another animation ControlRig */
	FTransform GetGlobalTransform(const FName& BoneName) const;

	/** Evaluate another animation ControlRig */
	void SetGlobalTransform(const FName& BoneName, const FTransform& InTransform, bool bPropagateTransform = true) ;

	/** Evaluate another animation ControlRig */
	FTransform GetGlobalTransform(const int32 BoneIndex) const;

	/** Evaluate another animation ControlRig */
	void SetGlobalTransform(const int32 BoneIndex, const FTransform& InTransform, bool bPropagateTransform = true) ;

	/** Evaluate another animation ControlRig */
	float GetCurveValue(const FName& CurveName) const;

	/** Evaluate another animation ControlRig */
	void SetCurveValue(const FName& CurveName, const float CurveValue);

	/** Evaluate another animation ControlRig */
	float GetCurveValue(const int32 CurveIndex) const;

	/** Evaluate another animation ControlRig */
	void SetCurveValue(const int32 CurveIndex, const float CurveValue);

#if WITH_EDITOR

	// called after post reinstance when compilng blueprint by Sequencer
	void PostReinstanceCallback(const UControlRig* Old);


#endif // WITH_EDITOR
	// BEGIN UObject interface
	static void AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector);
	virtual void BeginDestroy() override;
	// END UObject interface

	UPROPERTY(transient)
	ERigExecutionType ExecutionType;

	/** Execute */
	void Execute(const EControlRigState State);

	/** ExecuteUnits */
	virtual void ExecuteUnits(FRigUnitContext& InOutContext);

	/** Requests to perform an init during the next execution */
	void RequestInit() { bRequiresInitExecution = true;  }

	URigVM* GetVM();

	/** INodeMappingInterface implementation */
	virtual void GetMappableNodeData(TArray<FName>& OutNames, TArray<FNodeItem>& OutNodeItems) const override;

	/** Data Source Registry Getter */
	UAnimationDataSourceRegistry* GetDataSourceRegistry() { return DataSourceRegistry; }

	// BEGIN IControlRigManipulatable interface
	virtual const TArray<FRigSpace>& AvailableSpaces() const override;
	virtual FRigSpace* FindSpace(const FName& InSpaceName) override;
	virtual FTransform GetSpaceGlobalTransform(const FName& InSpaceName) override;
	virtual bool SetSpaceGlobalTransform(const FName& InSpaceName, const FTransform& InTransform) override;
	virtual const TArray<FRigControl>& AvailableControls() const override;
	virtual FRigControl* FindControl(const FName& InControlName) override;
	virtual FTransform GetControlGlobalTransform(const FName& InControlName) const override;
	virtual FRigControlValue GetControlValueFromGlobalTransform(const FName& InControlName, const FTransform& InGlobalTransform) override;
	virtual bool SetControlSpace(const FName& InControlName, const FName& InSpaceName) override;
	virtual UControlRigGizmoLibrary* GetGizmoLibrary() const override;
	virtual void CreateRigControlsForCurveContainer() override;


#if WITH_EDITOR
	virtual void SelectControl(const FName& InControlName, bool bSelect = true) override;
	virtual bool ClearControlSelection() override;
	virtual TArray<FName> CurrentControlSelection() const override;
	virtual bool IsControlSelected(const FName& InControlName)const override;
#endif
	// END IControlRigManipulatable interface

	// Not in IControlRigManipulatable *, but maybe should
	bool IsCurveControl(const FRigControl* InRigControl) const;

	DECLARE_EVENT_TwoParams(UControlRig, FControlRigExecuteEvent, class UControlRig*, const EControlRigState);
	FControlRigExecuteEvent& OnInitialized_AnyThread() { return InitializedEvent; }
	FControlRigExecuteEvent& OnExecuted_AnyThread() { return ExecutedEvent; }

private:

	UPROPERTY(VisibleAnywhere, Category = "VM")
	URigVM* VM;

	UPROPERTY(VisibleDefaultsOnly, Category = "Hierarchy")
	FRigHierarchyContainer Hierarchy;

	UPROPERTY()
	TAssetPtr<UControlRigGizmoLibrary> GizmoLibrary;

	/** Runtime object binding */
	TSharedPtr<IControlRigObjectBinding> ObjectBinding;

#if WITH_EDITOR
	FControlRigLog* ControlRigLog;
	bool bEnableControlRigLogging;
#endif

	// you either go Input or Output, currently if you put it in both place, Output will override
	UPROPERTY()
	TMap<FName, FCachedPropertyPath> InputProperties_DEPRECATED;

	UPROPERTY()
	TMap<FName, FCachedPropertyPath> OutputProperties_DEPRECATED;

private:
	// Controls for the container
	void HandleOnControlModified(IControlRigManipulatable* Subject, const FRigControl& Control, EControlRigSetKey InSetKey);


private:

	UPROPERTY()
	FControlRigDrawContainer DrawContainer;

	/** The draw interface for the units to use */
	FControlRigDrawInterface* DrawInterface;

	/** The registry to access data source */
	UPROPERTY(transient)
	UAnimationDataSourceRegistry* DataSourceRegistry;

	/** Copy the VM from the default object */
	void InstantiateVMFromCDO();
	
	/** Broadcasts a notification whenever the controlrig is initialized. */
	FControlRigExecuteEvent InitializedEvent;

	/** Broadcasts a notification whenever the controlrig is executed / updated. */
	FControlRigExecuteEvent ExecutedEvent;

#if WITH_EDITOR
	/** Handle a Control Being Selected */
	void HandleOnControlSelected(FRigHierarchyContainer* InContainer, const FRigElementKey& InKey, bool bSelected);

	/** Update the available controls within the control rig editor */
	void UpdateAvailableControls();

	/** Remove a transient / temporary control used to interact with a pin */
	FName AddTransientControl(URigVMPin* InPin, FName InSpaceName = NAME_None);

	/** Sets the value of a transient control based on a pin */
	bool SetTransientControlValue(URigVMPin* InPin);

	/** Remove a transient / temporary control used to interact with a pin */
	FName RemoveTransientControl(URigVMPin* InPin);

	FName AddTransientControl(const FRigElementKey& InElement);

	/** Sets the value of a transient control based on a bone */
	bool SetTransientControlValue(const FRigElementKey& InElement);

	/** Remove a transient / temporary control used to interact with a bone */
	FName RemoveTransientControl(const FRigElementKey& InElement);

	/** Removes all  transient / temporary control used to interact with pins */
	void ClearTransientControls();

	TArray<FRigControl> AvailableControlsOverride;
	TArray<FRigControl> TransientControls;
	UAnimPreviewInstance* PreviewInstance;

#endif

	void InitializeFromCDO();

	static FName GetNameForTransientControl(const FRigElementKey& InElement);

	bool bRequiresInitExecution;

	friend class FControlRigBlueprintCompilerContext;
	friend struct FRigHierarchyRef;
	friend class FControlRigEditor;
	friend class SRigCurveContainer;
	friend class SRigHierarchy;
	friend class UEngineTestControlRig;
 	friend class FControlRigEditMode;
	friend class FControlRigIOHelper;
	friend class UControlRigBlueprint;
	friend class UControlRigBlueprintGeneratedClass;
};