// Copyright Epic Games, Inc. All Rights Reserved.

#include "ControlRig.h"
#include "GameFramework/Actor.h"
#include "Misc/RuntimeErrors.h"
#include "ControlRigVM.h"
#include "IControlRigObjectBinding.h"
#include "HelperUtil.h"
#include "ControlRigBlueprintGeneratedClass.h"
#include "ControlRigObjectVersion.h"
#include "ControlRigVariables.h"
#if WITH_EDITOR
#include "ControlRigModule.h"
#include "Modules/ModuleManager.h"
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
const FName UControlRig::BoneNameMetaName("BoneName");
const FName UControlRig::ControlNameMetaName("ControlName");
const FName UControlRig::SpaceNameMetaName("SpaceName");
const FName UControlRig::CurveNameMetaName("CurveName");
const FName UControlRig::ConstantMetaName("Constant");
const FName UControlRig::TitleColorMetaName("TitleColor");
const FName UControlRig::NodeColorMetaName("NodeColor");
const FName UControlRig::KeywordsMetaName("Keywords");
const FName UControlRig::PrototypeNameMetaName("PrototypeName");

const FName UControlRig::AnimationInputMetaName("AnimationInput");
const FName UControlRig::AnimationOutputMetaName("AnimationOutput");

const FName UControlRig::ExpandPinByDefaultMetaName("ExpandByDefault");
const FName UControlRig::DefaultArraySizeMetaName("DefaultArraySize");

UControlRig::UControlRig()
	: DeltaTime(0.0f)
	, ExecutionType(ERigExecutionType::Runtime)
#if WITH_EDITOR
	, ControlRigLog(nullptr)
	, bEnableControlRigLogging(true)
	, DrawInterface(nullptr)
	, DataSourceRegistry(nullptr)
#endif
{
#if DEBUG_CONTROLRIG_PROPERTYCHANGE
	CacheDebugClassData();
#endif // #if DEBUG_CONTROLRIG_PROPERTYCHANGE
}

#if DEBUG_CONTROLRIG_PROPERTYCHANGE
void UControlRig::CacheDebugClassData()
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	// this can be debug only
	UControlRigBlueprintGeneratedClass* CurrentClass = Cast<UControlRigBlueprintGeneratedClass>(GetClass());
	if (CurrentClass)
	{
		DebugClassSize = CurrentClass->PropertiesSize;

		Destructors.Reset();
		for (FProperty* P = CurrentClass->DestructorLink; P; P = P->DestructorLinkNext)
		{
			FStructProperty* StructProperty = CastField<FStructProperty>(P);
			if (StructProperty)
			{
				Destructors.Add(StructProperty->Struct);
			}
		}

		PropertyData.Reset();
		for (FProperty* P = CurrentClass->PropertyLink; P; P = P->PropertyLinkNext)
		{
			FPropertyData PropData;
			PropData.Offset = P->GetOffset_ForDebug();
			PropData.Size = P->GetSize();
			PropData.PropertyName = P->GetFName();

			PropertyData.Add(PropData);
		}

		ensure(CurrentClass->UberGraphFunction == nullptr);
	}
}

void UControlRig::ValidateDebugClassData()
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	UControlRigBlueprintGeneratedClass* CurrentClass = Cast<UControlRigBlueprintGeneratedClass>(GetClass());
	if (CurrentClass)
	{
		ensureAlwaysMsgf(DebugClassSize == CurrentClass->PropertiesSize,
			TEXT("Class [%s] size has changed : used be [size %d], and current class is [size %d]"), *GetNameSafe(CurrentClass), DebugClassSize, CurrentClass->GetStructureSize());

		int32 Index = 0;
		for (FProperty* P = CurrentClass->DestructorLink; P; P = P->DestructorLinkNext)
		{
			FStructProperty* StructProperty = CastField<FStructProperty>(P);
			if (StructProperty)
			{
				ensureAlways(Destructors[Index++] == (StructProperty->Struct));
			}
		}

		Index = 0;
		for (FProperty* P = CurrentClass->PropertyLink; P; P = P->PropertyLinkNext)
		{
			const FPropertyData& PropData = PropertyData[Index++];
			ensureAlwaysMsgf(PropData.Offset == P->GetOffset_ForDebug() && PropData.Size == P->GetSize(), TEXT("Property (%s) size changes"), *PropData.PropertyName.ToString());
		}
	}
}
#endif // #if DEBUG_CONTROLRIG_PROPERTYCHANGE

void UControlRig::BeginDestroy()
{
#if DEBUG_CONTROLRIG_PROPERTYCHANGE
	ValidateDebugClassData(); 
#endif // #if DEBUG_CONTROLRIG_PROPERTYCHANGE
	Super::BeginDestroy();
	InitializedEvent.Clear();
	ExecutedEvent.Clear();
}

UWorld* UControlRig::GetWorld() const
{
	if(ObjectBinding.IsValid())
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

	InstantiateOperatorsFromGeneratedClass();
	ResolvePropertyPaths();

#if WITH_EDITORONLY_DATA
	// initialize rig unit cached names
	UControlRigBlueprintGeneratedClass* GeneratedClass = Cast<UControlRigBlueprintGeneratedClass>(GetClass());
	if (GeneratedClass)
	{
		for (FStructProperty* UnitProperty : GeneratedClass->RigUnitProperties)
		{
			FRigUnit* RigUnit = UnitProperty->ContainerPtrToValuePtr<FRigUnit>(this);
			RigUnit->RigUnitName = UnitProperty->GetFName();
			RigUnit->RigUnitStructName = UnitProperty->Struct->GetFName();
#if 0 
			FRigUnit* RigUnitDefault = UnitProperty->ContainerPtrToValuePtr<FRigUnit>(Class->GetDefaultObject());
			ensure(RigUnitDefault != nullptr);
#endif // DEBUG only
		}
	}
#endif // WITH_EDITORONLY_DATA

	// should refresh mapping 
	Hierarchy.Initialize();

	// resolve IO properties
	ResolveInputOutputProperties();

	if (bInitRigUnits)
	{
		// execute rig units with init state
		Execute(EControlRigState::Init);
	}

	// cache requested inputs
	// go through find requested inputs

#if WITH_EDITOR
	Hierarchy.ControlHierarchy.OnControlSelected.AddUObject(this, &UControlRig::HandleOnControlSelected);
#endif
}

void UControlRig::InitializeFromCDO()
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	// copy CDO property you need to here
	UControlRig* CDO = GetClass()->GetDefaultObject<UControlRig>();
	// copy operation
	Hierarchy = CDO->Hierarchy;
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

FText UControlRig::GetTooltipText() const
{
	return LOCTEXT("DefaultControlRigTooltip", "ControlRig");
}
#endif

void UControlRig::SetDeltaTime(float InDeltaTime)
{
	DeltaTime = InDeltaTime;
}

float UControlRig::GetDeltaTime() const
{
	return DeltaTime;
}

void UControlRig::InstantiateOperatorsFromGeneratedClass()
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	UControlRigBlueprintGeneratedClass* GeneratedClass = Cast<UControlRigBlueprintGeneratedClass>(GetClass());
	if (GeneratedClass)
	{
		Operators.Reset(GeneratedClass->Operators.Num());
		for (const FControlRigOperator& Op : GeneratedClass->Operators)
		{
			Operators.Add(FControlRigOperator::MakeUnresolvedCopy(Op));
		}
	}
	else
	{
		Operators.Reset();
	}
}

void UControlRig::ResolvePropertyPaths()
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	for (int32 Index = 0; Index < Operators.Num(); ++Index)
	{
		if (!Operators[Index].Resolve(this))
		{
			UE_LOG(LogControlRig, Error, TEXT("Operator '%s' cannot be resolved."), *Operators[Index].ToString());
		}
	}
}

void UControlRig::Execute(const EControlRigState InState)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	FRigUnitContext Context;
	Context.DrawInterface = DrawInterface;

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

	ControlRigVM::Execute(this, Context, Operators, ExecutionType);

#if WITH_EDITOR
	if (ControlRigLog != nullptr && bEnableControlRigLogging)
	{
		for (const FControlRigLog::FLogEntry& Entry : ControlRigLog->Entries)
		{
			if (Entry.Unit == NAME_None || Entry.Message.IsEmpty())
			{
				continue;
			}

			switch (Entry.Severity)
			{
				case EMessageSeverity::CriticalError:
				case EMessageSeverity::Error:
				{
					UE_LOG(LogControlRig, Error, TEXT("Unit '%s': '%s'"), *Entry.Unit.ToString(), *Entry.Message);
					break;
				}
				case EMessageSeverity::PerformanceWarning:
				case EMessageSeverity::Warning:
				{
					UE_LOG(LogControlRig, Warning, TEXT("Unit '%s': '%s'"), *Entry.Unit.ToString(), *Entry.Message);
					break;
				}
				case EMessageSeverity::Info:
				{
					UE_LOG(LogControlRig, Display, TEXT("Unit '%s': '%s'"), *Entry.Unit.ToString(), *Entry.Message);
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
		if (ExecutedEvent.IsBound())
		{
			ExecutedEvent.Broadcast(this, EControlRigState::Update);
		}
	}
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

void UControlRig::ResolveInputOutputProperties()
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	for (auto Iter = InputProperties.CreateIterator(); Iter; ++Iter)
	{
		FCachedPropertyPath& PropPath = Iter.Value();
		PropPath.Resolve(this);
	}

	for (auto Iter = OutputProperties.CreateIterator(); Iter; ++Iter)
	{
		FCachedPropertyPath& PropPath = Iter.Value();
		PropPath.Resolve(this);
	}
}

bool UControlRig::GetInOutPropertyPath(bool bInput, const FName& InPropertyPath, FCachedPropertyPath& OutCachedPath)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	// if root properties, use this
	TMap<FName, FCachedPropertyPath>& Properties = (bInput) ? InputProperties: OutputProperties;

	FCachedPropertyPath* CachedPath = Properties.Find(InPropertyPath);
	if (CachedPath)
	{
		if (!CachedPath->IsResolved())
		{
			CachedPath->Resolve(this);
		}

		if (CachedPath->IsResolved())
		{
			OutCachedPath = *CachedPath;
			return true;
		}
	}

	return false;
}
#if WITH_EDITORONLY_DATA
FName UControlRig::GetRigClassNameFromRigUnit(const FRigUnit* InRigUnit) const
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	if (InRigUnit)
	{
		UControlRigBlueprintGeneratedClass* GeneratedClass = Cast<UControlRigBlueprintGeneratedClass>(GetClass());
		for (FStructProperty* UnitProperty : GeneratedClass->RigUnitProperties)
		{
			if (UnitProperty->ContainerPtrToValuePtr<FRigUnit>(this) == InRigUnit)
			{
				return UnitProperty->Struct->GetFName();
			}
		}
	}

	return NAME_None;
}

FRigUnit_Control* UControlRig::GetControlRigUnitFromName(const FName& PropertyName) 
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	UControlRigBlueprintGeneratedClass* GeneratedClass = Cast<UControlRigBlueprintGeneratedClass>(GetClass());
	for (FStructProperty* ControlProperty : GeneratedClass->ControlUnitProperties)
	{
		if (ControlProperty->GetFName() == PropertyName)
		{
			return ControlProperty->ContainerPtrToValuePtr<FRigUnit_Control>(this);
		}
	}

	return nullptr;
}

FRigUnit* UControlRig::GetRigUnitFromName(const FName& PropertyName) 
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	UControlRigBlueprintGeneratedClass* GeneratedClass = Cast<UControlRigBlueprintGeneratedClass>(GetClass());
	for (FStructProperty* UnitProperty : GeneratedClass->RigUnitProperties)
	{
		if (UnitProperty->GetFName() == PropertyName)
		{
			return UnitProperty->ContainerPtrToValuePtr<FRigUnit>(this);
		}
	}

	return nullptr;
}

void UControlRig::PostReinstanceCallback(const UControlRig* Old)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	ObjectBinding = Old->ObjectBinding;
	Initialize();

	// propagate values from the CDO to the instance
	UControlRigBlueprintGeneratedClass* GeneratedClass = Cast<UControlRigBlueprintGeneratedClass>(GetClass());
	for (FStructProperty* UnitProperty : GeneratedClass->RigUnitProperties)
	{
		FRigUnit* Source = UnitProperty->ContainerPtrToValuePtr<FRigUnit>(GeneratedClass->ClassDefaultObject);
		FRigUnit* Dest = UnitProperty->ContainerPtrToValuePtr<FRigUnit>(this);
		UnitProperty->CopyCompleteValue(Dest, Source);
	}
}
#endif // WITH_EDITORONLY_DATA

void UControlRig::AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	Super::AddReferencedObjects(InThis, Collector);
#if WITH_EDITOR
	UControlRig* This = CastChecked<UControlRig>(InThis);
	for (auto Iter = This->RigUnitEditorObjects.CreateIterator(); Iter; ++Iter)
	{
		Collector.AddReferencedObject(Iter.Value());
	}
#endif // WITH_EDITOR
}
#if WITH_EDITOR
//undo will clear out the transient Operators, need to recreate them
void UControlRig::PostEditUndo()
{
	Super::PostEditUndo();
	InstantiateOperatorsFromGeneratedClass();
	ResolvePropertyPaths();
}
#endif // WITH_EDITOR

void UControlRig::Serialize(FArchive& Ar)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	Super::Serialize(Ar);

	Ar.UsingCustomVersion(FControlRigObjectVersion::GUID);

	if (Ar.IsLoading())
	{
		if (Ar.CustomVer(FControlRigObjectVersion::GUID) < FControlRigObjectVersion::OperatorsStoringPropertyPaths)
		{
			// create the cached paths based on the deprecated string
			for (FControlRigOperator& Operator : Operators)
			{
				if (!Operator.PropertyPath1_DEPRECATED.IsEmpty())
				{
					Operator.CachedPropertyPath1 = FCachedPropertyPath(Operator.PropertyPath1_DEPRECATED);
				}
				if (!Operator.PropertyPath2_DEPRECATED.IsEmpty())
				{
					Operator.CachedPropertyPath2 = FCachedPropertyPath(Operator.PropertyPath2_DEPRECATED);
				}
			}
		}
	}
}

bool UControlRig::IsValidIOVariables(bool bInput, const FName& PropertyName) const
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	const TMap<FName, FCachedPropertyPath>& Properties = (bInput) ? InputProperties : OutputProperties;
	return Properties.Contains(PropertyName);
}

void UControlRig::QueryIOVariables(bool bInput, TArray<FControlRigIOVariable>& OutVars) const
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	const TMap<FName, FCachedPropertyPath>& Properties = (bInput) ? InputProperties : OutputProperties;
	OutVars.Reset(Properties.Num());

	for (auto Iter = Properties.CreateConstIterator(); Iter; ++Iter)
	{
		const FCachedPropertyPath& PropPath = Iter.Value();
		FProperty* Property = PropPath.GetFProperty();
		FControlRigIOVariable NewVar;
		NewVar.PropertyPath = PropPath.ToString();
		NewVar.PropertyType = FControlRigIOHelper::GetFriendlyTypeName(Property);
		NewVar.Size = Property->GetSize();

		OutVars.Add(NewVar);
	}
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
	return Hierarchy.ControlHierarchy.GetControls();
}

FRigControl* UControlRig::FindControl(const FName& InControlName)
{
	const int32 ControlIndex = Hierarchy.ControlHierarchy.GetIndex(InControlName);
	if (ControlIndex != INDEX_NONE)
	{
		return &Hierarchy.ControlHierarchy[ControlIndex];
	}

	return nullptr;
}

FTransform UControlRig::GetControlGlobalTransform(const FName& InControlName) const
{
	return Hierarchy.ControlHierarchy.GetGlobalTransform(InControlName);
}

FRigControlValue UControlRig::GetControlValueFromGlobalTransform(const FName& InControlName, const FTransform& InGlobalTransform)
{
	FRigControlValue RetVal;

	const int32 ControlIndex = Hierarchy.ControlHierarchy.GetIndex(InControlName);
	if (ControlIndex != INDEX_NONE)
	{
		FRigControl& Control = Hierarchy.ControlHierarchy[ControlIndex];

		RetVal = Control.Value;

		FTransform ParentTransform = Hierarchy.ControlHierarchy.GetParentTransform(ControlIndex);
		switch (Control.ControlType)
		{
			case ERigControlType::Bool:
			case ERigControlType::Float:
			case ERigControlType::Vector2D:
			{
				// not sure how to extract this from global transform
				break;
			}
			case ERigControlType::Position:
			{
				FTransform Transform = InGlobalTransform.GetRelativeTransform(ParentTransform);
				RetVal.Set<FVector>(Transform.GetLocation());
				break;
			}
			case ERigControlType::Scale:
			{
				FTransform Transform = InGlobalTransform.GetRelativeTransform(ParentTransform);
				RetVal.Set<FVector>(Transform.GetScale3D());
				break;
			}
			case ERigControlType::Quat:
			{
				FTransform Transform = InGlobalTransform.GetRelativeTransform(ParentTransform);
				RetVal.Set<FQuat>(Transform.GetRotation());
				break;
			}
			case ERigControlType::Rotator:
			{
				FTransform Transform = InGlobalTransform.GetRelativeTransform(ParentTransform);
				RetVal.Set<FRotator>(Transform.GetRotation().Rotator());
				break;
			}
			case ERigControlType::Transform:
			{
				FTransform Transform = InGlobalTransform.GetRelativeTransform(ParentTransform);
				RetVal.Set<FTransform>(Transform);
				break;
			}
		}
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
#endif

#undef LOCTEXT_NAMESPACE


