// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

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

DEFINE_LOG_CATEGORY_STATIC(LogControlRig, Log, All);

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
#if WITH_EDITORONLY_DATA
	, bExecutionOn (true)
#endif // WITH_EDITORONLY_DATA
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
	// this can be debug only
	UControlRigBlueprintGeneratedClass* CurrentClass = Cast<UControlRigBlueprintGeneratedClass>(GetClass());
	if (CurrentClass)
	{
		DebugClassSize = CurrentClass->PropertiesSize;

		Destructors.Reset();
		for (UProperty* P = CurrentClass->DestructorLink; P; P = P->DestructorLinkNext)
		{
			UStructProperty* StructProperty = Cast<UStructProperty>(P);
			if (StructProperty)
			{
				Destructors.Add(StructProperty->Struct);
			}
		}

		PropertyData.Reset();
		for (UProperty* P = CurrentClass->PropertyLink; P; P = P->PropertyLinkNext)
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
	
	UControlRigBlueprintGeneratedClass* CurrentClass = Cast<UControlRigBlueprintGeneratedClass>(GetClass());
	if (CurrentClass)
	{
		ensureAlwaysMsgf(DebugClassSize == CurrentClass->PropertiesSize,
			TEXT("Class [%s] size has changed : used be [size %d], and current class is [size %d]"), *GetNameSafe(CurrentClass), DebugClassSize, CurrentClass->GetStructureSize());

		int32 Index = 0;
		for (UProperty* P = CurrentClass->DestructorLink; P; P = P->DestructorLinkNext)
		{
			UStructProperty* StructProperty = Cast<UStructProperty>(P);
			if (StructProperty)
			{
				ensureAlways(Destructors[Index++] == (StructProperty->Struct));
			}
		}

		Index = 0;
		for (UProperty* P = CurrentClass->PropertyLink; P; P = P->PropertyLinkNext)
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
	QUICK_SCOPE_CYCLE_COUNTER(STAT_ControlRig_Initialize);

	if (IsTemplate())
	{
		// don't initialize template class 
		return;
	}

	// initialize hierarchy refs
	// @todo re-think
	{
		static FName HierarchyRefType(TEXT("RigHierarchyRef"));
		UClass* MyClass = GetClass();
		UControlRig* CDO = MyClass->GetDefaultObject<UControlRig>();
		//@hack :  hack to fix hierarchy issue
		// it may need manual propagation like ComponentTransformDetails.cpp
		Hierarchy = CDO->Hierarchy;

		for (TFieldIterator<UProperty> It(MyClass); It; ++It)
		{
			if (UStructProperty* StructProperty = Cast<UStructProperty>(*It))
			{
				if (StructProperty->Struct->GetFName() == HierarchyRefType)
				{
					FRigHierarchyRef* HierarchyRef= StructProperty->ContainerPtrToValuePtr<FRigHierarchyRef>(this);
					check(HierarchyRef);
					HierarchyRef->Container = &Hierarchy;
				}
			}
		}
	}

	InstantiateOperatorsFromGeneratedClass();
	ResolvePropertyPaths();

#if WITH_EDITORONLY_DATA
	// initialize rig unit cached names
	UControlRigBlueprintGeneratedClass* GeneratedClass = Cast<UControlRigBlueprintGeneratedClass>(GetClass());
	if (GeneratedClass)
	{
		for (UStructProperty* UnitProperty : GeneratedClass->RigUnitProperties)
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
	Hierarchy.BaseHierarchy.Initialize();
	// resolve IO properties
	ResolveInputOutputProperties();

	if (bInitRigUnits)
	{
		// execute rig units with init state
		Execute(EControlRigState::Init);
	}

	// cache requested inputs
	// go through find requested inputs
}

void UControlRig::PreEvaluate_GameThread()
{
	// input delegates
	OnPreEvaluateGatherInput.ExecuteIfBound(this);
}

void UControlRig::Evaluate_AnyThread()
{
	Execute(EControlRigState::Update);
}

void UControlRig::PostEvaluate_GameThread()
{
	// output delgates
	OnPostEvaluateQueryOutput.ExecuteIfBound(this);
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
	SCOPE_CYCLE_COUNTER(STAT_RigExecution);

	FRigUnitContext Context;
	Context.DrawInterface = DrawInterface;

	if (InState == EControlRigState::Init)
	{
		Context.DataSourceRegistry = DataSourceRegistry;
	}

	Context.DeltaTime = DeltaTime;
	Context.State = InState;
	Context.HierarchyReference.Container = &Hierarchy;
	Context.HierarchyReference.bUseBaseHierarchy = true;

#if WITH_EDITOR
	Context.Log = ControlRigLog;
	if (ControlRigLog != nullptr)
	{
		ControlRigLog->Entries.Reset();
	}
#endif

#if WITH_EDITORONLY_DATA
	if (!bExecutionOn)
	{
		return;
	}
#endif // WITH_EDITORONLY_DATA

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

FTransform UControlRig::GetGlobalTransform(FName BoneName) const
{
	int32 Index = Hierarchy.BaseHierarchy.GetIndex(BoneName);
	if (Index != INDEX_NONE)
	{
		return Hierarchy.BaseHierarchy.GetGlobalTransform(Index);
	}

	return FTransform::Identity;

}

void UControlRig::SetGlobalTransform(const FName BoneName, const FTransform& InTransform) 
{
	int32 Index = Hierarchy.BaseHierarchy.GetIndex(BoneName);
	if (Index != INDEX_NONE)
	{
		return Hierarchy.BaseHierarchy.SetGlobalTransform(Index, InTransform);
	}
}

void UControlRig::GetMappableNodeData(TArray<FName>& OutNames, TArray<FNodeItem>& OutNodeItems) const
{
	OutNames.Reset();
	OutNodeItems.Reset();

	// now add all nodes
	const FRigHierarchy& BaseHierarchy = Hierarchy.BaseHierarchy;

	for (int32 Index = 0; Index < BaseHierarchy.Bones.Num(); ++Index)
	{
		OutNames.Add(BaseHierarchy.Bones[Index].Name);
		OutNodeItems.Add(FNodeItem(BaseHierarchy.Bones[Index].ParentName, BaseHierarchy.Bones[Index].InitialTransform));
	}

	// we also supply input/output properties
}

void UControlRig::ResolveInputOutputProperties()
{
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
	if (InRigUnit)
	{
		UControlRigBlueprintGeneratedClass* GeneratedClass = Cast<UControlRigBlueprintGeneratedClass>(GetClass());
		for (UStructProperty* UnitProperty : GeneratedClass->RigUnitProperties)
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
	UControlRigBlueprintGeneratedClass* GeneratedClass = Cast<UControlRigBlueprintGeneratedClass>(GetClass());
	for (UStructProperty* ControlProperty : GeneratedClass->ControlUnitProperties)
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
	UControlRigBlueprintGeneratedClass* GeneratedClass = Cast<UControlRigBlueprintGeneratedClass>(GetClass());
	for (UStructProperty* UnitProperty : GeneratedClass->RigUnitProperties)
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
	ObjectBinding = Old->ObjectBinding;
	Initialize();

	// propagate values from the CDO to the instance
	UControlRigBlueprintGeneratedClass* GeneratedClass = Cast<UControlRigBlueprintGeneratedClass>(GetClass());
	for (UStructProperty* UnitProperty : GeneratedClass->RigUnitProperties)
	{
		FRigUnit* Source = UnitProperty->ContainerPtrToValuePtr<FRigUnit>(GeneratedClass->ClassDefaultObject);
		FRigUnit* Dest = UnitProperty->ContainerPtrToValuePtr<FRigUnit>(this);
		UnitProperty->CopyCompleteValue(Dest, Source);
	}
}
#endif // WITH_EDITORONLY_DATA

void UControlRig::AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector)
{
	Super::AddReferencedObjects(InThis, Collector);
#if WITH_EDITOR
	UControlRig* This = CastChecked<UControlRig>(InThis);
	for (auto Iter = This->RigUnitEditorObjects.CreateIterator(); Iter; ++Iter)
	{
		Collector.AddReferencedObject(Iter.Value());
	}
#endif // WITH_EDITOR
}

void UControlRig::Serialize(FArchive& Ar)
{
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
	const TMap<FName, FCachedPropertyPath>& Properties = (bInput) ? InputProperties : OutputProperties;
	return Properties.Contains(PropertyName);
}

void UControlRig::QueryIOVariables(bool bInput, TArray<FControlRigIOVariable>& OutVars) const
{
	const TMap<FName, FCachedPropertyPath>& Properties = (bInput) ? InputProperties : OutputProperties;
	OutVars.Reset(Properties.Num());

	for (auto Iter = Properties.CreateConstIterator(); Iter; ++Iter)
	{
		const FCachedPropertyPath& PropPath = Iter.Value();
		UProperty* Property = PropPath.GetUProperty();
		FControlRigIOVariable NewVar;
		NewVar.PropertyPath = PropPath.ToString();
		NewVar.PropertyType = FControlRigIOHelper::GetFriendlyTypeName(Property);
		NewVar.Size = Property->GetSize();

		OutVars.Add(NewVar);
	}
}
#undef LOCTEXT_NAMESPACE
