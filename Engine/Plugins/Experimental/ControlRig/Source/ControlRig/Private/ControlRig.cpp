// Copyright Epic Games, Inc. All Rights Reserved.

#include "ControlRig.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/Actor.h"
#include "Misc/RuntimeErrors.h"
#include "IControlRigObjectBinding.h"
#include "HelperUtil.h"
#include "ControlRigBlueprintGeneratedClass.h"
#include "ControlRigObjectVersion.h"
#include "Units/Execution/RigUnit_BeginExecution.h"
#include "Units/Execution/RigUnit_PrepareForExecution.h"
#include "Units/Execution/RigUnit_InverseExecution.h"
#if WITH_EDITOR
#include "ControlRigModule.h"
#include "Modules/ModuleManager.h"
#include "RigVMModel/RigVMNode.h"
#include "Engine/Blueprint.h"
#include "EdGraphSchema_K2.h"
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
	, bResetInitialTransformsBeforeSetup(true)
	, bManipulationEnabled(false)
	, InitBracket(0)
	, UpdateBracket(0)
	, PreSetupBracket(0)
	, PostSetupBracket(0)
	, InteractionBracket(0)
	, InterRigSyncBracket(0)
{
	VM = ObjectInitializer.CreateDefaultSubobject<URigVM>(this, TEXT("VM"));
	// create default source registry
	DataSourceRegistry = CreateDefaultSubobject<UAnimationDataSourceRegistry>(TEXT("DataSourceRegistry"));

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
	Hierarchy.Initialize();
	RequestSetup();

	if (bInitRigUnits)
	{
		RequestInit();
	}

	Hierarchy.ControlHierarchy.OnControlSelected.RemoveAll(this);
	Hierarchy.ControlHierarchy.OnControlSelected.AddUObject(this, &UControlRig::HandleOnControlSelected);
	Hierarchy.ControlHierarchy.OnControlUISettingsChanged.AddUObject(this, &UControlRig::HandleOnControlUISettingChanged);
	Hierarchy.OnEventReceived.RemoveAll(this);
	Hierarchy.OnEventReceived.AddUObject(this, &UControlRig::HandleOnRigEvent);
}

void UControlRig::InitializeFromCDO()
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	// copy CDO property you need to here
	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		UControlRig* CDO = GetClass()->GetDefaultObject<UControlRig>();

		// copy hierarchy
		Hierarchy = CDO->Hierarchy;
		Hierarchy.Initialize();

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
		VM = NewObject<URigVM>(this, TEXT("VM"));
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
	}

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
	Context.DrawContainer = &DrawContainer;

	if (InState == EControlRigState::Init)
	{
		Context.DataSourceRegistry = DataSourceRegistry;
		AbsoluteTime = DeltaTime = 0.f;
	}

	Context.DeltaTime = DeltaTime;
	Context.AbsoluteTime = AbsoluteTime;
	Context.FramesPerSecond = GetCurrentFramesPerSecond();
	Context.bDuringInteraction = IsInteracting();
	Context.State = InState;
	Context.Hierarchy = &Hierarchy;

	Context.ToWorldSpaceTransform = FTransform::Identity;
	Context.OwningComponent = nullptr;
	Context.OwningActor = nullptr;
	Context.World = nullptr;

	if (!OuterSceneComponent.IsValid())
	{
		USceneComponent* SceneComponentFromRegistry = DataSourceRegistry->RequestSource<USceneComponent>(UControlRig::OwnerComponent);
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

#if WITH_EDITOR
	Context.Log = ControlRigLog;
	if (ControlRigLog != nullptr)
	{
		ControlRigLog->Entries.Reset();
	}
#endif

	// execute units
	if (bRequiresSetupEvent && InState != EControlRigState::Init)
	{
		if(!IsRunningPreSetup() && !IsRunningPostSetup())
		{
			bRequiresSetupEvent = bSetupModeEnabled;

			if (bResetInitialTransformsBeforeSetup && !bSetupModeEnabled)
			{
				if (UControlRig* CDO = Cast<UControlRig>(GetClass()->GetDefaultObject()))
				{
					Hierarchy.CopyInitialTransforms(CDO->Hierarchy);
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
				Hierarchy.BoneHierarchy.ResetTransforms();
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
			ExecuteUnits(Context, InEventName);

			if (InState == EControlRigState::Init)
			{
				ExecuteUnits(Context, FRigUnit_BeginExecution::EventName);
			}

			if (InState != EControlRigState::Init)
			{
				if (bAccumulateTime)
				{
					AbsoluteTime += DeltaTime;
				}
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

		for (const FRigControl& Control : Hierarchy.ControlHierarchy)
		{
			if (Control.bGizmoEnabled && Control.bGizmoVisible && !Control.bIsTransientControl && Control.bDrawLimits && (Control.bLimitTranslation || Control.bLimitRotation || Control.bLimitScale))
			{
				// for now we don't draw rotational limits
				if(!Control.bLimitTranslation)
				{
					continue;
				}

				FTransform Transform = Hierarchy.ControlHierarchy.GetParentTransform(Control.Index);
				FControlRigDrawInstruction Instruction(EControlRigDrawSettings::Lines, Control.GizmoColor, 0.f, Transform);

				switch (Control.ControlType)
				{
					case ERigControlType::Float:
					{
						FVector MinPos = FVector::ZeroVector;
						FVector MaxPos = FVector::ZeroVector;

						switch (Control.PrimaryAxis)
						{
							case ERigControlAxis::X:
							{
								MinPos.X = Control.MinimumValue.Get<float>();
								MaxPos.X = Control.MaximumValue.Get<float>();
								break;
							}
							case ERigControlAxis::Y:
							{
								MinPos.Y = Control.MinimumValue.Get<float>();
								MaxPos.Y = Control.MaximumValue.Get<float>();
								break;
							}
							case ERigControlAxis::Z:
							{
								MinPos.Z = Control.MinimumValue.Get<float>();
								MaxPos.Z = Control.MaximumValue.Get<float>();
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

						switch (Control.PrimaryAxis)
						{
							case ERigControlAxis::X:
							{
								MinPos.X = (float)Control.MinimumValue.Get<int32>();
								MaxPos.X = (float)Control.MaximumValue.Get<int32>();
								break;
							}
							case ERigControlAxis::Y:
							{
								MinPos.Y = (float)Control.MinimumValue.Get<int32>();
								MaxPos.Y = (float)Control.MaximumValue.Get<int32>();
								break;
							}
							case ERigControlAxis::Z:
							{
								MinPos.Z = (float)Control.MinimumValue.Get<int32>();
								MaxPos.Z = (float)Control.MaximumValue.Get<int32>();
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
						FVector2D MinPos = Control.MinimumValue.Get<FVector2D>();
						FVector2D MaxPos = Control.MaximumValue.Get<FVector2D>();

						switch (Control.PrimaryAxis)
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

						switch (Control.ControlType)
						{
							case ERigControlType::Position:
							case ERigControlType::Scale:
							{
								MinPos = Control.MinimumValue.Get<FVector>();
								MaxPos = Control.MaximumValue.Get<FVector>();
								break;
							}
							case ERigControlType::Transform:
							{
								MinPos = Control.MinimumValue.Get<FTransform>().GetLocation();
								MaxPos = Control.MaximumValue.Get<FTransform>().GetLocation();
								break;
							}
							case ERigControlType::TransformNoScale:
							{
								MinPos = Control.MinimumValue.Get<FTransformNoScale>().Location;
								MaxPos = Control.MaximumValue.Get<FTransformNoScale>().Location;
								break;
							}
							case ERigControlType::EulerTransform:
							{
								MinPos = Control.MinimumValue.Get<FEulerTransform>().Location;
								MaxPos = Control.MaximumValue.Get<FEulerTransform>().Location;
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
		}
	}
}

void UControlRig::ExecuteUnits(FRigUnitContext& InOutContext, const FName& InEventName)
{
	if (VM)
	{
		FRigVMMemoryContainer* LocalMemory[] = { VM->WorkMemoryPtr, VM->LiteralMemoryPtr };
		TArray<void*> AdditionalArguments;
		AdditionalArguments.Add(&InOutContext);

		if (InOutContext.State == EControlRigState::Init)
		{
			VM->Initialize(FRigVMMemoryContainerPtrArray(LocalMemory, 2), AdditionalArguments);
		}
		else
		{
			VM->Execute(FRigVMMemoryContainerPtrArray(LocalMemory, 2), AdditionalArguments, InEventName);
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

URigVM* UControlRig::GetVM()
{
	if (VM == nullptr)
	{
		Initialize(true);
		check(VM);
	}
	return VM;
}

FTransform UControlRig::GetGlobalTransform(const FName& BoneName) const
{
	int32 Index = Hierarchy.BoneHierarchy.GetIndex(BoneName);
	if (Index != INDEX_NONE)
	{
		return Hierarchy.BoneHierarchy.GetGlobalTransform(Index);
	}

	return FTransform::Identity;

}

void UControlRig::SetGlobalTransform(const FName& BoneName, const FTransform& InTransform, bool bPropagateTransform)
{
	int32 Index = Hierarchy.BoneHierarchy.GetIndex(BoneName);
	if (Index != INDEX_NONE)
	{
		return Hierarchy.BoneHierarchy.SetGlobalTransform(Index, InTransform, bPropagateTransform);
	}
}

FTransform UControlRig::GetGlobalTransform(const int32 BoneIndex) const
{
	return Hierarchy.BoneHierarchy.GetGlobalTransform(BoneIndex);
}

void UControlRig::SetGlobalTransform(const int32 BoneIndex, const FTransform& InTransform, bool bPropagateTransform)
{
	Hierarchy.BoneHierarchy.SetGlobalTransform(BoneIndex, InTransform, bPropagateTransform);
}

float UControlRig::GetCurveValue(const FName& CurveName) const
{
	return Hierarchy.CurveContainer.GetValue(CurveName);
}

void UControlRig::SetCurveValue(const FName& CurveName, const float CurveValue)
{
	Hierarchy.CurveContainer.SetValue(CurveName, CurveValue);
}

float UControlRig::GetCurveValue(const int32 CurveIndex) const
{
	return Hierarchy.CurveContainer.GetValue(CurveIndex);
}

void UControlRig::SetCurveValue(const int32 CurveIndex, const float CurveValue)
{
	Hierarchy.CurveContainer.SetValue(CurveIndex, CurveValue);
}

void UControlRig::GetMappableNodeData(TArray<FName>& OutNames, TArray<FNodeItem>& OutNodeItems) const
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	OutNames.Reset();
	OutNodeItems.Reset();

	// now add all nodes
	const FRigBoneHierarchy& BoneHierarchy = Hierarchy.BoneHierarchy;

	for (const FRigBone& Bone : BoneHierarchy)
	{
		OutNames.Add(Bone.Name);
		OutNodeItems.Add(FNodeItem(Bone.ParentName, Bone.InitialTransform));
	}
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

	if (Ar.IsLoading())
	{
		Hierarchy.ControlHierarchy.PostLoad();
	}
}

const TArray<FRigSpace>& UControlRig::AvailableSpaces() const
{
	return Hierarchy.SpaceHierarchy.GetSpaces();
}

// do we need to return pointer? They can't save this
FRigSpace* UControlRig::FindSpace(const FName& InSpaceName)
{
	const int32 SpaceIndex = Hierarchy.SpaceHierarchy.GetIndex(InSpaceName);
	if (SpaceIndex != INDEX_NONE)
	{
		return &Hierarchy.SpaceHierarchy[SpaceIndex];
	}

	return nullptr;
}

FTransform UControlRig::GetSpaceGlobalTransform(const FName& InSpaceName)
{
	return Hierarchy.SpaceHierarchy.GetGlobalTransform(InSpaceName);
}

bool UControlRig::SetSpaceGlobalTransform(const FName& InSpaceName, const FTransform& InTransform)
{
	Hierarchy.SpaceHierarchy.SetGlobalTransform(InSpaceName, InTransform);
	return true;
}

const TArray<FRigControl>& UControlRig::AvailableControls() const
{
#if WITH_EDITOR
	if (AvailableControlsOverride.Num() > 0)
	{
		return AvailableControlsOverride;
	}
#endif
	return Hierarchy.ControlHierarchy.GetControls();
}

FRigControl* UControlRig::FindControl(const FName& InControlName)
{
	const int32 ControlIndex = Hierarchy.ControlHierarchy.GetIndex(InControlName);
	if (ControlIndex != INDEX_NONE)
	{
		return &Hierarchy.ControlHierarchy[ControlIndex];
	}

#if WITH_EDITOR
	if (TransientControls.Num() > 0)
	{
		for (int32 Index = 0; Index < TransientControls.Num(); Index++)
		{
			if (TransientControls[Index].Name == InControlName)
			{
				return &TransientControls[Index];
			}
		}
	}
#endif

	return nullptr;
}

FTransform UControlRig::SetupControlFromGlobalTransform(const FName& InControlName, const FTransform& InGlobalTransform)
{
	if (IsSetupModeEnabled())
	{
		FRigControl* Control = FindControl(InControlName);
		if (Control && !Control->bIsTransientControl)
		{
			FTransform ParentTransform = Hierarchy.ControlHierarchy.GetParentTransform(Control->Index, false);
			FTransform OffsetTransform = InGlobalTransform.GetRelativeTransform(ParentTransform);
			Hierarchy.ControlHierarchy.SetControlOffset(Control->Index, OffsetTransform);
		}
	}
	return InGlobalTransform;
}

void UControlRig::CreateRigControlsForCurveContainer()
{
	const bool bCreateFloatControls = CVarControlRigCreateFloatControlsForCurves->GetInt() == 0 ? false : true;
	if(bCreateFloatControls)
	{
		FRigCurveContainer& CurveContainer = GetCurveContainer();
		FString CTRLName(TEXT("CTRL_"));
		for (FRigCurve& Curve : CurveContainer)
		{
			FString Name = Curve.Name.ToString();
			if (Name.Contains(CTRLName) && Hierarchy.ControlHierarchy.GetIndex(Curve.Name) == INDEX_NONE)
			{
				FRigControlValue Value;
				Value.Set<float>(Curve.Value);
				FRigControl& Control = Hierarchy.ControlHierarchy.Add(Curve.Name, ERigControlType::Float, NAME_None, NAME_None, FTransform::Identity, Value
	#if WITH_EDITORONLY_DATA
					, Curve.Name,
					FTransform::Identity,
					FLinearColor::Red
	#endif
				);
				Control.bIsCurve = true;
			}
		}

		ControlModified().AddUObject(this, &UControlRig::HandleOnControlModified);
	}
}

void UControlRig::HandleOnControlModified(UControlRig* Subject, const FRigControl& Control, const FRigControlModifiedContext& Context)
{
	if (Control.bIsCurve)
	{
		SetCurveValue(Control.Name, Control.Value.Get<float>());
	}	
}

bool UControlRig::IsCurveControl(const FRigControl* InControl) const
{
	return InControl->bIsCurve;
}

FTransform UControlRig::GetControlGlobalTransform(const FName& InControlName) const
{
#if WITH_EDITOR
	if (TransientControls.Num() > 0)
	{
		for (int32 Index = 0; Index < TransientControls.Num(); Index++)
		{
			const FRigControl& Control = TransientControls[Index];
			if (Control.Name == InControlName)
			{
				FTransform ParentTransform = FTransform::Identity;
				if (Control.ParentIndex != INDEX_NONE)
				{
					ParentTransform = Hierarchy.BoneHierarchy.GetGlobalTransform(Control.ParentIndex);
				}
// 				else if (Control.SpaceIndex != INDEX_NONE)
// 				{
// 					ParentTransform = Hierarchy.SpaceHierarchy.GetGlobalTransform(Control.SpaceIndex);
// 				}

				FTransform Transform = Control.GetTransformFromValue(ERigControlValueType::Current);
				return Transform * Control.OffsetTransform * ParentTransform;
			}
		}
	}
#endif
	return Hierarchy.ControlHierarchy.GetGlobalTransform(InControlName);
}

bool UControlRig::SetControlGlobalTransform(const FName& InControlName, const FTransform& InGlobalTransform, const FRigControlModifiedContext& Context)
{
	FTransform GlobalTransform = InGlobalTransform;
	if (IsSetupModeEnabled())
	{
		GlobalTransform = SetupControlFromGlobalTransform(InControlName, GlobalTransform);
	}

	FRigControlValue Value = GetControlValueFromGlobalTransform(InControlName, GlobalTransform);
	if (OnFilterControl.IsBound())
	{
		FRigControl* Control = FindControl(InControlName);
		if (Control)
		{
			OnFilterControl.Broadcast(this, *Control, Value);
		}
	}

	SetControlValue(InControlName, Value, true /* notify */, Context);
	return true;
}

FRigControlValue UControlRig::GetControlValueFromGlobalTransform(const FName& InControlName, const FTransform& InGlobalTransform)
{
	FRigControlValue RetVal;

	if (FRigControl* Control = FindControl(InControlName))
	{
		// ParentTransform should include both the global transform of the parent rig element and the offset transform
		FTransform ParentTransform = FTransform::Identity;
		if ((Control->ParentIndex != INDEX_NONE || Control->SpaceIndex != INDEX_NONE) && Control->bIsTransientControl)
		{
			if (Control->ParentIndex != INDEX_NONE)
			{
				ParentTransform = Hierarchy.BoneHierarchy.GetGlobalTransform(Control->ParentIndex);
				ParentTransform = Control->OffsetTransform * ParentTransform;
			}
		}
		else if(Control->Index != INDEX_NONE)
		{
			// Offset transform is considered within GetParentTransform(index, true)
			ParentTransform = Hierarchy.ControlHierarchy.GetParentTransform(Control->Index, true);
		}

		FTransform Transform = InGlobalTransform.GetRelativeTransform(ParentTransform);

		FRigControlValue PreviousVal = Control->Value;
		Control->SetValueFromTransform(Transform, ERigControlValueType::Current);
		RetVal = Control->Value;
		Control->Value = PreviousVal;

		if (ShouldApplyLimits())
		{
			Control->ApplyLimits(RetVal);
		}
	}

	return RetVal;
}

void UControlRig::SetControlLocalTransform(const FName& InControlName, const FTransform& InLocalTransform, bool bNotify, const FRigControlModifiedContext& Context)
{

	if (FRigControl* Control = FindControl(InControlName))
	{
		FRigControlValue PreviousVal = Control->Value;
		Control->SetValueFromTransform(InLocalTransform, ERigControlValueType::Current);
		FRigControlValue ValueToSet = Control->Value;
		Control->Value = PreviousVal;
		if (OnFilterControl.IsBound())
		{
			OnFilterControl.Broadcast(this, *Control, ValueToSet);
			
		}
		SetControlValue(InControlName, ValueToSet, bNotify, Context);
	}

}

FTransform UControlRig::GetControlLocalTransform(const FName& InControlName)
{
	FRigControl* Control = FindControl(InControlName);
	if (Control->bIsTransientControl)
	{
		return Control->GetTransformFromValue(ERigControlValueType::Current);
	}
	return GetControlHierarchy().GetLocalTransform(InControlName);
}

bool UControlRig::SetControlSpace(const FName& InControlName, const FName& InSpaceName)
{
	Hierarchy.ControlHierarchy.SetSpace(InControlName, InSpaceName);
	return true;
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

void UControlRig::HandleOnControlUISettingChanged(FRigHierarchyContainer* Container, const FRigElementKey& InKey)
{
	if (InKey.Type == ERigElementType::Control)
	{
		if (FRigControl* Control = FindControl(InKey.Name))
		{
			ControlModified().Broadcast(this, *Control, FRigControlModifiedContext(EControlRigSetKey::Never));
		}
	}
}

void UControlRig::SelectControl(const FName& InControlName, bool bSelect)
{
	Hierarchy.ControlHierarchy.Select(InControlName, bSelect);
}

bool UControlRig::ClearControlSelection()
{
	return Hierarchy.ControlHierarchy.ClearSelection();
}

TArray<FName> UControlRig::CurrentControlSelection() const
{
	return Hierarchy.ControlHierarchy.CurrentSelection();
}

bool UControlRig::IsControlSelected(const FName& InControlName)const
{
	return Hierarchy.ControlHierarchy.IsSelected(InControlName);
}

void UControlRig::HandleOnControlSelected(FRigHierarchyContainer* InContainer, const FRigElementKey& InKey, bool bSelected)
{
	if (InKey.Type == ERigElementType::Control)
	{
		FRigControl* Control = FindControl(InKey.Name);
		if (Control)
		{
			ControlSelected().Broadcast(this, *Control, bSelected);
		}
	}
}

#if WITH_EDITOR

void UControlRig::UpdateAvailableControls()
{
	AvailableControlsOverride = Hierarchy.ControlHierarchy.GetControls();
	AvailableControlsOverride.Append(TransientControls);
}

FName UControlRig::AddTransientControl(URigVMPin* InPin, FRigElementKey SpaceKey, FTransform OffsetTransform)
{
	if (InPin == nullptr)
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

	FName ControlName = *InPin->GetPinPath();

	FRigControl NewControl;
	NewControl.Name = ControlName;
	NewControl.bIsTransientControl = true;
	NewControl.GizmoTransform.SetScale3D(FVector::ZeroVector);

	URigVMPin* PinForLink = InPin->GetPinForLink();
	if (URigVMPin* ColorPin = PinForLink->GetNode()->FindPin(TEXT("Color")))
	{
		if (ColorPin->GetCPPType() == TEXT("FLinearColor"))
		{
			FRigControlValue Value;
			NewControl.GizmoColor = Value.SetFromString<FLinearColor>(ColorPin->GetDefaultValue());
		}
	}

	if(SpaceKey.IsValid() && SpaceKey.Type == ERigElementType::Bone)
	{
		NewControl.ParentIndex = Hierarchy.BoneHierarchy.GetIndex(SpaceKey.Name);
		if (NewControl.ParentIndex != INDEX_NONE)
		{
			NewControl.ParentName = Hierarchy.BoneHierarchy[NewControl.ParentIndex].Name;
		}
	}

	NewControl.OffsetTransform = OffsetTransform; 
	
	TransientControls.Add(NewControl); 

	SetTransientControlValue(InPin);
	UpdateAvailableControls();

	return ControlName;
}

bool UControlRig::SetTransientControlValue(URigVMPin* InPin)
{
	FString PinPath = InPin->GetPinForLink()->GetPinPath();
	FString OriginalPinPath = InPin->GetOriginalPinFromInjectedNode()->GetPinPath();
	for (FRigControl& Control : TransientControls)
	{
		FString ControlName = Control.Name.ToString();
		if (ControlName == PinPath || ControlName == OriginalPinPath)
		{
			FString DefaultValue = InPin->GetPinForLink()->GetDefaultValue();
			if (!DefaultValue.IsEmpty())
			{
				if (InPin->GetCPPType() == TEXT("FVector"))
				{
					Control.ControlType = ERigControlType::Position;
					FVector Value = Control.Value.SetFromString<FVector>(DefaultValue);
					Control.InitialValue.Set<FVector>(Value);
				}
				else if (InPin->GetCPPType() == TEXT("FQuat"))
				{
					Control.ControlType = ERigControlType::Rotator;
					FQuat Value = Control.Value.SetFromString<FQuat>(DefaultValue);
					Control.Value.Set<FRotator>(Value.Rotator());
					Control.InitialValue.Set<FRotator>(Value.Rotator());
				}
				else
				{
					Control.ControlType = ERigControlType::Transform;
					FTransform Value = Control.Value.SetFromString<FTransform>(DefaultValue);
					Control.InitialValue.Set<FTransform>(Value);
				}
			}
			return true;
		}
	}
	return false;
}

FName UControlRig::RemoveTransientControl(URigVMPin* InPin)
{
	if (InPin == nullptr)
	{
		return NAME_None;
	}

	FName ControlName = *InPin->GetPinPath();
	for (int32 Index = 0; Index < TransientControls.Num(); Index++)
	{
		if (TransientControls[Index].Name == ControlName)
		{
			FRigControl ControlToRemove = TransientControls[Index];
			TransientControls.RemoveAt(Index);
			UpdateAvailableControls();
			return ControlToRemove.Name;
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

	RemoveTransientControl(InElement);

	int32 ElementIndex = Hierarchy.GetIndex(InElement);
	if (ElementIndex == INDEX_NONE)
	{
		return NAME_None;
	}

	FName ControlName = GetNameForTransientControl(InElement);

	FRigControl NewControl;
	NewControl.Name = ControlName;
	NewControl.bIsTransientControl = true;
	NewControl.GizmoTransform.SetScale3D(FVector::ZeroVector);

	switch (InElement.Type)
	{
		case ERigElementType::Bone:
		{
			NewControl.ParentIndex = Hierarchy.BoneHierarchy[ElementIndex].ParentIndex;
			NewControl.ParentName = Hierarchy.BoneHierarchy[ElementIndex].ParentName;
			break;
		}
		case ERigElementType::Space:
		{
			NewControl.SpaceIndex = ElementIndex;
			NewControl.SpaceName = InElement.Name;
			break;
		}
	}

	TransientControls.Add(NewControl);

	if (InElement.Type == ERigElementType::Bone)
	{
		if (PreviewInstance)
		{
			PreviewInstance->ModifyBone(InElement.Name);
		}
	}

	SetTransientControlValue(InElement);
	UpdateAvailableControls();

	return ControlName;
}

bool UControlRig::SetTransientControlValue(const FRigElementKey& InElement)
{
	if (!InElement.IsValid())
	{
		return false;
	}

	FName ControlName = GetNameForTransientControl(InElement);
	for (FRigControl& Control : TransientControls)
	{
		if (Control.Name == ControlName)
		{
			Control.ControlType = ERigControlType::Transform;

			if (InElement.Type == ERigElementType::Bone)
			{
				FTransform LocalTransform = Hierarchy.GetLocalTransform(InElement);
				Control.Value.Set<FTransform>(LocalTransform);
				Control.InitialValue.Set<FTransform>(LocalTransform);

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
			else if (InElement.Type == ERigElementType::Space)
			{
				FTransform GlobalTransform = Hierarchy.GetGlobalTransform(InElement);
				Control.Value.Set<FTransform>(GlobalTransform);
				Control.InitialValue.Set<FTransform>(GlobalTransform);
			}

			return true;
		}
	}
	return false;
}

FName UControlRig::RemoveTransientControl(const FRigElementKey& InElement)
{
	if (!InElement.IsValid())
	{
		return NAME_None;
	}

	FName ControlName = GetNameForTransientControl(InElement);
	for (int32 Index = 0; Index < TransientControls.Num(); Index++)
	{
		if (TransientControls[Index].Name == ControlName)
		{
			FRigControl ControlToRemove = TransientControls[Index];
			TransientControls.RemoveAt(Index);
			UpdateAvailableControls();
			return ControlToRemove.Name;
		}
	}

	return NAME_None;

}

FName UControlRig::GetNameForTransientControl(const FRigElementKey& InElement)
{
	if (InElement.Type == ERigElementType::Control)
	{
		return InElement.Name;
	}

	FName EnumName = StaticEnum<ERigElementType>()->GetNameByValue((int64)InElement.Type);
	return *FString::Printf(TEXT("ControlForRigElement_%s_%s"), *EnumName.ToString(), *InElement.Name.ToString());
}

void UControlRig::ClearTransientControls()
{
	for (int32 Index = TransientControls.Num() - 1; Index >= 0; Index--)
	{
		FRigControl ControlToRemove = TransientControls[Index];
		TransientControls.RemoveAt(Index);
		UpdateAvailableControls();
	}
}

#endif

void UControlRig::HandleOnRigEvent(FRigHierarchyContainer* InContainer, const FRigEventContext& InEvent)
{
	if (RigEventDelegate.IsBound())
	{
		RigEventDelegate.Broadcast(InContainer, InEvent);
	}

	switch (InEvent.Event)
	{
		case ERigEvent::RequestAutoKey:
		{
			int32 Index = InContainer->GetIndex(InEvent.Key);
			if (Index != INDEX_NONE && InEvent.Key.Type == ERigElementType::Control)
			{
				const FRigControl& Control = InContainer->ControlHierarchy[Index];

				FRigControlModifiedContext Context;
				Context.SetKey = EControlRigSetKey::Always;
				Context.LocalTime = InEvent.LocalTime;
				Context.EventName = InEvent.SourceEventName;
				ControlModified().Broadcast(this, Control, Context);
			}
		}
	}
}

void UControlRig::GetControlsInOrder(TArray<FRigControl>& SortedControls) const
{
	TArray<FRigElementKey> Keys = Hierarchy.GetAllItems(true);
	for (const FRigElementKey& Key : Keys)
	{ 
		if (Key.Type == ERigElementType::Control)
		{
			const FRigControl& RigControl = Hierarchy.ControlHierarchy[Key.Name];
			FRigControl NewControl(RigControl);
			SortedControls.Add(NewControl);
		}
	}
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
	check(Subject);

	const FRigBoneHierarchy& SourceBones = Subject->GetBoneHierarchy();
	FRigBoneHierarchy& TargetBones = GetBoneHierarchy();

	for (const FRigBone& SourceBone : SourceBones)
	{
		int32 Index = TargetBones.GetIndex(SourceBone.Name);
		if (Index != INDEX_NONE)
		{
			TargetBones[Index].LocalTransform = SourceBone.LocalTransform;
		}
	}

	TargetBones.RecomputeGlobalTransforms();

	const FRigCurveContainer& SourceCurves = Subject->GetCurveContainer();
	FRigCurveContainer& TargetCurves = GetCurveContainer();

	for (const FRigCurve& SourceCurve : SourceCurves)
	{
		int32 Index = TargetCurves.GetIndex(SourceCurve.Name);
		if (Index != INDEX_NONE)
		{
			TargetCurves[Index].Value = SourceCurve.Value;
		}
	}
}

void UControlRig::HandleInteractionRigControlModified(UControlRig* Subject, const FRigControl& Control, const FRigControlModifiedContext& Context)
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
		if (const FRigInfluenceEntry* InfluenceEntry = InfluenceMap->Find(Control.GetElementKey()))
		{
			for (const FRigElementKey& AffectedKey : *InfluenceEntry)
			{
				if (AffectedKey.Type == ERigElementType::Control)
				{
					if (const FRigControl* AffectedControl = FindControl(AffectedKey.Name))
					{
						QueuedModifiedControls.Add(*AffectedControl);
					}
				}
				else if (
					AffectedKey.Type == ERigElementType::Bone ||
					AffectedKey.Type == ERigElementType::Curve)
				{
					// special case controls with a CONTROL suffix
					FName BoneControlName = *FString::Printf(TEXT("%s_CONTROL"), *AffectedKey.Name.ToString());
					if (const FRigControl* AffectedControl = FindControl(BoneControlName))
					{
						QueuedModifiedControls.Add(*AffectedControl);
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

	for (const FRigControl& QueuedModifiedControl : QueuedModifiedControls)
	{
		ControlModified().Broadcast(this, QueuedModifiedControl, Context);
	}
}

void UControlRig::HandleInteractionRigControlSelected(UControlRig* Subject, const FRigControl& InControl, bool bSelected, bool bInverted)
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

		Local::SelectAffectedElements(this, InfluenceMap, InControl.GetElementKey(), bSelected, bInverted);

		if (bInverted)
		{
			FString ControlName = InControl.Name.ToString();
			if (ControlName.EndsWith(TEXT("_CONTROL")))
			{
				FString BaseName = ControlName.Left(ControlName.Len() - 8);
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
	}
	else if (InPinType.PinCategory == UEdGraphSchema_K2::PC_Int)
	{
		ExternalVariable.TypeName = TEXT("int32");
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
	}
	else if (InPinType.PinCategory == UEdGraphSchema_K2::PC_Float)
	{
		ExternalVariable.TypeName = TEXT("float");
	}
	else if (InPinType.PinCategory == UEdGraphSchema_K2::PC_Name)
	{
		ExternalVariable.TypeName = TEXT("FName");
	}
	else if (InPinType.PinCategory == UEdGraphSchema_K2::PC_String)
	{
		ExternalVariable.TypeName = TEXT("FString");
	}
	else if (InPinType.PinCategory == UEdGraphSchema_K2::PC_Struct)
	{
		if (UScriptStruct* Struct = Cast<UScriptStruct>(InPinType.PinSubCategoryObject))
		{
			ExternalVariable.TypeName = *Struct->GetStructCPPName();
			ExternalVariable.TypeObject = Struct;
		}
	}

	return ExternalVariable;
}

FRigVMExternalVariable UControlRig::GetExternalVariableFromDescription(const FBPVariableDescription& InVariableDescription)
{
	bool bIsPublic = !((InVariableDescription.PropertyFlags & CPF_DisableEditOnInstance) == CPF_DisableEditOnInstance);
	bool bIsReadOnly = ((InVariableDescription.PropertyFlags & CPF_BlueprintReadOnly) == CPF_BlueprintReadOnly);
	return GetExternalVariableFromPinType(InVariableDescription.VarName, InVariableDescription.VarType, bIsPublic, bIsReadOnly);
}

#endif // WITH_EDITOR

void UControlRig::SetBoneInitialTransformsFromSkeletalMesh(USkeletalMesh* InSkeletalMesh)
{
	check(InSkeletalMesh);
	SetBoneInitialTransformsFromRefSkeleton(InSkeletalMesh->GetRefSkeleton());
}

void UControlRig::SetBoneInitialTransformsFromRefSkeleton(const FReferenceSkeleton& InReferenceSkeleton)
{
	for (const FRigBone& Bone : GetBoneHierarchy())
	{
		int32 BoneIndex = InReferenceSkeleton.FindBoneIndex(Bone.Name);
		if (BoneIndex != INDEX_NONE)
		{
			FTransform LocalInitialTransform = InReferenceSkeleton.GetRefBonePose()[BoneIndex];
			GetBoneHierarchy().SetInitialLocalTransform(Bone.Index, LocalInitialTransform);
		}
	}
	bResetInitialTransformsBeforeSetup = false;
}

#undef LOCTEXT_NAMESPACE


