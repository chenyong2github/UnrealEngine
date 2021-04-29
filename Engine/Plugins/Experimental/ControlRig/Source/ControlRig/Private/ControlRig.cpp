// Copyright Epic Games, Inc. All Rights Reserved.

#include "ControlRig.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/Actor.h"
#include "Misc/RuntimeErrors.h"
#include "IControlRigObjectBinding.h"
#include "HelperUtil.h"
#include "ControlRigBlueprintGeneratedClass.h"
#include "ControlRigObjectVersion.h"
#include "Rigs/RigHierarchyController.h"
#include "Units/Execution/RigUnit_BeginExecution.h"
#include "Units/Execution/RigUnit_PrepareForExecution.h"
#include "Units/Execution/RigUnit_InverseExecution.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimationPoseData.h"
#if WITH_EDITOR
#include "ControlRigModule.h"
#include "Modules/ModuleManager.h"
#include "RigVMModel/RigVMNode.h"
#include "Engine/Blueprint.h"
#include "EdGraphSchema_K2.h"
#include "Kismet2/BlueprintEditorUtils.h"
#endif// WITH_EDITOR

#define LOCTEXT_NAMESPACE "ControlRig"

DEFINE_LOG_CATEGORY(LogControlRig);

DECLARE_STATS_GROUP(TEXT("ControlRig"), STATGROUP_ControlRig, STATCAT_Advanced);
DECLARE_CYCLE_STAT_EXTERN(TEXT("Control Rig Execution"), STAT_RigExecution, STATGROUP_ControlRig, );
DEFINE_STAT(STAT_RigExecution);

const FName UControlRig::OwnerComponent("OwnerComponent");

//CVar to specify if we should create a float control for each curve in the curve container
//By default we don't but it may be useful to do so for debugging
static TAutoConsoleVariable<int32> CVarControlRigCreateFloatControlsForCurves(
	TEXT("ControlRig.CreateFloatControlsForCurves"),
	0,
	TEXT("If nonzero we create a float control for each curve in the curve container, useful for debugging low level controls."),
	ECVF_Default);

UControlRig::UControlRig(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, DeltaTime(0.0f)
	, AbsoluteTime(0.0f)
	, FramesPerSecond(0.0f)
	, bAccumulateTime(true)
	, LatestExecutedState(EControlRigState::Invalid)
#if WITH_EDITOR
	, ControlRigLog(nullptr)
	, bEnableControlRigLogging(true)
#endif
	, DataSourceRegistry(nullptr)
	, EventQueue()
#if WITH_EDITOR
	, PreviewInstance(nullptr)
#endif
	, bRequiresInitExecution(false)
	, bRequiresSetupEvent(false)
	, bSetupModeEnabled(false)
	, bCopyHierarchyBeforeSetup(true)
	, bResetInitialTransformsBeforeSetup(true)
	, bManipulationEnabled(false)
	, InitBracket(0)
	, UpdateBracket(0)
	, PreSetupBracket(0)
	, PostSetupBracket(0)
	, InteractionBracket(0)
	, InterRigSyncBracket(0)
#if WITH_EDITORONLY_DATA
	, VMSnapshotBeforeExecution(nullptr)
#endif
{
	SetVM(ObjectInitializer.CreateDefaultSubobject<URigVM>(this, TEXT("VM")));
	DynamicHierarchy = ObjectInitializer.CreateDefaultSubobject<URigHierarchy>(this, TEXT("DynamicHierarchy"));

	EventQueue.Add(FRigUnit_BeginExecution::EventName);
}

void UControlRig::BeginDestroy()
{
	Super::BeginDestroy();
	InitializedEvent.Clear();
	PreSetupEvent.Clear();
	PostSetupEvent.Clear();
	ExecutedEvent.Clear();
	SetInteractionRig(nullptr);

	if (VM)
	{
		VM->ExecutionReachedExit().RemoveAll(this);
	}

#if WITH_EDITORONLY_DATA
	if (VMSnapshotBeforeExecution)
	{
		VMSnapshotBeforeExecution = nullptr;
	}
#endif
}

UWorld* UControlRig::GetWorld() const
{
	if (ObjectBinding.IsValid())
	{
		AActor* HostingActor = ObjectBinding->GetHostingActor();
		if (HostingActor)
		{
			return HostingActor->GetWorld();
		}

		UObject* Owner = ObjectBinding->GetBoundObject();
		if (Owner)
		{
			return Owner->GetWorld();
		}
	}

	UObject* Outer = GetOuter();
	if (Outer)
	{
		return Outer->GetWorld();
	}

	return nullptr;
}

void UControlRig::Initialize(bool bInitRigUnits)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()
	QUICK_SCOPE_CYCLE_COUNTER(STAT_ControlRig_Initialize);

	if(IsInitializing())
	{
		UE_LOG(LogControlRig, Warning, TEXT("%s: Initialize is being called recursively."), *GetPathName());
		return;
	}

	if (IsTemplate())
	{
		// don't initialize template class 
		return;
	}

	InitializeFromCDO();
	InstantiateVMFromCDO();

	// should refresh mapping 
	RequestSetup();

	if (bInitRigUnits)
	{
		RequestInit();
	}

	
	GetHierarchy()->OnModified().RemoveAll(this);
	GetHierarchy()->OnModified().AddUObject(this, &UControlRig::HandleHierarchyModified);
	GetHierarchy()->OnEventReceived().RemoveAll(this);
	GetHierarchy()->OnEventReceived().AddUObject(this, &UControlRig::HandleHierarchyEvent);
}

void UControlRig::InitializeFromCDO()
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	// copy CDO property you need to here
	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		UControlRig* CDO = GetClass()->GetDefaultObject<UControlRig>();

		// copy hierarchy
		GetHierarchy()->CopyHierarchy(CDO->GetHierarchy());
		GetHierarchy()->ResetPoseToInitial(ERigElementType::All);

		// copy draw container
		DrawContainer = CDO->DrawContainer;
	}
}

void UControlRig::Evaluate_AnyThread()
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()
	QUICK_SCOPE_CYCLE_COUNTER(STAT_ControlRig_Evaluate);

	for (const FName& EventName : EventQueue)
	{
		Execute(EControlRigState::Update, EventName);
	}
}


TArray<FRigVMExternalVariable> UControlRig::GetExternalVariables() const
{
	return GetExternalVariablesImpl(true);
}

TArray<FRigVMExternalVariable> UControlRig::GetExternalVariablesImpl(bool bFallbackToBlueprint) const
{
	TArray<FRigVMExternalVariable> ExternalVariables;

	for (TFieldIterator<FProperty> PropertyIt(GetClass()); PropertyIt; ++PropertyIt)
	{
		FProperty* Property = *PropertyIt;
		if(Property->IsNative())
		{
			continue;
		}

		FRigVMExternalVariable ExternalVariable = FRigVMExternalVariable::Make(Property, (UObject*)this);
		if(!ExternalVariable.IsValid())
		{
			UE_LOG(LogControlRig, Warning, TEXT("%s: Property '%s' of type '%s' is not supported."), *GetClass()->GetName(), *Property->GetName(), *Property->GetCPPType());
			continue;
		}

		ExternalVariables.Add(ExternalVariable);
	}

#if WITH_EDITOR

	if (bFallbackToBlueprint)
	{
		// if we have a difference in the blueprint variables compared to us - let's 
		// use those instead. the assumption here is that the blueprint is dirty and
		// hasn't been compiled yet.
		if (UBlueprint* Blueprint = Cast<UBlueprint>(GetClass()->ClassGeneratedBy))
		{
			TArray<FRigVMExternalVariable> BlueprintVariables;
			for (const FBPVariableDescription& VariableDescription : Blueprint->NewVariables)
			{
				FRigVMExternalVariable ExternalVariable = GetExternalVariableFromDescription(VariableDescription);
				if (ExternalVariable.TypeName.IsNone())
				{
					continue;
				}

				ExternalVariable.Memory = nullptr;

				BlueprintVariables.Add(ExternalVariable);
			}

			if (ExternalVariables.Num() != BlueprintVariables.Num())
			{
				return BlueprintVariables;
			}

			TMap<FName, int32> NameMap;
			for (int32 Index = 0; Index < ExternalVariables.Num(); Index++)
			{
				NameMap.Add(ExternalVariables[Index].Name, Index);
			}

			for (FRigVMExternalVariable BlueprintVariable : BlueprintVariables)
			{
				const int32* Index = NameMap.Find(BlueprintVariable.Name);
				if (Index == nullptr)
				{
					return BlueprintVariables;
				}

				FRigVMExternalVariable ExternalVariable = ExternalVariables[*Index];
				if (ExternalVariable.bIsArray != BlueprintVariable.bIsArray ||
					ExternalVariable.bIsPublic != BlueprintVariable.bIsPublic ||
					ExternalVariable.TypeName != BlueprintVariable.TypeName ||
					ExternalVariable.TypeObject != BlueprintVariable.TypeObject)
				{
					return BlueprintVariables;
				}
			}
		}
	}
#endif

	return ExternalVariables;
}

TArray<FRigVMExternalVariable> UControlRig::GetPublicVariables() const
{
	TArray<FRigVMExternalVariable> PublicVariables;
	TArray<FRigVMExternalVariable> ExternalVariables = GetExternalVariables();
	for (const FRigVMExternalVariable& ExternalVariable : ExternalVariables)
	{
		if (ExternalVariable.bIsPublic)
		{
			PublicVariables.Add(ExternalVariable);
		}
	}
	return PublicVariables;
}

FRigVMExternalVariable UControlRig::GetPublicVariableByName(const FName& InVariableName) const
{
	if (FProperty* Property = GetPublicVariableProperty(InVariableName))
	{
		return FRigVMExternalVariable::Make(Property, (UObject*)this);
	}
	return FRigVMExternalVariable();
}

TArray<FName> UControlRig::GetScriptAccessibleVariables() const
{
	TArray<FRigVMExternalVariable> PublicVariables = GetPublicVariables();
	TArray<FName> Names;
	for (const FRigVMExternalVariable& PublicVariable : PublicVariables)
	{
		Names.Add(PublicVariable.Name);
	}
	return Names;
}

FName UControlRig::GetVariableType(const FName& InVariableName) const
{
	FRigVMExternalVariable PublicVariable = GetPublicVariableByName(InVariableName);
	if (PublicVariable.IsValid(true /* allow nullptr */))
	{
		return PublicVariable.TypeName;
	}
	return NAME_None;
}

FString UControlRig::GetVariableAsString(const FName& InVariableName) const
{
#if WITH_EDITOR
	if (const FProperty* Property = GetClass()->FindPropertyByName(InVariableName))
	{
		FString Result;
		const uint8* Container = (const uint8*)this;
		if (FBlueprintEditorUtils::PropertyValueToString(Property, Container, Result, nullptr))
		{
			return Result;
		}
	}
#endif
	return FString();
}

bool UControlRig::SetVariableFromString(const FName& InVariableName, const FString& InValue)
{
#if WITH_EDITOR
	if (const FProperty* Property = GetClass()->FindPropertyByName(InVariableName))
	{
		uint8* Container = (uint8*)this;
		return FBlueprintEditorUtils::PropertyValueFromString(Property, InValue, Container, nullptr);
	}
#endif
	return false;
}

bool UControlRig::SupportsEvent(const FName& InEventName) const
{
	if (VM)
	{
		return VM->ContainsEntry(InEventName);
	}
	return false;
}

TArray<FName> UControlRig::GetSupportedEvents() const
{
	if (VM)
	{
		return VM->GetEntryNames();
	}
	return TArray<FName>();
}

#if WITH_EDITOR
FText UControlRig::GetCategory() const
{
	return LOCTEXT("DefaultControlRigCategory", "Animation|ControlRigs");
}

FText UControlRig::GetToolTipText() const
{
	return LOCTEXT("DefaultControlRigTooltip", "ControlRig");
}
#endif

void UControlRig::SetDeltaTime(float InDeltaTime)
{
	DeltaTime = InDeltaTime;
}

void UControlRig::SetAbsoluteTime(float InAbsoluteTime, bool InSetDeltaTimeZero)
{
	if(InSetDeltaTimeZero)
	{
		DeltaTime = 0.f;
	}
	AbsoluteTime = InAbsoluteTime;
	bAccumulateTime = false;
}

void UControlRig::SetAbsoluteAndDeltaTime(float InAbsoluteTime, float InDeltaTime)
{
	AbsoluteTime = InAbsoluteTime;
	DeltaTime = InDeltaTime;
}

void UControlRig::SetFramesPerSecond(float InFramesPerSecond)
{
	FramesPerSecond = InFramesPerSecond;	
}

float UControlRig::GetCurrentFramesPerSecond() const
{
	if(FramesPerSecond > SMALL_NUMBER)
	{
		return FramesPerSecond;
	}
	if(DeltaTime > SMALL_NUMBER)
	{
		return 1.f / DeltaTime;
	}
	return 60.f;
}

void UControlRig::InstantiateVMFromCDO()
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	if (VM == nullptr || VM->GetOuter() != this)
	{
		SetVM(NewObject<URigVM>(this, TEXT("VM")));
	}
	
	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		UControlRig* CDO = GetClass()->GetDefaultObject<UControlRig>();
		if (VM && CDO && CDO->VM)
		{
			// reference the literal memory + byte code
			VM->CopyFrom(CDO->VM, true, true);
		}
		else
		{
			VM->Reset();
		}
	}

	RequestInit();
}

void UControlRig::Execute(const EControlRigState InState, const FName& InEventName)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()
	QUICK_SCOPE_CYCLE_COUNTER(STAT_ControlRig_Execute);
	
	LatestExecutedState = InState;

	if (VM)
	{
		if (VM->GetOuter() != this)
		{
			InstantiateVMFromCDO();
		}

		if (InState == EControlRigState::Init)
		{
			VM->ClearExternalVariables();

			TArray<FRigVMExternalVariable> ExternalVariables = GetExternalVariablesImpl(false);
			for (FRigVMExternalVariable ExternalVariable : ExternalVariables)
			{
				VM->AddExternalVariable(ExternalVariable);
			}
		}
#if WITH_EDITOR
		// default to always clear data after each execution
		// only set a valid first entry event later when execution
		// has passed the initialization stage and there are multiple events present in one evaluation
		// first entry event is used to determined when to clear data during an evaluation
		VM->SetFirstEntryEventInEventQueue(NAME_None);
#endif
	}

#if WITH_EDITOR
	if (UControlRig* CDO = GetClass()->GetDefaultObject<UControlRig>())
	{
		// Copy the breakpoints. This will not override the state of the breakpoints
		DebugInfo.SetBreakpoints(CDO->DebugInfo.GetBreakpoints());

		// If there are any breakpoints, create the Snapshot VM if it hasn't been created yet
		if (DebugInfo.GetBreakpoints().Num() > 0)
		{
			GetSnapshotVM();
		}
	}
	VM->SetDebugInfo(&DebugInfo);
#endif

	bool bJustRanInit = false;
	if (bRequiresInitExecution)
	{
		bRequiresInitExecution = false;

		if (InState != EControlRigState::Init)
		{
			Execute(EControlRigState::Init, InEventName);
			bJustRanInit = true;
		}
	}

	FRigUnitContext Context;
	DrawInterface.Reset();
	Context.DrawInterface = &DrawInterface;

	// draw container contains persistent draw instructions, 
	// so we cannot call Reset(), which will clear them,
	// instead, we re-initialize them from the CDO
	if (UControlRig* CDO = GetClass()->GetDefaultObject<UControlRig>())
	{
		DrawContainer = CDO->DrawContainer;
	}

	Context.DrawContainer = &DrawContainer;
	Context.DataSourceRegistry = GetDataSourceRegistry();

	if (InState == EControlRigState::Init)
	{
		AbsoluteTime = DeltaTime = 0.f;
		NameCache.Reset();
	}

	Context.DeltaTime = DeltaTime;
	Context.AbsoluteTime = AbsoluteTime;
	Context.FramesPerSecond = GetCurrentFramesPerSecond();
	Context.bDuringInteraction = IsInteracting();
	Context.State = InState;
	Context.Hierarchy = GetHierarchy();

	Context.ToWorldSpaceTransform = FTransform::Identity;
	Context.OwningComponent = nullptr;
	Context.OwningActor = nullptr;
	Context.World = nullptr;
	Context.NameCache = &NameCache;

	if (!OuterSceneComponent.IsValid())
	{
		USceneComponent* SceneComponentFromRegistry = Context.DataSourceRegistry->RequestSource<USceneComponent>(UControlRig::OwnerComponent);
		if (SceneComponentFromRegistry)
		{
			OuterSceneComponent = SceneComponentFromRegistry;
		}
		else
		{
			UObject* Parent = this;
			while (Parent)
			{
				Parent = Parent->GetOuter();
				if (Parent)
				{
					if (USceneComponent* SceneComponent = Cast<USceneComponent>(Parent))
					{
						OuterSceneComponent = SceneComponent;
						break;
					}
				}
			}
		}
	}

	if (OuterSceneComponent.IsValid())
	{
		Context.ToWorldSpaceTransform = OuterSceneComponent->GetComponentToWorld();
		Context.OwningComponent = OuterSceneComponent.Get();
		Context.OwningActor = Context.OwningComponent->GetOwner();
		Context.World = Context.OwningComponent->GetWorld();
	}
	else
	{
		if (ObjectBinding.IsValid())
		{
			AActor* HostingActor = ObjectBinding->GetHostingActor();
			if (HostingActor)
			{
				Context.OwningActor = HostingActor;
				Context.World = HostingActor->GetWorld();
			}
			else if (UObject* Owner = ObjectBinding->GetBoundObject())
			{
				Context.World = Owner->GetWorld();
			}
		}

		if (Context.World == nullptr)
		{
			if (UObject* Outer = GetOuter())
			{
				Context.World = Outer->GetWorld();
			}
		}
	}

	if(GetHierarchy())
	{
		// if we have any aux elements dirty them
		GetHierarchy()->UpdateSockets(&Context);
	}

#if WITH_EDITOR
	Context.Log = ControlRigLog;
	if (ControlRigLog != nullptr)
	{
		ControlRigLog->Reset();
	}
#endif

	// execute units
	if (bRequiresSetupEvent && InState != EControlRigState::Init)
	{
		if(!IsRunningPreSetup() && !IsRunningPostSetup())
		{
			bRequiresSetupEvent = bSetupModeEnabled;

			if (UControlRig* CDO = Cast<UControlRig>(GetClass()->GetDefaultObject()))
			{
				if(bCopyHierarchyBeforeSetup && !bSetupModeEnabled)
				{
					if(CDO->GetHierarchy()->GetTopologyVersion()!= GetHierarchy()->GetTopologyVersion())
					{
						GetHierarchy()->CopyHierarchy(CDO->GetHierarchy());
					}
				}
				
				if (bResetInitialTransformsBeforeSetup && !bSetupModeEnabled)
				{
					GetHierarchy()->CopyPose(CDO->GetHierarchy(), false, true);
				}
			}

			if (PreSetupEvent.IsBound())
			{
				FControlRigBracketScope BracketScope(PreSetupBracket);
				PreSetupEvent.Broadcast(this, EControlRigState::Update, FRigUnit_PrepareForExecution::EventName);
			}

			ExecuteUnits(Context, FRigUnit_PrepareForExecution::EventName);

			if (PostSetupEvent.IsBound())
			{
				FControlRigBracketScope BracketScope(PostSetupBracket);
				PostSetupEvent.Broadcast(this, EControlRigState::Update, FRigUnit_PrepareForExecution::EventName);
			}

			if (bSetupModeEnabled)
			{
				GetHierarchy()->ResetPoseToInitial(ERigElementType::Bone);
			}
		}
		else
		{
			UE_LOG(LogControlRig, Warning, TEXT("%s: Setup is being called recursively."), *GetPathName());
		}
	}

	if (!bSetupModeEnabled)
	{

		if(!IsExecuting())
		{ 

#if WITH_EDITOR
			// only set a valid first entry event when execution
			// has passed the initialization stage and there are multiple events present
			if (EventQueue.Num() >= 2 && VM && InState != EControlRigState::Init)
			{
				VM->SetFirstEntryEventInEventQueue(EventQueue[0]);
			}
#endif

			ExecuteUnits(Context, InEventName);

			if (InState == EControlRigState::Init)
			{
				ExecuteUnits(Context, FRigUnit_BeginExecution::EventName);
			}
		}
		else
		{
			UE_LOG(LogControlRig, Warning, TEXT("%s: Update is being called recursively."), *GetPathName());
		}
	}

#if WITH_EDITOR
	if (ControlRigLog != nullptr && bEnableControlRigLogging && InState != EControlRigState::Init && !bJustRanInit)
	{
		for (const FControlRigLog::FLogEntry& Entry : ControlRigLog->Entries)
		{
			if (Entry.FunctionName == NAME_None || Entry.InstructionIndex == INDEX_NONE || Entry.Message.IsEmpty())
			{
				continue;
			}

			switch (Entry.Severity)
			{
				case EMessageSeverity::CriticalError:
				case EMessageSeverity::Error:
				{
					UE_LOG(LogControlRig, Error, TEXT("Instruction[%d] '%s': '%s'"), Entry.InstructionIndex, *Entry.FunctionName.ToString(), *Entry.Message);
					break;
				}
				case EMessageSeverity::PerformanceWarning:
				case EMessageSeverity::Warning:
				{
					UE_LOG(LogControlRig, Warning, TEXT("Instruction[%d] '%s': '%s'"), Entry.InstructionIndex, *Entry.FunctionName.ToString(), *Entry.Message);
					break;
				}
				case EMessageSeverity::Info:
				{
					UE_LOG(LogControlRig, Display, TEXT("Instruction[%d] '%s': '%s'"), Entry.InstructionIndex, *Entry.FunctionName.ToString(), *Entry.Message);
					break;
				}
				default:
				{
					break;
				}
			}
		}
	}

	if (bJustRanInit && ControlRigLog != nullptr)
	{
		ControlRigLog->KnownMessages.Reset();
	}
#endif


	if (InState == EControlRigState::Init)
	{
		if (InitializedEvent.IsBound())
		{
			FControlRigBracketScope BracketScope(InitBracket);
			InitializedEvent.Broadcast(this, EControlRigState::Init, InEventName);
		}
	}
	else if (InState == EControlRigState::Update)
	{
		DeltaTime = 0.f;

		if (ExecutedEvent.IsBound())
		{
			FControlRigBracketScope BracketScope(UpdateBracket);
			ExecutedEvent.Broadcast(this, EControlRigState::Update, InEventName);
		}
	}

	if (Context.DrawInterface && Context.DrawContainer)
	{
		Context.DrawInterface->Instructions.Append(Context.DrawContainer->Instructions);

		GetHierarchy()->ForEach<FRigControlElement>([this](FRigControlElement* ControlElement) -> bool
		{
			const FRigControlSettings& Settings = ControlElement->Settings;
			
			if (Settings.bGizmoEnabled &&
				Settings.bGizmoVisible &&
				!Settings.bIsTransientControl &&
				Settings.bDrawLimits &&
				(Settings.bLimitTranslation
					|| Settings.bLimitRotation
					|| Settings.bLimitScale))
			{
				// for now we don't draw rotational limits
				if(!Settings.bLimitTranslation)
				{
					return true;
				}

				FTransform Transform = GetHierarchy()->GetGlobalControlOffsetTransformByIndex(ControlElement->GetIndex());
				FControlRigDrawInstruction Instruction(EControlRigDrawSettings::Lines, Settings.GizmoColor, 0.f, Transform);

				switch (Settings.ControlType)
				{
					case ERigControlType::Float:
					{
						FVector MinPos = FVector::ZeroVector;
						FVector MaxPos = FVector::ZeroVector;

						switch (Settings.PrimaryAxis)
						{
							case ERigControlAxis::X:
							{
								MinPos.X = Settings.MinimumValue.Get<float>();
								MaxPos.X = Settings.MaximumValue.Get<float>();
								break;
							}
							case ERigControlAxis::Y:
							{
								MinPos.Y = Settings.MinimumValue.Get<float>();
								MaxPos.Y = Settings.MaximumValue.Get<float>();
								break;
							}
							case ERigControlAxis::Z:
							{
								MinPos.Z = Settings.MinimumValue.Get<float>();
								MaxPos.Z = Settings.MaximumValue.Get<float>();
								break;
							}
						}

						Instruction.Positions.Add(MinPos);
						Instruction.Positions.Add(MaxPos);
						break;
					}
					case ERigControlType::Integer:
					{
						FVector MinPos = FVector::ZeroVector;
						FVector MaxPos = FVector::ZeroVector;

						switch (Settings.PrimaryAxis)
						{
							case ERigControlAxis::X:
							{
								MinPos.X = (float)Settings.MinimumValue.Get<int32>();
								MaxPos.X = (float)Settings.MaximumValue.Get<int32>();
								break;
							}
							case ERigControlAxis::Y:
							{
								MinPos.Y = (float)Settings.MinimumValue.Get<int32>();
								MaxPos.Y = (float)Settings.MaximumValue.Get<int32>();
								break;
							}
							case ERigControlAxis::Z:
							{
								MinPos.Z = (float)Settings.MinimumValue.Get<int32>();
								MaxPos.Z = (float)Settings.MaximumValue.Get<int32>();
								break;
							}
						}

						Instruction.Positions.Add(MinPos);
						Instruction.Positions.Add(MaxPos);
						break;
					}
					case ERigControlType::Vector2D:
					{
						Instruction.PrimitiveType = EControlRigDrawSettings::LineStrip;
						FVector2D MinPos = Settings.MinimumValue.Get<FVector2D>();
						FVector2D MaxPos = Settings.MaximumValue.Get<FVector2D>();

						switch (Settings.PrimaryAxis)
						{
							case ERigControlAxis::X:
							{
								Instruction.Positions.Add(FVector(0.f, MinPos.X, MinPos.Y));
								Instruction.Positions.Add(FVector(0.f, MaxPos.X, MinPos.Y));
								Instruction.Positions.Add(FVector(0.f, MaxPos.X, MaxPos.Y));
								Instruction.Positions.Add(FVector(0.f, MinPos.X, MaxPos.Y));
								Instruction.Positions.Add(FVector(0.f, MinPos.X, MinPos.Y));
								break;
							}
							case ERigControlAxis::Y:
							{
								Instruction.Positions.Add(FVector(MinPos.X, 0.f, MinPos.Y));
								Instruction.Positions.Add(FVector(MaxPos.X, 0.f, MinPos.Y));
								Instruction.Positions.Add(FVector(MaxPos.X, 0.f, MaxPos.Y));
								Instruction.Positions.Add(FVector(MinPos.X, 0.f, MaxPos.Y));
								Instruction.Positions.Add(FVector(MinPos.X, 0.f, MinPos.Y));
								break;
							}
							case ERigControlAxis::Z:
							{
								Instruction.Positions.Add(FVector(MinPos.X, MinPos.Y, 0.f));
								Instruction.Positions.Add(FVector(MaxPos.X, MinPos.Y, 0.f));
								Instruction.Positions.Add(FVector(MaxPos.X, MaxPos.Y, 0.f));
								Instruction.Positions.Add(FVector(MinPos.X, MaxPos.Y, 0.f));
								Instruction.Positions.Add(FVector(MinPos.X, MinPos.Y, 0.f));
								break;
							}
						}
						break;
					}
					case ERigControlType::Position:
					case ERigControlType::Scale:
					case ERigControlType::Transform:
					case ERigControlType::TransformNoScale:
					case ERigControlType::EulerTransform:
					{
						FVector MinPos, MaxPos;

						switch (Settings.ControlType)
						{
							case ERigControlType::Position:
							case ERigControlType::Scale:
							{
								MinPos = Settings.MinimumValue.Get<FVector>();
								MaxPos = Settings.MaximumValue.Get<FVector>();
								break;
							}
							case ERigControlType::Transform:
							{
								MinPos = Settings.MinimumValue.Get<FTransform>().GetLocation();
								MaxPos = Settings.MaximumValue.Get<FTransform>().GetLocation();
								break;
							}
							case ERigControlType::TransformNoScale:
							{
								MinPos = Settings.MinimumValue.Get<FTransformNoScale>().Location;
								MaxPos = Settings.MaximumValue.Get<FTransformNoScale>().Location;
								break;
							}
							case ERigControlType::EulerTransform:
							{
								MinPos = Settings.MinimumValue.Get<FEulerTransform>().Location;
								MaxPos = Settings.MaximumValue.Get<FEulerTransform>().Location;
								break;
							}
						}

						Instruction.Positions.Add(FVector(MinPos.X, MinPos.Y, MinPos.Z));
						Instruction.Positions.Add(FVector(MaxPos.X, MinPos.Y, MinPos.Z));
						Instruction.Positions.Add(FVector(MinPos.X, MaxPos.Y, MinPos.Z));
						Instruction.Positions.Add(FVector(MaxPos.X, MaxPos.Y, MinPos.Z));
						Instruction.Positions.Add(FVector(MinPos.X, MinPos.Y, MaxPos.Z));
						Instruction.Positions.Add(FVector(MaxPos.X, MinPos.Y, MaxPos.Z));
						Instruction.Positions.Add(FVector(MinPos.X, MaxPos.Y, MaxPos.Z));
						Instruction.Positions.Add(FVector(MaxPos.X, MaxPos.Y, MaxPos.Z));

						Instruction.Positions.Add(FVector(MinPos.X, MinPos.Y, MinPos.Z));
						Instruction.Positions.Add(FVector(MinPos.X, MaxPos.Y, MinPos.Z));
						Instruction.Positions.Add(FVector(MaxPos.X, MinPos.Y, MinPos.Z));
						Instruction.Positions.Add(FVector(MaxPos.X, MaxPos.Y, MinPos.Z));
						Instruction.Positions.Add(FVector(MinPos.X, MinPos.Y, MaxPos.Z));
						Instruction.Positions.Add(FVector(MinPos.X, MaxPos.Y, MaxPos.Z));
						Instruction.Positions.Add(FVector(MaxPos.X, MinPos.Y, MaxPos.Z));
						Instruction.Positions.Add(FVector(MaxPos.X, MaxPos.Y, MaxPos.Z));

						Instruction.Positions.Add(FVector(MinPos.X, MinPos.Y, MinPos.Z));
						Instruction.Positions.Add(FVector(MinPos.X, MinPos.Y, MaxPos.Z));
						Instruction.Positions.Add(FVector(MaxPos.X, MinPos.Y, MinPos.Z));
						Instruction.Positions.Add(FVector(MaxPos.X, MinPos.Y, MaxPos.Z));
						Instruction.Positions.Add(FVector(MinPos.X, MaxPos.Y, MinPos.Z));
						Instruction.Positions.Add(FVector(MinPos.X, MaxPos.Y, MaxPos.Z));
						Instruction.Positions.Add(FVector(MaxPos.X, MaxPos.Y, MinPos.Z));
						Instruction.Positions.Add(FVector(MaxPos.X, MaxPos.Y, MaxPos.Z));
						break;
					}
				}

				if (Instruction.Positions.Num() > 0)
				{
					DrawInterface.Instructions.Add(Instruction);
				}
			}

			return true;
		});
	}
}

void UControlRig::ExecuteUnits(FRigUnitContext& InOutContext, const FName& InEventName)
{
	if (VM)
	{
		FRigVMMemoryContainer* LocalMemory[] = { VM->WorkMemoryPtr, VM->LiteralMemoryPtr, VM->DebugMemoryPtr };
		TArray<void*> AdditionalArguments;
		AdditionalArguments.Add(&InOutContext);

		if (InOutContext.State == EControlRigState::Init)
		{
			VM->Initialize(FRigVMMemoryContainerPtrArray(LocalMemory, 3), AdditionalArguments);
		}
		else
		{
#if WITH_EDITOR
			if(URigVM* SnapShotVM = GetSnapshotVM(false)) // don't create it for normal runs
			{
				if (VM->GetHaltedAtInstruction() != INDEX_NONE)
				{
					VM->CopyFrom(SnapShotVM, false, false, false, true, true);	
				}
				else
				{
					SnapShotVM->CopyFrom(VM, false, false, false, true, true);
				}
			}
#endif
			VM->Execute(FRigVMMemoryContainerPtrArray(LocalMemory, 3), AdditionalArguments, InEventName);
		}
	}
}

void UControlRig::RequestInit()
{
	bRequiresInitExecution = true;
	RequestSetup();
}

void UControlRig::RequestSetup()
{
	bRequiresSetupEvent = true;
}

void UControlRig::SetEventQueue(const TArray<FName>& InEventNames)
{
	EventQueue = InEventNames;
}

void UControlRig::SetVM(URigVM* NewVM)
{
	if (VM)
	{
		VM->ExecutionReachedExit().RemoveAll(this);
	}
	
	if (NewVM)
	{
		if (!NewVM->ExecutionReachedExit().IsBoundToObject(this))
		{
			NewVM->ExecutionReachedExit().AddUObject(this, &UControlRig::HandleExecutionReachedExit);
		}
	}

	VM = NewVM;
}

URigVM* UControlRig::GetVM()
{
	if (VM == nullptr)
	{
		Initialize(true);
		check(VM);
	}
	return VM;
}

void UControlRig::GetMappableNodeData(TArray<FName>& OutNames, TArray<FNodeItem>& OutNodeItems) const
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	OutNames.Reset();
	OutNodeItems.Reset();

	check(DynamicHierarchy);

	// now add all nodes
	DynamicHierarchy->ForEach<FRigBoneElement>([&OutNames, &OutNodeItems, this](FRigBoneElement* BoneElement) -> bool
    {
		OutNames.Add(BoneElement->GetName());
		FRigElementKey ParentKey = DynamicHierarchy->GetFirstParent(BoneElement->GetKey());
		if(ParentKey.Type != ERigElementType::Bone)
		{
			ParentKey.Name = NAME_None;
		}

		const FTransform GlobalInitial = DynamicHierarchy->GetGlobalTransformByIndex(BoneElement->GetIndex(), true);
		OutNodeItems.Add(FNodeItem(ParentKey.Name, GlobalInitial));
		return true;
	});
}

UAnimationDataSourceRegistry* UControlRig::GetDataSourceRegistry()
{
	if (DataSourceRegistry)
	{
		if (DataSourceRegistry->GetOuter() != this)
		{
			DataSourceRegistry = nullptr;
		}
	}
	if (DataSourceRegistry == nullptr)
	{
		DataSourceRegistry = NewObject<UAnimationDataSourceRegistry>(this);
	}
	return DataSourceRegistry;
}

#if WITH_EDITORONLY_DATA

void UControlRig::PostReinstanceCallback(const UControlRig* Old)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	ObjectBinding = Old->ObjectBinding;
	Initialize();
}

#endif // WITH_EDITORONLY_DATA

void UControlRig::AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	Super::AddReferencedObjects(InThis, Collector);
}

#if WITH_EDITOR
//undo will clear out the transient Operators, need to recreate them
void UControlRig::PostEditUndo()
{
	Super::PostEditUndo();
}
#endif // WITH_EDITOR

void UControlRig::Serialize(FArchive& Ar)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	Super::Serialize(Ar);

	Ar.UsingCustomVersion(FControlRigObjectVersion::GUID);
}

void UControlRig::PostLoad()
{
	Super::PostLoad();

	if(HasAnyFlags(RF_ClassDefaultObject))
	{
		if(DynamicHierarchy)
		{
			// Some dynamic hierarchy objects have been created using NewObject<> instead of CreateDefaultSubObjects.
			// Assets from that version require the dynamic hierarchy to be flagged as below.
			DynamicHierarchy->SetFlags(DynamicHierarchy->GetFlags() | RF_Public | RF_DefaultSubObject);
		}
	}

#if WITH_EDITORONLY_DATA
	if (VMSnapshotBeforeExecution)
	{
		// Some VMSnapshots might have been created without the Transient flag.
		// Assets from that version require the snapshot to be flagged as below.
		VMSnapshotBeforeExecution->SetFlags(VMSnapshotBeforeExecution->GetFlags() | RF_Transient);
	}
#endif
}

TArray<FRigControlElement*> UControlRig::AvailableControls() const
{
	if(DynamicHierarchy)
	{
		return DynamicHierarchy->GetElementsOfType<FRigControlElement>();
	}
	return TArray<FRigControlElement*>();
}

FRigControlElement* UControlRig::FindControl(const FName& InControlName) const
{
	if(DynamicHierarchy == nullptr)
	{
		return nullptr;
	}
	return DynamicHierarchy->Find<FRigControlElement>(FRigElementKey(InControlName, ERigElementType::Control));
}

FTransform UControlRig::SetupControlFromGlobalTransform(const FName& InControlName, const FTransform& InGlobalTransform)
{
	if (IsSetupModeEnabled())
	{
		FRigControlElement* ControlElement = FindControl(InControlName);
		if (ControlElement && !ControlElement->Settings.bIsTransientControl)
		{
			const FTransform ParentTransform = GetHierarchy()->GetParentTransform(ControlElement, ERigTransformType::CurrentGlobal);
			const FTransform OffsetTransform = InGlobalTransform.GetRelativeTransform(ParentTransform);
			GetHierarchy()->SetControlOffsetTransform(ControlElement, OffsetTransform, ERigTransformType::InitialLocal, true, true);
			ControlElement->Offset.Current = ControlElement->Offset.Initial; 
		}
	}
	return InGlobalTransform;
}

void UControlRig::CreateRigControlsForCurveContainer()
{
	const bool bCreateFloatControls = CVarControlRigCreateFloatControlsForCurves->GetInt() == 0 ? false : true;
	if(bCreateFloatControls && DynamicHierarchy)
	{
		URigHierarchyController* Controller = DynamicHierarchy->GetController(true);
		if(Controller == nullptr)
		{
			return;
		}
		static const FString CtrlPrefix(TEXT("CTRL_"));

		DynamicHierarchy->ForEach<FRigCurveElement>([this, Controller](FRigCurveElement* CurveElement) -> bool
        {
			const FString Name = CurveElement->GetName().ToString();
			
			if (Name.Contains(CtrlPrefix) && !DynamicHierarchy->Contains(FRigElementKey(*Name, ERigElementType::Curve))) //-V1051
			{
				FRigControlSettings Settings;
				Settings.ControlType = ERigControlType::Float;
				Settings.bIsCurve = true;
				Settings.bAnimatable = true;
				Settings.bDrawLimits = false;
				Settings.bGizmoEnabled = false;
				Settings.bGizmoVisible = false;
				Settings.GizmoColor = FLinearColor::Red;

				FRigControlValue Value;
				Value.Set<float>(CurveElement->Value);

				Controller->AddControl(CurveElement->GetName(), FRigElementKey(), Settings, Value, FTransform::Identity, FTransform::Identity); 
			}

			return true;
		});

		ControlModified().AddUObject(this, &UControlRig::HandleOnControlModified);
	}
}

void UControlRig::HandleOnControlModified(UControlRig* Subject, FRigControlElement* Control, const FRigControlModifiedContext& Context)
{
	if (Control->Settings.bIsCurve && DynamicHierarchy)
	{
		const FRigControlValue Value = DynamicHierarchy->GetControlValue(Control, IsSetupModeEnabled() ? ERigControlValueType::Initial : ERigControlValueType::Current);
		DynamicHierarchy->SetCurveValue(FRigElementKey(Control->GetName(), ERigElementType::Curve), Value.Get<float>());
	}	
}

void UControlRig::HandleExecutionReachedExit()
{
#if WITH_EDITOR
	if(URigVM* SnapShotVM = GetSnapshotVM(false))
	{
		SnapShotVM->CopyFrom(VM, false, false, false, true, true);
	}
	DebugInfo.ResetState();
#endif
	
	if (LatestExecutedState != EControlRigState::Init && bAccumulateTime)
	{
		AbsoluteTime += DeltaTime;
	}
}

bool UControlRig::IsCurveControl(const FRigControlElement* InControlElement) const
{
	return InControlElement->Settings.bIsCurve;
}

FTransform UControlRig::GetControlGlobalTransform(const FName& InControlName) const
{
	if(DynamicHierarchy == nullptr)
	{
		return FTransform::Identity;
	}
	return DynamicHierarchy->GetGlobalTransform(FRigElementKey(InControlName, ERigElementType::Control), false);
}

bool UControlRig::SetControlGlobalTransform(const FName& InControlName, const FTransform& InGlobalTransform, bool bNotify, const FRigControlModifiedContext& Context, bool bSetupUndo)
{
	FTransform GlobalTransform = InGlobalTransform;
	if (IsSetupModeEnabled())
	{
		GlobalTransform = SetupControlFromGlobalTransform(InControlName, GlobalTransform);
	}

	FRigControlValue Value = GetControlValueFromGlobalTransform(InControlName, GlobalTransform);
	if (OnFilterControl.IsBound())
	{
		FRigControlElement* Control = FindControl(InControlName);
		if (Control)
		{
			OnFilterControl.Broadcast(this, Control, Value);
		}
	}

	SetControlValue(InControlName, Value, bNotify, Context, bSetupUndo);
	return true;
}

FRigControlValue UControlRig::GetControlValueFromGlobalTransform(const FName& InControlName, const FTransform& InGlobalTransform)
{
	FRigControlValue Value;

	if (FRigControlElement* ControlElement = FindControl(InControlName))
	{
		if(DynamicHierarchy)
		{
			FTransform ParentTransform = DynamicHierarchy->GetControlOffsetTransform(ControlElement, ERigTransformType::CurrentGlobal);
			FTransform Transform = InGlobalTransform.GetRelativeTransform(ParentTransform);
			Value.SetFromTransform(Transform, ControlElement->Settings.ControlType, ControlElement->Settings.PrimaryAxis);

			if (ShouldApplyLimits())
			{
				ControlElement->Settings.ApplyLimits(Value);
			}
		}
	}

	return Value;
}

void UControlRig::SetControlLocalTransform(const FName& InControlName, const FTransform& InLocalTransform, bool bNotify, const FRigControlModifiedContext& Context, bool bSetupUndo)
{
	if (FRigControlElement* ControlElement = FindControl(InControlName))
	{
		FRigControlValue Value;
		Value.SetFromTransform(InLocalTransform, ControlElement->Settings.ControlType, ControlElement->Settings.PrimaryAxis);

		if (OnFilterControl.IsBound())
		{
			OnFilterControl.Broadcast(this, ControlElement, Value);
			
		}
		SetControlValue(InControlName, Value, bNotify, Context, bSetupUndo);
	}
}

FTransform UControlRig::GetControlLocalTransform(const FName& InControlName)
{
	if(DynamicHierarchy == nullptr)
	{
		return FTransform::Identity;
	}
	return DynamicHierarchy->GetLocalTransform(FRigElementKey(InControlName, ERigElementType::Control));
}

UControlRigGizmoLibrary* UControlRig::GetGizmoLibrary() const
{
	if (UControlRig* CDO = Cast<UControlRig>(GetClass()->GetDefaultObject()))
	{
		if (!CDO->GizmoLibrary.IsValid())
		{
			CDO->GizmoLibrary.LoadSynchronous();
		}
		if (CDO->GizmoLibrary.IsValid())
		{
			return CDO->GizmoLibrary.Get();
		}
	}
	return nullptr;
}

void UControlRig::SelectControl(const FName& InControlName, bool bSelect)
{
	if(DynamicHierarchy)
	{
		if(URigHierarchyController* Controller = DynamicHierarchy->GetController(true))
		{
			Controller->SelectElement(FRigElementKey(InControlName, ERigElementType::Control), bSelect);
		}
	}
}

bool UControlRig::ClearControlSelection()
{
	if(DynamicHierarchy)
	{
		if(URigHierarchyController* Controller = DynamicHierarchy->GetController(true))
		{
			return Controller->ClearSelection();
		}
	}
	return false;
}

TArray<FName> UControlRig::CurrentControlSelection() const
{
	TArray<FName> SelectedControlNames;

	if(DynamicHierarchy)
	{
		TArray<FRigBaseElement*> SelectedControls = DynamicHierarchy->GetSelectedElements(ERigElementType::Control);
		for (FRigBaseElement* SelectedControl : SelectedControls)
		{
			SelectedControlNames.Add(SelectedControl->GetName());
		}
	}
	return SelectedControlNames;
}

bool UControlRig::IsControlSelected(const FName& InControlName)const
{
	if(DynamicHierarchy)
	{
		if(FRigControlElement* ControlElement = FindControl(InControlName))
		{
			return DynamicHierarchy->IsSelected(ControlElement);
		}
	}
	return false;
}

void UControlRig::HandleHierarchyModified(ERigHierarchyNotification InNotification, URigHierarchy* InHierarchy,
    const FRigBaseElement* InElement)
{
	switch(InNotification)
	{
		case ERigHierarchyNotification::ElementSelected:
		case ERigHierarchyNotification::ElementDeselected:
		{
			if(FRigControlElement* ControlElement = Cast<FRigControlElement>((FRigBaseElement*)InElement))
			{
				const bool bSelected = InNotification == ERigHierarchyNotification::ElementSelected;
				ControlSelected().Broadcast(this, ControlElement, bSelected);
			}
			break;
		}
		case ERigHierarchyNotification::ControlSettingChanged:
		case ERigHierarchyNotification::ControlGizmoTransformChanged:
		{
			if(FRigControlElement* ControlElement = Cast<FRigControlElement>((FRigBaseElement*)InElement))
			{
				ControlModified().Broadcast(this, ControlElement, FRigControlModifiedContext(EControlRigSetKey::Never));
			}
			break;
		}
		default:
		{
			break;
		}
	}
}

#if WITH_EDITOR

FName UControlRig::AddTransientControl(URigVMPin* InPin, FRigElementKey SpaceKey)
{
	if ((InPin == nullptr) || (DynamicHierarchy == nullptr))
	{
		return NAME_None;
	}

	if (InPin->GetCPPType() != TEXT("FVector") &&
		InPin->GetCPPType() != TEXT("FQuat") &&
		InPin->GetCPPType() != TEXT("FTransform"))
	{
		return NAME_None;
	}

	RemoveTransientControl(InPin);

	URigHierarchyController* Controller = DynamicHierarchy->GetController(true);
	if(Controller == nullptr)
	{
		return NAME_None;
	}

	URigVMPin* PinForLink = InPin->GetPinForLink();

	const FName ControlName = GetNameForTransientControl(InPin);
	FTransform GizmoTransform = FTransform::Identity;
	GizmoTransform.SetScale3D(FVector::ZeroVector);

	FRigControlSettings Settings;
	if (URigVMPin* ColorPin = PinForLink->GetNode()->FindPin(TEXT("Color")))
	{
		if (ColorPin->GetCPPType() == TEXT("FLinearColor"))
		{
			FRigControlValue Value;
			Settings.GizmoColor = Value.SetFromString<FLinearColor>(ColorPin->GetDefaultValue());
		}
	}
	Settings.bIsTransientControl = true;
	Settings.DisplayName = TEXT("Temporary Control");

	FRigElementKey Parent;
	if(SpaceKey.IsValid() && SpaceKey.Type == ERigElementType::Bone)
	{
		Parent = DynamicHierarchy->GetFirstParent(SpaceKey);
	}

	Controller->ClearSelection();

    const FRigElementKey ControlKey = Controller->AddControl(
    	ControlName,
    	Parent,
    	Settings,
    	FRigControlValue::Make(FTransform::Identity),
    	FTransform::Identity,
    	GizmoTransform, false);

	SetTransientControlValue(InPin);

	if(const FRigBaseElement* Element = DynamicHierarchy->Find(ControlKey))
	{
		DynamicHierarchy->Notify(ERigHierarchyNotification::ElementSelected, Element);
	}

	return ControlName;
}

bool UControlRig::SetTransientControlValue(URigVMPin* InPin)
{
	const FName ControlName = GetNameForTransientControl(InPin);
	if (FRigControlElement* ControlElement = FindControl(ControlName))
	{
		FString DefaultValue = InPin->GetPinForLink()->GetDefaultValue();
		if (!DefaultValue.IsEmpty())
		{
			if (InPin->GetCPPType() == TEXT("FVector"))
			{
				ControlElement->Settings.ControlType = ERigControlType::Position;
				FRigControlValue Value;
				Value.SetFromString<FVector>(DefaultValue);
				DynamicHierarchy->SetControlValue(ControlElement, Value, ERigControlValueType::Current, false);
			}
			else if (InPin->GetCPPType() == TEXT("FQuat"))
			{
				ControlElement->Settings.ControlType = ERigControlType::Rotator;
				FRigControlValue Value;
				Value.SetFromString<FRotator>(DefaultValue);
				DynamicHierarchy->SetControlValue(ControlElement, Value, ERigControlValueType::Current, false);
			}
			else
			{
				ControlElement->Settings.ControlType = ERigControlType::Transform;
				FRigControlValue Value;
				Value.SetFromString<FTransform>(DefaultValue);
				DynamicHierarchy->SetControlValue(ControlElement, Value, ERigControlValueType::Current, false);
			}
		}
		return true;
	}
	return false;
}

FName UControlRig::RemoveTransientControl(URigVMPin* InPin)
{
	if ((InPin == nullptr) || (DynamicHierarchy == nullptr))
	{
		return NAME_None;
	}

	URigHierarchyController* Controller = DynamicHierarchy->GetController(true);
	if(Controller == nullptr)
	{
		return NAME_None;
	}

	const FName ControlName = GetNameForTransientControl(InPin);
	if(FRigControlElement* ControlElement = FindControl(ControlName))
	{
		DynamicHierarchy->Notify(ERigHierarchyNotification::ElementDeselected, ControlElement);
		if(Controller->RemoveElement(ControlElement))
		{
			return ControlName;
		}
	}

	return NAME_None;
}

FName UControlRig::AddTransientControl(const FRigElementKey& InElement)
{
	if (!InElement.IsValid())
	{
		return NAME_None;
	}

	if(DynamicHierarchy == nullptr)
	{
		return NAME_None;
	}

	URigHierarchyController* Controller = DynamicHierarchy->GetController(true);
	if(Controller == nullptr)
	{
		return NAME_None;
	}

	const FName ControlName = GetNameForTransientControl(InElement);
	if(DynamicHierarchy->Contains(FRigElementKey(ControlName, ERigElementType::Control)))
	{
		SetTransientControlValue(InElement);
		return ControlName;
	}

	const int32 ElementIndex = DynamicHierarchy->GetIndex(InElement);
	if (ElementIndex == INDEX_NONE)
	{
		return NAME_None;
	}

	FTransform GizmoTransform = FTransform::Identity;
	GizmoTransform.SetScale3D(FVector::ZeroVector);

	FRigControlSettings Settings;
	Settings.bIsTransientControl = true;
	Settings.DisplayName = TEXT("Temporary Control");

	FRigElementKey Parent;
	switch (InElement.Type)
	{
		case ERigElementType::Bone:
		{
			Parent = DynamicHierarchy->GetFirstParent(InElement);
			break;
		}
		case ERigElementType::Null:
		{
			Parent = InElement;
			break;
		}
		default:
		{
			break;
		}
	}

	TArray<FRigElementKey> SelectedControls = DynamicHierarchy->GetSelectedKeys(ERigElementType::Control);
	for(const FRigElementKey& SelectedControl : SelectedControls)
	{
		Controller->DeselectElement(SelectedControl);
	}

	const FRigElementKey ControlKey = Controller->AddControl(
        ControlName,
        Parent,
        Settings,
        FRigControlValue::Make(FTransform::Identity),
        FTransform::Identity,
        GizmoTransform, false);

	if (InElement.Type == ERigElementType::Bone)
	{
		// don't allow transient control to modify forward mode poses when we
		// already switched to the setup mode
		if (!IsSetupModeEnabled())
		{
			if(FRigBoneElement* BoneElement = DynamicHierarchy->Find<FRigBoneElement>(InElement))
			{
				// add a modify bone AnimNode internally that the transient control controls for imported bones only
				// for user created bones, refer to UControlRig::TransformOverrideForUserCreatedBones 
				if (BoneElement->BoneType == ERigBoneType::Imported)
				{ 
					if (PreviewInstance)
					{
						PreviewInstance->ModifyBone(InElement.Name);
					}
				}
				else if (BoneElement->BoneType == ERigBoneType::User)
				{
					// add an empty entry, which will be given the correct value in
					// SetTransientControlValue(InElement);
					TransformOverrideForUserCreatedBones.FindOrAdd(InElement.Name);
				}
			}
		}
	}

	SetTransientControlValue(InElement);

	if(const FRigBaseElement* Element = DynamicHierarchy->Find(ControlKey))
	{
		DynamicHierarchy->Notify(ERigHierarchyNotification::ElementSelected, Element);
	}

	return ControlName;
}

bool UControlRig::SetTransientControlValue(const FRigElementKey& InElement)
{
	if (!InElement.IsValid())
	{
		return false;
	}

	if(DynamicHierarchy == nullptr)
	{
		return false;
	}

	const FName ControlName = GetNameForTransientControl(InElement);
	if(FRigControlElement* ControlElement = FindControl(ControlName))
	{
		if (InElement.Type == ERigElementType::Bone)
		{
			if (IsSetupModeEnabled())
			{
				// need to get initial because that is what setup mode uses
				// specifically, when user change the initial from the details panel
				// this will allow the transient control to react to that change
				const FTransform InitialLocalTransform = DynamicHierarchy->GetInitialLocalTransform(InElement);
				DynamicHierarchy->SetTransform(ControlElement, InitialLocalTransform, ERigTransformType::InitialLocal, true, false);
				DynamicHierarchy->SetTransform(ControlElement, InitialLocalTransform, ERigTransformType::CurrentLocal, true, false);
			}
			else
			{
				const FTransform LocalTransform = DynamicHierarchy->GetLocalTransform(InElement);
				DynamicHierarchy->SetTransform(ControlElement, LocalTransform, ERigTransformType::InitialLocal, true, false);
				DynamicHierarchy->SetTransform(ControlElement, LocalTransform, ERigTransformType::CurrentLocal, true, false);

				if (FRigBoneElement* BoneElement = DynamicHierarchy->Find<FRigBoneElement>(InElement))
				{
					if (BoneElement->BoneType == ERigBoneType::Imported)
					{
						if (PreviewInstance)
						{
							if (FAnimNode_ModifyBone* Modify = PreviewInstance->FindModifiedBone(InElement.Name))
							{
								Modify->Translation = LocalTransform.GetTranslation();
								Modify->Rotation = LocalTransform.GetRotation().Rotator();
								Modify->TranslationSpace = EBoneControlSpace::BCS_ParentBoneSpace;
								Modify->RotationSpace = EBoneControlSpace::BCS_ParentBoneSpace;
							}
						}	
					}
					else if (BoneElement->BoneType == ERigBoneType::User)
					{
						if (FTransform* TransformOverride = TransformOverrideForUserCreatedBones.Find(InElement.Name))
						{
							*TransformOverride = LocalTransform;
						}	
					}
				}
			}
		}
		else if (InElement.Type == ERigElementType::Null)
		{
			const FTransform GlobalTransform = DynamicHierarchy->GetGlobalTransform(InElement);
			DynamicHierarchy->SetTransform(ControlElement, GlobalTransform, ERigTransformType::InitialGlobal, true, false);
			DynamicHierarchy->SetTransform(ControlElement, GlobalTransform, ERigTransformType::CurrentGlobal, true, false);
		}

		return true;
	}
	return false;
}

FName UControlRig::RemoveTransientControl(const FRigElementKey& InElement)
{
	if (!InElement.IsValid())
	{
		return NAME_None;
	}

	if(DynamicHierarchy == nullptr)
	{
		return NAME_None;
	}

	URigHierarchyController* Controller = DynamicHierarchy->GetController(true);
	if(Controller == nullptr)
	{
		return NAME_None;
	}

	const FName ControlName = GetNameForTransientControl(InElement);
	if(FRigControlElement* ControlElement = FindControl(ControlName))
	{
		DynamicHierarchy->Notify(ERigHierarchyNotification::ElementDeselected, ControlElement);
		if(Controller->RemoveElement(ControlElement))
		{
			return ControlName;
		}
	}

	return NAME_None;
}

FName UControlRig::GetNameForTransientControl(URigVMPin* InPin) const
{
	check(InPin);
	check(DynamicHierarchy);
	
	const FString OriginalPinPath = InPin->GetOriginalPinFromInjectedNode()->GetPinPath();
	return DynamicHierarchy->GetSanitizedName(FString::Printf(TEXT("ControlForPin_%s"), *OriginalPinPath));
}

FString UControlRig::GetPinNameFromTransientControl(const FRigElementKey& InKey)
{
	FString Name = InKey.Name.ToString();
	if(Name.StartsWith(TEXT("ControlForPin_")))
	{
		Name.RightChopInline(14);
	}
	return Name;
}

FName UControlRig::GetNameForTransientControl(const FRigElementKey& InElement)
{
	if (InElement.Type == ERigElementType::Control)
	{
		return InElement.Name;
	}

	const FName EnumName = *StaticEnum<ERigElementType>()->GetDisplayNameTextByValue((int64)InElement.Type).ToString();
	return *FString::Printf(TEXT("ControlForRigElement_%s_%s"), *EnumName.ToString(), *InElement.Name.ToString());
}

FRigElementKey UControlRig::GetElementKeyFromTransientControl(const FRigElementKey& InKey)
{
	if(InKey.Type != ERigElementType::Control)
	{
		return FRigElementKey();
	}
	
	static FString ControlRigForElementBoneName;
	static FString ControlRigForElementNullName;

	if (ControlRigForElementBoneName.IsEmpty())
	{
		ControlRigForElementBoneName = FString::Printf(TEXT("ControlForRigElement_%s_"),
            *StaticEnum<ERigElementType>()->GetDisplayNameTextByValue((int64)ERigElementType::Bone).ToString());
		ControlRigForElementNullName = FString::Printf(TEXT("ControlForRigElement_%s_"),
            *StaticEnum<ERigElementType>()->GetDisplayNameTextByValue((int64)ERigElementType::Null).ToString());
	}
	
	FString Name = InKey.Name.ToString();
	if(Name.StartsWith(ControlRigForElementBoneName))
	{
		Name.RightChopInline(ControlRigForElementBoneName.Len());
		return FRigElementKey(*Name, ERigElementType::Bone);
	}
	if(Name.StartsWith(ControlRigForElementNullName))
	{
		Name.RightChopInline(ControlRigForElementNullName.Len());
		return FRigElementKey(*Name, ERigElementType::Null);
	}
	
	return FRigElementKey();;
}

void UControlRig::ClearTransientControls()
{
	if(DynamicHierarchy == nullptr)
	{
		return;
	}

	URigHierarchyController* Controller = DynamicHierarchy->GetController(true);
	if(Controller == nullptr)
	{
		return;
	}

	const TArray<FRigControlElement*> ControlsToRemove = DynamicHierarchy->GetTransientControls();
	for (FRigControlElement* ControlToRemove : ControlsToRemove)
	{
		DynamicHierarchy->Notify(ERigHierarchyNotification::ElementDeselected, ControlToRemove);
		Controller->RemoveElement(ControlToRemove);
	}
}

void UControlRig::ApplyTransformOverrideForUserCreatedBones()
{
	if(DynamicHierarchy == nullptr)
	{
		return;
	}
	
	for (const auto& Entry : TransformOverrideForUserCreatedBones)
	{
		DynamicHierarchy->SetLocalTransform(FRigElementKey(Entry.Key, ERigElementType::Bone), Entry.Value, false);
	}
}

#endif

void UControlRig::HandleHierarchyEvent(URigHierarchy* InHierarchy, const FRigEventContext& InEvent)
{
	if (RigEventDelegate.IsBound())
	{
		RigEventDelegate.Broadcast(InHierarchy, InEvent);
	}

	switch (InEvent.Event)
	{
		case ERigEvent::RequestAutoKey:
		{
			int32 Index = InHierarchy->GetIndex(InEvent.Key);
			if (Index != INDEX_NONE && InEvent.Key.Type == ERigElementType::Control)
			{
				if(FRigControlElement* ControlElement = InHierarchy->GetChecked<FRigControlElement>(Index))
				{
					FRigControlModifiedContext Context;
					Context.SetKey = EControlRigSetKey::Always;
					Context.LocalTime = InEvent.LocalTime;
					Context.EventName = InEvent.SourceEventName;
					ControlModified().Broadcast(this, ControlElement, Context);
				}
			}
		}
		default:
		{
			break;
		}
	}
}

void UControlRig::GetControlsInOrder(TArray<FRigControlElement*>& SortedControls) const
{
	SortedControls.Reset();

	if(DynamicHierarchy == nullptr)
	{
		return;
	}

	SortedControls = DynamicHierarchy->GetControls(true);
}

const FRigInfluenceMap* UControlRig::FindInfluenceMap(const FName& InEventName)
{
	if (UControlRig* CDO = GetClass()->GetDefaultObject<UControlRig>())
	{
		return CDO->Influences.Find(InEventName);
	}
	return nullptr;
}

void UControlRig::SetInteractionRig(UControlRig* InInteractionRig)
{
	if (InteractionRig == InInteractionRig)
	{
		return;
	}

	if (InteractionRig != nullptr)
	{
		InteractionRig->ControlModified().RemoveAll(this);
		InteractionRig->OnInitialized_AnyThread().RemoveAll(this);
		InteractionRig->OnExecuted_AnyThread().RemoveAll(this);
		InteractionRig->ControlSelected().RemoveAll(this);
		OnInitialized_AnyThread().RemoveAll(InteractionRig);
		OnExecuted_AnyThread().RemoveAll(InteractionRig);
		ControlSelected().RemoveAll(InteractionRig);
	}

	InteractionRig = InInteractionRig;

	if (InteractionRig != nullptr)
	{
		SetInteractionRigClass(InteractionRig->GetClass());

		InteractionRig->Initialize(true);
		InteractionRig->CopyPoseFromOtherRig(this);
		InteractionRig->RequestSetup();
		InteractionRig->Execute(EControlRigState::Update, FRigUnit_BeginExecution::EventName);

		InteractionRig->ControlModified().AddUObject(this, &UControlRig::HandleInteractionRigControlModified);
		InteractionRig->OnInitialized_AnyThread().AddUObject(this, &UControlRig::HandleInteractionRigInitialized);
		InteractionRig->OnExecuted_AnyThread().AddUObject(this, &UControlRig::HandleInteractionRigExecuted);
		InteractionRig->ControlSelected().AddUObject(this, &UControlRig::HandleInteractionRigControlSelected, false);
		OnInitialized_AnyThread().AddUObject(InteractionRig, &UControlRig::HandleInteractionRigInitialized);
		OnExecuted_AnyThread().AddUObject(InteractionRig, &UControlRig::HandleInteractionRigExecuted);
		ControlSelected().AddUObject(InteractionRig, &UControlRig::HandleInteractionRigControlSelected, true);

		FControlRigBracketScope BracketScope(InterRigSyncBracket);
		InteractionRig->HandleInteractionRigExecuted(this, EControlRigState::Update, FRigUnit_BeginExecution::EventName);
	}
}

void UControlRig::SetInteractionRigClass(TSubclassOf<UControlRig> InInteractionRigClass)
{
	if (InteractionRigClass == InInteractionRigClass)
	{
		return;
	}

	InteractionRigClass = InInteractionRigClass;

	if(InteractionRigClass)
	{
		if(InteractionRig != nullptr)
		{
			if(InteractionRig->GetClass() != InInteractionRigClass)
			{
				SetInteractionRig(nullptr);
			}
		}

		if(InteractionRig == nullptr)
		{
			UControlRig* NewInteractionRig = NewObject<UControlRig>(this, InteractionRigClass);
			SetInteractionRig(NewInteractionRig);
		}
	}
}

#if WITH_EDITOR

void UControlRig::PreEditChange(FProperty* PropertyAboutToChange)
{
	Super::PreEditChange(PropertyAboutToChange);

	if (PropertyAboutToChange && PropertyAboutToChange->GetFName() == GET_MEMBER_NAME_CHECKED(UControlRig, InteractionRig))
	{
		SetInteractionRig(nullptr);
	}
	else if (PropertyAboutToChange && PropertyAboutToChange->GetFName() == GET_MEMBER_NAME_CHECKED(UControlRig, InteractionRigClass))
	{
		SetInteractionRigClass(nullptr);
	}
}

void UControlRig::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (PropertyChangedEvent.MemberProperty && PropertyChangedEvent.MemberProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UControlRig, InteractionRig))
	{
		UControlRig* NewInteractionRig = InteractionRig;
		SetInteractionRig(nullptr);
		SetInteractionRig(NewInteractionRig);
	}
	else if (PropertyChangedEvent.MemberProperty && PropertyChangedEvent.MemberProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UControlRig, InteractionRigClass))
	{
		TSubclassOf<UControlRig> NewInteractionRigClass = InteractionRigClass;
		SetInteractionRigClass(nullptr);
		SetInteractionRigClass(NewInteractionRigClass);
		if (NewInteractionRigClass == nullptr)
		{
			SetInteractionRig(nullptr);
		}
	}
}
#endif

void UControlRig::AddAssetUserData(UAssetUserData* InUserData)
{
	if (InUserData != NULL)
	{
		UAssetUserData* ExistingData = GetAssetUserDataOfClass(InUserData->GetClass());
		if (ExistingData != NULL)
		{
			AssetUserData.Remove(ExistingData);
		}
		AssetUserData.Add(InUserData);
	}
}

UAssetUserData* UControlRig::GetAssetUserDataOfClass(TSubclassOf<UAssetUserData> InUserDataClass)
{
	for (int32 DataIdx = 0; DataIdx < AssetUserData.Num(); DataIdx++)
	{
		UAssetUserData* Datum = AssetUserData[DataIdx];
		if (Datum != NULL && Datum->IsA(InUserDataClass))
		{
			return Datum;
		}
	}
	return NULL;
}

void UControlRig::RemoveUserDataOfClass(TSubclassOf<UAssetUserData> InUserDataClass)
{
	for (int32 DataIdx = 0; DataIdx < AssetUserData.Num(); DataIdx++)
	{
		UAssetUserData* Datum = AssetUserData[DataIdx];
		if (Datum != NULL && Datum->IsA(InUserDataClass))
		{
			AssetUserData.RemoveAt(DataIdx);
			return;
		}
	}
}

const TArray<UAssetUserData*>* UControlRig::GetAssetUserDataArray() const
{
	return &AssetUserData;
}

void UControlRig::CopyPoseFromOtherRig(UControlRig* Subject)
{
	check(DynamicHierarchy);
	check(Subject);
	URigHierarchy* OtherHierarchy = Subject->GetHierarchy();
	check(OtherHierarchy);

	for (FRigBaseElement* Element : *DynamicHierarchy)
	{
		FRigBaseElement* OtherElement = OtherHierarchy->Find(Element->GetKey());
		if(OtherElement == nullptr)
		{
			continue;
		}

		if(OtherElement->GetType() != Element->GetType())
		{
			continue;
		}

		if(FRigBoneElement* BoneElement = Cast<FRigBoneElement>(Element))
		{
			FRigBoneElement* OtherBoneElement = CastChecked<FRigBoneElement>(OtherElement);
			const FTransform Transform = OtherHierarchy->GetTransform(OtherBoneElement, ERigTransformType::CurrentLocal);
			DynamicHierarchy->SetTransform(BoneElement, Transform, ERigTransformType::CurrentLocal, true, false);
		}
		else if(FRigCurveElement* CurveElement = Cast<FRigCurveElement>(Element))
		{
			FRigCurveElement* OtherCurveElement = CastChecked<FRigCurveElement>(OtherElement);
			const float Value = OtherHierarchy->GetCurveValue(OtherCurveElement);
			DynamicHierarchy->SetCurveValue(CurveElement, Value, false);
		}
	}
}

void UControlRig::HandleInteractionRigControlModified(UControlRig* Subject, FRigControlElement* Control, const FRigControlModifiedContext& Context)
{
	check(Subject);

	if (IsSyncingWithOtherRig() || IsExecuting())
	{
		return;
	}
	FControlRigBracketScope BracketScope(InterRigSyncBracket);

	if (Subject != InteractionRig)
	{
		return;
	}

	if (const FRigInfluenceMap* InfluenceMap = Subject->FindInfluenceMap(Context.EventName))
	{
		if (const FRigInfluenceEntry* InfluenceEntry = InfluenceMap->Find(Control->GetKey()))
		{
			for (const FRigElementKey& AffectedKey : *InfluenceEntry)
			{
				if (AffectedKey.Type == ERigElementType::Control)
				{
					if (FRigControlElement* AffectedControl = FindControl(AffectedKey.Name))
					{
						QueuedModifiedControls.Add(AffectedControl->GetKey());
					}
				}
				else if (
					AffectedKey.Type == ERigElementType::Bone ||
					AffectedKey.Type == ERigElementType::Curve)
				{
					// special case controls with a CONTROL suffix
					FName BoneControlName = *FString::Printf(TEXT("%s_CONTROL"), *AffectedKey.Name.ToString());
					if (FRigControlElement* AffectedControl = FindControl(BoneControlName))
					{
						QueuedModifiedControls.Add(AffectedControl->GetKey());
					}
				}
			}
		}
	}

}

void UControlRig::HandleInteractionRigInitialized(UControlRig* Subject, EControlRigState State, const FName& EventName)
{
	check(Subject);

	if (IsSyncingWithOtherRig())
	{
		return;
	}
	FControlRigBracketScope BracketScope(InterRigSyncBracket);
	RequestInit();
}

void UControlRig::HandleInteractionRigExecuted(UControlRig* Subject, EControlRigState State, const FName& EventName)
{
	check(Subject);

	if (IsSyncingWithOtherRig() || IsExecuting())
	{
		return;
	}
	FControlRigBracketScope BracketScope(InterRigSyncBracket);

	CopyPoseFromOtherRig(Subject);
	Execute(EControlRigState::Update, FRigUnit_InverseExecution::EventName);

	FRigControlModifiedContext Context;
	Context.EventName = FRigUnit_InverseExecution::EventName;
	Context.SetKey = EControlRigSetKey::DoNotCare;

	for (const FRigElementKey& QueuedModifiedControl : QueuedModifiedControls)
	{
		if(FRigControlElement* ControlElement = FindControl(QueuedModifiedControl.Name))
		{
			ControlModified().Broadcast(this, ControlElement, Context);
		}
	}
}

void UControlRig::HandleInteractionRigControlSelected(UControlRig* Subject, FRigControlElement* Control, bool bSelected, bool bInverted)
{
	check(Subject);

	if (IsSyncingWithOtherRig() || IsExecuting())
	{
		return;
	}
	if (Subject->IsSyncingWithOtherRig() || Subject->IsExecuting())
	{
		return;
	}
	FControlRigBracketScope BracketScope(InterRigSyncBracket);

	const FRigInfluenceMap* InfluenceMap = nullptr;
	if (bInverted)
	{
		InfluenceMap = FindInfluenceMap(FRigUnit_BeginExecution::EventName);
	}
	else
	{
		InfluenceMap = Subject->FindInfluenceMap(FRigUnit_BeginExecution::EventName);
	}

	if (InfluenceMap)
	{
		FRigInfluenceMap InvertedMap;
		if (bInverted)
		{
			InvertedMap = InfluenceMap->Inverse();
			InfluenceMap = &InvertedMap;
		}

		struct Local
		{
			static void SelectAffectedElements(UControlRig* ThisRig, const FRigInfluenceMap* InfluenceMap, const FRigElementKey& InKey, bool bSelected, bool bInverted)
			{
				if (const FRigInfluenceEntry* InfluenceEntry = InfluenceMap->Find(InKey))
				{
					for (const FRigElementKey& AffectedKey : *InfluenceEntry)
					{
						if (AffectedKey.Type == ERigElementType::Control)
						{
							ThisRig->SelectControl(AffectedKey.Name, bSelected);
						}

						if (bInverted)
						{
							if (AffectedKey.Type == ERigElementType::Control)
							{
								ThisRig->SelectControl(AffectedKey.Name, bSelected);
							}
						}
						else
						{
							if (AffectedKey.Type == ERigElementType::Control)
							{
								ThisRig->SelectControl(AffectedKey.Name, bSelected);
							}
							else if (AffectedKey.Type == ERigElementType::Bone ||
								AffectedKey.Type == ERigElementType::Curve)
							{
								FName ControlName = *FString::Printf(TEXT("%s_CONTROL"), *AffectedKey.Name.ToString());
								ThisRig->SelectControl(ControlName, bSelected);
							}
						}
					}
				}
			}
		};

		Local::SelectAffectedElements(this, InfluenceMap, Control->GetKey(), bSelected, bInverted);

		if (bInverted)
		{
			const FString ControlName = Control->GetName().ToString();
			if (ControlName.EndsWith(TEXT("_CONTROL")))
			{
				const FString BaseName = ControlName.Left(ControlName.Len() - 8);
				Local::SelectAffectedElements(this, InfluenceMap, FRigElementKey(*BaseName, ERigElementType::Bone), bSelected, bInverted);
				Local::SelectAffectedElements(this, InfluenceMap, FRigElementKey(*BaseName, ERigElementType::Curve), bSelected, bInverted);
			}
		}
	}
}

#if WITH_EDITOR

FEdGraphPinType UControlRig::GetPinTypeFromExternalVariable(const FRigVMExternalVariable& InExternalVariable)
{
	FEdGraphPinType PinType;
	PinType.ResetToDefaults();
	PinType.PinCategory = NAME_None;

	if (InExternalVariable.TypeName == TEXT("bool"))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Boolean;
	}
	else if (InExternalVariable.TypeName == TEXT("int32"))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Int;
	}
	else if (InExternalVariable.TypeName == TEXT("float"))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Float;
	}
	else if (InExternalVariable.TypeName == TEXT("FName"))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Name;
	}
	else if (InExternalVariable.TypeName == TEXT("FString"))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_String;
	}
	else if (Cast<UScriptStruct>(InExternalVariable.TypeObject))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
		PinType.PinSubCategoryObject = InExternalVariable.TypeObject;
	}
	else if (Cast<UEnum>(InExternalVariable.TypeObject))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Byte;
		PinType.PinSubCategoryObject = InExternalVariable.TypeObject;
	}

	if (InExternalVariable.bIsArray)
	{
		PinType.ContainerType = EPinContainerType::Array;
	}
	else
	{
		PinType.ContainerType = EPinContainerType::None;
	}

	return PinType;
}

FRigVMExternalVariable UControlRig::GetExternalVariableFromPinType(const FName& InName, const FEdGraphPinType& InPinType, bool bInPublic, bool bInReadonly)
{
	FRigVMExternalVariable ExternalVariable;
	ExternalVariable.Name = InName;
	ExternalVariable.bIsPublic = bInPublic;
	ExternalVariable.bIsReadOnly = bInReadonly;

	if (InPinType.ContainerType == EPinContainerType::None)
	{
		ExternalVariable.bIsArray = false;
	}
	else if (InPinType.ContainerType == EPinContainerType::Array)
	{
		ExternalVariable.bIsArray = true;
	}
	else
	{
		return FRigVMExternalVariable();
	}

	if (InPinType.PinCategory == UEdGraphSchema_K2::PC_Boolean)
	{
		ExternalVariable.TypeName = TEXT("bool");
		ExternalVariable.Size = sizeof(bool);
	}
	else if (InPinType.PinCategory == UEdGraphSchema_K2::PC_Int)
	{
		ExternalVariable.TypeName = TEXT("int32");
		ExternalVariable.Size = sizeof(int32);
	}
	else if (InPinType.PinCategory == UEdGraphSchema_K2::PC_Enum ||
		InPinType.PinCategory == UEdGraphSchema_K2::PC_Byte)
	{
		if (UEnum* Enum = Cast<UEnum>(InPinType.PinSubCategoryObject))
		{
			ExternalVariable.TypeName = Enum->GetFName();
			ExternalVariable.TypeObject = Enum;
		}
		else
		{
			ExternalVariable.TypeName = TEXT("uint8");
		}
		ExternalVariable.Size = sizeof(uint8);
	}
	else if (InPinType.PinCategory == UEdGraphSchema_K2::PC_Float)
	{
		ExternalVariable.TypeName = TEXT("float");
		ExternalVariable.Size = sizeof(float);
	}
	else if (InPinType.PinCategory == UEdGraphSchema_K2::PC_Name)
	{
		ExternalVariable.TypeName = TEXT("FName");
		ExternalVariable.Size = sizeof(FName);
	}
	else if (InPinType.PinCategory == UEdGraphSchema_K2::PC_String)
	{
		ExternalVariable.TypeName = TEXT("FString");
		ExternalVariable.Size = sizeof(FString);
	}
	else if (InPinType.PinCategory == UEdGraphSchema_K2::PC_Struct)
	{
		if (UScriptStruct* Struct = Cast<UScriptStruct>(InPinType.PinSubCategoryObject))
		{
			ExternalVariable.TypeName = *Struct->GetStructCPPName();
			ExternalVariable.TypeObject = Struct;
			ExternalVariable.Size = Struct->GetStructureSize();
		}
	}

	return ExternalVariable;
}

FRigVMExternalVariable UControlRig::GetExternalVariableFromDescription(const FBPVariableDescription& InVariableDescription)
{
	const bool bIsPublic = !((InVariableDescription.PropertyFlags & CPF_DisableEditOnInstance) == CPF_DisableEditOnInstance);
	const bool bIsReadOnly = ((InVariableDescription.PropertyFlags & CPF_BlueprintReadOnly) == CPF_BlueprintReadOnly);
	return GetExternalVariableFromPinType(InVariableDescription.VarName, InVariableDescription.VarType, bIsPublic, bIsReadOnly);
}

URigVM* UControlRig::GetSnapshotVM(bool bCreateIfNeeded)
{
#if WITH_EDITOR
	if ((VMSnapshotBeforeExecution == nullptr) && bCreateIfNeeded)
	{
		VMSnapshotBeforeExecution = NewObject<URigVM>(GetTransientPackage(), NAME_None, RF_Transient);
	}
	return VMSnapshotBeforeExecution;
#else
	return nullptr;
#endif
}

void UControlRig::AddBreakpoint(int32 InstructionIndex, URigVMNode* InNode)
{
	DebugInfo.AddBreakpoint(InstructionIndex, InNode);
}

void UControlRig::ResumeExecution()
{
	// this makes sure that the snapshot exists
	if(URigVM* SnapShotVM = GetSnapshotVM())
	{
		VM->CopyFrom(SnapShotVM, false, false, false, true, true);
	}
	VM->ResumeExecution();
}

#endif // WITH_EDITOR

void UControlRig::SetBoneInitialTransformsFromSkeletalMeshComponent(USkeletalMeshComponent* InSkelMeshComp)
{
	check(InSkelMeshComp);
	check(DynamicHierarchy);
	if (InSkelMeshComp->GetAnimInstance() == nullptr)
	{
		SetBoneInitialTransformsFromSkeletalMesh(InSkelMeshComp->SkeletalMesh);
		return;
	}
	else
	{
		FMemMark Mark(FMemStack::Get());
		FCompactPose OutPose;
		OutPose.ResetToRefPose(InSkelMeshComp->GetAnimInstance()->GetRequiredBones());
		if(!OutPose.GetBoneContainer().IsValid())
		{
			return;
		}

		DynamicHierarchy->ForEach<FRigBoneElement>([this, &OutPose, InSkelMeshComp](FRigBoneElement* BoneElement) -> bool
			{
				if (BoneElement->BoneType == ERigBoneType::Imported)
				{
					int32 MeshIndex = OutPose.GetBoneContainer().GetPoseBoneIndexForBoneName(BoneElement->GetName());
					if (MeshIndex != INDEX_NONE)
					{
						FCompactPoseBoneIndex CPIndex = OutPose.GetBoneContainer().MakeCompactPoseIndex(FMeshPoseBoneIndex(MeshIndex));
						if (CPIndex != INDEX_NONE)
						{
							FTransform LocalInitialTransform = OutPose.GetRefPose(CPIndex);
							DynamicHierarchy->SetTransform(BoneElement, LocalInitialTransform, ERigTransformType::InitialLocal, true, false);
						}
					}
				}
				return true;
			});
		bResetInitialTransformsBeforeSetup = false;
	}
}

void UControlRig::SetBoneInitialTransformsFromSkeletalMesh(USkeletalMesh* InSkeletalMesh)
{
	check(InSkeletalMesh);
	SetBoneInitialTransformsFromRefSkeleton(InSkeletalMesh->GetRefSkeleton());
}

void UControlRig::SetBoneInitialTransformsFromRefSkeleton(const FReferenceSkeleton& InReferenceSkeleton)
{
	check(DynamicHierarchy);

	DynamicHierarchy->ForEach<FRigBoneElement>([this, InReferenceSkeleton](FRigBoneElement* BoneElement) -> bool
	{
		if(BoneElement->BoneType == ERigBoneType::Imported)
		{
			const int32 BoneIndex = InReferenceSkeleton.FindBoneIndex(BoneElement->GetName());
			if (BoneIndex != INDEX_NONE)
			{
				const FTransform LocalInitialTransform = InReferenceSkeleton.GetRefBonePose()[BoneIndex];
				DynamicHierarchy->SetTransform(BoneElement, LocalInitialTransform, ERigTransformType::InitialLocal, true, false);
			}
		}
		return true;
	});
	bResetInitialTransformsBeforeSetup = false;
}

void UControlRig::OnHierarchyTransformUndoRedo(URigHierarchy* InHierarchy, const FRigElementKey& InKey, ERigTransformType::Type InTransformType, const FTransform& InTransform, bool bIsUndo)
{
	if(InKey.Type == ERigElementType::Control)
	{
		if(FRigControlElement* ControlElement = InHierarchy->Find<FRigControlElement>(InKey))
		{
			ControlModified().Broadcast(this, ControlElement, FRigControlModifiedContext(EControlRigSetKey::Never));
		}
	}
}

#undef LOCTEXT_NAMESPACE


