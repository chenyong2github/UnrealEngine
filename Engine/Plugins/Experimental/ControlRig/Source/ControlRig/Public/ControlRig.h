// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "UObject/Object.h"
#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/ScriptMacros.h"
#include "Engine/EngineBaseTypes.h"
#include "Templates/SubclassOf.h"
#include "ControlRigDefines.h"
#include "ControlRigGizmoLibrary.h"
#include "Rigs/RigHierarchy.h"
#include "Units/RigUnitContext.h"
#include "Animation/NodeMappingProviderInterface.h"
#include "Units/RigUnit.h"
#include "Units/Control/RigUnit_Control.h"
#include "RigVMCore/RigVM.h"
#include "Components/SceneComponent.h"
#include "Engine/AssetUserData.h"
#include "Interfaces/Interface_AssetUserData.h"

#if WITH_EDITOR
#include "RigVMModel/RigVMPin.h"
#endif

#if WITH_EDITOR
#include "AnimPreviewInstance.h"
#endif 

#include "ControlRig.generated.h"

class IControlRigObjectBinding;
class UScriptStruct;
class USkeletalMesh;

struct FReferenceSkeleton;
struct FRigUnit;
struct FRigControl;

CONTROLRIG_API DECLARE_LOG_CATEGORY_EXTERN(LogControlRig, Log, All);

/** Runs logic for mapping input data to transforms (the "Rig") */
UCLASS(Blueprintable, Abstract, editinlinenew)
class CONTROLRIG_API UControlRig : public UObject, public INodeMappingProviderInterface, public IInterface_AssetUserData
{
	GENERATED_UCLASS_BODY()

	friend class UControlRigComponent;
	friend class SControlRigStackView;

public:

	/** Bindable event for external objects to contribute to / filter a control value */
	DECLARE_EVENT_ThreeParams(UControlRig, FFilterControlEvent, UControlRig*, FRigControlElement*, FRigControlValue&);

	/** Bindable event for external objects to be notified of Control changes */
	DECLARE_EVENT_ThreeParams(UControlRig, FControlModifiedEvent, UControlRig*, FRigControlElement*, const FRigControlModifiedContext&);

	/** Bindable event for external objects to be notified that a Control is Selected */
	DECLARE_EVENT_ThreeParams(UControlRig, FControlSelectedEvent, UControlRig*, FRigControlElement*, bool);

	static const FName OwnerComponent;

private:
	/** Current delta time */
	float DeltaTime;

	/** Current delta time */
	float AbsoluteTime;

	/** Current delta time */
	float FramesPerSecond;

	/** true if the rig itself should increase the AbsoluteTime */
	bool bAccumulateTime;

public:
	virtual void Serialize(FArchive& Ar) override;
	virtual void PostLoad() override;
#if WITH_EDITOR
	virtual void PostEditUndo() override;
#endif
	
	/** Set the current delta time */
	UFUNCTION(BlueprintCallable, Category="Control Rig")
	void SetDeltaTime(float InDeltaTime);

	/** Set the current absolute time */
	UFUNCTION(BlueprintCallable, Category = "Control Rig")
	void SetAbsoluteTime(float InAbsoluteTime, bool InSetDeltaTimeZero = false);

	/** Set the current absolute and delta times */
	UFUNCTION(BlueprintCallable, Category = "Control Rig")
	void SetAbsoluteAndDeltaTime(float InAbsoluteTime, float InDeltaTime);

	/** Set the current fps */
	UFUNCTION(BlueprintCallable, Category = "Control Rig")
	void SetFramesPerSecond(float InFramesPerSecond);

	/** Returns the current frames per second (this may change over time) */
	UFUNCTION(BlueprintPure, Category = "Control Rig")
	float GetCurrentFramesPerSecond() const;

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

	/** Returns the member properties as an external variable array */
	TArray<FRigVMExternalVariable> GetExternalVariables() const;

	/** Returns the public member properties as an external variable array */
	TArray<FRigVMExternalVariable> GetPublicVariables() const;

	/** Returns a public variable given its name */
	FRigVMExternalVariable GetPublicVariableByName(const FName& InVariableName) const;

	/** Returns the names of variables accessible in scripting */
	UFUNCTION(BlueprintPure, Category = "Control Rig", meta=(DisplayName="Get Variables"))
	TArray<FName> GetScriptAccessibleVariables() const;

	/** Returns the type of a given variable */
	UFUNCTION(BlueprintPure, Category = "Control Rig")
	FName GetVariableType(const FName& InVariableName) const;

	/** Returns the value of a given variable as a string */
	UFUNCTION(BlueprintPure, Category = "Control Rig")
	FString GetVariableAsString(const FName& InVariableName) const;

	/** Returns the value of a given variable as a string */
	UFUNCTION(BlueprintCallable, Category = "Control Rig")
	bool SetVariableFromString(const FName& InVariableName, const FString& InValue);

	template<class T>
	FORCEINLINE T GetPublicVariableValue(const FName& InVariableName)
	{
		return GetPublicVariableByName(InVariableName).GetValue<T>();
	}

	template<class T>
	FORCEINLINE void SetPublicVariableValue(const FName& InVariableName, const T& InValue)
	{
		GetPublicVariableByName(InVariableName).SetValue<T>();
	}

	template<class T>
	FORCEINLINE bool SupportsEvent() const
	{
		return SupportsEvent(T::EventName);
	}

	UFUNCTION(BlueprintPure, Category = "Control Rig")
	bool SupportsEvent(const FName& InEventName) const;

	UFUNCTION(BlueprintPure, Category = "Control Rig")
	TArray<FName> GetSupportedEvents() const;

	/** Setup bindings to a runtime object (or clear by passing in nullptr). */
	FORCEINLINE void SetObjectBinding(TSharedPtr<IControlRigObjectBinding> InObjectBinding)
	{
		ObjectBinding = InObjectBinding;
	}

	FORCEINLINE TSharedPtr<IControlRigObjectBinding> GetObjectBinding() const
	{
		return ObjectBinding;
	}

	virtual FString GetName() const
	{
		FString ObjectName = (GetClass()->GetName());
		ObjectName.RemoveFromEnd(TEXT("_C"));
		return ObjectName;
	}

	UFUNCTION(BlueprintPure, Category = "Control Rig")
	FORCEINLINE_DEBUGGABLE URigHierarchy* GetHierarchy()
	{
		if(DynamicHierarchy != nullptr && DynamicHierarchy->GetOuter() != this)
		{
#if WITH_EDITOR
			DynamicHierarchy->OnUndoRedo().RemoveAll(this);
#endif
			DynamicHierarchy = nullptr;
		}
		
		if(DynamicHierarchy == nullptr)
		{
			DynamicHierarchy = NewObject<URigHierarchy>(this);
			if (!HasAnyFlags(RF_ClassDefaultObject))
			{
				DynamicHierarchy->SetFlags(RF_Transient | RF_Transactional);
			}
#if WITH_EDITOR
			DynamicHierarchy->OnUndoRedo().AddUObject(this, &UControlRig::OnHierarchyTransformUndoRedo);
#endif
		}
		return DynamicHierarchy;
	}

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
	UFUNCTION(BlueprintCallable, Category = "Control Rig")
	void Execute(const EControlRigState State, const FName& InEventName);

	/** ExecuteUnits */
	virtual void ExecuteUnits(FRigUnitContext& InOutContext, const FName& InEventName);

	/** Requests to perform an init during the next execution */
	UFUNCTION(BlueprintCallable, Category = "Control Rig")
	void RequestInit();

	/** Requests to perform a setup during the next execution */
	UFUNCTION(BlueprintCallable, Category = "Control Rig")
	void RequestSetup();

	/** Returns the queue of events to run */
	const TArray<FName>& GetEventQueue() const { return EventQueue; }

	/** Sets the queue of events to run */
	void SetEventQueue(const TArray<FName>& InEventNames);

	URigVM* GetVM();

	/** INodeMappingInterface implementation */
	virtual void GetMappableNodeData(TArray<FName>& OutNames, TArray<FNodeItem>& OutNodeItems) const override;

	/** Data Source Registry Getter */
	UAnimationDataSourceRegistry* GetDataSourceRegistry();

	virtual TArray<FRigControlElement*> AvailableControls() const;
	virtual FRigControlElement* FindControl(const FName& InControlName) const;
	virtual bool ShouldApplyLimits() const { return !bSetupModeEnabled; }
	virtual bool IsSetupModeEnabled() const { return bSetupModeEnabled; }
	virtual FTransform SetupControlFromGlobalTransform(const FName& InControlName, const FTransform& InGlobalTransform);
	virtual FTransform GetControlGlobalTransform(const FName& InControlName) const;

	// Sets the relative value of a Control
	template<class T>
	FORCEINLINE_DEBUGGABLE void SetControlValue(const FName& InControlName, T InValue, bool bNotify = true,
		const FRigControlModifiedContext& Context = FRigControlModifiedContext(), bool bSetupUndo = true)
	{
		SetControlValueImpl(InControlName, FRigControlValue::Make<T>(InValue), bNotify, Context, bSetupUndo);
	}

	// Returns the value of a Control
	FORCEINLINE_DEBUGGABLE FRigControlValue GetControlValue(const FName& InControlName)
	{
		const FRigElementKey Key(InControlName, ERigElementType::Control);
		return DynamicHierarchy->GetControlValue(Key);
	}

	// Sets the relative value of a Control
	FORCEINLINE_DEBUGGABLE virtual void SetControlValueImpl(const FName& InControlName, const FRigControlValue& InValue, bool bNotify = true,
		const FRigControlModifiedContext& Context = FRigControlModifiedContext(), bool bSetupUndo = true)
	{
		const FRigElementKey Key(InControlName, ERigElementType::Control);

		FRigControlElement* ControlElement = DynamicHierarchy->Find<FRigControlElement>(Key);
		if(ControlElement == nullptr)
		{
			return;
		}

		DynamicHierarchy->SetControlValue(ControlElement, InValue, ERigControlValueType::Current, bSetupUndo);

		if (bNotify && OnControlModified.IsBound())
		{
			OnControlModified.Broadcast(this, ControlElement, Context);
		}
	}

	/** Turn On Interact, MUST Call SetInteractOff*/
	FORCEINLINE_DEBUGGABLE void SetInteractOn()
	{
		++InteractionBracket;
		++InterRigSyncBracket;
	}

	/** Turn Off Interact, MUST Have Called SetInteractOn*/
	FORCEINLINE_DEBUGGABLE void SetInteractOff()
	{
		--InteractionBracket;
		--InterRigSyncBracket;
	}

	bool SetControlGlobalTransform(const FName& InControlName, const FTransform& InGlobalTransform, bool bNotify = true, const FRigControlModifiedContext& Context = FRigControlModifiedContext(), bool bSetupUndo = true);

	virtual FRigControlValue GetControlValueFromGlobalTransform(const FName& InControlName, const FTransform& InGlobalTransform);

	virtual void SetControlLocalTransform(const FName& InControlName, const FTransform& InLocalTransform, bool bNotify = true, const FRigControlModifiedContext& Context = FRigControlModifiedContext(), bool bSetupUndo = true);
	virtual FTransform GetControlLocalTransform(const FName& InControlName) ;

	virtual UControlRigGizmoLibrary* GetGizmoLibrary() const;
	virtual void CreateRigControlsForCurveContainer();
	virtual void GetControlsInOrder(TArray<FRigControlElement*>& SortedControls) const;

	virtual void SelectControl(const FName& InControlName, bool bSelect = true);
	virtual bool ClearControlSelection();
	virtual TArray<FName> CurrentControlSelection() const;
	virtual bool IsControlSelected(const FName& InControlName)const;

	// Returns true if this manipulatable subject is currently
	// available for manipulation / is enabled.
	virtual bool ManipulationEnabled() const
	{
		return bManipulationEnabled;
	}

	// Sets the manipulatable subject to enabled or disabled
	virtual bool SetManipulationEnabled(bool Enabled = true)
	{
		if (bManipulationEnabled == Enabled)
		{
			return false;
		}
		bManipulationEnabled = Enabled;
		return true;
	}

	// Returns a event that can be used to subscribe to
	// filtering control data when needed
	FFilterControlEvent& ControlFilter() { return OnFilterControl; }

	// Returns a event that can be used to subscribe to
	// change notifications coming from the manipulated subject.
	FControlModifiedEvent& ControlModified() { return OnControlModified; }

	// Returns a event that can be used to subscribe to
	// selection changes coming from the manipulated subject.
	FControlSelectedEvent& ControlSelected() { return OnControlSelected; }

	bool IsCurveControl(const FRigControlElement* InControlElement) const;

	DECLARE_EVENT_ThreeParams(UControlRig, FControlRigExecuteEvent, class UControlRig*, const EControlRigState, const FName&);
	FControlRigExecuteEvent& OnInitialized_AnyThread() { return InitializedEvent; }
	FControlRigExecuteEvent& OnPreSetup_AnyThread() { return PreSetupEvent; }
	FControlRigExecuteEvent& OnPostSetup_AnyThread() { return PostSetupEvent; }
	FControlRigExecuteEvent& OnExecuted_AnyThread() { return ExecutedEvent; }
	FRigEventDelegate& OnRigEvent_AnyThread() { return RigEventDelegate; }

	// Setup the initial transforms / ref pose of the bones based on a skeletal mesh
	void SetBoneInitialTransformsFromSkeletalMesh(USkeletalMesh* InSkeletalMesh);

	// Setup the initial transforms / ref pose of the bones based on a reference skeleton
	void SetBoneInitialTransformsFromRefSkeleton(const FReferenceSkeleton& InReferenceSkeleton);

	const FControlRigDrawInterface& GetDrawInterface() const { return DrawInterface; };
	FControlRigDrawInterface& GetDrawInterface() { return DrawInterface; };

	const FControlRigDrawContainer& GetDrawContainer() const { return DrawContainer; };
	FControlRigDrawContainer& GetDrawContainer() { return DrawContainer; };

private:

	UPROPERTY()
	URigVM* VM;

	UPROPERTY()
	URigHierarchy *DynamicHierarchy;

	UPROPERTY()
	TSoftObjectPtr<UControlRigGizmoLibrary> GizmoLibrary;

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

	FRigNameCache NameCache;

private:
	
	void HandleOnControlModified(UControlRig* Subject, FRigControlElement* Control, const FRigControlModifiedContext& Context);

	TArray<FRigVMExternalVariable> GetExternalVariablesImpl(bool bFallbackToBlueprint) const;

	FORCEINLINE FProperty* GetPublicVariableProperty(const FName& InVariableName) const
	{
		if (FProperty* Property = GetClass()->FindPropertyByName(InVariableName))
		{
			if (!Property->IsNative())
			{
				if (!Property->HasAllPropertyFlags(CPF_DisableEditOnInstance))
				{
					return Property;
				}
			}
		}
		return nullptr;
	}

private:

	UPROPERTY()
	FControlRigDrawContainer DrawContainer;

	/** The draw interface for the units to use */
	FControlRigDrawInterface DrawInterface;

	/** The registry to access data source */
	UPROPERTY(transient)
	UAnimationDataSourceRegistry* DataSourceRegistry;

	/** The event name used during an update */
	UPROPERTY(transient)
	TArray<FName> EventQueue;

	/** Copy the VM from the default object */
	void InstantiateVMFromCDO();
	
	/** Broadcasts a notification whenever the controlrig's memory is initialized. */
	FControlRigExecuteEvent InitializedEvent;

	/** Broadcasts a notification just before the controlrig is setup. */
	FControlRigExecuteEvent PreSetupEvent;

	/** Broadcasts a notification whenever the controlrig has been setup. */
	FControlRigExecuteEvent PostSetupEvent;

	/** Broadcasts a notification whenever the controlrig is executed / updated. */
	FControlRigExecuteEvent ExecutedEvent;

	/** Handle changes within the hierarchy */
	void HandleHierarchyModified(ERigHierarchyNotification InNotification, URigHierarchy* InHierarchy, const FRigBaseElement* InElement);

#if WITH_EDITOR
	/** Remove a transient / temporary control used to interact with a pin */
	FName AddTransientControl(URigVMPin* InPin, FRigElementKey SpaceKey = FRigElementKey());

	/** Sets the value of a transient control based on a pin */
	bool SetTransientControlValue(URigVMPin* InPin);

	/** Remove a transient / temporary control used to interact with a pin */
	FName RemoveTransientControl(URigVMPin* InPin);

	FName AddTransientControl(const FRigElementKey& InElement);

	/** Sets the value of a transient control based on a bone */
	bool SetTransientControlValue(const FRigElementKey& InElement);

	/** Remove a transient / temporary control used to interact with a bone */
	FName RemoveTransientControl(const FRigElementKey& InElement);

	static FName GetNameForTransientControl(const FRigElementKey& InElement);
	FName GetNameForTransientControl(URigVMPin* InPin) const;
	static FString GetPinNameFromTransientControl(const FRigElementKey& InKey);
	static FRigElementKey GetElementKeyFromTransientControl(const FRigElementKey& InKey);

	/** Removes all  transient / temporary control used to interact with pins */
	void ClearTransientControls();

	UAnimPreviewInstance* PreviewInstance;

#endif

	void HandleHierarchyEvent(URigHierarchy* InHierarchy, const FRigEventContext& InEvent);
	FRigEventDelegate RigEventDelegate;

	void InitializeFromCDO();

	UPROPERTY()
	FRigInfluenceMapPerEvent Influences;

	const FRigInfluenceMap* FindInfluenceMap(const FName& InEventName);

	UPROPERTY(transient, BlueprintGetter = GetInteractionRig, BlueprintSetter = SetInteractionRig, Category = "Interaction")
	UControlRig* InteractionRig;

	UPROPERTY(EditAnywhere, transient, BlueprintGetter = GetInteractionRigClass, BlueprintSetter = SetInteractionRigClass, Category = "Interaction", Meta=(DisplayName="Interaction Rig"))
	TSubclassOf<UControlRig> InteractionRigClass;

public:

	UFUNCTION(BlueprintGetter)
	UControlRig* GetInteractionRig() const { return InteractionRig; }

	UFUNCTION(BlueprintSetter)
	void SetInteractionRig(UControlRig* InInteractionRig);

	UFUNCTION(BlueprintGetter)
	TSubclassOf<UControlRig> GetInteractionRigClass() const { return InteractionRigClass; }

	UFUNCTION(BlueprintSetter)
	void SetInteractionRigClass(TSubclassOf<UControlRig> InInteractionRigClass);

	// UObject interface
#if WITH_EDITOR
	virtual void PreEditChange(FProperty* PropertyAboutToChange) override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

public:
	//~ Begin IInterface_AssetUserData Interface
	virtual void AddAssetUserData(UAssetUserData* InUserData) override;
	virtual void RemoveUserDataOfClass(TSubclassOf<UAssetUserData> InUserDataClass) override;
	virtual UAssetUserData* GetAssetUserDataOfClass(TSubclassOf<UAssetUserData> InUserDataClass) override;
	virtual const TArray<UAssetUserData*>* GetAssetUserDataArray() const override;
	//~ End IInterface_AssetUserData Interface
protected
	:
	/** Array of user data stored with the asset */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Instanced, Category = "Default")
	TArray<UAssetUserData*> AssetUserData;

private:

	void CopyPoseFromOtherRig(UControlRig* Subject);
	void HandleInteractionRigControlModified(UControlRig* Subject, FRigControlElement* Control, const FRigControlModifiedContext& Context);
	void HandleInteractionRigInitialized(UControlRig* Subject, EControlRigState State, const FName& EventName);
	void HandleInteractionRigExecuted(UControlRig* Subject, EControlRigState State, const FName& EventName);
	void HandleInteractionRigControlSelected(UControlRig* Subject, FRigControlElement* InControl, bool bSelected, bool bInverted);

#if WITH_EDITOR

public:

	static FEdGraphPinType GetPinTypeFromExternalVariable(const FRigVMExternalVariable& InExternalVariable);
	static FRigVMExternalVariable GetExternalVariableFromPinType(const FName& InName, const FEdGraphPinType& InPinType, bool bInPublic = false, bool bInReadonly = false);
	static FRigVMExternalVariable GetExternalVariableFromDescription(const FBPVariableDescription& InVariableDescription);

#endif

protected:
	bool bRequiresInitExecution;
	bool bRequiresSetupEvent;
	bool bSetupModeEnabled;
	bool bCopyHierarchyBeforeSetup;
	bool bResetInitialTransformsBeforeSetup;
	bool bManipulationEnabled;

	int32 InitBracket;
	int32 UpdateBracket;
	int32 PreSetupBracket;
	int32 PostSetupBracket;
	int32 InteractionBracket;
	int32 InterRigSyncBracket;

	TWeakObjectPtr<USceneComponent> OuterSceneComponent;

	FORCEINLINE bool IsInitializing() const
	{
		return InitBracket > 0;
	}

	FORCEINLINE bool IsExecuting() const
	{
		return UpdateBracket > 0;
	}

	FORCEINLINE bool IsRunningPreSetup() const
	{
		return PreSetupBracket > 0;
	}

	FORCEINLINE bool IsRunningPostSetup() const
	{
		return PostSetupBracket > 0;
	}

	FORCEINLINE bool IsInteracting() const
	{
		return InteractionBracket > 0;
	}

	FORCEINLINE bool IsSyncingWithOtherRig() const
	{
		return InterRigSyncBracket > 0;
	}

	void OnHierarchyTransformUndoRedo(URigHierarchy* InHierarchy, const FRigElementKey& InKey, ERigTransformType::Type InTransformType, const FTransform& InTransform, bool bIsUndo);

	FFilterControlEvent OnFilterControl;
	FControlModifiedEvent OnControlModified;
	FControlSelectedEvent OnControlSelected;

	TArray<FRigElementKey> QueuedModifiedControls;

	friend class FControlRigBlueprintCompilerContext;
	friend struct FRigHierarchyRef;
	friend class FControlRigEditor;
	friend class SRigCurveContainer;
	friend class SRigHierarchy;
	friend class UEngineTestControlRig;
 	friend class FControlRigEditMode;
	friend class UControlRigBlueprint;
	friend class UControlRigComponent;
	friend class UControlRigBlueprintGeneratedClass;
	friend class FControlRigInteractionScope;
	friend class UControlRigValidator;
};

class CONTROLRIG_API FControlRigBracketScope
{
public:

	FORCEINLINE FControlRigBracketScope(int32& InBracket)
		: Bracket(InBracket)
	{
		Bracket++;
	}

	FORCEINLINE ~FControlRigBracketScope()
	{
		Bracket--;
	}

private:

	int32& Bracket;
};

class CONTROLRIG_API FControlRigInteractionScope
{
public:

	FORCEINLINE FControlRigInteractionScope(UControlRig* InControlRig)
		: ControlRig(InControlRig)
		, InteractionBracketScope(InControlRig->InteractionBracket)
		, SyncBracketScope(InControlRig->InterRigSyncBracket)
	{
		InControlRig->GetHierarchy()->StartInteraction();
	}

	FORCEINLINE ~FControlRigInteractionScope()
	{
		if(ControlRig.IsValid())
		{
			ControlRig->GetHierarchy()->EndInteraction();
		}
	}

private:

	TWeakObjectPtr<UControlRig> ControlRig;
	FControlRigBracketScope InteractionBracketScope;
	FControlRigBracketScope SyncBracketScope;
};
