// Copyright Epic Games, Inc. All Rights Reserved.

#include "ControlRig.h"
#include "GameFramework/Actor.h"
#include "Misc/RuntimeErrors.h"
#include "IControlRigObjectBinding.h"
#include "HelperUtil.h"
#include "ControlRigBlueprintGeneratedClass.h"
#include "ControlRigObjectVersion.h"
#if WITH_EDITOR
#include "ControlRigModule.h"
#include "Modules/ModuleManager.h"
#include "RigVMModel/RigVMNode.h"
#endif// WITH_EDITOR

#define LOCTEXT_NAMESPACE "ControlRig"

DEFINE_LOG_CATEGORY(LogControlRig);

DECLARE_STATS_GROUP(TEXT("ControlRig"), STATGROUP_ControlRig, STATCAT_Advanced);
DECLARE_CYCLE_STAT_EXTERN(TEXT("Control Rig Execution"), STAT_RigExecution, STATGROUP_ControlRig, );
DEFINE_STAT(STAT_RigExecution);

const FName UControlRig::DeprecatedMetaName("Deprecated");
const FName UControlRig::InputMetaName("Input");
const FName UControlRig::OutputMetaName("Output");
const FName UControlRig::AbstractMetaName("Abstract");
const FName UControlRig::CategoryMetaName("Category");
const FName UControlRig::DisplayNameMetaName("DisplayName");
const FName UControlRig::MenuDescSuffixMetaName("MenuDescSuffix");
const FName UControlRig::ShowVariableNameInTitleMetaName("ShowVariableNameInTitle");
const FName UControlRig::CustomWidgetMetaName("CustomWidget");
const FName UControlRig::ConstantMetaName("Constant");
const FName UControlRig::TitleColorMetaName("TitleColor");
const FName UControlRig::NodeColorMetaName("NodeColor");
const FName UControlRig::KeywordsMetaName("Keywords");
const FName UControlRig::PrototypeNameMetaName("PrototypeName");

const FName UControlRig::ExpandPinByDefaultMetaName("ExpandByDefault");
const FName UControlRig::DefaultArraySizeMetaName("DefaultArraySize");

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
#if WITH_EDITOR
	, ControlRigLog(nullptr)
	, bEnableControlRigLogging(true)
	, DrawInterface(nullptr)
	, DataSourceRegistry(nullptr)
	, PreviewInstance(nullptr)
	, bRequiresInitExecution(false)
#endif
{
	VM = ObjectInitializer.CreateDefaultSubobject<URigVM>(this, TEXT("VM"));
	// create default source registry
	DataSourceRegistry = CreateDefaultSubobject<UAnimationDataSourceRegistry>(TEXT("DataSourceRegistry"));
}

void UControlRig::BeginDestroy()
{
	Super::BeginDestroy();
	InitializedEvent.Clear();
	ExecutedEvent.Clear();
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

	if (IsTemplate())
	{
		// don't initialize template class 
		return;
	}

	InitializeFromCDO();
	InstantiateVMFromCDO();

	// should refresh mapping 
	Hierarchy.Initialize();

	if (bInitRigUnits)
	{
		RequestInit();
	}

#if WITH_EDITOR
	Hierarchy.ControlHierarchy.OnControlSelected.RemoveAll(this);
	Hierarchy.ControlHierarchy.OnControlSelected.AddUObject(this, &UControlRig::HandleOnControlSelected);
#endif
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

	Execute(EControlRigState::Update);
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
			VM->CopyFrom(CDO->VM);
	}
	else
	{
			VM->Reset();
		}
	}
	RequestInit();
}

void UControlRig::Execute(const EControlRigState InState)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	if (bRequiresInitExecution)
	{
		bRequiresInitExecution = false;

		if (InState != EControlRigState::Init)
		{
			Execute(EControlRigState::Init);
		}
	}

	FRigUnitContext Context;
	Context.DrawInterface = DrawInterface;
	Context.DrawContainer = &DrawContainer;

	if (InState == EControlRigState::Init)
	{
		Context.DataSourceRegistry = DataSourceRegistry;
	}

	Context.DeltaTime = DeltaTime;
	Context.State = InState;
	Context.Hierarchy = &Hierarchy;

#if WITH_EDITOR
	Context.Log = ControlRigLog;
	if (ControlRigLog != nullptr)
	{
		ControlRigLog->Entries.Reset();
	}
#endif

	// execute units
	ExecuteUnits(Context);

#if WITH_EDITOR
	if (ControlRigLog != nullptr && bEnableControlRigLogging)
	{
		for (const FControlRigLog::FLogEntry& Entry : ControlRigLog->Entries)
		{
			if (Entry.OperatorName == NAME_None || Entry.InstructionIndex == INDEX_NONE || Entry.Message.IsEmpty())
			{
				continue;
			}

			switch (Entry.Severity)
			{
				case EMessageSeverity::CriticalError:
				case EMessageSeverity::Error:
				{
				UE_LOG(LogControlRig, Error, TEXT("Operator[%d] '%s': '%s'"), Entry.InstructionIndex, *Entry.OperatorName.ToString(), *Entry.Message);
					break;
				}
				case EMessageSeverity::PerformanceWarning:
				case EMessageSeverity::Warning:
				{
				UE_LOG(LogControlRig, Warning, TEXT("Operator[%d] '%s': '%s'"), Entry.InstructionIndex, *Entry.OperatorName.ToString(), *Entry.Message);
					break;
				}
				case EMessageSeverity::Info:
				{
				UE_LOG(LogControlRig, Display, TEXT("Operator[%d] '%s': '%s'"), Entry.InstructionIndex, *Entry.OperatorName.ToString(), *Entry.Message);
					break;
				}
				default:
				{
					break;
				}
			}
		}
	}
#endif

	if (InState == EControlRigState::Init)
	{
		if (InitializedEvent.IsBound())
		{
			InitializedEvent.Broadcast(this, EControlRigState::Init);
		}
	}
	else if (InState == EControlRigState::Update)
	{
		DeltaTime = 0.f;

		if (ExecutedEvent.IsBound())
		{
			ExecutedEvent.Broadcast(this, EControlRigState::Update);
		}
	}

	if (Context.DrawInterface && Context.DrawContainer)
	{
		Context.DrawInterface->Instructions.Append(Context.DrawContainer->Instructions);

		for (const FRigControl& Control : Hierarchy.ControlHierarchy)
		{
			if (Control.bGizmoEnabled && !Control.bIsTransientControl && Control.bDrawLimits && (Control.bLimitTranslation || Control.bLimitRotation || Control.bLimitScale))
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
					DrawInterface->Instructions.Add(Instruction);
				}
			}
		}
	}
}

void UControlRig::ExecuteUnits(FRigUnitContext& InOutContext)
{
	if (VM)
	{
		FRigVMMemoryContainer* LocalMemory[] = { &VM->WorkMemory, &VM->LiteralMemory };
		TArray<void*> AdditionalArguments;
		AdditionalArguments.Add(&InOutContext);
		VM->Execute(FRigVMMemoryContainerPtrArray(LocalMemory, 2), AdditionalArguments);
	}
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
}

// BEGIN IControlRigManipulatable interface
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
				FRigControl& Control = Hierarchy.ControlHierarchy.Add(Curve.Name, ERigControlType::Float, NAME_None, NAME_None, Value
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

void UControlRig::HandleOnControlModified(IControlRigManipulatable* Subject, const FRigControl& Control, EControlRigSetKey InSetKey)
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
				FTransform Transform = FTransform::Identity;

				FTransform ParentTransform = FTransform::Identity;
				if (Control.ParentIndex != INDEX_NONE)
				{
					ParentTransform = Hierarchy.BoneHierarchy.GetGlobalTransform(Control.ParentIndex);
				}

				switch (Control.ControlType)
				{
					case ERigControlType::Bool:
					{
						// not sure how to extract this from global transform
						break;
					}
					case ERigControlType::Float:
					{
						float Value = Control.Value.Get<float>();
						switch (Control.PrimaryAxis)
						{
							case ERigControlAxis::X:
							{
								Transform.SetLocation(FVector(Value, 0.f, 0.f));
								break;
							}
							case ERigControlAxis::Y:
							{
								Transform.SetLocation(FVector(0.f, Value, 0.f));
								break;
							}
							case ERigControlAxis::Z:
							{
								Transform.SetLocation(FVector(0.f, 0.f, Value));
								break;
							}
						}
						break;
					}
					case ERigControlType::Vector2D:
					{
						FVector2D Value = Control.Value.Get<FVector2D>();
						switch (Control.PrimaryAxis)
						{
							case ERigControlAxis::X:
							{
								Transform.SetLocation(FVector(0.f, Value.X, Value.Y));
								break;
							}
							case ERigControlAxis::Y:
							{
								Transform.SetLocation(FVector(Value.X, 0.f, Value.Y));
								break;
							}
							case ERigControlAxis::Z:
							{
								Transform.SetLocation(FVector(Value.X, Value.Y, 0.f));
								break;
							}
						}
						break;
					}
					case ERigControlType::Position:
					{
						Transform.SetLocation(Control.Value.Get<FVector>());
						break;
					}
					case ERigControlType::Scale:
					{
						Transform.SetScale3D(Control.Value.Get<FVector>());
						break;
					}
					case ERigControlType::Rotator:
					{
						Transform.SetRotation(FQuat(Control.Value.Get<FRotator>()));
						break;
					}
					case ERigControlType::Transform:
					{
						Transform = Control.Value.Get<FTransform>();
						break;
					}
					case ERigControlType::TransformNoScale:
					{
						Transform = Control.Value.Get<FTransformNoScale>();
						break;
					}
				}
				return Transform * ParentTransform;
			}
		}
	}
#endif
	return Hierarchy.ControlHierarchy.GetGlobalTransform(InControlName);
}

FRigControlValue UControlRig::GetControlValueFromGlobalTransform(const FName& InControlName, const FTransform& InGlobalTransform)
{
	FRigControlValue RetVal;

	if (FRigControl* Control = FindControl(InControlName))
	{
		RetVal = Control->Value;

		FTransform ParentTransform = FTransform::Identity;
		if ((Control->ParentIndex != INDEX_NONE || Control->SpaceIndex != INDEX_NONE) && Control->bIsTransientControl)
		{
			if (Control->ParentIndex != INDEX_NONE)
			{
				ParentTransform = Hierarchy.BoneHierarchy.GetGlobalTransform(Control->ParentIndex);
			}
		}
		else if(Control->Index != INDEX_NONE)
		{
			ParentTransform = Hierarchy.ControlHierarchy.GetParentTransform(Control->Index);
		}

		FTransform Transform = InGlobalTransform.GetRelativeTransform(ParentTransform);
		switch (Control->ControlType)
		{
			case ERigControlType::Bool:
			{
				// not sure how to extract this from global transform
				break;
			}
			case ERigControlType::Float:
			{
				switch (Control->PrimaryAxis)
				{
					case ERigControlAxis::X:
					{
						RetVal.Set<float>(Transform.GetLocation().X);
						break;
					}
					case ERigControlAxis::Y:
					{
						RetVal.Set<float>(Transform.GetLocation().Y);
						break;
					}
					case ERigControlAxis::Z:
					{
						RetVal.Set<float>(Transform.GetLocation().Z);
						break;
					}
				}
				break;
			}
			case ERigControlType::Vector2D:
			{
				FVector Location = Transform.GetLocation();
				switch (Control->PrimaryAxis)
				{
					case ERigControlAxis::X:
					{
						RetVal.Set<FVector2D>(FVector2D(Location.Y, Location.Z));
						break;
					}
					case ERigControlAxis::Y:
					{
						RetVal.Set<FVector2D>(FVector2D(Location.X, Location.Z));
						break;
					}
					case ERigControlAxis::Z:
					{
						RetVal.Set<FVector2D>(FVector2D(Location.X, Location.Y));
						break;
					}
				}
				break;
			}
			case ERigControlType::Position:
			{
				RetVal.Set<FVector>(Transform.GetLocation());
				break;
			}
			case ERigControlType::Scale:
			{
				RetVal.Set<FVector>(Transform.GetScale3D());
				break;
			}
			case ERigControlType::Rotator:
			{
				RetVal.Set<FRotator>(Transform.GetRotation().Rotator());
				break;
			}
			case ERigControlType::Transform:
			{
				RetVal.Set<FTransform>(Transform);
				break;
			}
			case ERigControlType::TransformNoScale:
			{
				RetVal.Set<FTransformNoScale>(Transform);
				break;
			}
		}

		Control->ApplyLimits(RetVal);
	}

	return RetVal;
}

bool UControlRig::SetControlSpace(const FName& InControlName, const FName& InSpaceName)
{
	Hierarchy.ControlHierarchy.SetSpace(InControlName, InSpaceName);
	return true;
}

UControlRigGizmoLibrary* UControlRig::GetGizmoLibrary() const
{
#if WITH_EDITOR
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
#endif
	return nullptr;
}

#if WITH_EDITOR

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

// END IControlRigManipulatable interface

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

void UControlRig::UpdateAvailableControls()
{
	AvailableControlsOverride = Hierarchy.ControlHierarchy.GetControls();
	AvailableControlsOverride.Append(TransientControls);
}

FName UControlRig::AddTransientControl(URigVMPin* InPin, FName InSpaceName)
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

	if(!InSpaceName.IsNone())
	{
		NewControl.ParentIndex = Hierarchy.BoneHierarchy.GetIndex(InSpaceName);
		if (NewControl.ParentIndex != INDEX_NONE)
		{
			NewControl.ParentName = Hierarchy.BoneHierarchy[NewControl.ParentIndex].Name;
		}
	}

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

#undef LOCTEXT_NAMESPACE


