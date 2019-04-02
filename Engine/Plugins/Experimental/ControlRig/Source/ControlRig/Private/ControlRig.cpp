// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "ControlRig.h"
#include "GameFramework/Actor.h"
#include "Misc/RuntimeErrors.h"
#include "ControlRigVM.h"
#include "IControlRigObjectBinding.h"
#include "HelperUtil.h"
#include "ControlRigBlueprintGeneratedClass.h"
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

UControlRig::UControlRig()
	: DeltaTime(0.0f)
#if WITH_EDITORONLY_DATA
	, bExecutionOn (true)
#endif // WITH_EDITORONLY_DATA
	, ExecutionType(ERigExecutionType::Runtime)
#if WITH_EDITOR
	, ControlRigLog(nullptr)
	, bEnableControlRigLogging(true)
#endif
{
#if DEBUG_CONTROLRIG_PROPERTYCHANGE
	// this can be debug only
	UControlRigBlueprintGeneratedClass* CurrentClass = Cast<UControlRigBlueprintGeneratedClass>(GetClass());
	if (CurrentClass)
	{
		DebugClassSize = CurrentClass->GetStructureSize();
	}
#endif // #if DEBUG_CONTROLRIG_PROPERTYCHANGE
}

void UControlRig::ValidateClassData()
{
#if DEBUG_CONTROLRIG_PROPERTYCHANGE
	UControlRigBlueprintGeneratedClass* CurrentClass = Cast<UControlRigBlueprintGeneratedClass>(GetClass());
	if (CurrentClass)
	{
		ensureAlwaysMsgf(DebugClassSize == CurrentClass->GetStructureSize(),
			TEXT("Class [%s] size has changed : used be [size %d], and current class is [size %d]"), *GetNameSafe(CurrentClass), DebugClassSize, CurrentClass->GetStructureSize());

		// @todo: if you hit this, possibly we could break down to each destructor link, and property link size to verify. 
		// we could save "property name" and "size" and verify here. 
		// check comment on the DebugClassSize
	}
#endif // #if DEBUG_CONTROLRIG_PROPERTYCHANGE
}

void UControlRig::BeginDestroy()
{
	ValidateClassData(); 
	Super::BeginDestroy();
	InitializedEvent.Clear();
	ExecutedEvent.Clear();
}

UWorld* UControlRig::GetWorld() const
{
	if(ObjectBinding.IsValid())
	{
		AActor* HostingActor = ObjectBinding->GetHostingActor();
		return HostingActor ? HostingActor->GetWorld() : nullptr;
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

#if WITH_EDITOR
	// initialize rig unit cached names
	UControlRigBlueprintGeneratedClass* Class = Cast<UControlRigBlueprintGeneratedClass>(GetClass());
	if (Class)
	{
		for (UStructProperty* UnitProperty : Class->RigUnitProperties)
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
#endif // WITH_EDITOR

	// initialize executor
	InstantiateExecutor();

	// should refresh mapping 
	Hierarchy.BaseHierarchy.Initialize();

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
	// this won't work with procedural rigging
	// so I wonder this should be just bool option for control rig?
	Hierarchy.ResetTransforms();

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

void UControlRig::InstantiateExecutor()
{
	const int32 NumOps = Operators.Num();
	Executors.Reset(NumOps);
	for (int32 Index = 0; Index < Operators.Num(); ++Index)
	{
		// only allow if succedding on initialization
		// this bring question, where your property copy might fail because of missing properties
		//or link but we might still want to continue to next operator
		FRigExecutor Executor;
		if (Operators[Index].InitializeParam(this, Executor))
		{
			Executors.Add(Executor);
		}
		else
		{
			// fail compile? 
			// or warn
			UE_LOG(LogControlRig, Warning, TEXT("Failed to initialize execution on instruction %d : This will cause incorrect execution. - %s"), Index, *Operators[Index].ToString());
		}
	}
}

void UControlRig::Execute(const EControlRigState InState)
{
	SCOPE_CYCLE_COUNTER(STAT_RigExecution);

	FRigUnitContext Context;
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
	
	ControlRigVM::Execute(this, Context, Executors, ExecutionType);

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

#if WITH_EDITOR
FName UControlRig::GetRigClassNameFromRigUnit(const FRigUnit* InRigUnit) const
{
	if (InRigUnit)
	{
		UControlRigBlueprintGeneratedClass* Class = Cast<UControlRigBlueprintGeneratedClass>(GetClass());
		for (UStructProperty* UnitProperty : Class->RigUnitProperties)
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
	UControlRigBlueprintGeneratedClass* Class = Cast<UControlRigBlueprintGeneratedClass>(GetClass());
	for (UStructProperty* ControlProperty : Class->ControlUnitProperties)
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
	UControlRigBlueprintGeneratedClass* Class = Cast<UControlRigBlueprintGeneratedClass>(GetClass());
	for (UStructProperty* UnitProperty : Class->RigUnitProperties)
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

	// initialize rig unit cached names
	// @fixme: we noticed the CDO changes are not propagating all the time
	// so here we forcefully set to default class when compiled
	UControlRigBlueprintGeneratedClass* Class = Cast<UControlRigBlueprintGeneratedClass>(GetClass());
	if (Class)
	{
		const UObject* CurrentDefaultObject = Class->GetDefaultObject();
		for (UStructProperty* UnitProperty : Class->RigUnitProperties)
		{
			const FRigUnit* RigUnitDefault = UnitProperty->ContainerPtrToValuePtr<FRigUnit>(CurrentDefaultObject);
			ensure(RigUnitDefault != nullptr);

			FRigUnit* RigUnit = UnitProperty->ContainerPtrToValuePtr<FRigUnit>(this);
			FMemory::Memcpy(RigUnit, RigUnitDefault, UnitProperty->ElementSize);
			RigUnit->RigUnitName = UnitProperty->GetFName();
			RigUnit->RigUnitStructName = UnitProperty->Struct->GetFName();
		}
	}

	Initialize();
}
#endif // WITH_EDITOR

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

#undef LOCTEXT_NAMESPACE
