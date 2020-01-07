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
#include "ControlRig.generated.h"

class IControlRigObjectBinding;
class UScriptStruct;
struct FRigUnit;
struct FControlRigIOVariable;

DECLARE_LOG_CATEGORY_EXTERN(LogControlRig, Log, All);

/** Delegate used to optionally gather inputs before evaluating a ControlRig */
DECLARE_DELEGATE_OneParam(FPreEvaluateGatherInput, UControlRig*);
DECLARE_DELEGATE_OneParam(FPostEvaluateQueryOutput, UControlRig*);

#define DEBUG_CONTROLRIG_PROPERTYCHANGE !(UE_BUILD_SHIPPING || UE_BUILD_TEST)

/** Runs logic for mapping input data to transforms (the "Rig") */
UCLASS(Blueprintable, Abstract, editinlinenew)
class CONTROLRIG_API UControlRig : public UObject, public INodeMappingProviderInterface, public IControlRigManipulatable
{
	GENERATED_BODY()

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
	static const FName BoneNameMetaName;
	static const FName ControlNameMetaName;
	static const FName SpaceNameMetaName;
	static const FName CurveNameMetaName;
	static const FName ConstantMetaName;
	static const FName TitleColorMetaName;
	static const FName NodeColorMetaName;
	static const FName KeywordsMetaName;
	static const FName PrototypeNameMetaName;
	static const FName AnimationInputMetaName;
	static const FName AnimationOutputMetaName;
	static const FName ExpandPinByDefaultMetaName;
	static const FName DefaultArraySizeMetaName;

private:
	/** Current delta time */
	float DeltaTime;

public:
	UControlRig();

	virtual void Serialize(FArchive& Ar) override;
#if WITH_EDITOR
	virtual void PostEditUndo() override;
#endif
	
	/** Get the current delta time */
	UFUNCTION(BlueprintPure, Category = "Animation")
	float GetDeltaTime() const;

	/** Set the current delta time */
	void SetDeltaTime(float InDeltaTime);

#if WITH_EDITOR
	/** Get the category of this ControlRig (for display in menus) */
	virtual FText GetCategory() const;

	/** Get the tooltip text to display for this node (displayed in graphs and from context menus) */
	virtual FText GetTooltipText() const;
#endif

	/** UObject interface */
	virtual UWorld* GetWorld() const override;

	/** Initialize things for the ControlRig */
	virtual void Initialize(bool bInitRigUnits = true);

	/** Evaluate at Any Thread */
	virtual void Evaluate_AnyThread();

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

	/* 
	 * Query input output variables
	 *
	 * @param bInput - True if it's input. False if you want to query Output
	 * @param OutVars - Output array of variables
	 */
	void QueryIOVariables(bool bInput, TArray<FControlRigIOVariable>& OutVars) const;
	/*
	 * Return true if the PropertyName is in IO
	 *
	 * @param bInput - True if it's input. False if you want to query Output
	 * @return true if it's valid
	 */
	bool IsValidIOVariables(bool bInput, const FName& PropertyName) const;

	/*
	 * Get IO property path
	 *
	 * @param bInput - True if it's input. False if you want to query Output
	 * @param InPropertyPath - Property path to query
	 * @param OutCachedPath - output of cached path
	 * @return true if succeed
	 */
	bool GetInOutPropertyPath(bool bInput, const FName& InPropertyPath, FCachedPropertyPath& OutCachedPath);

#if WITH_EDITOR
	// get class name of rig unit that is owned by this rig
	FName GetRigClassNameFromRigUnit(const FRigUnit* InRigUnit) const;
	FRigUnit_Control* GetControlRigUnitFromName(const FName& PropertyName);
	FRigUnit* GetRigUnitFromName(const FName& PropertyName);
	
	// called after post reinstance when compilng blueprint by Sequencer
	void PostReinstanceCallback(const UControlRig* Old);


#endif // WITH_EDITOR
	// BEGIN UObject interface
	static void AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector);
	virtual void BeginDestroy() override;
	// END UObject interface

	UPROPERTY(transient)
	ERigExecutionType ExecutionType;

	/** Execute the rig unit */
	void Execute(const EControlRigState State);

	/** INodeMappingInterface implementation */
	virtual void GetMappableNodeData(TArray<FName>& OutNames, TArray<FNodeItem>& OutNodeItems) const override;

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
#if WITH_EDITOR
	virtual void SelectControl(const FName& InControlName, bool bSelect = true) override;
	virtual bool ClearControlSelection() override;
	virtual TArray<FName> CurrentControlSelection() const override;
	virtual bool IsControlSelected(const FName& InControlName)const override;
#endif
	// END IControlRigManipulatable interface

	DECLARE_EVENT_TwoParams(UControlRig, FControlRigExecuteEvent, class UControlRig*, const EControlRigState);
	FControlRigExecuteEvent& OnInitialized() { return InitializedEvent; }
	FControlRigExecuteEvent& OnExecuted() { return ExecutedEvent; }

private:
	UPROPERTY(VisibleDefaultsOnly, Category = "Hierarchy")
	FRigHierarchyContainer Hierarchy;

#if WITH_EDITORONLY_DATA
	/** The properties of source accessible <target, source local path> when source -> target
	 * For example, if you have property RigUnitA.B->RigUnitB.C, this will save as <RigUnitB.C, RigUnitA.B> */
	UPROPERTY()
	TMap<FName, FString> AllowSourceAccessProperties;

	/** Cached editor object reference by rig unit */
	TMap<FRigUnit*, UObject*> RigUnitEditorObjects;
#endif // WITH_EDITOR

	/** list of operators. */
	UPROPERTY(Transient)
	TArray<FControlRigOperator> Operators;

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
	TMap<FName, FCachedPropertyPath> InputProperties;

	UPROPERTY()
	TMap<FName, FCachedPropertyPath> OutputProperties;

private:

	/** The draw interface for the units to use */
	FControlRigDrawInterface* DrawInterface;

	/** The registry to access data source */
	UPROPERTY(transient)
	UAnimationDataSourceRegistry* DataSourceRegistry;

#if DEBUG_CONTROLRIG_PROPERTYCHANGE
	// This is to debug class size when constructed and destroyed to verify match
	// if this size changes, that implies more problem, where properties have been changed and layout has been modified
	// also it caches if destructor and property has been chagned
	// the name can be destroyed but we should make sure we have proper properties size/offset is linked
	int32 DebugClassSize;
	TArray<UScriptStruct*> Destructors;
	struct FPropertyData
	{
		int32 Offset;
		int32 Size;
		FName PropertyName;
	};
	TArray<FPropertyData> PropertyData;
	void ValidateDebugClassData();
	void CacheDebugClassData();
#endif // 	DEBUG_CONTROLRIG_PROPERTYCHANGE

	/** Copy the operators from the generated class */
	void InstantiateOperatorsFromGeneratedClass();
	
	/** Re-resolve operator property paths */
	void ResolvePropertyPaths();

	/** Broadcasts a notification whenever the controlrig is initialized. */
	FControlRigExecuteEvent InitializedEvent;

	/** Broadcasts a notification whenever the controlrig is executed / updated. */
	FControlRigExecuteEvent ExecutedEvent;

#if WITH_EDITOR
	/** Handle a Control Being Selected */
	void HandleOnControlSelected(FRigHierarchyContainer* InContainer, const FRigElementKey& InKey, bool bSelected);
#endif
	void ResolveInputOutputProperties();

	void InitializeFromCDO();

	friend class FControlRigBlueprintCompilerContext;
	friend struct FRigHierarchyRef;
	friend class UControlRigEditorLibrary;
	friend class URigUnitEditor_Base;
	friend class FControlRigEditor;
	friend class SRigCurveContainer;
	friend class SRigHierarchy;
	friend class UEngineTestControlRig;
 	friend class FControlRigEditMode;
	friend class FControlRigIOHelper;
	friend class UControlRigBlueprint;
};