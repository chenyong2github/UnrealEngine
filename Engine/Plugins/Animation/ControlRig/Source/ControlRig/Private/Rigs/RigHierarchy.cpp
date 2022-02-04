// Copyright Epic Games, Inc. All Rights Reserved.

#include "Rigs/RigHierarchy.h"
#include "ControlRig.h"

#include "Rigs/RigHierarchyElements.h"
#include "Rigs/RigHierarchyController.h"
#include "Units/RigUnitContext.h"
#include "Math/ControlRigMathLibrary.h"
#include "UObject/AnimObjectVersion.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "HAL/LowLevelMemTracker.h"

LLM_DEFINE_TAG(Animation_ControlRig);

#if WITH_EDITOR
#include "RigVMPythonUtils.h"
#include "ScopedTransaction.h"
#include "HAL/PlatformStackWalk.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "JsonObjectConverter.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "ControlRig/Private/Units/Execution/RigUnit_BeginExecution.h"

static FCriticalSection GRigHierarchyStackTraceMutex;
static char GRigHierarchyStackTrace[65536];
static void RigHierarchyCaptureCallStack(FString& OutCallstack, uint32 NumCallsToIgnore)
{
	FScopeLock ScopeLock(&GRigHierarchyStackTraceMutex);
	GRigHierarchyStackTrace[0] = 0;
	FPlatformStackWalk::StackWalkAndDump(GRigHierarchyStackTrace, 65535, 1 + NumCallsToIgnore);
	OutCallstack = ANSI_TO_TCHAR(GRigHierarchyStackTrace);
}

// CVar to record all transform changes 
static TAutoConsoleVariable<int32> CVarControlRigHierarchyTraceAlways(TEXT("ControlRig.Hierarchy.TraceAlways"), 0, TEXT("if nonzero we will record all transform changes."));
static TAutoConsoleVariable<int32> CVarControlRigHierarchyTraceCallstack(TEXT("ControlRig.Hierarchy.TraceCallstack"), 0, TEXT("if nonzero we will record the callstack for any trace entry.\nOnly works if(ControlRig.Hierarchy.TraceEnabled != 0)"));
static TAutoConsoleVariable<int32> CVarControlRigHierarchyTracePrecision(TEXT("ControlRig.Hierarchy.TracePrecision"), 3, TEXT("sets the number digits in a float when tracing hierarchies."));
static TAutoConsoleVariable<int32> CVarControlRigHierarchyTraceOnSpawn(TEXT("ControlRig.Hierarchy.TraceOnSpawn"), 0, TEXT("sets the number of frames to trace when a new hierarchy is spawned"));
static int32 sRigHierarchyLastTrace = INDEX_NONE;
static TCHAR sRigHierarchyTraceFormat[16];

// A console command to trace a single frame / single execution for a control rig anim node / control rig component
FAutoConsoleCommandWithWorldAndArgs FCmdControlRigHierarchyTraceFrames
(
	TEXT("ControlRig.Hierarchy.Trace"),
	TEXT("Traces changes in a hierarchy for a provided number of executions (defaults to 1).\nYou can use ControlRig.Hierarchy.TraceCallstack to enable callstack tracing as part of this."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& InParams, UWorld* InWorld)
	{
		int32 NumFrames = 1;
		if(InParams.Num() > 0)
		{
			NumFrames = FCString::Atoi(*InParams[0]);
		}
		
		TArray<UObject*> Instances;
		URigHierarchy::StaticClass()->GetDefaultObject()->GetArchetypeInstances(Instances);

		for(UObject* Instance : Instances)
		{
			if (Instance->HasAnyFlags(RF_ClassDefaultObject))
			{
				continue;
			}
			
			// we'll just trace all of them for now
			//if(Instance->GetWorld() == InWorld)
			if(Instance->GetTypedOuter<UControlRig>() != nullptr)
			{
				CastChecked<URigHierarchy>(Instance)->TraceFrames(NumFrames);
			}
		}
	})
);

#endif

////////////////////////////////////////////////////////////////////////////////
// URigHierarchy
////////////////////////////////////////////////////////////////////////////////

const FRigBaseElementChildrenArray URigHierarchy::EmptyElementArray;

URigHierarchy::URigHierarchy()
: TopologyVersion(0)
, bEnableDirtyPropagation(true)
, Elements()
, IndexLookup()
, TransformStackIndex(0)
, bTransactingForTransformChange(false)
, bIsInteracting(false)
, LastInteractedKey()
, bSuspendNotifications(false)
, HierarchyController(nullptr)
, ResetPoseHash(INDEX_NONE)
#if WITH_EDITOR
, bPropagatingChange(false)
, bForcePropagation(false)
, TraceFramesLeft(0)
, TraceFramesCaptured(0)
#endif
#if URIGHIERARCHY_ENSURE_CACHE_VALIDITY
, bEnableCacheValidityCheck(true)
#else
, bEnableCacheValidityCheck(false)
#endif
, HierarchyForCacheValidation()
#if WITH_EDITOR
, ExecuteContext(nullptr)
, bRecordTransformsPerInstruction(true)
#endif
{
	Reset();
#if WITH_EDITOR
	TraceFrames(CVarControlRigHierarchyTraceOnSpawn->GetInt());
#endif
}

URigHierarchy::~URigHierarchy()
{
	Reset();
}

void URigHierarchy::Serialize(FArchive& Ar)
{
	LLM_SCOPE_BYNAME(TEXT("Animation/ControlRig"));
	
	Ar.UsingCustomVersion(FAnimObjectVersion::GUID);

	if (Ar.IsSaving() || Ar.IsObjectReferenceCollector() || Ar.IsCountingMemory())
	{
		Save(Ar);
	}
	else if (Ar.IsLoading())
	{
		Load(Ar);
	}
	else
	{
		// remove due to FPIEFixupSerializer hitting this checkNoEntry();
	}
}

void URigHierarchy::Save(FArchive& Ar)
{
	if(Ar.IsTransacting())
	{
		Ar << TransformStackIndex;
		Ar << bTransactingForTransformChange;
		
		if(bTransactingForTransformChange)
		{
			return;
		}

		TArray<FRigElementKey> SelectedKeys = GetSelectedKeys();
		Ar << SelectedKeys;
	}

	// make sure all parts of pose are valid.
	// this ensures cache validity.
	EnsureCacheValidity();

	ComputeAllTransforms();

	int32 ElementCount = Elements.Num();
	Ar << ElementCount;

	for(int32 ElementIndex = 0; ElementIndex < ElementCount; ElementIndex++)
	{
		FRigBaseElement* Element = Elements[ElementIndex];

		// store the key
		FRigElementKey Key = Element->GetKey();
		Ar << Key;

		// allow the element to store more information
		Element->Serialize(Ar, this, FRigBaseElement::StaticData);
	}

	for(int32 ElementIndex = 0; ElementIndex < ElementCount; ElementIndex++)
	{
		FRigBaseElement* Element = Elements[ElementIndex];
		Element->Serialize(Ar, this, FRigBaseElement::InterElementData);
	}
}

void URigHierarchy::Load(FArchive& Ar)
{
	TArray<FRigElementKey> SelectedKeys;
	if(Ar.IsTransacting())
	{
		bool bOnlySerializedTransformStackIndex = false;
		Ar << TransformStackIndex;
		Ar << bOnlySerializedTransformStackIndex;
		
		if(bOnlySerializedTransformStackIndex)
		{
			return;
		}

		Ar << SelectedKeys;
	}

	Reset();

	int32 ElementCount = 0;
	Ar << ElementCount;

	for(int32 ElementIndex = 0; ElementIndex < ElementCount; ElementIndex++)
	{
		FRigElementKey Key;
		Ar << Key;

		FRigBaseElement* Element = MakeElement(Key.Type);
		check(Element);

		Element->SubIndex = Num(Key.Type);
		Element->Index = Elements.Add(Element);
		ElementsPerType[RigElementTypeToFlatIndex(Key.Type)].Add(Element);
		IndexLookup.Add(Key, Element->Index);
		
		Element->Load(Ar, this, FRigBaseElement::StaticData);
	}

	IncrementTopologyVersion();

	for(int32 ElementIndex = 0; ElementIndex < ElementCount; ElementIndex++)
	{
		FRigBaseElement* Element = Elements[ElementIndex];
		Element->Load(Ar, this, FRigBaseElement::InterElementData);
	}

	IncrementTopologyVersion();

	for(int32 ElementIndex = 0; ElementIndex < ElementCount; ElementIndex++)
	{
		if(FRigTransformElement* TransformElement = Cast<FRigTransformElement>(Elements[ElementIndex]))
		{
#if URIGHIERARCHY_RECURSIVE_DIRTY_PROPAGATION
			FRigBaseElementParentArray CurrentParents = GetParents(TransformElement, false);
#else
			FRigBaseElementParentArray CurrentParents = GetParents(TransformElement, true);
#endif
			for (FRigBaseElement* CurrentParent : CurrentParents)
			{
				if(FRigTransformElement* TransformParent = Cast<FRigTransformElement>(CurrentParent))
				{
					TransformParent->ElementsToDirty.AddUnique(TransformElement);
				}
			}
		}
	}

	UpdateAllCachedChildren();

	if(Ar.IsTransacting())
	{
		for(const FRigElementKey& SelectedKey : SelectedKeys)
		{
			if(FRigBaseElement* Element = Find<FRigBaseElement>(SelectedKey))
			{
				Element->bSelected = true;
			}
		}
	}

	Notify(ERigHierarchyNotification::HierarchyReset, nullptr);
}

void URigHierarchy::PostLoad()
{
	UObject::PostLoad();

	LLM_SCOPE_BYNAME(TEXT("Animation/ControlRig"));
	
	struct Local
	{
		static bool NeedsCheck(const FRigLocalAndGlobalTransform& InTransform)
		{
			return !InTransform.Local.bDirty && !InTransform.Global.bDirty;
		}
	};

	// we need to check the elements for integrity (global vs local) to be correct.
	for(int32 ElementIndex = 0; ElementIndex < Elements.Num(); ElementIndex++)
	{
		FRigBaseElement* BaseElement = Elements[ElementIndex];

		if(FRigControlElement* ControlElement = Cast<FRigControlElement>(BaseElement))
		{
			if(Local::NeedsCheck(ControlElement->Offset.Initial))
			{
				const FTransform ComputedGlobalTransform = SolveParentConstraints(
					ControlElement->ParentConstraints, ERigTransformType::InitialGlobal,
					ControlElement->Offset.Get(ERigTransformType::InitialLocal), true,
					FTransform::Identity, false);

				const FTransform CachedGlobalTransform = ControlElement->Offset.Get(ERigTransformType::InitialGlobal);
				if(!FRigComputedTransform::Equals(ComputedGlobalTransform, CachedGlobalTransform, 0.01))
				{
					ControlElement->Offset.MarkDirty(ERigTransformType::InitialGlobal);
				}
			}

			if(Local::NeedsCheck(ControlElement->Pose.Initial))
			{
				const FTransform ComputedGlobalTransform = SolveParentConstraints(
					ControlElement->ParentConstraints, ERigTransformType::InitialGlobal,
					GetControlOffsetTransform(ControlElement, ERigTransformType::InitialGlobal), true,
					ControlElement->Pose.Get(ERigTransformType::InitialLocal), true);
				
				const FTransform CachedGlobalTransform = ControlElement->Pose.Get(ERigTransformType::InitialGlobal);
				
				if(!FRigComputedTransform::Equals(ComputedGlobalTransform, CachedGlobalTransform, 0.01))
				{
					// for nulls we perceive the local transform as less relevant
					ControlElement->Pose.MarkDirty(ERigTransformType::InitialLocal);
				}
			}

			// we also need to check the pose here - for controls it is a bit different than for other
			// types.
			continue;
		}

		if(FRigMultiParentElement* MultiParentElement = Cast<FRigMultiParentElement>(BaseElement))
		{
			if(Local::NeedsCheck(MultiParentElement->Pose.Initial))
			{
				const FTransform ComputedGlobalTransform = SolveParentConstraints(
					MultiParentElement->ParentConstraints, ERigTransformType::InitialGlobal,
					FTransform::Identity, false,
					MultiParentElement->Pose.Get(ERigTransformType::InitialLocal), true);
				
				const FTransform CachedGlobalTransform = MultiParentElement->Pose.Get(ERigTransformType::InitialGlobal);
				
				if(!FRigComputedTransform::Equals(ComputedGlobalTransform, CachedGlobalTransform, 0.01))
				{
					// for nulls we perceive the local transform as less relevant
					MultiParentElement->Pose.MarkDirty(ERigTransformType::InitialLocal);
				}
			}
		}

		if(FRigTransformElement* TransformElement = Cast<FRigTransformElement>(BaseElement))
		{
			if(Local::NeedsCheck(TransformElement->Pose.Initial))
			{
				const FTransform ParentTransform = GetParentTransform(TransformElement, ERigTransformType::InitialGlobal);
				const FTransform ComputedGlobalTransform = TransformElement->Pose.Get(ERigTransformType::InitialLocal) * ParentTransform;
				const FTransform CachedGlobalTransform = TransformElement->Pose.Get(ERigTransformType::InitialGlobal);
				if(!FRigComputedTransform::Equals(ComputedGlobalTransform, CachedGlobalTransform, 0.01))
				{
					TransformElement->Pose.MarkDirty(ERigTransformType::InitialGlobal);
				}
			}
		}
	}
}

void URigHierarchy::Reset()
{
	LLM_SCOPE_BYNAME(TEXT("Animation/ControlRig"));
	
	TopologyVersion = 0;
	bEnableDirtyPropagation = true;

	// walk in reverse since certain elements might not have been allocated themselves
	for(int32 ElementIndex = Elements.Num() - 1; ElementIndex >= 0; ElementIndex--)
	{
		DestroyElement(Elements[ElementIndex]);
	}
	Elements.Reset();
	ElementsPerType.Reset();
	for(int32 TypeIndex=0;TypeIndex<RigElementTypeToFlatIndex(ERigElementType::Last);TypeIndex++)
	{
		ElementsPerType.Add(TArray<FRigBaseElement*>());
	}
	IndexLookup.Reset();

	ResetPoseHash = INDEX_NONE;
	ResetPoseIsFilteredOut.Reset();

	if(!IsGarbageCollecting())
	{
		Notify(ERigHierarchyNotification::HierarchyReset, nullptr);
	}
}

void URigHierarchy::CopyHierarchy(URigHierarchy* InHierarchy)
{
	check(InHierarchy);
	
	LLM_SCOPE_BYNAME(TEXT("Animation/ControlRig"));
	
	Reset();

	// Allocate the elements in batches to improve performance
	TArray<uint8*> NewElementsPerType;
	TArray<int32> StructureSizePerType;
	for(int32 ElementTypeIndex = 0; ElementTypeIndex < InHierarchy->ElementsPerType.Num(); ElementTypeIndex++)
	{
		const ERigElementType ElementType = FlatIndexToRigElementType(ElementTypeIndex);
		int32 StructureSize = 0;

		const int32 Count = InHierarchy->ElementsPerType[ElementTypeIndex].Num();
		if(Count)
		{
			FRigBaseElement* ElementMemory = MakeElement(ElementType, Count, &StructureSize);
			NewElementsPerType.Add((uint8*)ElementMemory);
		}
		else
		{
			NewElementsPerType.Add(nullptr);
		}

		StructureSizePerType.Add(StructureSize);
		ElementsPerType[ElementTypeIndex].Reserve(Count);
	}

	Elements.Reserve(InHierarchy->Elements.Num());
	IndexLookup.Reserve(InHierarchy->IndexLookup.Num());
	
	for(int32 Index = 0; Index < InHierarchy->Num(); Index++)
	{
		FRigBaseElement* Source = InHierarchy->Get(Index);
		const FRigElementKey& Key = Source->Key;

		const int32 ElementTypeIndex = RigElementTypeToFlatIndex(Key.Type);
		
		const int32 SubIndex = Num(Key.Type);

		const int32 StructureSize = StructureSizePerType[ElementTypeIndex];
		check(NewElementsPerType[ElementTypeIndex] != nullptr);
		FRigBaseElement* Target = (FRigBaseElement*)&NewElementsPerType[ElementTypeIndex][StructureSize * SubIndex];
		//FRigBaseElement* Target = MakeElement(Key.Type);
		
		Target->Key = Key;
		Target->NameString = Source->NameString;
		Target->SubIndex = SubIndex;
		Target->Index = Elements.Add(Target);

		ElementsPerType[ElementTypeIndex].Add(Target);
		IndexLookup.Add(Key, Target->Index);

		check(Source->Index == Index);
		check(Target->Index == Index);
	}

	for(int32 Index = 0; Index < InHierarchy->Num(); Index++)
	{
		FRigBaseElement* Source = InHierarchy->Get(Index);
		FRigBaseElement* Target = Elements[Index];
		Target->CopyFrom(this, Source, InHierarchy);
	}

	for (const TPair<FRigElementKey, FRigElementKey>& NameMapPair : InHierarchy->PreviousNameMap)
	{
		PreviousNameMap.FindOrAdd(NameMapPair.Key) = NameMapPair.Value;
	}


	TopologyVersion = InHierarchy->GetTopologyVersion();
	UpdateAllCachedChildren();
	
	EnsureCacheValidity();
}

uint32 URigHierarchy::GetNameHash() const
{
	uint32 Hash = GetTypeHash(GetTopologyVersion());

	for (int32 ElementIndex = 0; ElementIndex < Elements.Num(); ElementIndex++)
	{
		const FRigBaseElement* Element = Elements[ElementIndex];
		Hash = HashCombine(Hash, GetTypeHash(Element->GetName()));
	}

	return Hash;
}

#if WITH_EDITOR
void URigHierarchy::RegisterListeningHierarchy(URigHierarchy* InHierarchy)
{
	if (InHierarchy)
	{
		bool bFoundListener = false;
		for(int32 ListenerIndex = ListeningHierarchies.Num() - 1; ListenerIndex >= 0; ListenerIndex--)
		{
			const URigHierarchy::FRigHierarchyListener& Listener = ListeningHierarchies[ListenerIndex];
			if(Listener.Hierarchy.IsValid())
			{
				if(Listener.Hierarchy.Get() == InHierarchy)
				{
					bFoundListener = true;
					break;
				}
			}
		}

		if(!bFoundListener)
		{
			URigHierarchy::FRigHierarchyListener Listener;
			Listener.Hierarchy = InHierarchy; 
			ListeningHierarchies.Add(Listener);
		}
	}
}

void URigHierarchy::UnregisterListeningHierarchy(URigHierarchy* InHierarchy)
{
	if (InHierarchy)
	{
		for(int32 ListenerIndex = ListeningHierarchies.Num() - 1; ListenerIndex >= 0; ListenerIndex--)
		{
			const URigHierarchy::FRigHierarchyListener& Listener = ListeningHierarchies[ListenerIndex];
			if(Listener.Hierarchy.IsValid())
			{
				if(Listener.Hierarchy.Get() == InHierarchy)
				{
					ListeningHierarchies.RemoveAt(ListenerIndex);
				}
			}
		}
	}
}

void URigHierarchy::ClearListeningHierarchy()
{
	ListeningHierarchies.Reset();
}
#endif

void URigHierarchy::CopyPose(URigHierarchy* InHierarchy, bool bCurrent, bool bInitial)
{
	check(InHierarchy);
	LLM_SCOPE_BYNAME(TEXT("Animation/ControlRig"));

	for (int32 ElementIndex = 0; ElementIndex < Elements.Num(); ElementIndex++)
	{
		FRigBaseElement* Element = Elements[ElementIndex];
		if(FRigBaseElement* OtherElement = InHierarchy->Find(Element->GetKey()))
		{
			Element->CopyPose(OtherElement, bCurrent, bInitial);
		}
	}

	EnsureCacheValidity();
}

void URigHierarchy::UpdateReferences(const FRigUnitContext* InContext)
{
	check(InContext);
	LLM_SCOPE_BYNAME(TEXT("Animation/ControlRig"));
	
	for (int32 ElementIndex = 0; ElementIndex < Elements.Num(); ElementIndex++)
	{
		if(FRigReferenceElement* Reference = Cast<FRigReferenceElement>(Elements[ElementIndex]))
		{
			const FTransform InitialWorldTransform = Reference->GetReferenceWorldTransform(InContext, true);
			const FTransform CurrentWorldTransform = Reference->GetReferenceWorldTransform(InContext, false);

			const FTransform InitialGlobalTransform = InitialWorldTransform.GetRelativeTransform(InContext->ToWorldSpaceTransform);
			const FTransform CurrentGlobalTransform = CurrentWorldTransform.GetRelativeTransform(InContext->ToWorldSpaceTransform);

			const FTransform InitialParentTransform = GetParentTransform(Reference, ERigTransformType::InitialGlobal); 
			const FTransform CurrentParentTransform = GetParentTransform(Reference, ERigTransformType::CurrentGlobal);

			const FTransform InitialLocalTransform = InitialGlobalTransform.GetRelativeTransform(InitialParentTransform);
			const FTransform CurrentLocalTransform = CurrentGlobalTransform.GetRelativeTransform(CurrentParentTransform);

			SetTransform(Reference, InitialLocalTransform, ERigTransformType::InitialLocal, true, false);
			SetTransform(Reference, CurrentLocalTransform, ERigTransformType::CurrentLocal, true, false);
		}
	}
}

void URigHierarchy::ResetPoseToInitial(ERigElementType InTypeFilter)
{
	LLM_SCOPE_BYNAME(TEXT("Animation/ControlRig"));
	bool bPerformFiltering = InTypeFilter != ERigElementType::All;

	// if we are resetting the pose on some elements, we need to check if
	// any of affected elements has any children that would not be affected
	// by resetting the pose. if all children are affected we can use the
	// fast path.
	if(bPerformFiltering)
	{
		const int32 Hash = HashCombine(GetTopologyVersion(), (int32)InTypeFilter);
		if(Hash != ResetPoseHash)
		{
			ResetPoseIsFilteredOut.Reset();
			ResetPoseHash = Hash;

			// let's look at all elements and mark all parent of unaffected children
			ResetPoseIsFilteredOut.AddZeroed(Elements.Num());

			Traverse([this, InTypeFilter](FRigBaseElement* InElement, bool& bContinue)
			{
				bContinue = true;
				ResetPoseIsFilteredOut[InElement->GetIndex()] = !InElement->IsTypeOf(InTypeFilter);

				// make sure to distribute the filtering options from
				// the parent to the children of the part of the tree
				const FRigBaseElementParentArray Parents = GetParents(InElement);
				for(const FRigBaseElement* Parent : Parents)
				{
					if(!ResetPoseIsFilteredOut[Parent->GetIndex()])
					{
						ResetPoseIsFilteredOut[InElement->GetIndex()] = false;
					}
				}
			});
		}

		// if the per element state is empty
		// it means that the filter doesn't affect 
		if(ResetPoseIsFilteredOut.IsEmpty())
		{
			bPerformFiltering = false;
		}
	}
	
	for(int32 ElementIndex=0; ElementIndex<Elements.Num(); ElementIndex++)
	{
		if(!ResetPoseIsFilteredOut.IsEmpty() && bPerformFiltering)
		{
			if(ResetPoseIsFilteredOut[ElementIndex])
			{
				continue;
			}
		}

		if(FRigControlElement* ControlElement = Cast<FRigControlElement>(Elements[ElementIndex]))
		{
			ControlElement->Offset.Current = ControlElement->Offset.Initial;
			ControlElement->Shape.Current = ControlElement->Shape.Initial;
		}

		if(FRigTransformElement* TransformElement = Cast<FRigTransformElement>(Elements[ElementIndex]))
		{
			TransformElement->Pose.Current = TransformElement->Pose.Initial;
		}

		if(FRigMultiParentElement* MultiParentElement = Cast<FRigMultiParentElement>(Elements[ElementIndex]))
		{
			MultiParentElement->Parent.Current = MultiParentElement->Parent.Initial;
		}
	}
	
	EnsureCacheValidity();
}

void URigHierarchy::ResetCurveValues()
{
	LLM_SCOPE_BYNAME(TEXT("Animation/ControlRig"));
	for(int32 ElementIndex=0; ElementIndex<Elements.Num(); ElementIndex++)
	{
		if(FRigCurveElement* CurveElement = Cast<FRigCurveElement>(Elements[ElementIndex]))
		{
			SetCurveValue(CurveElement, 0.f);
		}
	}
}

int32 URigHierarchy::Num(ERigElementType InElementType) const
{
	return ElementsPerType[RigElementTypeToFlatIndex(InElementType)].Num();
}

TArray<FRigBaseElement*> URigHierarchy::GetSelectedElements(ERigElementType InTypeFilter) const
{
	LLM_SCOPE_BYNAME(TEXT("Animation/ControlRig"));
	TArray<FRigBaseElement*> Selection;

	if(URigHierarchy* HierarchyForSelection = HierarchyForSelectionPtr.Get())
	{
		TArray<FRigElementKey> SelectedKeys = HierarchyForSelection->GetSelectedKeys(InTypeFilter);
		for(const FRigElementKey& SelectedKey : SelectedKeys)
		{
			if(const FRigBaseElement* Element = Find(SelectedKey))
			{
				Selection.Add((FRigBaseElement*)Element);
			}
		}
		return Selection;
	}

	for (int32 ElementIndex = 0; ElementIndex < Elements.Num(); ElementIndex++)
	{
		FRigBaseElement* Element = Elements[ElementIndex];
		if(Element->IsTypeOf(InTypeFilter))
		{
			if(IsSelected(Element))
			{
				Selection.Add(Element);
			}
		}
	}
	return Selection;
}

TArray<FRigElementKey> URigHierarchy::GetSelectedKeys(ERigElementType InTypeFilter) const
{
	LLM_SCOPE_BYNAME(TEXT("Animation/ControlRig"));
	
	if(URigHierarchy* HierarchyForSelection = HierarchyForSelectionPtr.Get())
	{
		return HierarchyForSelection->GetSelectedKeys(InTypeFilter);
	}

	TArray<FRigElementKey> Selection;
	for (int32 ElementIndex = 0; ElementIndex < Elements.Num(); ElementIndex++)
	{
		FRigBaseElement* Element = Elements[ElementIndex];
		if(Element->IsTypeOf(InTypeFilter))
		{
			if(IsSelected(Element))
			{
				Selection.Add(Element->GetKey());
			}
		}
	}
	return Selection;
}

void URigHierarchy::SanitizeName(FString& InOutName)
{
	// Sanitize the name
	for (int32 i = 0; i < InOutName.Len(); ++i)
	{
		TCHAR& C = InOutName[i];

		const bool bGoodChar = FChar::IsAlpha(C) ||				// Any letter
			(C == '_') || (C == '-') || (C == '.') ||			// _  - . anytime
			((i > 0) && (FChar::IsDigit(C))) ||					// 0-9 after the first character
			((i > 0) && (C== ' '));								// Space after the first character to support virtual bones

		if (!bGoodChar)
		{
			C = '_';
		}
	}

	if (InOutName.Len() > GetMaxNameLength())
	{
		InOutName.LeftChopInline(InOutName.Len() - GetMaxNameLength());
	}
}

FName URigHierarchy::GetSanitizedName(const FString& InName)
{
	FString Name = InName;
	SanitizeName(Name);

	if (Name.IsEmpty())
	{
		return NAME_None;
	}

	return *Name;
}

bool URigHierarchy::IsNameAvailable(const FString& InPotentialNewName, ERigElementType InType, FString* OutErrorMessage) const
{
	FString UnsanitizedName = InPotentialNewName;
	if (UnsanitizedName.Len() > GetMaxNameLength())
	{
		if (OutErrorMessage)
		{
			*OutErrorMessage = TEXT("Name too long.");
		}
		return false;
	}

	if (UnsanitizedName == TEXT("None"))
	{
		if (OutErrorMessage)
		{
			*OutErrorMessage = TEXT("None is not a valid name.");
		}
		return false;
	}

	FString SanitizedName = UnsanitizedName;
	SanitizeName(SanitizedName);

	if (SanitizedName != UnsanitizedName)
	{
		if (OutErrorMessage)
		{
			*OutErrorMessage = TEXT("Name contains invalid characters.");
		}
		return false;
	}

	// check for fixed keywords
	const FRigElementKey PotentialKey(*InPotentialNewName, InType);
	if(PotentialKey == URigHierarchy::GetDefaultParentKey())
	{
		return false;
	}

	if (GetIndex(PotentialKey) != INDEX_NONE)
	{
		if (OutErrorMessage)
		{
			*OutErrorMessage = TEXT("Name already used.");
		}
		return false;
	}

	return true;
}

FName URigHierarchy::GetSafeNewName(const FString& InPotentialNewName, ERigElementType InType) const
{
	FString SanitizedName = InPotentialNewName;
	SanitizeName(SanitizedName);
	FString Name = SanitizedName;

	int32 Suffix = 1;
	while (!IsNameAvailable(Name, InType))
	{
		FString BaseString = SanitizedName;
		if (BaseString.Len() > GetMaxNameLength() - 4)
		{
			BaseString.LeftChopInline(BaseString.Len() - (GetMaxNameLength() - 4));
		}
		Name = *FString::Printf(TEXT("%s_%d"), *BaseString, ++Suffix);
	}
	return *Name;
}

FEdGraphPinType URigHierarchy::GetControlPinType(FRigControlElement* InControlElement) const
{
	check(InControlElement);
	LLM_SCOPE_BYNAME(TEXT("Animation/ControlRig"));

	// local copy of UEdGraphSchema_K2::PC_ ... static members
	static const FName PC_Boolean(TEXT("bool"));
	static const FName PC_Float(TEXT("float"));
	static const FName PC_Int(TEXT("int"));
	static const FName PC_Struct(TEXT("struct"));
	static const FName PC_Real(TEXT("real"));

	FEdGraphPinType PinType;

	switch(InControlElement->Settings.ControlType)
	{
		case ERigControlType::Bool:
		{
			PinType.PinCategory = PC_Boolean;
			break;
		}
		case ERigControlType::Float:
		{
			PinType.PinCategory = PC_Real;
			PinType.PinSubCategory = PC_Float;
			break;
		}
		case ERigControlType::Integer:
		{
			PinType.PinCategory = PC_Int;
			break;
		}
		case ERigControlType::Vector2D:
		{
			PinType.PinCategory = PC_Struct;
			PinType.PinSubCategoryObject = TBaseStructure<FVector2D>::Get();
			break;
		}
		case ERigControlType::Position:
		case ERigControlType::Scale:
		{
			PinType.PinCategory = PC_Struct;
			PinType.PinSubCategoryObject = TBaseStructure<FVector>::Get();
			break;
		}
		case ERigControlType::Rotator:
		{
			PinType.PinCategory = PC_Struct;
			PinType.PinSubCategoryObject = TBaseStructure<FRotator>::Get();
			break;
		}
		case ERigControlType::Transform:
		case ERigControlType::TransformNoScale:
		case ERigControlType::EulerTransform:
		{
			PinType.PinCategory = PC_Struct;
			PinType.PinSubCategoryObject = TBaseStructure<FTransform>::Get();
			break;
		}
	}

	return PinType;
}

FString URigHierarchy::GetControlPinDefaultValue(FRigControlElement* InControlElement, bool bForEdGraph, ERigControlValueType InValueType) const
{
	check(InControlElement);
	LLM_SCOPE_BYNAME(TEXT("Animation/ControlRig"));

	FRigControlValue Value = GetControlValue(InControlElement, InValueType);
	switch(InControlElement->Settings.ControlType)
	{
		case ERigControlType::Bool:
		{
			return Value.ToString<bool>();
		}
		case ERigControlType::Float:
		{
			return Value.ToString<float>();
		}
		case ERigControlType::Integer:
		{
			return Value.ToString<int32>();
		}
		case ERigControlType::Vector2D:
		{
			const FVector3f Vector = Value.Get<FVector3f>();
			const FVector2D Vector2D(Vector.X, Vector.Y);

			if(bForEdGraph)
			{
				return Vector2D.ToString();
			}

			FString Result;
			TBaseStructure<FVector2D>::Get()->ExportText(Result, &Vector2D, nullptr, nullptr, PPF_None, nullptr);
			return Result;
		}
		case ERigControlType::Position:
		case ERigControlType::Scale:
		{
			if(bForEdGraph)
			{
				return FVector(Value.Get<FVector3f>()).ToString();
			}
			return Value.ToString<FVector>();
		}
		case ERigControlType::Rotator:
		{
				if(bForEdGraph)
				{
					const FRotator Rotator = FRotator::MakeFromEuler((FVector)Value.GetRef<FVector3f>());
					return Rotator.ToString();
				}
				return Value.ToString<FRotator>();
		}
		case ERigControlType::Transform:
		case ERigControlType::TransformNoScale:
		case ERigControlType::EulerTransform:
		{
			const FTransform Transform = Value.GetAsTransform(
				InControlElement->Settings.ControlType,
				InControlElement->Settings.PrimaryAxis);
				
			if(bForEdGraph)
			{
				return Transform.ToString();
			}

			FString Result;
			TBaseStructure<FTransform>::Get()->ExportText(Result, &Transform, nullptr, nullptr, PPF_None, nullptr);
			return Result;
		}
	}
	return FString();
}

TArray<FRigElementKey> URigHierarchy::GetChildren(FRigElementKey InKey, bool bRecursive) const
{
	LLM_SCOPE_BYNAME(TEXT("Animation/ControlRig"));
	FRigBaseElementChildrenArray LocalChildren;
	const FRigBaseElementChildrenArray* ChildrenPtr = nullptr;
	if(bRecursive)
	{
		LocalChildren = GetChildren(Find(InKey), true);
		ChildrenPtr = & LocalChildren;
	}
	else
	{
		ChildrenPtr = &GetChildren(Find(InKey));
	}

	const FRigBaseElementChildrenArray& Children = *ChildrenPtr;

	TArray<FRigElementKey> Keys;
	for(const FRigBaseElement* Child : Children)
	{
		Keys.Add(Child->Key);
	}
	return Keys;
}

TArray<int32> URigHierarchy::GetChildren(int32 InIndex, bool bRecursive) const
{
	LLM_SCOPE_BYNAME(TEXT("Animation/ControlRig"));
	FRigBaseElementChildrenArray LocalChildren;
	const FRigBaseElementChildrenArray* ChildrenPtr = nullptr;
	if(bRecursive)
	{
		LocalChildren = GetChildren(Get(InIndex), true);
		ChildrenPtr = & LocalChildren;
	}
	else
	{
		ChildrenPtr = &GetChildren(Get(InIndex));
	}

	const FRigBaseElementChildrenArray& Children = *ChildrenPtr;

	TArray<int32> Indices;
	for(const FRigBaseElement* Child : Children)
	{
		Indices.Add(Child->Index);
	}
	return Indices;
}

const FRigBaseElementChildrenArray& URigHierarchy::GetChildren(const FRigBaseElement* InElement) const
{
	LLM_SCOPE_BYNAME(TEXT("Animation/ControlRig"));
	if(InElement)
	{
		UpdateCachedChildren(InElement);
		return InElement->CachedChildren;
	}
	return EmptyElementArray;
}

FRigBaseElementChildrenArray URigHierarchy::GetChildren(const FRigBaseElement* InElement, bool bRecursive) const
{
	LLM_SCOPE_BYNAME(TEXT("Animation/ControlRig"));
	// call the non-recursive variation
	FRigBaseElementChildrenArray Children = GetChildren(InElement);
	
	if(bRecursive)
	{
		for(int32 ChildIndex = 0; ChildIndex < Children.Num(); ChildIndex++)
		{
			Children.Append(GetChildren(Children[ChildIndex], true));
		}
	}

	return Children;
}

TArray<FRigElementKey> URigHierarchy::GetParents(FRigElementKey InKey, bool bRecursive) const
{
	LLM_SCOPE_BYNAME(TEXT("Animation/ControlRig"));
	const FRigBaseElementParentArray& Parents = GetParents(Find(InKey), bRecursive);
	TArray<FRigElementKey> Keys;
	for(const FRigBaseElement* Parent : Parents)
	{
		Keys.Add(Parent->Key);
	}
	return Keys;
}

TArray<int32> URigHierarchy::GetParents(int32 InIndex, bool bRecursive) const
{
	LLM_SCOPE_BYNAME(TEXT("Animation/ControlRig"));
	const FRigBaseElementParentArray& Parents = GetParents(Get(InIndex), bRecursive);
	TArray<int32> Indices;
	for(const FRigBaseElement* Parent : Parents)
	{
		Indices.Add(Parent->Index);
	}
	return Indices;
}

FRigBaseElementParentArray URigHierarchy::GetParents(const FRigBaseElement* InElement, bool bRecursive) const
{
	LLM_SCOPE_BYNAME(TEXT("Animation/ControlRig"));
	FRigBaseElementParentArray Parents;

	if(const FRigSingleParentElement* SingleParentElement = Cast<FRigSingleParentElement>(InElement))
	{
		if(SingleParentElement->ParentElement)
		{
			Parents.Add(SingleParentElement->ParentElement);
		}
	}
	else if(const FRigMultiParentElement* MultiParentElement = Cast<FRigMultiParentElement>(InElement))
	{
		Parents.Reserve(MultiParentElement->ParentConstraints.Num());
		for(const FRigElementParentConstraint& ParentConstraint : MultiParentElement->ParentConstraints)
		{
			Parents.Add(ParentConstraint.ParentElement);
		}
	}

	if(bRecursive)
	{
		const int32 CurrentNumberParents = Parents.Num();
		for(int32 ParentIndex = 0;ParentIndex < CurrentNumberParents; ParentIndex++)
		{
			const FRigBaseElementParentArray GrandParents = GetParents(Parents[ParentIndex], bRecursive);
			for (FRigBaseElement* GrandParent : GrandParents)
			{
				Parents.AddUnique(GrandParent);
			}
		}
	}

	return Parents;
}

FRigElementKey URigHierarchy::GetFirstParent(FRigElementKey InKey) const
{
	if(FRigBaseElement* FirstParent = GetFirstParent(Find(InKey)))
	{
		return FirstParent->Key;
	}
	return FRigElementKey();
}

int32 URigHierarchy::GetFirstParent(int32 InIndex) const
{
	if(FRigBaseElement* FirstParent = GetFirstParent(Get(InIndex)))
	{
		return FirstParent->Index;
	}
	return INDEX_NONE;
}

FRigBaseElement* URigHierarchy::GetFirstParent(const FRigBaseElement* InElement) const
{
	if(const FRigSingleParentElement* SingleParentElement = Cast<FRigSingleParentElement>(InElement))
	{
		return SingleParentElement->ParentElement;
	}
	else if(const FRigMultiParentElement* MultiParentElement = Cast<FRigMultiParentElement>(InElement))
	{
		if(MultiParentElement->ParentConstraints.Num() > 0)
		{
			return MultiParentElement->ParentConstraints[0].ParentElement;
		}
	}
	
	return nullptr;
}

int32 URigHierarchy::GetNumberOfParents(FRigElementKey InKey) const
{
	return GetNumberOfParents(Find(InKey));
}

int32 URigHierarchy::GetNumberOfParents(int32 InIndex) const
{
	return GetNumberOfParents(Get(InIndex));
}

int32 URigHierarchy::GetNumberOfParents(const FRigBaseElement* InElement) const
{
	if(InElement == nullptr)
	{
		return 0;
	}

	if(const FRigSingleParentElement* SingleParentElement = Cast<FRigSingleParentElement>(InElement))
	{
		return SingleParentElement->ParentElement == nullptr ? 0 : 1;
	}
	else if(const FRigMultiParentElement* MultiParentElement = Cast<FRigMultiParentElement>(InElement))
	{
		return MultiParentElement->ParentConstraints.Num();
	}

	return 0;
}

FRigElementWeight URigHierarchy::GetParentWeight(FRigElementKey InChild, FRigElementKey InParent, bool bInitial) const
{
	return GetParentWeight(Find(InChild), Find(InParent), bInitial);
}

FRigElementWeight URigHierarchy::GetParentWeight(const FRigBaseElement* InChild, const FRigBaseElement* InParent, bool bInitial) const
{
	if(const FRigMultiParentElement* MultiParentElement = Cast<FRigMultiParentElement>(InChild))
	{
		if(const int32* ParentIndexPtr = MultiParentElement->IndexLookup.Find(InParent->GetKey()))
		{
			return GetParentWeight(InChild, *ParentIndexPtr, bInitial);
		}
	}
	return FRigElementWeight(FLT_MAX);
}

FRigElementWeight URigHierarchy::GetParentWeight(const FRigBaseElement* InChild, int32 InParentIndex, bool bInitial) const
{
	if(const FRigMultiParentElement* MultiParentElement = Cast<FRigMultiParentElement>(InChild))
	{
		if(MultiParentElement->ParentConstraints.IsValidIndex(InParentIndex))
		{
			if(bInitial)
			{
				return MultiParentElement->ParentConstraints[InParentIndex].InitialWeight;
			}
			else
			{
				return MultiParentElement->ParentConstraints[InParentIndex].Weight;
			}
		}
	}
	return FRigElementWeight(FLT_MAX);
}

TArray<FRigElementWeight> URigHierarchy::GetParentWeightArray(FRigElementKey InChild, bool bInitial) const
{
	LLM_SCOPE_BYNAME(TEXT("Animation/ControlRig"));
	return GetParentWeightArray(Find(InChild), bInitial);
}

TArray<FRigElementWeight> URigHierarchy::GetParentWeightArray(const FRigBaseElement* InChild, bool bInitial) const
{
	LLM_SCOPE_BYNAME(TEXT("Animation/ControlRig"));
	
	TArray<FRigElementWeight> Weights;
	if(const FRigMultiParentElement* MultiParentElement = Cast<FRigMultiParentElement>(InChild))
	{
		for(int32 ParentIndex = 0; ParentIndex < MultiParentElement->ParentConstraints.Num(); ParentIndex++)
		{
			if(bInitial)
			{
				Weights.Add(MultiParentElement->ParentConstraints[ParentIndex].InitialWeight);
			}
			else
			{
				Weights.Add(MultiParentElement->ParentConstraints[ParentIndex].Weight);
			}
		}
	}
	return Weights;
}

FRigElementKey URigHierarchy::GetActiveParent(const FRigElementKey& InKey) const
{
	const TArray<FRigElementWeight> ParentWeights = GetParentWeightArray(InKey);
	if (ParentWeights.Num() > 0)
	{
		const TArray<FRigElementKey> ParentKeys = GetParents(InKey);
		check(ParentKeys.Num() == ParentWeights.Num());
		for (int32 ParentIndex = 0; ParentIndex < ParentKeys.Num(); ParentIndex++)
		{
			if (ParentWeights[ParentIndex].IsAlmostZero())
			{
				continue;
			}
			if (ParentIndex == 0)
			{
				if (!(ParentKeys[ParentIndex] == URigHierarchy::GetDefaultParentKey() || ParentKeys[ParentIndex] == URigHierarchy::GetWorldSpaceReferenceKey()))
				{
					return URigHierarchy::GetDefaultParentKey();
				}
			}
			return ParentKeys[ParentIndex];
		}
	}
	return URigHierarchy::GetDefaultParentKey();
}


bool URigHierarchy::SetParentWeight(FRigElementKey InChild, FRigElementKey InParent, FRigElementWeight InWeight, bool bInitial, bool bAffectChildren)
{
	return SetParentWeight(Find(InChild), Find(InParent), InWeight, bInitial, bAffectChildren);
}

bool URigHierarchy::SetParentWeight(FRigBaseElement* InChild, const FRigBaseElement* InParent, FRigElementWeight InWeight, bool bInitial, bool bAffectChildren)
{
	LLM_SCOPE_BYNAME(TEXT("Animation/ControlRig"));
	if(const FRigMultiParentElement* MultiParentElement = Cast<FRigMultiParentElement>(InChild))
	{
		if(const int32* ParentIndexPtr = MultiParentElement->IndexLookup.Find(InParent->GetKey()))
		{
			return SetParentWeight(InChild, *ParentIndexPtr, InWeight, bInitial, bAffectChildren);
		}
	}
	return false;
}

bool URigHierarchy::SetParentWeight(FRigBaseElement* InChild, int32 InParentIndex, FRigElementWeight InWeight, bool bInitial, bool bAffectChildren)
{
	LLM_SCOPE_BYNAME(TEXT("Animation/ControlRig"));
	using namespace ERigTransformType;

	if(FRigMultiParentElement* MultiParentElement = Cast<FRigMultiParentElement>(InChild))
	{
		if(MultiParentElement->ParentConstraints.IsValidIndex(InParentIndex))
		{
			InWeight.Location = FMath::Max(InWeight.Location, 0.f);
			InWeight.Rotation = FMath::Max(InWeight.Rotation, 0.f);
			InWeight.Scale = FMath::Max(InWeight.Scale, 0.f);

			FRigElementWeight& TargetWeight = bInitial?
				MultiParentElement->ParentConstraints[InParentIndex].InitialWeight :
				MultiParentElement->ParentConstraints[InParentIndex].Weight;

			if(FMath::IsNearlyZero(InWeight.Location - TargetWeight.Location) &&
				FMath::IsNearlyZero(InWeight.Rotation - TargetWeight.Rotation) &&
				FMath::IsNearlyZero(InWeight.Scale - TargetWeight.Scale))
			{
				return false;
			}
			
			const ERigTransformType::Type LocalType = bInitial ? InitialLocal : CurrentLocal;
			const ERigTransformType::Type GlobalType = SwapLocalAndGlobal(LocalType);

			if(bAffectChildren)
			{
				GetParentTransform(MultiParentElement, LocalType);
				if(FRigControlElement* ControlElement = Cast<FRigControlElement>(MultiParentElement))
				{
					GetControlOffsetTransform(ControlElement, LocalType);
				}
				GetTransform(MultiParentElement, LocalType);
				MultiParentElement->Pose.MarkDirty(GlobalType);
			}
			else
			{
				GetParentTransform(MultiParentElement, GlobalType);
				if(FRigControlElement* ControlElement = Cast<FRigControlElement>(MultiParentElement))
				{
					GetControlOffsetTransform(ControlElement, GlobalType);
				}
				GetTransform(MultiParentElement, GlobalType);
				MultiParentElement->Pose.MarkDirty(LocalType);
			}

			TargetWeight = InWeight;
			MultiParentElement->Parent.MarkDirty(GlobalType);

			if(FRigControlElement* ControlElement = Cast<FRigControlElement>(MultiParentElement))
			{
				ControlElement->Offset.MarkDirty(GlobalType);
			}

			PropagateDirtyFlags(MultiParentElement, ERigTransformType::IsInitial(LocalType), bAffectChildren);
			EnsureCacheValidity();
			
#if WITH_EDITOR
			if (ensure(!bPropagatingChange))
			{
				TGuardValue<bool> bPropagatingChangeGuardValue(bPropagatingChange, true);
			
				for(FRigHierarchyListener& Listener : ListeningHierarchies)
				{
					if(!bForcePropagation && !Listener.ShouldReactToChange(LocalType))
					{
						continue;
					}

					URigHierarchy* ListeningHierarchy = Listener.Hierarchy.Get();
					if (ListeningHierarchy)
					{
						if(FRigBaseElement* ListeningElement = ListeningHierarchy->Find(InChild->GetKey()))
						{
							ListeningHierarchy->SetParentWeight(ListeningElement, InParentIndex, InWeight, bInitial, bAffectChildren);
						}
					}
				}	
			}
#endif

			Notify(ERigHierarchyNotification::ParentWeightsChanged, MultiParentElement);
			return true;
		}
	}
	return false;
}

bool URigHierarchy::SetParentWeightArray(FRigElementKey InChild, TArray<FRigElementWeight> InWeights, bool bInitial,
	bool bAffectChildren)
{
	return SetParentWeightArray(Find(InChild), InWeights, bInitial, bAffectChildren);
}

bool URigHierarchy::SetParentWeightArray(FRigBaseElement* InChild, const TArray<FRigElementWeight>& InWeights,
	bool bInitial, bool bAffectChildren)
{
	LLM_SCOPE_BYNAME(TEXT("Animation/ControlRig"));
	if(InWeights.Num() == 0)
	{
		return false;
	}
	
	TArrayView<const FRigElementWeight> View(InWeights.GetData(), InWeights.Num());
	return SetParentWeightArray(InChild, View, bInitial, bAffectChildren);
}

bool URigHierarchy::SetParentWeightArray(FRigBaseElement* InChild,  const TArrayView<const FRigElementWeight>& InWeights,
	bool bInitial, bool bAffectChildren)
{
	LLM_SCOPE_BYNAME(TEXT("Animation/ControlRig"));
	using namespace ERigTransformType;

	if(FRigMultiParentElement* MultiParentElement = Cast<FRigMultiParentElement>(InChild))
	{
		if(MultiParentElement->ParentConstraints.Num() == InWeights.Num())
		{
			TArray<FRigElementWeight> InputWeights;
			InputWeights.Reserve(InWeights.Num());

			bool bFoundDifference = false;
			for(int32 WeightIndex=0; WeightIndex < InWeights.Num(); WeightIndex++)
			{
				FRigElementWeight InputWeight = InWeights[WeightIndex];
				InputWeight.Location = FMath::Max(InputWeight.Location, 0.f);
				InputWeight.Rotation = FMath::Max(InputWeight.Rotation, 0.f);
				InputWeight.Scale = FMath::Max(InputWeight.Scale, 0.f);
				InputWeights.Add(InputWeight);

				FRigElementWeight& TargetWeight = bInitial?
					MultiParentElement->ParentConstraints[WeightIndex].InitialWeight :
					MultiParentElement->ParentConstraints[WeightIndex].Weight;

				if(!FMath::IsNearlyZero(InputWeight.Location - TargetWeight.Location) ||
					!FMath::IsNearlyZero(InputWeight.Rotation - TargetWeight.Rotation) ||
					!FMath::IsNearlyZero(InputWeight.Scale - TargetWeight.Scale))
				{
					bFoundDifference = true;
				}
			}

			if(!bFoundDifference)
			{
				return false;
			}
			
			const ERigTransformType::Type LocalType = bInitial ? InitialLocal : CurrentLocal;
			const ERigTransformType::Type GlobalType = SwapLocalAndGlobal(LocalType);

			if(bAffectChildren)
			{
				GetTransform(MultiParentElement, LocalType);
				MultiParentElement->Pose.MarkDirty(GlobalType);
			}
			else
			{
				GetTransform(MultiParentElement, GlobalType);
				MultiParentElement->Pose.MarkDirty(LocalType);
			}

			for(int32 WeightIndex=0; WeightIndex < InWeights.Num(); WeightIndex++)
			{
				if(bInitial)
				{
					MultiParentElement->ParentConstraints[WeightIndex].InitialWeight = InputWeights[WeightIndex];
				}
				else
				{
					MultiParentElement->ParentConstraints[WeightIndex].Weight = InputWeights[WeightIndex];
				}
			}

			MultiParentElement->Parent.MarkDirty(GlobalType);

			if(FRigControlElement* ControlElement = Cast<FRigControlElement>(MultiParentElement))
			{
				ControlElement->Offset.MarkDirty(GlobalType);
			}

			PropagateDirtyFlags(MultiParentElement, ERigTransformType::IsInitial(LocalType), bAffectChildren);
			
#if WITH_EDITOR
			if (ensure(!bPropagatingChange))
			{
				TGuardValue<bool> bPropagatingChangeGuardValue(bPropagatingChange, true);
			
				for(FRigHierarchyListener& Listener : ListeningHierarchies)
				{
					if(!bForcePropagation && !Listener.ShouldReactToChange(LocalType))
					{
						continue;
					}

					URigHierarchy* ListeningHierarchy = Listener.Hierarchy.Get();
					if (ListeningHierarchy)
					{
						if(FRigBaseElement* ListeningElement = ListeningHierarchy->Find(InChild->GetKey()))
						{
							ListeningHierarchy->SetParentWeightArray(ListeningElement, InWeights, bInitial, bAffectChildren);
						}
					}
				}	
			}
#endif

			Notify(ERigHierarchyNotification::ParentWeightsChanged, MultiParentElement);

			return true;
		}
	}
	return false;
}

bool URigHierarchy::CanSwitchToParent(FRigElementKey InChild, FRigElementKey InParent, const TElementDependencyMap& InDependencyMap, FString* OutFailureReason)
{
	InParent = PreprocessParentElementKeyForSpaceSwitching(InChild, InParent);

	FRigBaseElement* Child = Find(InChild);
	if(Child == nullptr)
	{
		if(OutFailureReason)
		{
			*OutFailureReason = FString::Printf(TEXT("Child Element %s cannot be found."), *InChild.ToString());
		}
		return false;
	}

	FRigBaseElement* Parent = Find(InParent);
	if(Parent == nullptr)
	{
		// if we don't specify anything and the element is parented directly to the world,
		// perfomring this switch means unparenting it from world (since there is no default parent)
		if(!InParent.IsValid() && GetFirstParent(InChild) == GetWorldSpaceReferenceKey())
		{
			return true;
		}
		
		if(OutFailureReason)
		{
			*OutFailureReason = FString::Printf(TEXT("Parent Element %s cannot be found."), *InParent.ToString());
		}
		return false;
	}

	// see if this is already parented to the target parent
	if(GetFirstParent(Child) == Parent)
	{
		return true;
	}

	const FRigMultiParentElement* MultiParentChild = Cast<FRigMultiParentElement>(Child);
	if(MultiParentChild == nullptr)
	{
		if(OutFailureReason)
		{
			*OutFailureReason = FString::Printf(TEXT("Child Element %s does not allow space switching (it's not a multi parent element)."), *InChild.ToString());
		}
	}

	const FRigTransformElement* TransformParent = Cast<FRigMultiParentElement>(Parent);
	if(TransformParent == nullptr)
	{
		if(OutFailureReason)
		{
			*OutFailureReason = FString::Printf(TEXT("Parent Element %s is not a transform element"), *InParent.ToString());
		}
	}

	if(IsParentedTo(Parent, Child, InDependencyMap))
	{
		if(OutFailureReason)
		{
			*OutFailureReason = FString::Printf(TEXT("Cannot switch '%s' to '%s' - would cause a cycle."), *InChild.ToString(), *InParent.ToString());
		}
		return false;
	}

	return true;
}

bool URigHierarchy::SwitchToParent(FRigElementKey InChild, FRigElementKey InParent, bool bInitial, bool bAffectChildren, const TElementDependencyMap& InDependencyMap, FString* OutFailureReason)
{
	InParent = PreprocessParentElementKeyForSpaceSwitching(InChild, InParent);
	return SwitchToParent(Find(InChild), Find(InParent), bInitial, bAffectChildren, InDependencyMap, OutFailureReason);
}

bool URigHierarchy::SwitchToParent(FRigBaseElement* InChild, FRigBaseElement* InParent, bool bInitial,
	bool bAffectChildren, const TElementDependencyMap& InDependencyMap, FString* OutFailureReason)
{
	if(InChild && InParent)
	{
		if(!CanSwitchToParent(InChild->GetKey(), InParent->GetKey(), InDependencyMap, OutFailureReason))
		{
			return false;
		}
	}

	if(const FRigMultiParentElement* MultiParentElement = Cast<FRigMultiParentElement>(InChild))
	{
		int32 ParentIndex = INDEX_NONE;
		if(InParent)
		{
			if(const int32* ParentIndexPtr = MultiParentElement->IndexLookup.Find(InParent->GetKey()))
			{
				ParentIndex = *ParentIndexPtr;
			}
			else
			{
				if(URigHierarchyController* Controller = GetController(true))
				{
					if(Controller->AddParent(InChild, InParent, 0.f, true, false))
					{
						ParentIndex = MultiParentElement->IndexLookup.FindChecked(InParent->GetKey());
					}
				}
			}
		}
		return SwitchToParent(InChild, ParentIndex, bInitial, bAffectChildren);
	}
	return false;
}

bool URigHierarchy::SwitchToParent(FRigBaseElement* InChild, int32 InParentIndex, bool bInitial, bool bAffectChildren)
{
	TArray<FRigElementWeight> Weights = GetParentWeightArray(InChild, bInitial);
	FMemory::Memzero(Weights.GetData(), Weights.GetAllocatedSize());
	if(Weights.IsValidIndex(InParentIndex))
	{
		Weights[InParentIndex] = 1.f;
	}
	return SetParentWeightArray(InChild, Weights, bInitial, bAffectChildren);
}

bool URigHierarchy::SwitchToDefaultParent(FRigElementKey InChild, bool bInitial, bool bAffectChildren)
{
	return SwitchToParent(InChild, GetDefaultParentKey(), bInitial, bAffectChildren);
}

bool URigHierarchy::SwitchToDefaultParent(FRigBaseElement* InChild, bool bInitial, bool bAffectChildren)
{
	// we assume that the first stored parent is the default parent
	check(InChild);
	return SwitchToParent(InChild->GetKey(), GetDefaultParentKey(), bInitial, bAffectChildren);
}

bool URigHierarchy::SwitchToWorldSpace(FRigElementKey InChild, bool bInitial, bool bAffectChildren)
{
	return SwitchToParent(InChild, GetWorldSpaceReferenceKey(), bInitial, bAffectChildren);
}

bool URigHierarchy::SwitchToWorldSpace(FRigBaseElement* InChild, bool bInitial, bool bAffectChildren)
{
	check(InChild);
	return SwitchToParent(InChild->GetKey(), GetWorldSpaceReferenceKey(), bInitial, bAffectChildren);
}

FRigElementKey URigHierarchy::GetOrAddWorldSpaceReference()
{
	const FRigElementKey WorldSpaceReferenceKey = GetWorldSpaceReferenceKey();

	FRigBaseElement* Parent = Find(WorldSpaceReferenceKey);
	if(Parent)
	{
		return Parent->GetKey();
	}

	if(URigHierarchyController* Controller = GetController(true))
	{
		return Controller->AddReference(
			WorldSpaceReferenceKey.Name,
			FRigElementKey(),
			FRigReferenceGetWorldTransformDelegate::CreateUObject(this, &URigHierarchy::GetWorldTransformForReference),
			false);
	}

	return FRigElementKey();
}

FRigElementKey URigHierarchy::GetDefaultParentKey()
{
	static const FName DefaultParentName = TEXT("DefaultParent");
	return FRigElementKey(DefaultParentName, ERigElementType::Reference); 
}

FRigElementKey URigHierarchy::GetWorldSpaceReferenceKey()
{
	static const FName WorldSpaceReferenceName = TEXT("WorldSpace");
	return FRigElementKey(WorldSpaceReferenceName, ERigElementType::Reference); 
}

TArray<FRigElementKey> URigHierarchy::GetAllKeys(bool bTraverse, ERigElementType InElementType) const
{
	LLM_SCOPE_BYNAME(TEXT("Animation/ControlRig"))

	return GetKeysByPredicate([InElementType](const FRigBaseElement& InElement)
	{
		return InElement.IsTypeOf(InElementType);
	}, bTraverse);
}

TArray<FRigElementKey> URigHierarchy::GetKeysByPredicate(
	TFunctionRef<bool(const FRigBaseElement&)> InPredicateFunc,
	bool bTraverse
	) const
{
	auto ElementTraverser = [&](TFunctionRef<void(const FRigBaseElement&)> InProcessFunc)
	{
		if(bTraverse)
		{
			// TBitArray reserves 4, we'll do 16 so we can remember at least 512 elements before
			// we need to hit the heap.
			TBitArray<TInlineAllocator<16>> ElementVisited(false, Elements.Num());
			
			for (int32 ElementIndex = 0; ElementIndex < Elements.Num(); ElementIndex++)
			{
				FRigBaseElement* Element = Elements[ElementIndex];
				Traverse(Element, true, [&ElementVisited, InProcessFunc, InPredicateFunc](FRigBaseElement* InElement, bool& bContinue)
				{
					bContinue = !ElementVisited[InElement->GetIndex()];

					if(bContinue)
					{
						if(InPredicateFunc(*InElement))
						{
							InProcessFunc(*InElement);
						}
						ElementVisited[InElement->GetIndex()] = true;
					}
				});
			}
		}
		else
		{
			for (int32 ElementIndex = 0; ElementIndex < Elements.Num(); ElementIndex++)
			{
				const FRigBaseElement* Element = Elements[ElementIndex];
				if(InPredicateFunc(*Element))
				{
					InProcessFunc(*Element);
				}
			}
		}
	};
	
	// First count up how many elements we matched and only reserve that amount. There's very little overhead
	// since we're just running over the same data, so it should still be hot when we do the second pass.
	int32 NbElements = 0;
	ElementTraverser([&NbElements](const FRigBaseElement&) { NbElements++; });
	
	TArray<FRigElementKey> Keys;
	Keys.Reserve(NbElements);
	ElementTraverser([&Keys](const FRigBaseElement& InElement) { Keys.Add(InElement.GetKey()); });

	return Keys;
}

void URigHierarchy::Traverse(FRigBaseElement* InElement, bool bTowardsChildren,
                             TFunction<void(FRigBaseElement*, bool&)> PerElementFunction) const
{
	bool bContinue = true;
	PerElementFunction(InElement, bContinue);

	if(bContinue)
	{
		if(bTowardsChildren)
		{
			const FRigBaseElementChildrenArray& Children = GetChildren(InElement);
			for (FRigBaseElement* Child : Children)
			{
				Traverse(Child, true, PerElementFunction);
			}
		}
		else
		{
			FRigBaseElementParentArray Parents = GetParents(InElement);
			for (FRigBaseElement* Parent : Parents)
			{
				Traverse(Parent, false, PerElementFunction);
			}
		}
	}
}

void URigHierarchy::Traverse(TFunction<void(FRigBaseElement*, bool& /* continue */)> PerElementFunction, bool bTowardsChildren) const
{
	if(bTowardsChildren)
	{
		for (int32 ElementIndex = 0; ElementIndex < Elements.Num(); ElementIndex++)
		{
			FRigBaseElement* Element = Elements[ElementIndex];
			if(GetNumberOfParents(Element) == 0)
			{
				Traverse(Element, bTowardsChildren, PerElementFunction);
			}
        }
	}
	else
	{
		for (int32 ElementIndex = 0; ElementIndex < Elements.Num(); ElementIndex++)
		{
			FRigBaseElement* Element = Elements[ElementIndex];
			if(GetChildren(Element).Num() == 0)
			{
				Traverse(Element, bTowardsChildren, PerElementFunction);
			}
		}
	}
}

bool URigHierarchy::Undo()
{
#if WITH_EDITOR
	
	if(TransformUndoStack.IsEmpty())
	{
		return false;
	}

	const FRigTransformStackEntry Entry = TransformUndoStack.Pop();
	ApplyTransformFromStack(Entry, true);
	UndoRedoEvent.Broadcast(this, Entry.Key, Entry.TransformType, Entry.OldTransform, true);
	TransformRedoStack.Push(Entry);
	TransformStackIndex = TransformUndoStack.Num();
	return true;
	
#else
	
	return false;
	
#endif
}

bool URigHierarchy::Redo()
{
#if WITH_EDITOR

	if(TransformRedoStack.IsEmpty())
	{
		return false;
	}

	const FRigTransformStackEntry Entry = TransformRedoStack.Pop();
	ApplyTransformFromStack(Entry, false);
	UndoRedoEvent.Broadcast(this, Entry.Key, Entry.TransformType, Entry.NewTransform, false);
	TransformUndoStack.Push(Entry);
	TransformStackIndex = TransformUndoStack.Num();
	return true;
	
#else
	
	return false;
	
#endif
}

bool URigHierarchy::SetTransformStackIndex(int32 InTransformStackIndex)
{
#if WITH_EDITOR

	while(TransformUndoStack.Num() > InTransformStackIndex)
	{
		if(TransformUndoStack.Num() == 0)
		{
			return false;
		}

		if(!Undo())
		{
			return false;
		}
	}
	
	while(TransformUndoStack.Num() < InTransformStackIndex)
	{
		if(TransformRedoStack.Num() == 0)
		{
			return false;
		}

		if(!Redo())
		{
			return false;
		}
	}

	return InTransformStackIndex == TransformStackIndex;

#else
	
	return false;
	
#endif
}

#if WITH_EDITOR

void URigHierarchy::PostTransacted(const FTransactionObjectEvent& TransactionEvent)
{
	if (TransactionEvent.GetEventType() == ETransactionObjectEventType::UndoRedo)
	{
		const int32 DesiredStackIndex = TransformStackIndex;
		TransformStackIndex = TransformUndoStack.Num();
		if (DesiredStackIndex == TransformStackIndex)
		{
			return;
		}
		SetTransformStackIndex(DesiredStackIndex);
	}
}

#endif

void URigHierarchy::SendEvent(const FRigEventContext& InEvent, bool bAsynchronous)
{
	if(EventDelegate.IsBound())
	{
		TWeakObjectPtr<URigHierarchy> WeakThis = this;
		FRigEventDelegate& Delegate = EventDelegate;

		if (bAsynchronous)
		{
			FFunctionGraphTask::CreateAndDispatchWhenReady([WeakThis, Delegate, InEvent]()
            {
                Delegate.Broadcast(WeakThis.Get(), InEvent);
            }, TStatId(), NULL, ENamedThreads::GameThread);
		}
		else
		{
			Delegate.Broadcast(this, InEvent);
		}
	}

}

void URigHierarchy::SendAutoKeyEvent(FRigElementKey InElement, float InOffsetInSeconds, bool bAsynchronous)
{
	FRigEventContext Context;
	Context.Event = ERigEvent::RequestAutoKey;
	Context.Key = InElement;
	Context.LocalTime = InOffsetInSeconds;
	if(UControlRig* Rig = Cast<UControlRig>(GetOuter()))
	{
		Context.LocalTime += Rig->AbsoluteTime;
	}
	SendEvent(Context, bAsynchronous);
}

URigHierarchyController* URigHierarchy::GetController(bool bCreateIfNeeded)
{
	if(HierarchyController)
	{
		return HierarchyController;
	}
	else if(bCreateIfNeeded)
	{
		 if(!IsGarbageCollecting())
		 {
			 HierarchyController = NewObject<URigHierarchyController>(this, TEXT("HierarchyController"), RF_Transient);
			 HierarchyController->SetHierarchy(this);
			 return HierarchyController;
		 }
	}
	return nullptr;
}

void URigHierarchy::IncrementTopologyVersion()
{
	TopologyVersion++;
	KeyCollectionCache.Reset();
}

FRigPose URigHierarchy::GetPose(
	bool bInitial,
	ERigElementType InElementType,
	const FRigElementKeyCollection& InItems 
) const
{
	return GetPose(bInitial, InElementType, TArrayView<const FRigElementKey>(InItems.Keys.GetData(), InItems.Num()));
}

FRigPose URigHierarchy::GetPose(bool bInitial, ERigElementType InElementType,
	const TArrayView<const FRigElementKey>& InItems) const
{
	LLM_SCOPE_BYNAME(TEXT("Animation/ControlRig"));
	FRigPose Pose;
	Pose.HierarchyTopologyVersion = GetTopologyVersion();
	Pose.PoseHash = Pose.HierarchyTopologyVersion;

	for (int32 ElementIndex = 0; ElementIndex < Elements.Num(); ElementIndex++)
	{
		FRigBaseElement* Element = Elements[ElementIndex];

		// filter by type
		if (((uint8)InElementType & (uint8)Element->GetType()) == 0)
		{
			continue;
		}

		// filter by optional collection
		if(InItems.Num() > 0)
		{
			if(!InItems.Contains(Element->GetKey()))
			{
				continue;
			}
		}
		
		FRigPoseElement PoseElement;
		PoseElement.Index.UpdateCache(Element->GetKey(), this);
		
		if(FRigTransformElement* TransformElement = Cast<FRigTransformElement>(Element))
		{
			PoseElement.LocalTransform = GetTransform(TransformElement, bInitial ? ERigTransformType::InitialLocal : ERigTransformType::CurrentLocal);
			PoseElement.GlobalTransform = GetTransform(TransformElement, bInitial ? ERigTransformType::InitialGlobal : ERigTransformType::CurrentGlobal);
		}
		else if(FRigCurveElement* CurveElement = Cast<FRigCurveElement>(Element))
		{
			PoseElement.CurveValue = GetCurveValue(CurveElement);
		}
		else
		{
			continue;
		}
		Pose.Elements.Add(PoseElement);
		Pose.PoseHash = HashCombine(Pose.PoseHash, GetTypeHash(PoseElement.Index.GetKey()));
	}
	return Pose;
}

void URigHierarchy::SetPose(
	const FRigPose& InPose,
	ERigTransformType::Type InTransformType,
	ERigElementType InElementType,
	const FRigElementKeyCollection& InItems,
	float InWeight
)
{
	SetPose(InPose, InTransformType, InElementType, TArrayView<const FRigElementKey>(InItems.Keys.GetData(), InItems.Num()), InWeight);
}

void URigHierarchy::SetPose(const FRigPose& InPose, ERigTransformType::Type InTransformType,
	ERigElementType InElementType, const TArrayView<const FRigElementKey>& InItems, float InWeight)
{
	LLM_SCOPE_BYNAME(TEXT("Animation/ControlRig"));
	const float U = FMath::Clamp(InWeight, 0.f, 1.f);
	if(U < SMALL_NUMBER)
	{
		return;
	}
	
	for(const FRigPoseElement& PoseElement : InPose)
	{
		FCachedRigElement Index = PoseElement.Index;

		// filter by type
		if (((uint8)InElementType & (uint8)Index.GetKey().Type) == 0)
		{
			continue;
		}

		// filter by optional collection
		if(InItems.Num() > 0)
		{
			if(!InItems.Contains(Index.GetKey()))
			{
				continue;
			}
		}

		if(Index.UpdateCache(this))
		{
			FRigBaseElement* Element = Get(Index.GetIndex());
			if(FRigTransformElement* TransformElement = Cast<FRigTransformElement>(Element))
			{
				FTransform TransformToSet =
					ERigTransformType::IsLocal(InTransformType) ?
						PoseElement.LocalTransform :
						PoseElement.GlobalTransform;
				
				if(U < 1.f - SMALL_NUMBER)
				{
					const FTransform PreviousTransform = GetTransform(TransformElement, InTransformType);
					TransformToSet = FControlRigMathLibrary::LerpTransform(PreviousTransform, TransformToSet, U);
				}

				SetTransform(TransformElement, TransformToSet, InTransformType, true);
			}
			else if(FRigCurveElement* CurveElement = Cast<FRigCurveElement>(Element))
			{
				SetCurveValue(CurveElement, PoseElement.CurveValue);
			}
		}
	}
}

void URigHierarchy::Notify(ERigHierarchyNotification InNotifType, const FRigBaseElement* InElement)
{
	if(bSuspendNotifications)
	{
		return;
	}
	ModifiedEvent.Broadcast(InNotifType, this, InElement);

#if WITH_EDITOR

	// certain events needs to be forwarded to the listening hierarchies.
	// this mainly has to do with topological change within the hierarchy.
	switch (InNotifType)
	{
		case ERigHierarchyNotification::ElementAdded:
		case ERigHierarchyNotification::ElementRemoved:
		case ERigHierarchyNotification::ElementRenamed:
		case ERigHierarchyNotification::ParentChanged:
		case ERigHierarchyNotification::ParentWeightsChanged:
		{
			if (ensure(InElement != nullptr))
			{
				for(FRigHierarchyListener& Listener : ListeningHierarchies)
				{
					URigHierarchy* ListeningHierarchy = Listener.Hierarchy.Get();
					if (ListeningHierarchy)
					{			
						if(const FRigBaseElement* ListeningElement = ListeningHierarchy->Find( InElement->GetKey()))
						{
							ListeningHierarchy->Notify(InNotifType, ListeningElement);
						}
					}
				}
			}
			break;
		}
		default:
		{
			break;
		}
	}

#endif
}

FTransform URigHierarchy::GetTransform(FRigTransformElement* InTransformElement,
	const ERigTransformType::Type InTransformType) const
{
	LLM_SCOPE_BYNAME(TEXT("Animation/ControlRig"));
	if(InTransformElement == nullptr)
	{
		return FTransform::Identity;
	}

#if WITH_EDITOR

	if(bRecordTransformsPerInstruction && ExecuteContext)
	{
		TArray<TArray<int32>>& ReadTransformsPerSlice = ReadTransformsPerInstructionPerSlice[ExecuteContext->InstructionIndex];
		while(ReadTransformsPerSlice.Num() < ExecuteContext->GetSlice().TotalNum())
		{
			ReadTransformsPerSlice.Add(TArray<int32>());
		}
		ReadTransformsPerSlice[ExecuteContext->GetSlice().GetIndex()].Add(InTransformElement->GetIndex());
	}
	TGuardValue<bool> RecordTransformsPerInstructionGuard(bRecordTransformsPerInstruction, false);
	
#endif
	
	if(InTransformElement->Pose.IsDirty(InTransformType))
	{
		const ERigTransformType::Type OpposedType = SwapLocalAndGlobal(InTransformType);
		const ERigTransformType::Type GlobalType = MakeGlobal(InTransformType);
		ensure(!InTransformElement->Pose.IsDirty(OpposedType));

		FTransform ParentTransform;
		if(IsLocal(InTransformType))
		{
			if(FRigControlElement* ControlElement = Cast<FRigControlElement>(InTransformElement))
			{
				const FTransform NewTransform = ComputeLocalControlValue(ControlElement, ControlElement->Pose.Get(OpposedType), GlobalType);
				InTransformElement->Pose.Set(InTransformType, NewTransform);
			}
			else
			{
				ParentTransform = GetParentTransform(InTransformElement, GlobalType);

				FTransform NewTransform = InTransformElement->Pose.Get(OpposedType).GetRelativeTransform(ParentTransform);
				NewTransform.NormalizeRotation();
				InTransformElement->Pose.Set(InTransformType, NewTransform);
			}
		}
		else
		{
			if(FRigControlElement* ControlElement = Cast<FRigControlElement>(InTransformElement))
			{
				
				// using GetControlOffsetTransform to check dirty flag before accessing the transform
				// note: no need to do the same for Pose.Local because there is already an ensure:
				// "ensure(!InTransformElement->Pose.IsDirty(OpposedType));" above
				const FTransform NewTransform = SolveParentConstraints(
					ControlElement->ParentConstraints, InTransformType,
					GetControlOffsetTransform(ControlElement, OpposedType), true,
					ControlElement->Pose.Get(OpposedType), true);
				ControlElement->Pose.Set(InTransformType, NewTransform);
			}
			else
			{
				ParentTransform = GetParentTransform(InTransformElement, GlobalType);

				FTransform NewTransform = InTransformElement->Pose.Get(OpposedType) * ParentTransform;
				NewTransform.NormalizeRotation();
				InTransformElement->Pose.Set(InTransformType, NewTransform);
			}
		}

		EnsureCacheValidity();
	}
	return InTransformElement->Pose.Get(InTransformType);
}

void URigHierarchy::SetTransform(FRigTransformElement* InTransformElement, const FTransform& InTransform, const ERigTransformType::Type InTransformType, bool bAffectChildren, bool bSetupUndo, bool bForce, bool bPrintPythonCommands)
{
	LLM_SCOPE_BYNAME(TEXT("Animation/ControlRig"));
	if(InTransformElement == nullptr)
	{
		return;
	}

	if(IsGlobal(InTransformType))
	{
		if(FRigControlElement* ControlElement = Cast<FRigControlElement>(InTransformElement))
		{
			FTransform LocalTransform = ComputeLocalControlValue(ControlElement, InTransform, InTransformType);
			ControlElement->Settings.ApplyLimits(LocalTransform);
			SetTransform(ControlElement, LocalTransform, MakeLocal(InTransformType), bAffectChildren, false, false, bPrintPythonCommands);
			return;
		}
	}

#if WITH_EDITOR

	if(bRecordTransformsPerInstruction && ExecuteContext)
	{
		TArray<TArray<int32>>& WrittenTransformsPerSlice = WrittenTransformsPerInstructionPerSlice[ExecuteContext->InstructionIndex];
		while(WrittenTransformsPerSlice.Num() < ExecuteContext->GetSlice().TotalNum())
		{
			WrittenTransformsPerSlice.Add(TArray<int32>());
		}
		WrittenTransformsPerSlice[ExecuteContext->GetSlice().GetIndex()].Add(InTransformElement->GetIndex());
	}
	TGuardValue<bool> RecordTransformsPerInstructionGuard(bRecordTransformsPerInstruction, false);
	
#endif

	if(!InTransformElement->Pose.IsDirty(InTransformType))
	{
		const FTransform PreviousTransform = InTransformElement->Pose.Get(InTransformType);
		if(!bForce && FRigComputedTransform::Equals(PreviousTransform, InTransform))
		{
			return;
		}
	}

	const FTransform PreviousTransform = GetTransform(InTransformElement, InTransformType);
	PropagateDirtyFlags(InTransformElement, ERigTransformType::IsInitial(InTransformType), bAffectChildren);

	const ERigTransformType::Type OpposedType = SwapLocalAndGlobal(InTransformType);
	InTransformElement->Pose.Set(InTransformType, InTransform);
	InTransformElement->Pose.MarkDirty(OpposedType);

	if(FRigControlElement* ControlElement = Cast<FRigControlElement>(InTransformElement))
	{
		ControlElement->Shape.MarkDirty(MakeGlobal(InTransformType));
	}

	EnsureCacheValidity();
	
#if WITH_EDITOR
	if(bSetupUndo || IsTracingChanges())
	{
		PushTransformToStack(
			InTransformElement->GetKey(),
			ERigTransformStackEntryType::TransformPose,
			InTransformType,
			PreviousTransform,
			InTransformElement->Pose.Get(InTransformType),
			bAffectChildren,
			bSetupUndo);
	}

	if (ensure(!bPropagatingChange))
	{
		TGuardValue<bool> bPropagatingChangeGuardValue(bPropagatingChange, true);
			
		for(FRigHierarchyListener& Listener : ListeningHierarchies)
		{
			if(!bForcePropagation && !Listener.ShouldReactToChange(InTransformType))
			{
				continue;
			}

			URigHierarchy* ListeningHierarchy = Listener.Hierarchy.Get();
			if (ListeningHierarchy)
			{			
				if(FRigTransformElement* ListeningElement = Cast<FRigTransformElement>(ListeningHierarchy->Find(InTransformElement->GetKey())))
				{
					// bSetupUndo = false such that all listening hierarchies performs undo at the same time the root hierachy undos
					ListeningHierarchy->SetTransform(ListeningElement, InTransform, InTransformType, bAffectChildren, false, bForce);
				}
			}
		}
	}

	if (bPrintPythonCommands)
	{
		FString BlueprintName;
		if (UBlueprint* Blueprint = GetTypedOuter<UBlueprint>())
		{
			BlueprintName = Blueprint->GetFName().ToString();
		}
		else if (UControlRig* Rig = Cast<UControlRig>(GetOuter()))
		{
			if (UBlueprint* BlueprintCR = Cast<UBlueprint>(Rig->GetClass()->ClassGeneratedBy))
			{
				BlueprintName = BlueprintCR->GetFName().ToString();
			}
		}
		if (!BlueprintName.IsEmpty())
		{
			FString MethodName;
			switch (InTransformType)
			{
				case ERigTransformType::InitialLocal: 
				case ERigTransformType::CurrentLocal:
				{
					MethodName = TEXT("set_local_transform");
					break;
				}
				case ERigTransformType::InitialGlobal: 
				case ERigTransformType::CurrentGlobal:
				{
					MethodName = TEXT("set_global_transform");
					break;
				}
			}

			RigVMPythonUtils::Print(BlueprintName,
				FString::Printf(TEXT("hierarchy.%s(%s, %s, %s, %s)"),
				*MethodName,
				*InTransformElement->GetKey().ToPythonString(),
				*RigVMPythonUtils::TransformToPythonString(InTransform),
				(InTransformType == ERigTransformType::InitialGlobal || InTransformType == ERigTransformType::InitialLocal) ? TEXT("True") : TEXT("False"),
				(bAffectChildren) ? TEXT("True") : TEXT("False")));
		}
	}
#endif
}

FTransform URigHierarchy::GetControlOffsetTransform(FRigControlElement* InControlElement,
	const ERigTransformType::Type InTransformType) const
{
	LLM_SCOPE_BYNAME(TEXT("Animation/ControlRig"));
	if(InControlElement == nullptr)
	{
		return FTransform::Identity;
	}

#if WITH_EDITOR

	if(bRecordTransformsPerInstruction && ExecuteContext)
	{
		TArray<TArray<int32>>& ReadTransformsPerSlice = ReadTransformsPerInstructionPerSlice[ExecuteContext->InstructionIndex];
		while(ReadTransformsPerSlice.Num() < ExecuteContext->GetSlice().TotalNum())
		{
			ReadTransformsPerSlice.Add(TArray<int32>());
		}
		ReadTransformsPerSlice[ExecuteContext->GetSlice().GetIndex()].Add(InControlElement->GetIndex());
	}
	TGuardValue<bool> RecordTransformsPerInstructionGuard(bRecordTransformsPerInstruction, false);
	
#endif

	if(InControlElement->Offset.IsDirty(InTransformType))
	{
		const ERigTransformType::Type OpposedType = SwapLocalAndGlobal(InTransformType);
		const ERigTransformType::Type GlobalType = MakeGlobal(InTransformType);
		ensure(!InControlElement->Offset.IsDirty(OpposedType));

		if(IsLocal(InTransformType))
		{
			const FTransform LocalTransform = InverseSolveParentConstraints(
				InControlElement->Offset.Get(GlobalType), 
				InControlElement->ParentConstraints, GlobalType, FTransform::Identity);
			InControlElement->Offset.Set(InTransformType, LocalTransform);
		}
		else
		{
			const FTransform GlobalTransform = SolveParentConstraints(
				InControlElement->ParentConstraints, InTransformType,
				InControlElement->Offset.Get(OpposedType), true,
				FTransform::Identity, false);
			InControlElement->Offset.Set(InTransformType, GlobalTransform);
		}

		EnsureCacheValidity();
	}
	return InControlElement->Offset.Get(InTransformType);
}

void URigHierarchy::SetControlOffsetTransform(FRigControlElement* InControlElement, const FTransform& InTransform,
                                              const ERigTransformType::Type InTransformType, bool bAffectChildren, bool bSetupUndo, bool bForce, bool bPrintPythonCommands)
{
	if(InControlElement == nullptr)
	{
		return;
	}

#if WITH_EDITOR

	if(bRecordTransformsPerInstruction && ExecuteContext)
	{
		TArray<TArray<int32>>& WrittenTransformsPerSlice = WrittenTransformsPerInstructionPerSlice[ExecuteContext->InstructionIndex];
		while(WrittenTransformsPerSlice.Num() < ExecuteContext->GetSlice().TotalNum())
		{
			WrittenTransformsPerSlice.Add(TArray<int32>());
		}
		WrittenTransformsPerSlice[ExecuteContext->GetSlice().GetIndex()].Add(InControlElement->GetIndex());
	}
	TGuardValue<bool> RecordTransformsPerInstructionGuard(bRecordTransformsPerInstruction, false);
	
#endif

	if(!InControlElement->Offset.IsDirty(InTransformType))
	{
		const FTransform PreviousTransform = InControlElement->Offset.Get(InTransformType);
		if(!bForce && FRigComputedTransform::Equals(PreviousTransform, InTransform))
		{
			return;
		}
	}
	
	const FTransform PreviousTransform = GetControlOffsetTransform(InControlElement, InTransformType);
	PropagateDirtyFlags(InControlElement, ERigTransformType::IsInitial(InTransformType), bAffectChildren);

	GetTransform(InControlElement, MakeLocal(InTransformType));
	InControlElement->Pose.MarkDirty(MakeGlobal(InTransformType));

	const ERigTransformType::Type OpposedType = SwapLocalAndGlobal(InTransformType);
	InControlElement->Offset.Set(InTransformType, InTransform);
	InControlElement->Offset.MarkDirty(OpposedType);
	InControlElement->Shape.MarkDirty(MakeGlobal(InTransformType));

	EnsureCacheValidity();

	if (ERigTransformType::IsInitial(InTransformType))
	{
		// control's offset transform is considered a special type of transform
		// whenever its initial value is changed, we want to make sure the current is kept in sync
		// such that the viewport can reflect this change
		SetControlOffsetTransform(InControlElement, InTransform, ERigTransformType::MakeCurrent(InTransformType), bAffectChildren, false, bForce);
	}
	

#if WITH_EDITOR
	if(bSetupUndo || IsTracingChanges())
	{
		PushTransformToStack(
            InControlElement->GetKey(),
            ERigTransformStackEntryType::ControlOffset,
            InTransformType,
            PreviousTransform,
            InControlElement->Offset.Get(InTransformType),
            bAffectChildren,
            bSetupUndo);
	}

	if (ensure(!bPropagatingChange))
	{
		TGuardValue<bool> bPropagatingChangeGuardValue(bPropagatingChange, true);
			
		for(FRigHierarchyListener& Listener : ListeningHierarchies)
		{
			URigHierarchy* ListeningHierarchy = Listener.Hierarchy.Get();
			if (ListeningHierarchy)
			{	
				if(FRigControlElement* ListeningElement = Cast<FRigControlElement>(ListeningHierarchy->Find(InControlElement->GetKey())))
				{
					// bSetupUndo = false such that all listening hierarchies performs undo at the same time the root hierachy undos
					ListeningHierarchy->SetControlOffsetTransform(ListeningElement, InTransform, InTransformType, bAffectChildren, false, bForce);
				}
			}
		}
	}

	if (bPrintPythonCommands)
	{
		FString BlueprintName;
		if (UBlueprint* Blueprint = GetTypedOuter<UBlueprint>())
		{
			BlueprintName = Blueprint->GetFName().ToString();
		}
		else if (UControlRig* Rig = Cast<UControlRig>(GetOuter()))
		{
			if (UBlueprint* BlueprintCR = Cast<UBlueprint>(Rig->GetClass()->ClassGeneratedBy))
			{
				BlueprintName = BlueprintCR->GetFName().ToString();
			}
		}
		if (!BlueprintName.IsEmpty())
		{
			RigVMPythonUtils::Print(BlueprintName,
				FString::Printf(TEXT("hierarchy.set_control_offset_transform(%s, %s, %s, %s)"),
				*InControlElement->GetKey().ToPythonString(),
				*RigVMPythonUtils::TransformToPythonString(InTransform),
				(ERigTransformType::IsInitial(InTransformType)) ? TEXT("True") : TEXT("False"),
				(bAffectChildren) ? TEXT("True") : TEXT("False")));
		}
	}
#endif
}

FTransform URigHierarchy::GetControlShapeTransform(FRigControlElement* InControlElement,
	const ERigTransformType::Type InTransformType) const
{
	LLM_SCOPE_BYNAME(TEXT("Animation/ControlRig"));
	if(InControlElement == nullptr)
	{
		return FTransform::Identity;
	}
	
	if(InControlElement->Shape.IsDirty(InTransformType))
	{
		const ERigTransformType::Type OpposedType = SwapLocalAndGlobal(InTransformType);
		const ERigTransformType::Type GlobalType = MakeGlobal(InTransformType);
		ensure(!InControlElement->Shape.IsDirty(OpposedType));

		const FTransform ParentTransform = GetTransform(InControlElement, GlobalType);
		if(IsLocal(InTransformType))
		{
			InControlElement->Shape.Set(InTransformType, InControlElement->Shape.Get(OpposedType).GetRelativeTransform(ParentTransform));
		}
		else
		{
			InControlElement->Shape.Set(InTransformType, InControlElement->Shape.Get(OpposedType) * ParentTransform);
		}

		EnsureCacheValidity();
	}
	return InControlElement->Shape.Get(InTransformType);
}

void URigHierarchy::SetControlShapeTransform(FRigControlElement* InControlElement, const FTransform& InTransform,
	const ERigTransformType::Type InTransformType, bool bSetupUndo, bool bForce, bool bPrintPythonCommands)
{
	LLM_SCOPE_BYNAME(TEXT("Animation/ControlRig"));
	if(InControlElement == nullptr)
	{
		return;
	}

	if(!InControlElement->Shape.IsDirty(InTransformType))
	{
		const FTransform PreviousTransform = InControlElement->Shape.Get(InTransformType);
		if(!bForce && FRigComputedTransform::Equals(PreviousTransform, InTransform))
		{
			return;
		}
	}

	const FTransform PreviousTransform = GetControlShapeTransform(InControlElement, InTransformType);
	const ERigTransformType::Type OpposedType = SwapLocalAndGlobal(InTransformType);
	InControlElement->Shape.Set(InTransformType, InTransform);
	InControlElement->Shape.MarkDirty(OpposedType);

	if (IsInitial(InTransformType))
	{
		// control's shape transform, similar to offset transform, is considered a special type of transform
		// whenever its initial value is changed, we want to make sure the current is kept in sync
		// such that the viewport can reflect this change
		SetControlShapeTransform(InControlElement, InTransform, ERigTransformType::MakeCurrent(InTransformType), false, bForce);
	}
	
	EnsureCacheValidity();
	
#if WITH_EDITOR
	if(bSetupUndo || IsTracingChanges())
	{
		PushTransformToStack(
            InControlElement->GetKey(),
            ERigTransformStackEntryType::ControlShape,
            InTransformType,
            PreviousTransform,
            InControlElement->Shape.Get(InTransformType),
            false,
            bSetupUndo);
	}
#endif

	if(IsLocal(InTransformType))
	{
		Notify(ERigHierarchyNotification::ControlShapeTransformChanged, InControlElement);
	}

#if WITH_EDITOR
	if (ensure(!bPropagatingChange))
	{
		TGuardValue<bool> bPropagatingChangeGuardValue(bPropagatingChange, true);
			
		for(FRigHierarchyListener& Listener : ListeningHierarchies)
		{
			URigHierarchy* ListeningHierarchy = Listener.Hierarchy.Get();
			if (ListeningHierarchy)
			{	
				if(FRigControlElement* ListeningElement = Cast<FRigControlElement>(ListeningHierarchy->Find(InControlElement->GetKey())))
				{
					// bSetupUndo = false such that all listening hierarchies performs undo at the same time the root hierachy undos
					ListeningHierarchy->SetControlShapeTransform(ListeningElement, InTransform, InTransformType, false, bForce);
				}
			}
		}
	}

	if (bPrintPythonCommands)
	{
		FString BlueprintName;
		if (UBlueprint* Blueprint = GetTypedOuter<UBlueprint>())
		{
			BlueprintName = Blueprint->GetFName().ToString();
		}
		else if (UControlRig* Rig = Cast<UControlRig>(GetOuter()))
		{
			if (UBlueprint* BlueprintCR = Cast<UBlueprint>(Rig->GetClass()->ClassGeneratedBy))
			{
				BlueprintName = BlueprintCR->GetFName().ToString();
			}
		}
		if (!BlueprintName.IsEmpty())
		{
			RigVMPythonUtils::Print(BlueprintName,
				FString::Printf(TEXT("hierarchy.set_control_shape_transform(%s, %s, %s)"),
				*InControlElement->GetKey().ToPythonString(),
				*RigVMPythonUtils::TransformToPythonString(InTransform),
				ERigTransformType::IsInitial(InTransformType) ? TEXT("True") : TEXT("False")));
		}
	
	}
#endif
}

void URigHierarchy::SetControlSettings(FRigControlElement* InControlElement, FRigControlSettings InSettings, bool bSetupUndo, bool bForce, bool bPrintPythonCommands)
{
	LLM_SCOPE_BYNAME(TEXT("Animation/ControlRig"));
	if(InControlElement == nullptr)
	{
		return;
	}

	const FRigControlSettings PreviousSettings = InControlElement->Settings;
	if(!bForce && PreviousSettings == InSettings)
	{
		return;
	}

	if(bSetupUndo && !HasAnyFlags(RF_Transient))
	{
		Modify();
	}

	InControlElement->Settings = InSettings;
	Notify(ERigHierarchyNotification::ControlSettingChanged, InControlElement);
	
#if WITH_EDITOR
	if (ensure(!bPropagatingChange))
	{
		TGuardValue<bool> bPropagatingChangeGuardValue(bPropagatingChange, true);
			
		for(FRigHierarchyListener& Listener : ListeningHierarchies)
		{
			URigHierarchy* ListeningHierarchy = Listener.Hierarchy.Get();
			if (ListeningHierarchy)
			{	
				if(FRigControlElement* ListeningElement = Cast<FRigControlElement>(ListeningHierarchy->Find(InControlElement->GetKey())))
				{
					// bSetupUndo = false such that all listening hierarchies performs undo at the same time the root hierachy undos
					ListeningHierarchy->SetControlSettings(ListeningElement, InSettings, false, bForce);
				}
			}
		}
	}

	if (bPrintPythonCommands)
	{
		FString BlueprintName;
		if (UBlueprint* Blueprint = GetTypedOuter<UBlueprint>())
		{
			BlueprintName = Blueprint->GetFName().ToString();
		}
		else if (UControlRig* Rig = Cast<UControlRig>(GetOuter()))
		{
			if (UBlueprint* BlueprintCR = Cast<UBlueprint>(Rig->GetClass()->ClassGeneratedBy))
			{
				BlueprintName = BlueprintCR->GetFName().ToString();
			}
		}
		if (!BlueprintName.IsEmpty())
		{
			FString ControlNamePythonized = RigVMPythonUtils::NameToPep8(InControlElement->GetName().ToString());
			FString SettingsName = FString::Printf(TEXT("control_settings_%s"),
				*ControlNamePythonized);
			TArray<FString> Commands = ControlSettingsToPythonCommands(InControlElement->Settings, SettingsName);

			for (const FString& Command : Commands)
			{
				RigVMPythonUtils::Print(BlueprintName, Command);
			}
			
			RigVMPythonUtils::Print(BlueprintName,
				FString::Printf(TEXT("hierarchy.set_control_settings(%s, %s)"),
				*InControlElement->GetKey().ToPythonString(),
				*SettingsName));
		}
	}
#endif
}

FTransform URigHierarchy::GetParentTransform(FRigBaseElement* InElement, const ERigTransformType::Type InTransformType) const
{
	LLM_SCOPE_BYNAME(TEXT("Animation/ControlRig"));
	if(FRigSingleParentElement* SingleParentElement = Cast<FRigSingleParentElement>(InElement))
	{
		return GetTransform(SingleParentElement->ParentElement, InTransformType);
	}
	else if(FRigMultiParentElement* MultiParentElement = Cast<FRigMultiParentElement>(InElement))
	{
		FRigComputedTransform& Output = MultiParentElement->Parent[InTransformType];

		if(Output.bDirty)
		{
			const FTransform OutputTransform = SolveParentConstraints(
				MultiParentElement->ParentConstraints,
				InTransformType,
				FTransform::Identity,
				false,
				FTransform::Identity,
				false
			);
			MultiParentElement->Parent.Set(InTransformType, OutputTransform);

			EnsureCacheValidity();
		}
		return Output.Transform;
	}
	return FTransform::Identity;
}

FRigControlValue URigHierarchy::GetControlValue(FRigControlElement* InControlElement, ERigControlValueType InValueType) const
{
	LLM_SCOPE_BYNAME(TEXT("Animation/ControlRig"));
	using namespace ERigTransformType;

	FRigControlValue Value;

	if(InControlElement != nullptr)
	{
		switch(InValueType)
		{
			case ERigControlValueType::Current:
			{
				Value.SetFromTransform(
                    GetTransform(InControlElement, CurrentLocal),
                    InControlElement->Settings.ControlType,
                    InControlElement->Settings.PrimaryAxis
                );
				break;
			}
			case ERigControlValueType::Initial:
			{
				Value.SetFromTransform(
                    GetTransform(InControlElement, InitialLocal),
                    InControlElement->Settings.ControlType,
                    InControlElement->Settings.PrimaryAxis
                );
				break;
			}
			case ERigControlValueType::Minimum:
			{
				return InControlElement->Settings.MinimumValue;
			}
			case ERigControlValueType::Maximum:
			{
				return InControlElement->Settings.MaximumValue;
			}
		}
	}
	return Value;
}

void URigHierarchy::SetControlValue(FRigControlElement* InControlElement, const FRigControlValue& InValue, ERigControlValueType InValueType, bool bSetupUndo, bool bForce, bool bPrintPythonCommands)
{
	LLM_SCOPE_BYNAME(TEXT("Animation/ControlRig"));
	
	using namespace ERigTransformType;

	if(InControlElement != nullptr)
	{
		switch(InValueType)
		{
			case ERigControlValueType::Current:
			{
				FRigControlValue Value = InValue;
				InControlElement->Settings.ApplyLimits(Value);
					
				SetTransform(
					InControlElement,
					Value.GetAsTransform(
						InControlElement->Settings.ControlType,
						InControlElement->Settings.PrimaryAxis
					),
					CurrentLocal,
					true,
					bSetupUndo,
					bForce,
					bPrintPythonCommands
				); 
				break;
			}
			case ERigControlValueType::Initial:
			{
				FRigControlValue Value = InValue;
				InControlElement->Settings.ApplyLimits(Value);

				SetTransform(
					InControlElement,
					Value.GetAsTransform(
						InControlElement->Settings.ControlType,
						InControlElement->Settings.PrimaryAxis
					),
					InitialLocal,
					true,
					bSetupUndo,
					bForce,
					bPrintPythonCommands
				); 
				break;
			}
			case ERigControlValueType::Minimum:
			case ERigControlValueType::Maximum:
			{
				if(bSetupUndo)
				{
					Modify();
				}

				if(InValueType == ERigControlValueType::Minimum)
				{
					InControlElement->Settings.MinimumValue = InValue;
				}
				else
				{
					InControlElement->Settings.MaximumValue = InValue;
				}
				
				Notify(ERigHierarchyNotification::ControlSettingChanged, InControlElement);

#if WITH_EDITOR
				if (ensure(!bPropagatingChange))
				{
					TGuardValue<bool> bPropagatingChangeGuardValue(bPropagatingChange, true);
			
					for(FRigHierarchyListener& Listener : ListeningHierarchies)
					{
						URigHierarchy* ListeningHierarchy = Listener.Hierarchy.Get();
					
						if (ListeningHierarchy)
						{
							if(FRigControlElement* ListeningElement = Cast<FRigControlElement>(ListeningHierarchy->Find(InControlElement->GetKey())))
							{
								ListeningHierarchy->SetControlValue(ListeningElement, InValue, InValueType, false, bForce);
							}
						}
					}
				}

				if (bPrintPythonCommands)
				{
					FString BlueprintName;
					if (UBlueprint* Blueprint = GetTypedOuter<UBlueprint>())
					{
						BlueprintName = Blueprint->GetFName().ToString();
					}
					else if (UControlRig* Rig = Cast<UControlRig>(GetOuter()))
					{
						if (UBlueprint* BlueprintCR = Cast<UBlueprint>(Rig->GetClass()->ClassGeneratedBy))
						{
							BlueprintName = BlueprintCR->GetFName().ToString();
						}
					}
					if (!BlueprintName.IsEmpty())
					{
						FString TypeStr;
						switch (InValueType)
						{
						case ERigControlValueType::Minimum: TypeStr = TEXT("MINIMUM"); break;
						case ERigControlValueType::Maximum: TypeStr = TEXT("MAXIMUM"); break;
						default: ensure(false);
						}
						RigVMPythonUtils::Print(BlueprintName,
							FString::Printf(TEXT("hierarchy.set_control_value(%s, %s, unreal.RigControlValueType.%s)"),
							*InControlElement->GetKey().ToPythonString(),
							*InValue.ToPythonString(InControlElement->Settings.ControlType),
							*TypeStr));
					}
				}
#endif
				break;
			}
		}	
	}
}

void URigHierarchy::SetControlVisibility(FRigControlElement* InControlElement, bool bVisibility)
{
	if(InControlElement == nullptr)
	{
		return;
	}

	InControlElement->Settings.bShapeVisible = bVisibility;
	Notify(ERigHierarchyNotification::ControlVisibilityChanged, InControlElement);

#if WITH_EDITOR
	if (ensure(!bPropagatingChange))
	{
		TGuardValue<bool> bPropagatingChangeGuardValue(bPropagatingChange, true);
			
		for(FRigHierarchyListener& Listener : ListeningHierarchies)
		{
			URigHierarchy* ListeningHierarchy = Listener.Hierarchy.Get();
			if (ListeningHierarchy)
			{
				if(FRigControlElement* ListeningElement = Cast<FRigControlElement>(ListeningHierarchy->Find(InControlElement->GetKey())))
				{
					ListeningHierarchy->SetControlVisibility(ListeningElement, bVisibility);
				}
			}
		}
	}
#endif
}

float URigHierarchy::GetCurveValue(FRigCurveElement* InCurveElement) const
{
	if(InCurveElement == nullptr)
	{
		return 0.f;
	}
	return InCurveElement->Value;
}

void URigHierarchy::SetCurveValue(FRigCurveElement* InCurveElement, float InValue, bool bSetupUndo, bool bForce)
{
	LLM_SCOPE_BYNAME(TEXT("Animation/ControlRig"));
	if(InCurveElement == nullptr)
	{
		return;
	}

	const float PreviousValue = InCurveElement->Value;
	if(!bForce && FMath::IsNearlyZero(PreviousValue - InValue))
	{
		return;
	}

	InCurveElement->Value = InValue;

#if WITH_EDITOR
	if(bSetupUndo || IsTracingChanges())
	{
		PushCurveToStack(InCurveElement->GetKey(), PreviousValue, InCurveElement->Value, bSetupUndo);
	}

	if (ensure(!bPropagatingChange))
	{
		TGuardValue<bool> bPropagatingChangeGuardValue(bPropagatingChange, true);
			
		for(FRigHierarchyListener& Listener : ListeningHierarchies)
		{
			if(!Listener.Hierarchy.IsValid())
			{
				continue;
			}

			URigHierarchy* ListeningHierarchy = Listener.Hierarchy.Get();
			if (ListeningHierarchy)
			{
				if(FRigCurveElement* ListeningElement = Cast<FRigCurveElement>(ListeningHierarchy->Find(InCurveElement->GetKey())))
				{
					// bSetupUndo = false such that all listening hierarchies performs undo at the same time the root hierachy undos
					ListeningHierarchy->SetCurveValue(ListeningElement, InValue, false, bForce);
				}
			}
		}
	}
#endif
}

FName URigHierarchy::GetPreviousName(const FRigElementKey& InKey) const
{
	if(const FRigElementKey* OldKeyPtr = PreviousNameMap.Find(InKey))
	{
		return OldKeyPtr->Name;
	}
	return NAME_None;
}

FRigElementKey URigHierarchy::GetPreviousParent(const FRigElementKey& InKey) const
{
	if(const FRigElementKey* OldParentPtr = PreviousParentMap.Find(InKey))
	{
		return *OldParentPtr;
	}
	return FRigElementKey();
}

bool URigHierarchy::IsParentedTo(FRigBaseElement* InChild, FRigBaseElement* InParent, const TElementDependencyMap& InDependencyMap) const
{
	TArray<bool> ElementsVisited;
	return IsDependentOn(InChild, InParent, ElementsVisited, InDependencyMap);
}

bool URigHierarchy::IsDependentOn(FRigBaseElement* InDependent, FRigBaseElement* InDependency, TArray<bool>& InElementsVisited, const TElementDependencyMap& InDependencyMap) const
{
	if (InElementsVisited.Num() != Elements.Num())
	{
		InElementsVisited.Reset();
		InElementsVisited.AddZeroed(Elements.Num());
	}

	if((InDependent == nullptr) || (InDependency == nullptr))
	{
		return false;
	}

	if(InDependent == InDependency)
	{
		return true;
	}

	const int32 DependentElementIndex = InDependent->GetIndex();

	if (!InElementsVisited.IsValidIndex(DependentElementIndex))
	{
		return false;
	}
	
	if (InElementsVisited[DependentElementIndex])
	{
		return false;
	}

	InElementsVisited[DependentElementIndex] = true;

	// collect all possible parents of the dependent
	TArray<FRigBaseElement*> DependentParents;
	if(const FRigSingleParentElement* SingleParentElement = Cast<FRigSingleParentElement>(InDependent))
	{
		 DependentParents.AddUnique(SingleParentElement->ParentElement);
	}
	else if(const FRigMultiParentElement* MultiParentElement = Cast<FRigMultiParentElement>(InDependent))
	{
		for(const FRigElementParentConstraint& ParentConstraint : MultiParentElement->ParentConstraints)
		{
			 DependentParents.AddUnique(ParentConstraint.ParentElement);
		}
	}

	// check the optional dependency map
	if(const TArray<int32>* DependentIndicesPtr = InDependencyMap.Find(InDependent->GetIndex()))
	{
		const TArray<int32>& DependentIndices = *DependentIndicesPtr;
		for(const int32 DependentIndex : DependentIndices)
		{
			ensure(Elements.IsValidIndex(DependentIndex));
			DependentParents.AddUnique(Elements[DependentIndex]);
		}
	}

	for (FRigBaseElement* DependentParent :  DependentParents)
	{
		if(IsDependentOn(DependentParent, InDependency, InElementsVisited, InDependencyMap))
		{
			return true;
		}
	}

	return false;
}

bool URigHierarchy::IsTracingChanges() const
{
#if WITH_EDITOR
	return (CVarControlRigHierarchyTraceAlways->GetInt() != 0) || (TraceFramesLeft > 0);
#else
	return false;
#endif
}

#if WITH_EDITOR

void URigHierarchy::ResetTransformStack()
{
	LLM_SCOPE_BYNAME(TEXT("Animation/ControlRig"));
	TransformUndoStack.Reset();
	TransformRedoStack.Reset();
	TransformStackIndex = TransformUndoStack.Num();

	if(IsTracingChanges())
	{
		TracePoses.Reset();
		StorePoseForTrace(TEXT("BeginOfFrame"));
	}
}

void URigHierarchy::StorePoseForTrace(const FString& InPrefix)
{
	LLM_SCOPE_BYNAME(TEXT("Animation/ControlRig"));
	check(!InPrefix.IsEmpty());
	
	FName InitialKey = *FString::Printf(TEXT("%s_Initial"), *InPrefix);
	FName CurrentKey = *FString::Printf(TEXT("%s_Current"), *InPrefix);
	TracePoses.FindOrAdd(InitialKey) = GetPose(true);
	TracePoses.FindOrAdd(CurrentKey) = GetPose(false);
}

void URigHierarchy::CheckTraceFormatIfRequired()
{
	if(sRigHierarchyLastTrace != CVarControlRigHierarchyTracePrecision->GetInt())
	{
		sRigHierarchyLastTrace = CVarControlRigHierarchyTracePrecision->GetInt();
		const FString Format = FString::Printf(TEXT("%%.%df"), sRigHierarchyLastTrace);
		check(Format.Len() < 16);
		sRigHierarchyTraceFormat[Format.Len()] = '\0';
		FMemory::Memcpy(sRigHierarchyTraceFormat, *Format, Format.Len() * sizeof(TCHAR));
	}
}

template <class CharType>
struct TRigHierarchyJsonPrintPolicy
	: public TPrettyJsonPrintPolicy<CharType>
{
	static inline void WriteDouble(  FArchive* Stream, double Value )
	{
		URigHierarchy::CheckTraceFormatIfRequired();
		TJsonPrintPolicy<CharType>::WriteString(Stream, FString::Printf(sRigHierarchyTraceFormat, Value));
	}
};

void URigHierarchy::DumpTransformStackToFile(FString* OutFilePath)
{
	LLM_SCOPE_BYNAME(TEXT("Animation/ControlRig"));
	if(IsTracingChanges())
	{
		StorePoseForTrace(TEXT("EndOfFrame"));
	}

	FString PathName = GetPathName();
	PathName.Split(TEXT(":"), nullptr, &PathName);
	PathName.ReplaceCharInline('.', '/');

	FString Suffix;
	if(TraceFramesLeft > 0)
	{
		Suffix = FString::Printf(TEXT("_Trace_%03d"), TraceFramesCaptured);
	}

	FString FileName = FString::Printf(TEXT("%sControlRig/%s%s.json"), *FPaths::ProjectLogDir(), *PathName, *Suffix);
	FString FullFilename = FPlatformFileManager::Get().GetPlatformFile().ConvertToAbsolutePathForExternalAppForWrite(*FileName);

	TSharedPtr<FJsonObject> JsonData = MakeShareable(new FJsonObject);
	JsonData->SetStringField(TEXT("PathName"), GetPathName());

	TSharedRef<FJsonObject> JsonTracedPoses = MakeShareable(new FJsonObject);
	for(const TPair<FName, FRigPose>& Pair : TracePoses)
	{
		TSharedRef<FJsonObject> JsonTracedPose = MakeShareable(new FJsonObject);
		if (FJsonObjectConverter::UStructToJsonObject(FRigPose::StaticStruct(), &Pair.Value, JsonTracedPose, 0, 0))
		{
			JsonTracedPoses->SetObjectField(Pair.Key.ToString(), JsonTracedPose);
		}
	}
	JsonData->SetObjectField(TEXT("TracedPoses"), JsonTracedPoses);

	TArray<TSharedPtr<FJsonValue>> JsonTransformStack;
	for (const FRigTransformStackEntry& TransformStackEntry : TransformUndoStack)
	{
		TSharedRef<FJsonObject> JsonTransformStackEntry = MakeShareable(new FJsonObject);
		if (FJsonObjectConverter::UStructToJsonObject(FRigTransformStackEntry::StaticStruct(), &TransformStackEntry, JsonTransformStackEntry, 0, 0))
		{
			JsonTransformStack.Add(MakeShareable(new FJsonValueObject(JsonTransformStackEntry)));
		}
	}
	JsonData->SetArrayField(TEXT("TransformStack"), JsonTransformStack);

	FString JsonText;
	const TSharedRef< TJsonWriter< TCHAR, TRigHierarchyJsonPrintPolicy<TCHAR> > > JsonWriter = TJsonWriterFactory< TCHAR, TRigHierarchyJsonPrintPolicy<TCHAR> >::Create(&JsonText);
	if (FJsonSerializer::Serialize(JsonData.ToSharedRef(), JsonWriter))
	{
		if ( FFileHelper::SaveStringToFile(JsonText, *FullFilename) )
		{
			UE_LOG(LogControlRig, Display, TEXT("Saved hierarchy trace to %s"), *FullFilename);

			if(OutFilePath)
			{
				*OutFilePath = FullFilename;
			}
		}
	}

	TraceFramesLeft = FMath::Max(0, TraceFramesLeft - 1);
	TraceFramesCaptured++;
}

void URigHierarchy::TraceFrames(int32 InNumFramesToTrace)
{
	LLM_SCOPE_BYNAME(TEXT("Animation/ControlRig"));
	
	TraceFramesLeft = InNumFramesToTrace;
	TraceFramesCaptured = 0;
	ResetTransformStack();
}

#endif

bool URigHierarchy::IsSelected(const FRigBaseElement* InElement) const
{
	if(InElement == nullptr)
	{
		return false;
	}
	if(URigHierarchy* HierarchyForSelection = HierarchyForSelectionPtr.Get())
	{
		return HierarchyForSelection->IsSelected(InElement->GetKey());
	}
	return InElement->IsSelected();
}

void URigHierarchy::ResetCachedChildren()
{
	LLM_SCOPE_BYNAME(TEXT("Animation/ControlRig"));
	for (int32 ElementIndex = 0; ElementIndex < Elements.Num(); ElementIndex++)
	{
		FRigBaseElement* Element = Elements[ElementIndex];
		Element->CachedChildren.Reset();
	}
}

void URigHierarchy::UpdateCachedChildren(const FRigBaseElement* InElement, bool bForce) const
{
	LLM_SCOPE_BYNAME(TEXT("Animation/ControlRig"));
	check(InElement);

	if(InElement->TopologyVersion == TopologyVersion && !bForce)
	{
		return;
	}
	
	InElement->CachedChildren.Reset();
	
	for (int32 ElementIndex = 0; ElementIndex < Elements.Num(); ElementIndex++)
	{
		FRigBaseElement* Element = Elements[ElementIndex];
		if(FRigSingleParentElement* SingleParentElement = Cast<FRigSingleParentElement>(Element))
		{
			if(SingleParentElement->ParentElement == InElement)
			{
				InElement->CachedChildren.Add(SingleParentElement);
			}
		}
		else if(FRigMultiParentElement* MultiParentElement = Cast<FRigMultiParentElement>(Element))
		{
			for(const FRigElementParentConstraint& ParentConstraint : MultiParentElement->ParentConstraints)
			{
				if(ParentConstraint.ParentElement == InElement)
				{
					InElement->CachedChildren.Add(MultiParentElement);
					break;
				}
			}
		}
	}

	InElement->TopologyVersion = TopologyVersion;
}

void URigHierarchy::UpdateAllCachedChildren() const
{
	LLM_SCOPE_BYNAME(TEXT("Animation/ControlRig"));
	TArray<bool> ParentVisited;
	ParentVisited.AddZeroed(Elements.Num());
	
	for (int32 ElementIndex = 0; ElementIndex < Elements.Num(); ElementIndex++)
	{
		FRigBaseElement* Element = Elements[ElementIndex];
		Element->TopologyVersion = TopologyVersion;
		
		if(FRigSingleParentElement* SingleParentElement = Cast<FRigSingleParentElement>(Element))
		{
			if(FRigTransformElement* ParentElement = SingleParentElement->ParentElement)
			{
				if(!ParentVisited[ParentElement->Index])
				{
					ParentElement->CachedChildren.Reset();
					ParentVisited[ParentElement->Index] = true;
				}
				ParentElement->CachedChildren.Add(Element);
			}
		}
		else if(FRigMultiParentElement* MultiParentElement = Cast<FRigMultiParentElement>(Element))
		{
			for(const FRigElementParentConstraint& ParentConstraint : MultiParentElement->ParentConstraints)
			{
				if(ParentConstraint.ParentElement)
				{
					if(!ParentVisited[ParentConstraint.ParentElement->Index])
					{
						ParentConstraint.ParentElement->CachedChildren.Reset();
						ParentVisited[ParentConstraint.ParentElement->Index] = true;
					}
					ParentConstraint.ParentElement->CachedChildren.Add(Element);
				}
			}
		}
	}
}
	
FRigElementKey URigHierarchy::PreprocessParentElementKeyForSpaceSwitching(const FRigElementKey& InChildKey, const FRigElementKey& InParentKey)
{
	LLM_SCOPE_BYNAME(TEXT("Animation/ControlRig"));
	
	if(InParentKey == GetWorldSpaceReferenceKey())
	{
		return GetOrAddWorldSpaceReference();
	}
	else if(InParentKey == GetDefaultParentKey())
	{
		const FRigElementKey FirstParent = GetFirstParent(InChildKey);
		if(FirstParent == GetWorldSpaceReferenceKey())
		{
			return FRigElementKey();
		}
		else
		{
			return FirstParent;
		}
	}

	return InParentKey;
}

FRigBaseElement* URigHierarchy::MakeElement(ERigElementType InElementType, int32 InCount, int32* OutStructureSize)
{
	LLM_SCOPE_BYNAME(TEXT("Animation/ControlRig"));
	check(InCount > 0);
	
	FRigBaseElement* Element = nullptr;
	switch(InElementType)
	{
		case ERigElementType::Bone:
		{
			if(OutStructureSize)
			{
				*OutStructureSize = sizeof(FRigBoneElement);
			}
			FRigBoneElement* Elements = (FRigBoneElement*)FMemory::Malloc(sizeof(FRigBoneElement) * InCount);
			for(int32 Index=0;Index<InCount;Index++)
			{
				new(&Elements[Index]) FRigBoneElement(); 
			} 
			Element = Elements;
			break;
		}
		case ERigElementType::Null:
		{
			if(OutStructureSize)
			{
				*OutStructureSize = sizeof(FRigNullElement);
			}
			FRigNullElement* Elements = (FRigNullElement*)FMemory::Malloc(sizeof(FRigNullElement) * InCount);
			for(int32 Index=0;Index<InCount;Index++)
			{
				new(&Elements[Index]) FRigNullElement(); 
			} 
			Element = Elements;
			break;
		}
		case ERigElementType::Control:
		{
			if(OutStructureSize)
			{
				*OutStructureSize = sizeof(FRigControlElement);
			}
			FRigControlElement* Elements = (FRigControlElement*)FMemory::Malloc(sizeof(FRigControlElement) * InCount);
			for(int32 Index=0;Index<InCount;Index++)
			{
				new(&Elements[Index]) FRigControlElement(); 
			} 
			Element = Elements;
			break;
		}
		case ERigElementType::Curve:
		{
			if(OutStructureSize)
			{
				*OutStructureSize = sizeof(FRigCurveElement);
			}
			FRigCurveElement* Elements = (FRigCurveElement*)FMemory::Malloc(sizeof(FRigCurveElement) * InCount);
			for(int32 Index=0;Index<InCount;Index++)
			{
				new(&Elements[Index]) FRigCurveElement(); 
			} 
			Element = Elements;
			break;
		}
		case ERigElementType::RigidBody:
		{
			if(OutStructureSize)
			{
				*OutStructureSize = sizeof(FRigRigidBodyElement);
			}
			FRigRigidBodyElement* Elements = (FRigRigidBodyElement*)FMemory::Malloc(sizeof(FRigRigidBodyElement) * InCount);
			for(int32 Index=0;Index<InCount;Index++)
			{
				new(&Elements[Index]) FRigRigidBodyElement(); 
			} 
			Element = Elements;
			break;
		}
		case ERigElementType::Reference:
		{
			if(OutStructureSize)
			{
				*OutStructureSize = sizeof(FRigReferenceElement);
			}
			FRigReferenceElement* Elements = (FRigReferenceElement*)FMemory::Malloc(sizeof(FRigReferenceElement) * InCount);
			for(int32 Index=0;Index<InCount;Index++)
			{
				new(&Elements[Index]) FRigReferenceElement(); 
			} 
			Element = Elements;
			break;
		}
		default:
		{
			ensure(false);
		}
	}

	if(Element)
	{
		Element->OwnedInstances = InCount;
	}
	return Element;
}

void URigHierarchy::DestroyElement(FRigBaseElement*& InElement)
{
	LLM_SCOPE_BYNAME(TEXT("Animation/ControlRig"));
	check(InElement != nullptr);

	if(InElement->OwnedInstances == 0)
	{
		return;
	}

	const int32 Count = InElement->OwnedInstances;
	switch(InElement->GetType())
	{
		case ERigElementType::Bone:
		{
			FRigBoneElement* Elements = Cast<FRigBoneElement>(InElement);
			for(int32 Index=0;Index<Count;Index++)
			{
				Elements[Index].~FRigBoneElement(); 
			}
			break;
		}
		case ERigElementType::Null:
		{
			FRigNullElement* Elements = Cast<FRigNullElement>(InElement);
			for(int32 Index=0;Index<Count;Index++)
			{
				Elements[Index].~FRigNullElement(); 
			}
			break;
		}
		case ERigElementType::Control:
		{
			FRigControlElement* Elements = Cast<FRigControlElement>(InElement);
			for(int32 Index=0;Index<Count;Index++)
			{
				Elements[Index].~FRigControlElement(); 
			}
			break;
		}
		case ERigElementType::Curve:
		{
			FRigCurveElement* Elements = Cast<FRigCurveElement>(InElement);
			for(int32 Index=0;Index<Count;Index++)
			{
				Elements[Index].~FRigCurveElement(); 
			}
			break;
		}
		case ERigElementType::RigidBody:
		{
			FRigRigidBodyElement* Elements = Cast<FRigRigidBodyElement>(InElement);
			for(int32 Index=0;Index<Count;Index++)
			{
				Elements[Index].~FRigRigidBodyElement(); 
			}
			break;
		}
		case ERigElementType::Reference:
		{
			FRigReferenceElement* Elements = Cast<FRigReferenceElement>(InElement);
			for(int32 Index=0;Index<Count;Index++)
			{
				Elements[Index].~FRigReferenceElement(); 
			}
			break;
		}
		default:
		{
			ensure(false);
			return;
		}
	}

	FMemory::Free(InElement);
	InElement = nullptr;
}

#if URIGHIERARCHY_RECURSIVE_DIRTY_PROPAGATION
void URigHierarchy::PropagateDirtyFlags(FRigTransformElement* InTransformElement, bool bInitial, bool bAffectChildren, bool bComputeOpposed, bool bMarkDirty) const
#else
void URigHierarchy::PropagateDirtyFlags(FRigTransformElement* InTransformElement, bool bInitial, bool bAffectChildren) const
#endif
{
	if(!bEnableDirtyPropagation)
	{
		return;
	}
	
	check(InTransformElement);

	const ERigTransformType::Type LocalType = bInitial ? ERigTransformType::InitialLocal : ERigTransformType::CurrentLocal;
	const ERigTransformType::Type GlobalType = bInitial ? ERigTransformType::InitialGlobal : ERigTransformType::CurrentGlobal;
	const ERigTransformType::Type TypeToCompute = bAffectChildren ? LocalType : GlobalType;
	const ERigTransformType::Type TypeToDirty = SwapLocalAndGlobal(TypeToCompute);

#if URIGHIERARCHY_RECURSIVE_DIRTY_PROPAGATION
	if(bComputeOpposed)
#endif
	{
		for(const FRigTransformElement::FElementToDirty& ElementToDirty : InTransformElement->ElementsToDirty)
		{
#if URIGHIERARCHY_RECURSIVE_DIRTY_PROPAGATION
			if(FRigControlElement* ControlElement = Cast<FRigControlElement>(ElementToDirty.Element))
			{
				if(ERigTransformType::IsGlobal(TypeToDirty))
				{
					if(ControlElement->Parent.IsDirty(TypeToDirty) &&
						ControlElement->Offset.IsDirty(TypeToDirty) &&
						ControlElement->Pose.IsDirty(TypeToDirty))
					{
						continue;
					}
				}
				else
				{
					if(ControlElement->Parent.IsDirty(TypeToDirty))
					{
						continue;
					}
				}
			}
			else if(FRigMultiParentElement* MultiParentElement = Cast<FRigMultiParentElement>(ElementToDirty.Element))
			{
				if(ERigTransformType::IsGlobal(TypeToDirty))
				{
					if(MultiParentElement->Parent.IsDirty(TypeToDirty) &&
						MultiParentElement->Pose.IsDirty(TypeToDirty))
					{
						continue;
					}
				}
				else
				{
					if(MultiParentElement->Parent.IsDirty(TypeToDirty))
					{
						continue;
					}
				}
			}
			else
			{
				if(ElementToDirty.Element->Pose.IsDirty(TypeToDirty))
				{
					continue;
				}
			}
#else

			if(!bAffectChildren && ElementToDirty.HierarchyDistance > 1)
			{
				continue;
			}

#endif

			if(FRigControlElement* ControlElement = Cast<FRigControlElement>(ElementToDirty.Element))
			{
				GetControlOffsetTransform(ControlElement, LocalType);
			}
			GetTransform(ElementToDirty.Element, TypeToCompute); // make sure the local / global transform is up 2 date

#if URIGHIERARCHY_RECURSIVE_DIRTY_PROPAGATION
			PropagateDirtyFlags(ElementToDirty.Element, bInitial, bAffectChildren, true, false);
#endif
		}
	}
#if URIGHIERARCHY_RECURSIVE_DIRTY_PROPAGATION

	if(bMarkDirty)
#endif
	{
		for(const FRigTransformElement::FElementToDirty& ElementToDirty : InTransformElement->ElementsToDirty)
		{
#if URIGHIERARCHY_RECURSIVE_DIRTY_PROPAGATION

			if(FRigControlElement* ControlElement = Cast<FRigControlElement>(ElementToDirty.Element))
			{
				if(ERigTransformType::IsGlobal(TypeToDirty))
				{
					if(ControlElement->Parent.IsDirty(TypeToDirty) &&
						ControlElement->Offset.IsDirty(TypeToDirty) &&
						ControlElement->Pose.IsDirty(TypeToDirty))
					{
						continue;
					}
				}
				else
				{
					if(ControlElement->Parent.IsDirty(TypeToDirty))
					{
						continue;
					}
				}
			}
			else if(FRigMultiParentElement* MultiParentElement = Cast<FRigMultiParentElement>(ElementToDirty.Element))
			{
				if(ERigTransformType::IsGlobal(TypeToDirty))
				{
					if(MultiParentElement->Parent.IsDirty(TypeToDirty) &&
						MultiParentElement->Pose.IsDirty(TypeToDirty))
					{
						continue;
					}
				}
				else
				{
					if(MultiParentElement->Parent.IsDirty(TypeToDirty))
					{
						continue;
					}
				}
			}
			else
			{
				if(ElementToDirty.Element->Pose.IsDirty(TypeToDirty))
				{
					continue;
				}
			}
						
#else

			if(!bAffectChildren && ElementToDirty.HierarchyDistance > 1)
			{
				continue;
			}
			
#endif

			ElementToDirty.Element->Pose.MarkDirty(TypeToDirty);
		
			if(FRigMultiParentElement* MultiParentElement = Cast<FRigMultiParentElement>(ElementToDirty.Element))
			{
				MultiParentElement->Parent.MarkDirty(GlobalType);
			}
			if(FRigControlElement* ControlElement = Cast<FRigControlElement>(ElementToDirty.Element))
			{
				ControlElement->Offset.MarkDirty(GlobalType);
				ControlElement->Shape.MarkDirty(GlobalType);
			}

#if URIGHIERARCHY_RECURSIVE_DIRTY_PROPAGATION

			if(bAffectChildren)
			{
#if URIGHIERARCHY_RECURSIVE_DIRTY_PROPAGATION
				PropagateDirtyFlags(ElementToDirty.Element, bInitial, bAffectChildren, false, true);
#endif
			}
			
#endif
		}
	}
}

void URigHierarchy::EnsureCacheValidityImpl()
{
	LLM_SCOPE_BYNAME(TEXT("Animation/ControlRig"));
	if(!bEnableCacheValidityCheck)
	{
		return;
	}
	TGuardValue<bool> Guard(bEnableCacheValidityCheck, false);

	static TArray<FString> TransformTypeStrings;
	if(TransformTypeStrings.IsEmpty())
	{
		for(int32 TransformTypeIndex = 0; TransformTypeIndex < (int32) ERigTransformType::NumTransformTypes; TransformTypeIndex++)
		{
			TransformTypeStrings.Add(StaticEnum<ERigTransformType::Type>()->GetDisplayNameTextByValue((int64)TransformTypeIndex).ToString());
		}
	}

	// make sure that elements which are marked as dirty don't have fully cached children
	ForEach<FRigTransformElement>([](FRigTransformElement* TransformElement)
    {
		for(int32 TransformTypeIndex = 0; TransformTypeIndex < (int32) ERigTransformType::NumTransformTypes; TransformTypeIndex++)
		{
			const ERigTransformType::Type GlobalType = (ERigTransformType::Type)TransformTypeIndex;
			const ERigTransformType::Type LocalType = ERigTransformType::SwapLocalAndGlobal(GlobalType);
			const FString& TransformTypeString = TransformTypeStrings[TransformTypeIndex];

			if(ERigTransformType::IsLocal(GlobalType))
			{
				continue;
			}

			if(!TransformElement->Pose.IsDirty(GlobalType))
			{
				continue;
			}

			for(const FRigTransformElement::FElementToDirty& ElementToDirty : TransformElement->ElementsToDirty)
			{
				if(FRigMultiParentElement* MultiParentElementToDirty = Cast<FRigMultiParentElement>(ElementToDirty.Element))
				{
					check(MultiParentElementToDirty->Parent.IsDirty(GlobalType) ||
                        MultiParentElementToDirty->Parent.IsDirty(LocalType));

                    if(FRigControlElement* ControlElementToDirty = Cast<FRigControlElement>(ElementToDirty.Element))
                    {
                        if(ControlElementToDirty->Parent.IsDirty(GlobalType))
                        {
                            checkf(ControlElementToDirty->Offset.IsDirty(GlobalType) ||
                                    ControlElementToDirty->Offset.IsDirty(LocalType),
                                    TEXT("Control '%s' %s Parent Cache is dirty, but the Offset is not."),
                                    *ControlElementToDirty->GetKey().ToString(),
                                    *TransformTypeString);
                        }

                        if(ControlElementToDirty->Offset.IsDirty(GlobalType))
                        {
                            checkf(ControlElementToDirty->Pose.IsDirty(GlobalType) ||
                                    ControlElementToDirty->Pose.IsDirty(LocalType),
                                    TEXT("Control '%s' %s Offset Cache is dirty, but the Pose is not."),
									*ControlElementToDirty->GetKey().ToString(),
									*TransformTypeString);
						}

                        if(ControlElementToDirty->Pose.IsDirty(GlobalType))
                        {
                            checkf(ControlElementToDirty->Shape.IsDirty(GlobalType) ||
                                    ControlElementToDirty->Shape.IsDirty(LocalType),
                                    TEXT("Control '%s' %s Pose Cache is dirty, but the Shape is not."),
									*ControlElementToDirty->GetKey().ToString(),
									*TransformTypeString);
						}
                    }
                    else
                    {
                        if(MultiParentElementToDirty->Parent.IsDirty(GlobalType))
                        {
                            checkf(MultiParentElementToDirty->Pose.IsDirty(GlobalType) ||
                                    MultiParentElementToDirty->Pose.IsDirty(LocalType),
                                    TEXT("MultiParent '%s' %s Parent Cache is dirty, but the Pose is not."),
									*MultiParentElementToDirty->GetKey().ToString(),
									*TransformTypeString);
						}
                    }
				}
				else
				{
					checkf(ElementToDirty.Element->Pose.IsDirty(GlobalType) ||
						ElementToDirty.Element->Pose.IsDirty(LocalType),
						TEXT("SingleParent '%s' %s Pose is not dirty in Local or Global"),
						*ElementToDirty.Element->GetKey().ToString(),
						*TransformTypeString);
				}
			}
		}
		
        return true;
    });

	// store our own pose in a transient hierarchy used for cache validation
	if(HierarchyForCacheValidation == nullptr)
	{
		HierarchyForCacheValidation = NewObject<URigHierarchy>(this, NAME_None, RF_Transient);
		HierarchyForCacheValidation->bEnableCacheValidityCheck = false;
	}
	if(HierarchyForCacheValidation->GetTopologyVersion() != GetTopologyVersion())
	{
		HierarchyForCacheValidation->CopyHierarchy(this);
	}
	HierarchyForCacheValidation->CopyPose(this, true, true);

	// traverse the copied hierarchy and compare cached vs computed values
	URigHierarchy* HierarchyForLambda = HierarchyForCacheValidation;
	HierarchyForLambda->Traverse([HierarchyForLambda](FRigBaseElement* Element, bool& bContinue)
	{
		bContinue = true;

		if(FRigMultiParentElement* MultiParentElement = Cast<FRigMultiParentElement>(Element))
		{
			for(int32 TransformTypeIndex = 0; TransformTypeIndex < (int32) ERigTransformType::NumTransformTypes; TransformTypeIndex++)
			{
				const ERigTransformType::Type TransformType = (ERigTransformType::Type)TransformTypeIndex;
				const ERigTransformType::Type OpposedType = ERigTransformType::SwapLocalAndGlobal(TransformType);
				const FString& TransformTypeString = TransformTypeStrings[TransformTypeIndex];

				if(ERigTransformType::IsLocal(TransformType))
				{
					continue;
				}

				if(!MultiParentElement->Parent.IsDirty(TransformType))
				{
					const FTransform CachedTransform = HierarchyForLambda->GetParentTransform(MultiParentElement, TransformType);
					MultiParentElement->Parent.MarkDirty(TransformType);
					const FTransform ComputedTransform = HierarchyForLambda->GetParentTransform(MultiParentElement, TransformType);
					checkf(FRigComputedTransform::Equals(CachedTransform, ComputedTransform),
						TEXT("Element '%s' Parent %s Cached vs Computed doesn't match."),
						*Element->GetName().ToString(),
						*TransformTypeString);
				}
			}
		}

		if(FRigControlElement* ControlElement = Cast<FRigControlElement>(Element))
		{
			for(int32 TransformTypeIndex = 0; TransformTypeIndex < (int32) ERigTransformType::NumTransformTypes; TransformTypeIndex++)
			{
				const ERigTransformType::Type TransformType = (ERigTransformType::Type)TransformTypeIndex;
				const ERigTransformType::Type OpposedType = ERigTransformType::SwapLocalAndGlobal(TransformType);
				const FString& TransformTypeString = TransformTypeStrings[TransformTypeIndex];

				if(!ControlElement->Offset.IsDirty(TransformType) && !ControlElement->Offset.IsDirty(OpposedType))
				{
					const FTransform CachedTransform = HierarchyForLambda->GetControlOffsetTransform(ControlElement, TransformType);
					ControlElement->Offset.MarkDirty(TransformType);
					const FTransform ComputedTransform = HierarchyForLambda->GetControlOffsetTransform(ControlElement, TransformType);
					checkf(FRigComputedTransform::Equals(CachedTransform, ComputedTransform),
						TEXT("Element '%s' Offset %s Cached vs Computed doesn't match."),
						*Element->GetName().ToString(),
						*TransformTypeString);
				}
			}
		}

		if(FRigTransformElement* TransformElement = Cast<FRigTransformElement>(Element))
		{
			for(int32 TransformTypeIndex = 0; TransformTypeIndex < (int32) ERigTransformType::NumTransformTypes; TransformTypeIndex++)
			{
				const ERigTransformType::Type TransformType = (ERigTransformType::Type)TransformTypeIndex;
				const ERigTransformType::Type OpposedType = ERigTransformType::SwapLocalAndGlobal(TransformType);
				const FString& TransformTypeString = TransformTypeStrings[TransformTypeIndex];

				if(!TransformElement->Pose.IsDirty(TransformType) && !TransformElement->Pose.IsDirty(OpposedType))
				{
					const FTransform CachedTransform = HierarchyForLambda->GetTransform(TransformElement, TransformType);
					TransformElement->Pose.MarkDirty(TransformType);
					const FTransform ComputedTransform = HierarchyForLambda->GetTransform(TransformElement, TransformType);
					checkf(FRigComputedTransform::Equals(CachedTransform, ComputedTransform),
						TEXT("Element '%s' Pose %s Cached vs Computed doesn't match."),
						*Element->GetName().ToString(),
						*TransformTypeString);
				}
			}
		}

		if(FRigControlElement* ControlElement = Cast<FRigControlElement>(Element))
		{
			for(int32 TransformTypeIndex = 0; TransformTypeIndex < (int32) ERigTransformType::NumTransformTypes; TransformTypeIndex++)
			{
				const ERigTransformType::Type TransformType = (ERigTransformType::Type)TransformTypeIndex;
				const ERigTransformType::Type OpposedType = ERigTransformType::SwapLocalAndGlobal(TransformType);
				const FString& TransformTypeString = TransformTypeStrings[TransformTypeIndex];

				if(!ControlElement->Shape.IsDirty(TransformType) && !ControlElement->Shape.IsDirty(OpposedType))
				{
					const FTransform CachedTransform = HierarchyForLambda->GetControlShapeTransform(ControlElement, TransformType);
					ControlElement->Shape.MarkDirty(TransformType);
					const FTransform ComputedTransform = HierarchyForLambda->GetControlShapeTransform(ControlElement, TransformType);
					checkf(FRigComputedTransform::Equals(CachedTransform, ComputedTransform),
						TEXT("Element '%s' Shape %s Cached vs Computed doesn't match."),
						*Element->GetName().ToString(),
						*TransformTypeString);
				}
			}
		}
	});
}

#if WITH_EDITOR

URigHierarchy::TElementDependencyMap URigHierarchy::GetDependenciesForVM(URigVM* InVM, FName InEventName)
{
	LLM_SCOPE_BYNAME(TEXT("Animation/ControlRig"));
	check(InVM);

	if(InEventName.IsNone())
	{
		InEventName = FRigUnit_BeginExecution().GetEventName();
	}

	URigHierarchy::TElementDependencyMap Dependencies;
	const FRigVMInstructionArray Instructions = InVM->GetByteCode().GetInstructions();

	// make sure the vm matches our cached data
	if(ReadTransformsPerInstructionPerSlice.Num() != Instructions.Num())
	{
		return Dependencies;
	}

	// if the VM doesn't implement the given event
	if(!InVM->ContainsEntry(InEventName))
	{
		return Dependencies;
	}

	// only represent instruction for a given event
	const int32 EntryIndex = InVM->GetByteCode().FindEntryIndex(InEventName);
	const int32 EntryInstructionIndex = InVM->GetByteCode().GetEntry(EntryIndex).InstructionIndex;

	TMap<FRigVMOperand, TArray<int32>> OperandToInstructions;

	for(int32 InstructionIndex = EntryInstructionIndex; InstructionIndex < Instructions.Num(); InstructionIndex++)
	{
		// early exit since following instructions belong to another event
		if(Instructions[InstructionIndex].OpCode == ERigVMOpCode::Exit)
		{
			break;
		}

		const FRigVMOperandArray InputOperands = InVM->GetByteCode().GetInputOperands(InstructionIndex);
		for(const FRigVMOperand InputOperand : InputOperands)
		{
			const FRigVMOperand InputOperandNoRegisterOffset(InputOperand.GetMemoryType(), InputOperand.GetRegisterIndex());
			OperandToInstructions.FindOrAdd(InputOperandNoRegisterOffset).Add(InstructionIndex);
		}
	}

	// for each read transform on an instruction
	// follow the operands to the next instruction affected by it
	for(int32 InstructionIndex = EntryInstructionIndex; InstructionIndex < Instructions.Num(); InstructionIndex++)
	{
		// early exit since following instructions belong to another event
		if(Instructions[InstructionIndex].OpCode == ERigVMOpCode::Exit)
		{
			break;
		}

		const TArray<TArray<int32>>& ReadTransformsPerSlice = ReadTransformsPerInstructionPerSlice[InstructionIndex];

		for(int32 SliceIndex = 0; SliceIndex < ReadTransformsPerSlice.Num(); SliceIndex++)
		{
			const TArray<int32>& ReadTransforms = ReadTransformsPerSlice[SliceIndex];
			if(ReadTransforms.IsEmpty())
			{
				continue;
			}

			TArray<int32> InstructionsToVisit;
			InstructionsToVisit.Add(InstructionIndex);
			
			TArray<int32> WrittenTransforms;

			for(int32 InstructionToVisitIndex = 0; InstructionToVisitIndex < InstructionsToVisit.Num(); InstructionToVisitIndex++)
			{
				const int32 InstructionToVisit = InstructionsToVisit[InstructionToVisitIndex];
				const TArray<TArray<int32>>& WrittenTransformsPerSlice = WrittenTransformsPerInstructionPerSlice[InstructionToVisit];
				if(WrittenTransformsPerSlice.IsValidIndex(SliceIndex))
				{
					const TArray<int32>& WrittenTransformsForSlice = WrittenTransformsPerSlice[SliceIndex];
					for(int32 WrittenTransform : WrittenTransformsForSlice)
					{
						// for the first instruction in this pass let's only care about
						// written transforms which have not been read before
						if(InstructionToVisit == InstructionIndex)
						{
							if(ReadTransforms.Contains(WrittenTransform))
							{
								continue;
							}
						}
						
						WrittenTransforms.AddUnique(WrittenTransform);
					}
				}

				FRigVMOperandArray OutputOperands = InVM->GetByteCode().GetOutputOperands(InstructionToVisit);
				for(const FRigVMOperand OutputOperand : OutputOperands)
				{
					const FRigVMOperand OutputOperandNoRegisterOffset(OutputOperand.GetMemoryType(), OutputOperand.GetRegisterIndex());

					if(const TArray<int32>* InstructionsWithInputOperand = OperandToInstructions.Find(OutputOperandNoRegisterOffset))
					{
						for(int32 InstructionWithInputOperand : *InstructionsWithInputOperand)
						{
							InstructionsToVisit.AddUnique(InstructionWithInputOperand);
						}
					}
				}
			}

			for(const int32 ReadTransform : ReadTransforms)
			{
				for(const int32 WrittenTransform : WrittenTransforms)
				{
					if(ReadTransform != WrittenTransform)
					{
						Dependencies.FindOrAdd(WrittenTransform).AddUnique(ReadTransform);
					}
				}
			}
		}
	}

	return Dependencies;
}

#endif

void URigHierarchy::PushTransformToStack(const FRigElementKey& InKey, ERigTransformStackEntryType InEntryType,
                                         ERigTransformType::Type InTransformType, const FTransform& InOldTransform, const FTransform& InNewTransform,
                                         bool bAffectChildren, bool bModify)
{
#if WITH_EDITOR

	if(GIsTransacting)
	{
		return;
	}

	static const FText TransformPoseTitle = NSLOCTEXT("RigHierarchy", "Set Pose Transform", "Set Pose Transform");
	static const FText ControlOffsetTitle = NSLOCTEXT("RigHierarchy", "Set Control Offset", "Set Control Offset");
	static const FText ControlShapeTitle = NSLOCTEXT("RigHierarchy", "Set Control Gizo", "Set Control Gizo");
	static const FText CurveValueTitle = NSLOCTEXT("RigHierarchy", "Set Curve Value", "Set Curve Value");
	
	FText Title;
	switch(InEntryType)
	{
		case ERigTransformStackEntryType::TransformPose:
		{
			Title = TransformPoseTitle;
			break;
		}
		case ERigTransformStackEntryType::ControlOffset:
		{
			Title = TransformPoseTitle;
			break;
		}
		case ERigTransformStackEntryType::ControlShape:
		{
			Title = TransformPoseTitle;
			break;
		}
		case ERigTransformStackEntryType::CurveValue:
		{
			Title = TransformPoseTitle;
			break;
		}
	}

	TGuardValue<bool> TransactingGuard(bTransactingForTransformChange, true);

	TSharedPtr<FScopedTransaction> TransactionPtr;
	if(bModify)
	{
		TransactionPtr = MakeShareable(new FScopedTransaction(Title));
	}

	if(bIsInteracting)
	{
		bool bCanMerge = LastInteractedKey == InKey;

		FRigTransformStackEntry LastEntry;
		if(!TransformUndoStack.IsEmpty())
		{
			LastEntry = TransformUndoStack.Last();
		}

		if(bCanMerge && LastEntry.Key == InKey && LastEntry.EntryType == InEntryType && LastEntry.bAffectChildren == bAffectChildren)
		{
			// merge the entries on the stack
			TransformUndoStack.Last() = 
                FRigTransformStackEntry(InKey, InEntryType, InTransformType, LastEntry.OldTransform, InNewTransform, bAffectChildren);
		}
		else
		{
			Modify();

			TransformUndoStack.Add(
                FRigTransformStackEntry(InKey, InEntryType, InTransformType, InOldTransform, InNewTransform, bAffectChildren));
			TransformStackIndex = TransformUndoStack.Num();
		}

		TransformRedoStack.Reset();
		LastInteractedKey = InKey;
		return;
	}

	if(bModify)
	{
		Modify();
	}

	TArray<FString> Callstack;
	if(IsTracingChanges() && (CVarControlRigHierarchyTraceCallstack->GetInt() != 0))
	{
		FString JoinedCallStack;
		RigHierarchyCaptureCallStack(JoinedCallStack, 1);
		JoinedCallStack.ReplaceInline(TEXT("\r"), TEXT(""));

		FString Left, Right;
		do
		{
			if(!JoinedCallStack.Split(TEXT("\n"), &Left, &Right))
			{
				Left = JoinedCallStack;
				Right.Empty();
			}

			Left.TrimStartAndEndInline();
			if(Left.StartsWith(TEXT("0x")))
			{
				Left.Split(TEXT(" "), nullptr, &Left);
			}
			Callstack.Add(Left);
			JoinedCallStack = Right;
		}
		while(!JoinedCallStack.IsEmpty());
	}

	TransformUndoStack.Add(
		FRigTransformStackEntry(InKey, InEntryType, InTransformType, InOldTransform, InNewTransform, bAffectChildren, Callstack));
	TransformStackIndex = TransformUndoStack.Num();

	TransformRedoStack.Reset();
	
#endif
}

void URigHierarchy::PushCurveToStack(const FRigElementKey& InKey, float InOldCurveValue, float InNewCurveValue, bool bModify)
{
#if WITH_EDITOR

	FTransform OldTransform = FTransform::Identity;
	FTransform NewTransform = FTransform::Identity;

	OldTransform.SetTranslation(FVector(InOldCurveValue, 0.f, 0.f));
	NewTransform.SetTranslation(FVector(InNewCurveValue, 0.f, 0.f));

	PushTransformToStack(InKey, ERigTransformStackEntryType::CurveValue, ERigTransformType::CurrentLocal, OldTransform, NewTransform, false, bModify);

#endif
}

bool URigHierarchy::ApplyTransformFromStack(const FRigTransformStackEntry& InEntry, bool bUndo)
{
#if WITH_EDITOR

	bool bApplyInitialForCurrent = false;
	FRigBaseElement* Element = Find(InEntry.Key);
	if(Element == nullptr)
	{
		// this might be a transient control which had been removed.
		if(InEntry.Key.Type == ERigElementType::Control)
		{
			const FRigElementKey TargetKey = UControlRig::GetElementKeyFromTransientControl(InEntry.Key);
			Element = Find(TargetKey);
			bApplyInitialForCurrent = Element != nullptr;
		}

		if(Element == nullptr)
		{
			return false;
		}
	}

	const FTransform& Transform = bUndo ? InEntry.OldTransform : InEntry.NewTransform;
	
	switch(InEntry.EntryType)
	{
		case ERigTransformStackEntryType::TransformPose:
		{
			SetTransform(Cast<FRigTransformElement>(Element), Transform, InEntry.TransformType, InEntry.bAffectChildren, false);

			if(ERigTransformType::IsCurrent(InEntry.TransformType) && bApplyInitialForCurrent)
			{
				SetTransform(Cast<FRigTransformElement>(Element), Transform, ERigTransformType::MakeInitial(InEntry.TransformType), InEntry.bAffectChildren, false);
			}
			break;
		}
		case ERigTransformStackEntryType::ControlOffset:
		{
			SetControlOffsetTransform(Cast<FRigControlElement>(Element), Transform, InEntry.TransformType, InEntry.bAffectChildren, false); 
			break;
		}
		case ERigTransformStackEntryType::ControlShape:
		{
			SetControlShapeTransform(Cast<FRigControlElement>(Element), Transform, InEntry.TransformType, false); 
			break;
		}
		case ERigTransformStackEntryType::CurveValue:
		{
			const float CurveValue = Transform.GetTranslation().X;
			SetCurveValue(Cast<FRigCurveElement>(Element), CurveValue, false);
			break;
		}
	}

	return true;

#endif

	return false;
}

void URigHierarchy::ComputeAllTransforms()
{
	for(int32 ElementIndex = 0; ElementIndex < Elements.Num(); ElementIndex++)
	{
		for(int32 TransformTypeIndex = 0; TransformTypeIndex < (int32) ERigTransformType::NumTransformTypes; TransformTypeIndex++)
		{
			const ERigTransformType::Type TransformType = (ERigTransformType::Type)TransformTypeIndex; 
			if(FRigTransformElement* TransformElement = Get<FRigTransformElement>(ElementIndex))
			{
				GetTransform(TransformElement, TransformType);
			}
			if(FRigControlElement* ControlElement = Get<FRigControlElement>(ElementIndex))
			{
				GetControlOffsetTransform(ControlElement, TransformType);
				GetControlShapeTransform(ControlElement, TransformType);
			}
		}
	}
}

FTransform URigHierarchy::GetWorldTransformForReference(const FRigUnitContext* InContext, const FRigElementKey& InKey, bool bInitial)
{
	if(const USceneComponent* OuterSceneComponent = GetTypedOuter<USceneComponent>())
	{
		return OuterSceneComponent->GetComponentToWorld().Inverse();
	}
	return FTransform::Identity;
}

FTransform URigHierarchy::ComputeLocalControlValue(FRigControlElement* ControlElement,
	const FTransform& InGlobalTransform, ERigTransformType::Type InTransformType) const
{
	check(ERigTransformType::IsGlobal(InTransformType));

	const FTransform OffsetTransform =
		GetControlOffsetTransform(ControlElement, ERigTransformType::MakeLocal(InTransformType));

	return InverseSolveParentConstraints(
		InGlobalTransform,
		ControlElement->ParentConstraints,
		InTransformType,
		OffsetTransform);
}

FTransform URigHierarchy::SolveParentConstraints(
	const FRigElementParentConstraintArray& InConstraints,
	const ERigTransformType::Type InTransformType,
	const FTransform& InLocalOffsetTransform,
	bool bApplyLocalOffsetTransform,
	const FTransform& InLocalPoseTransform,
	bool bApplyLocalPoseTransform) const
{
	LLM_SCOPE_BYNAME(TEXT("Animation/ControlRig"));
	FTransform Result = FTransform::Identity;
	const bool bInitial = IsInitial(InTransformType);

	// collect all of the weights
	FConstraintIndex FirstConstraint;
	FConstraintIndex SecondConstraint;
	FConstraintIndex NumConstraintsAffecting(0);
	FRigElementWeight TotalWeight(0.f);
	ComputeParentConstraintIndices(InConstraints, InTransformType, FirstConstraint, SecondConstraint, NumConstraintsAffecting, TotalWeight);

	if(NumConstraintsAffecting.Location == 0 ||
		NumConstraintsAffecting.Rotation == 0 ||
		NumConstraintsAffecting.Scale == 0)
	{
		if(bApplyLocalOffsetTransform)
		{
			Result = InLocalOffsetTransform;
		}
		
		if(bApplyLocalPoseTransform)
		{
			Result = InLocalPoseTransform * Result;
		}

		if(NumConstraintsAffecting.Location == 0 &&
			NumConstraintsAffecting.Rotation == 0 &&
			NumConstraintsAffecting.Scale == 0)
		{
			Result.NormalizeRotation();
			return Result;
		}
	}

	if(NumConstraintsAffecting.Location == 1)
	{
		check(FirstConstraint.Location != INDEX_NONE);

		const FRigElementParentConstraint& ParentConstraint = InConstraints[FirstConstraint.Location];
		const FRigElementWeight& Weight = ParentConstraint.GetWeight(bInitial);
		const FTransform Transform = LazilyComputeParentConstraint(InConstraints, FirstConstraint.Location, InTransformType,
			InLocalOffsetTransform, bApplyLocalOffsetTransform, InLocalPoseTransform, bApplyLocalPoseTransform);

		check(Weight.AffectsLocation());
		Result.SetLocation(Transform.GetLocation());
	}
	else if(NumConstraintsAffecting.Location == 2)
	{
		check(FirstConstraint.Location != INDEX_NONE);
		check(SecondConstraint.Location != INDEX_NONE);

		const FRigElementParentConstraint& ParentConstraintA = InConstraints[FirstConstraint.Location];
		const FRigElementParentConstraint& ParentConstraintB = InConstraints[SecondConstraint.Location];

		const FRigElementWeight& WeightA = ParentConstraintA.GetWeight(bInitial); 
		const FRigElementWeight& WeightB = ParentConstraintB.GetWeight(bInitial);
		check(WeightA.AffectsLocation());
		check(WeightB.AffectsLocation());
		const float Weight = GetWeightForLerp(WeightA.Location, WeightB.Location);

		const FTransform TransformA = LazilyComputeParentConstraint(InConstraints, FirstConstraint.Location, InTransformType,
			InLocalOffsetTransform, bApplyLocalOffsetTransform, InLocalPoseTransform, bApplyLocalPoseTransform);
		const FTransform TransformB = LazilyComputeParentConstraint(InConstraints, SecondConstraint.Location, InTransformType,
			InLocalOffsetTransform, bApplyLocalOffsetTransform, InLocalPoseTransform, bApplyLocalPoseTransform);

		const FVector ParentLocationA = TransformA.GetLocation();
		const FVector ParentLocationB = TransformB.GetLocation();
		Result.SetLocation(FMath::Lerp<FVector>(ParentLocationA, ParentLocationB, Weight));
	}
	else if(NumConstraintsAffecting.Location > 2)
	{
		check(TotalWeight.Location > SMALL_NUMBER);
		
		FVector Location = FVector::ZeroVector;
		
		for(int32 ConstraintIndex = 0; ConstraintIndex < InConstraints.Num(); ConstraintIndex++)
		{
			const FRigElementParentConstraint& ParentConstraint = InConstraints[ConstraintIndex];
			const FRigElementWeight& Weight = ParentConstraint.GetWeight(bInitial);
			if(!Weight.AffectsLocation())
			{
				continue;
			}

			const FTransform Transform = LazilyComputeParentConstraint(InConstraints, ConstraintIndex, InTransformType,
				InLocalOffsetTransform, bApplyLocalOffsetTransform, InLocalPoseTransform, bApplyLocalPoseTransform);

			IntegrateParentConstraintVector(Location, Transform, Weight.Location / TotalWeight.Location, true);
		}

		Result.SetLocation(Location);
	}

	if(NumConstraintsAffecting.Rotation == 1)
	{
		check(FirstConstraint.Rotation != INDEX_NONE);

		const FRigElementParentConstraint& ParentConstraint = InConstraints[FirstConstraint.Rotation];
		const FRigElementWeight& Weight = ParentConstraint.GetWeight(bInitial);
		const FTransform Transform = LazilyComputeParentConstraint(InConstraints, FirstConstraint.Rotation, InTransformType,
			InLocalOffsetTransform, bApplyLocalOffsetTransform, InLocalPoseTransform, bApplyLocalPoseTransform);
		check(Weight.AffectsRotation());
		Result.SetRotation(Transform.GetRotation());
	}
	else if(NumConstraintsAffecting.Rotation == 2)
	{
		check(FirstConstraint.Rotation != INDEX_NONE);
		check(SecondConstraint.Rotation != INDEX_NONE);

		const FRigElementParentConstraint& ParentConstraintA = InConstraints[FirstConstraint.Rotation];
		const FRigElementParentConstraint& ParentConstraintB = InConstraints[SecondConstraint.Rotation];

		const FRigElementWeight& WeightA = ParentConstraintA.GetWeight(bInitial); 
		const FRigElementWeight& WeightB = ParentConstraintB.GetWeight(bInitial);
		check(WeightA.AffectsRotation());
		check(WeightB.AffectsRotation());
		const float Weight = GetWeightForLerp(WeightA.Rotation, WeightB.Rotation);

		const FTransform TransformA = LazilyComputeParentConstraint(InConstraints, FirstConstraint.Rotation, InTransformType,
			InLocalOffsetTransform, bApplyLocalOffsetTransform, InLocalPoseTransform, bApplyLocalPoseTransform);
		const FTransform TransformB = LazilyComputeParentConstraint(InConstraints, SecondConstraint.Rotation, InTransformType,
			InLocalOffsetTransform, bApplyLocalOffsetTransform, InLocalPoseTransform, bApplyLocalPoseTransform);

		const FQuat ParentRotationA = TransformA.GetRotation();
		const FQuat ParentRotationB = TransformB.GetRotation();
		Result.SetRotation(FQuat::Slerp(ParentRotationA, ParentRotationB, Weight));
	}
	else if(NumConstraintsAffecting.Rotation > 2)
	{
		check(TotalWeight.Rotation > SMALL_NUMBER);
		
		int NumMixedRotations = 0;
		FQuat FirstRotation = FQuat::Identity;
		FQuat MixedRotation = FQuat(0.f, 0.f, 0.f, 0.f);

		for(int32 ConstraintIndex = 0; ConstraintIndex < InConstraints.Num(); ConstraintIndex++)
		{
			const FRigElementParentConstraint& ParentConstraint = InConstraints[ConstraintIndex];
			const FRigElementWeight& Weight = ParentConstraint.GetWeight(bInitial);
			if(!Weight.AffectsRotation())
			{
				continue;
			}

			const FTransform Transform = LazilyComputeParentConstraint(InConstraints, ConstraintIndex, InTransformType,
				InLocalOffsetTransform, bApplyLocalOffsetTransform, InLocalPoseTransform, bApplyLocalPoseTransform);

			IntegrateParentConstraintQuat(
				NumMixedRotations,
				FirstRotation,
				MixedRotation,
				Transform,
				Weight.Rotation / TotalWeight.Rotation);
		}

		Result.SetRotation(MixedRotation.GetNormalized());
	}

	if(NumConstraintsAffecting.Scale == 1)
	{
		check(FirstConstraint.Scale != INDEX_NONE);

		const FRigElementParentConstraint& ParentConstraint = InConstraints[FirstConstraint.Scale];
		const FRigElementWeight& Weight = ParentConstraint.GetWeight(bInitial);
		
		const FTransform Transform = LazilyComputeParentConstraint(InConstraints, FirstConstraint.Scale, InTransformType,
			InLocalOffsetTransform, bApplyLocalOffsetTransform, InLocalPoseTransform, bApplyLocalPoseTransform);

		check(Weight.AffectsScale());
		Result.SetScale3D(Transform.GetScale3D());
	}
	else if(NumConstraintsAffecting.Scale == 2)
	{
		check(FirstConstraint.Scale != INDEX_NONE);
		check(SecondConstraint.Scale != INDEX_NONE);

		const FRigElementParentConstraint& ParentConstraintA = InConstraints[FirstConstraint.Scale];
		const FRigElementParentConstraint& ParentConstraintB = InConstraints[SecondConstraint.Scale];

		const FRigElementWeight& WeightA = ParentConstraintA.GetWeight(bInitial); 
		const FRigElementWeight& WeightB = ParentConstraintB.GetWeight(bInitial);
		check(WeightA.AffectsScale());
		check(WeightB.AffectsScale());
		const float Weight = GetWeightForLerp(WeightA.Scale, WeightB.Scale);

		const FTransform TransformA = LazilyComputeParentConstraint(InConstraints, FirstConstraint.Scale, InTransformType,
			InLocalOffsetTransform, bApplyLocalOffsetTransform, InLocalPoseTransform, bApplyLocalPoseTransform);
		const FTransform TransformB = LazilyComputeParentConstraint(InConstraints, SecondConstraint.Scale, InTransformType,
			InLocalOffsetTransform, bApplyLocalOffsetTransform, InLocalPoseTransform, bApplyLocalPoseTransform);

		const FVector ParentScaleA = TransformA.GetScale3D();
		const FVector ParentScaleB = TransformB.GetScale3D();
		Result.SetScale3D(FMath::Lerp<FVector>(ParentScaleA, ParentScaleB, Weight));
	}
	else if(NumConstraintsAffecting.Scale > 2)
	{
		check(TotalWeight.Scale > SMALL_NUMBER);
		
		FVector Scale = FVector::ZeroVector;
		
		for(int32 ConstraintIndex = 0; ConstraintIndex < InConstraints.Num(); ConstraintIndex++)
		{
			const FRigElementParentConstraint& ParentConstraint = InConstraints[ConstraintIndex];
			const FRigElementWeight& Weight = ParentConstraint.GetWeight(bInitial);
			if(!Weight.AffectsScale())
			{
				continue;
			}

			const FTransform Transform = LazilyComputeParentConstraint(InConstraints, ConstraintIndex, InTransformType,
				InLocalOffsetTransform, bApplyLocalOffsetTransform, InLocalPoseTransform, bApplyLocalPoseTransform);

			IntegrateParentConstraintVector(Scale, Transform, Weight.Scale / TotalWeight.Scale, false);
		}

		Result.SetScale3D(Scale);
	}

	Result.NormalizeRotation();
	return Result;
}

FTransform URigHierarchy::InverseSolveParentConstraints(
	const FTransform& InGlobalTransform,
	const FRigElementParentConstraintArray& InConstraints,
	const ERigTransformType::Type InTransformType,
	const FTransform& InLocalOffsetTransform) const
{
	LLM_SCOPE_BYNAME(TEXT("Animation/ControlRig"));

	FTransform Result = FTransform::Identity;

	// this function is doing roughly the following 
	// ResultLocalTransform = InGlobalTransform.GetRelative(ParentGlobal)
	// InTransformType is only used to determine Initial vs Current
	const bool bInitial = IsInitial(InTransformType);
	check(ERigTransformType::IsGlobal(InTransformType));


	// collect all of the weights
	FConstraintIndex FirstConstraint;
	FConstraintIndex SecondConstraint;
	FConstraintIndex NumConstraintsAffecting(0);
	FRigElementWeight TotalWeight(0.f);
	ComputeParentConstraintIndices(InConstraints, InTransformType, FirstConstraint, SecondConstraint, NumConstraintsAffecting, TotalWeight);

	if(NumConstraintsAffecting.Location == 0 ||
		NumConstraintsAffecting.Rotation == 0 ||
		NumConstraintsAffecting.Scale == 0)
	{
		Result = InGlobalTransform.GetRelativeTransform(InLocalOffsetTransform);
		
		if(NumConstraintsAffecting.Location == 0 &&
			NumConstraintsAffecting.Rotation == 0 &&
			NumConstraintsAffecting.Scale == 0)
		{
			Result.NormalizeRotation();
			return Result;
		}
	}

	if(NumConstraintsAffecting.Location == 1)
	{
		check(FirstConstraint.Location != INDEX_NONE);

		const FRigElementParentConstraint& ParentConstraint = InConstraints[FirstConstraint.Location];
		const FRigElementWeight& Weight = ParentConstraint.GetWeight(bInitial);
		const FTransform Transform = LazilyComputeParentConstraint(InConstraints, FirstConstraint.Location, InTransformType,
			InLocalOffsetTransform, true, FTransform::Identity, false);

		check(Weight.AffectsLocation());
		Result.SetLocation(InGlobalTransform.GetRelativeTransform(Transform).GetLocation());
	}
	else if(NumConstraintsAffecting.Location == 2)
	{
		check(FirstConstraint.Location != INDEX_NONE);
		check(SecondConstraint.Location != INDEX_NONE);

		const FRigElementParentConstraint& ParentConstraintA = InConstraints[FirstConstraint.Location];
		const FRigElementParentConstraint& ParentConstraintB = InConstraints[SecondConstraint.Location];

		const FRigElementWeight& WeightA = ParentConstraintA.GetWeight(bInitial); 
		const FRigElementWeight& WeightB = ParentConstraintB.GetWeight(bInitial);
		check(WeightA.AffectsLocation());
		check(WeightB.AffectsLocation());
		const float Weight = GetWeightForLerp(WeightA.Location, WeightB.Location);

		const FTransform TransformA = LazilyComputeParentConstraint(InConstraints, FirstConstraint.Location, InTransformType,
			InLocalOffsetTransform, true, FTransform::Identity, false);
		const FTransform TransformB = LazilyComputeParentConstraint(InConstraints, SecondConstraint.Location, InTransformType,
			InLocalOffsetTransform, true, FTransform::Identity, false);

		const FTransform MixedTransform = FControlRigMathLibrary::LerpTransform(TransformA, TransformB, Weight);
		Result.SetLocation(InGlobalTransform.GetRelativeTransform(MixedTransform).GetLocation());
	}
	else if(NumConstraintsAffecting.Location > 2)
	{
		check(TotalWeight.Location > SMALL_NUMBER);
		
		FVector Location = FVector::ZeroVector;
		int NumMixedRotations = 0;
		FQuat FirstRotation = FQuat::Identity;
		FQuat MixedRotation = FQuat(0.f, 0.f, 0.f, 0.f);
		FVector Scale = FVector::ZeroVector;

		for(int32 ConstraintIndex = 0; ConstraintIndex < InConstraints.Num(); ConstraintIndex++)
		{
			const FRigElementParentConstraint& ParentConstraint = InConstraints[ConstraintIndex];
			const FRigElementWeight& Weight = ParentConstraint.GetWeight(bInitial);
			if(!Weight.AffectsLocation())
			{
				continue;
			}

			const FTransform Transform = LazilyComputeParentConstraint(InConstraints, ConstraintIndex, InTransformType,
				InLocalOffsetTransform, true, FTransform::Identity, false);

			const float NormalizedWeight = Weight.Location / TotalWeight.Location;
			IntegrateParentConstraintVector(Location, Transform, NormalizedWeight, true);
			IntegrateParentConstraintQuat(NumMixedRotations, FirstRotation, MixedRotation, Transform, NormalizedWeight);
			IntegrateParentConstraintVector(Scale, Transform, NormalizedWeight, false);
		}

		FTransform ParentTransform(MixedRotation.GetNormalized(), Location, Scale);
		Result.SetLocation(InGlobalTransform.GetRelativeTransform(ParentTransform).GetLocation());
	}

	if(NumConstraintsAffecting.Rotation == 1)
	{
		check(FirstConstraint.Rotation != INDEX_NONE);

		const FRigElementParentConstraint& ParentConstraint = InConstraints[FirstConstraint.Rotation];
		const FRigElementWeight& Weight = ParentConstraint.GetWeight(bInitial);
		const FTransform Transform = LazilyComputeParentConstraint(InConstraints, FirstConstraint.Rotation, InTransformType,
			InLocalOffsetTransform, true, FTransform::Identity, false);
		check(Weight.AffectsRotation());
		Result.SetRotation(InGlobalTransform.GetRelativeTransform(Transform).GetRotation());
	}
	else if(NumConstraintsAffecting.Rotation == 2)
	{
		check(FirstConstraint.Rotation != INDEX_NONE);
		check(SecondConstraint.Rotation != INDEX_NONE);

		const FRigElementParentConstraint& ParentConstraintA = InConstraints[FirstConstraint.Rotation];
		const FRigElementParentConstraint& ParentConstraintB = InConstraints[SecondConstraint.Rotation];

		const FRigElementWeight& WeightA = ParentConstraintA.GetWeight(bInitial); 
		const FRigElementWeight& WeightB = ParentConstraintB.GetWeight(bInitial);
		check(WeightA.AffectsRotation());
		check(WeightB.AffectsRotation());
		const float Weight = GetWeightForLerp(WeightA.Rotation, WeightB.Rotation);

		const FTransform TransformA = LazilyComputeParentConstraint(InConstraints, FirstConstraint.Rotation, InTransformType,
			InLocalOffsetTransform, true, FTransform::Identity, false);
		const FTransform TransformB = LazilyComputeParentConstraint(InConstraints, SecondConstraint.Rotation, InTransformType,
			InLocalOffsetTransform, true, FTransform::Identity, false);

		const FTransform MixedTransform = FControlRigMathLibrary::LerpTransform(TransformA, TransformB, Weight);
		Result.SetRotation(InGlobalTransform.GetRelativeTransform(MixedTransform).GetRotation());
	}
	else if(NumConstraintsAffecting.Rotation > 2)
	{
		check(TotalWeight.Rotation > SMALL_NUMBER);
		
		FVector Location = FVector::ZeroVector;
		int NumMixedRotations = 0;
		FQuat FirstRotation = FQuat::Identity;
		FQuat MixedRotation = FQuat(0.f, 0.f, 0.f, 0.f);
		FVector Scale = FVector::ZeroVector;

		for(int32 ConstraintIndex = 0; ConstraintIndex < InConstraints.Num(); ConstraintIndex++)
		{
			const FRigElementParentConstraint& ParentConstraint = InConstraints[ConstraintIndex];
			const FRigElementWeight& Weight = ParentConstraint.GetWeight(bInitial);
			if(!Weight.AffectsRotation())
			{
				continue;
			}

			const FTransform Transform = LazilyComputeParentConstraint(InConstraints, ConstraintIndex, InTransformType,
				InLocalOffsetTransform, true, FTransform::Identity, false);

			const float NormalizedWeight = Weight.Rotation / TotalWeight.Rotation;
			IntegrateParentConstraintVector(Location, Transform, NormalizedWeight, true);
			IntegrateParentConstraintQuat(NumMixedRotations, FirstRotation, MixedRotation, Transform, NormalizedWeight);
			IntegrateParentConstraintVector(Scale, Transform, NormalizedWeight, false);
		}

		FTransform ParentTransform(MixedRotation.GetNormalized(), Location, Scale);
		Result.SetRotation(InGlobalTransform.GetRelativeTransform(ParentTransform).GetRotation());
	}

	if(NumConstraintsAffecting.Scale == 1)
	{
		check(FirstConstraint.Scale != INDEX_NONE);

		const FRigElementParentConstraint& ParentConstraint = InConstraints[FirstConstraint.Scale];
		const FRigElementWeight& Weight = ParentConstraint.GetWeight(bInitial);
		
		const FTransform Transform = LazilyComputeParentConstraint(InConstraints, FirstConstraint.Scale, InTransformType,
			InLocalOffsetTransform, true, FTransform::Identity, false);

		check(Weight.AffectsScale());
		Result.SetScale3D(InGlobalTransform.GetRelativeTransform(Transform).GetScale3D());
	}
	else if(NumConstraintsAffecting.Scale == 2)
	{
		check(FirstConstraint.Scale != INDEX_NONE);
		check(SecondConstraint.Scale != INDEX_NONE);

		const FRigElementParentConstraint& ParentConstraintA = InConstraints[FirstConstraint.Scale];
		const FRigElementParentConstraint& ParentConstraintB = InConstraints[SecondConstraint.Scale];

		const FRigElementWeight& WeightA = ParentConstraintA.GetWeight(bInitial); 
		const FRigElementWeight& WeightB = ParentConstraintB.GetWeight(bInitial);
		check(WeightA.AffectsScale());
		check(WeightB.AffectsScale());
		const float Weight = GetWeightForLerp(WeightA.Scale, WeightB.Scale);

		const FTransform TransformA = LazilyComputeParentConstraint(InConstraints, FirstConstraint.Scale, InTransformType,
			InLocalOffsetTransform, true, FTransform::Identity, false);
		const FTransform TransformB = LazilyComputeParentConstraint(InConstraints, SecondConstraint.Scale, InTransformType,
			InLocalOffsetTransform, true, FTransform::Identity, false);

		const FTransform MixedTransform = FControlRigMathLibrary::LerpTransform(TransformA, TransformB, Weight);
		Result.SetScale3D(InGlobalTransform.GetRelativeTransform(MixedTransform).GetScale3D());
	}
	else if(NumConstraintsAffecting.Scale > 2)
	{
		check(TotalWeight.Scale > SMALL_NUMBER);
		
		FVector Location = FVector::ZeroVector;
		int NumMixedRotations = 0;
		FQuat FirstRotation = FQuat::Identity;
		FQuat MixedRotation = FQuat(0.f, 0.f, 0.f, 0.f);
		FVector Scale = FVector::ZeroVector;
		
		for(int32 ConstraintIndex = 0; ConstraintIndex < InConstraints.Num(); ConstraintIndex++)
		{
			const FRigElementParentConstraint& ParentConstraint = InConstraints[ConstraintIndex];
			const FRigElementWeight& Weight = ParentConstraint.GetWeight(bInitial);
			if(!Weight.AffectsScale())
			{
				continue;
			}

			const FTransform Transform = LazilyComputeParentConstraint(InConstraints, ConstraintIndex, InTransformType,
				InLocalOffsetTransform, true, FTransform::Identity, false);

			const float NormalizedWeight = Weight.Scale / TotalWeight.Scale;
			IntegrateParentConstraintVector(Location, Transform, NormalizedWeight, true);
			IntegrateParentConstraintQuat(NumMixedRotations, FirstRotation, MixedRotation, Transform, NormalizedWeight);
			IntegrateParentConstraintVector(Scale, Transform, NormalizedWeight, false);
		}

		FTransform ParentTransform(MixedRotation.GetNormalized(), Location, Scale);
		Result.SetScale3D(InGlobalTransform.GetRelativeTransform(ParentTransform).GetScale3D());
	}

	Result.NormalizeRotation();
	return Result;
}

FTransform URigHierarchy::LazilyComputeParentConstraint(
	const FRigElementParentConstraintArray& InConstraints,
	int32 InIndex,
	const ERigTransformType::Type InTransformType,
	const FTransform& InLocalOffsetTransform,
	bool bApplyLocalOffsetTransform,
	const FTransform& InLocalPoseTransform,
	bool bApplyLocalPoseTransform) const
{
	LLM_SCOPE_BYNAME(TEXT("Animation/ControlRig"));
	const FRigElementParentConstraint& Constraint = InConstraints[InIndex];
	if(Constraint.Cache.bDirty)
	{
		FTransform Transform = GetTransform(Constraint.ParentElement, InTransformType);
		if(bApplyLocalOffsetTransform)
		{
			Transform = InLocalOffsetTransform * Transform;
		}
		if(bApplyLocalPoseTransform)
		{
			Transform = InLocalPoseTransform * Transform;
		}

		Constraint.Cache.Transform = Transform;
		Constraint.Cache.bDirty = false;
	}
	return Constraint.Cache.Transform;
}

void URigHierarchy::ComputeParentConstraintIndices(
	const FRigElementParentConstraintArray& InConstraints,
	ERigTransformType::Type InTransformType,
	FConstraintIndex& OutFirstConstraint,
	FConstraintIndex& OutSecondConstraint,
	FConstraintIndex& OutNumConstraintsAffecting,
	FRigElementWeight& OutTotalWeight)
{
	const bool bInitial = IsInitial(InTransformType);
	
	// find all of the weights affecting this output
	for(int32 ConstraintIndex = 0; ConstraintIndex < InConstraints.Num(); ConstraintIndex++)
	{
		InConstraints[ConstraintIndex].Cache.bDirty = true;
		
		const FRigElementWeight& Weight = InConstraints[ConstraintIndex].GetWeight(bInitial);
		if(Weight.AffectsLocation())
		{
			OutNumConstraintsAffecting.Location++;
			OutTotalWeight.Location += Weight.Location;

			if(OutFirstConstraint.Location == INDEX_NONE)
			{
				OutFirstConstraint.Location = ConstraintIndex;
			}
			else if(OutSecondConstraint.Location == INDEX_NONE)
			{
				OutSecondConstraint.Location = ConstraintIndex;
			}
		}
		if(Weight.AffectsRotation())
		{
			OutNumConstraintsAffecting.Rotation++;
			OutTotalWeight.Rotation += Weight.Rotation;

			if(OutFirstConstraint.Rotation == INDEX_NONE)
			{
				OutFirstConstraint.Rotation = ConstraintIndex;
			}
			else if(OutSecondConstraint.Rotation == INDEX_NONE)
			{
				OutSecondConstraint.Rotation = ConstraintIndex;
			}
		}
		if(Weight.AffectsScale())
		{
			OutNumConstraintsAffecting.Scale++;
			OutTotalWeight.Scale += Weight.Scale;

			if(OutFirstConstraint.Scale == INDEX_NONE)
			{
				OutFirstConstraint.Scale = ConstraintIndex;
			}
			else if(OutSecondConstraint.Scale == INDEX_NONE)
			{
				OutSecondConstraint.Scale = ConstraintIndex;
			}
		}
	}
}
void URigHierarchy::IntegrateParentConstraintVector(
	FVector& OutVector,
	const FTransform& InTransform,
	float InWeight,
	bool bIsLocation)
{
	if(bIsLocation)
	{
		OutVector += InTransform.GetLocation() * InWeight;
	}
	else
	{
		OutVector += InTransform.GetScale3D() * InWeight;
	}
}

void URigHierarchy::IntegrateParentConstraintQuat(
	int32& OutNumMixedRotations,
	FQuat& OutFirstRotation,
	FQuat& OutMixedRotation,
	const FTransform& InTransform,
	float InWeight)
{
	LLM_SCOPE_BYNAME(TEXT("Animation/ControlRig"));
	FQuat ParentRotation = InTransform.GetRotation().GetNormalized();

	if(OutNumMixedRotations == 0)
	{
		OutFirstRotation = ParentRotation; 
	}
	else if ((ParentRotation | OutFirstRotation) <= 0.f)
	{
		InWeight = -InWeight;
	}

	OutMixedRotation.X += InWeight * ParentRotation.X;
	OutMixedRotation.Y += InWeight * ParentRotation.Y;
	OutMixedRotation.Z += InWeight * ParentRotation.Z;
	OutMixedRotation.W += InWeight * ParentRotation.W;
	OutNumMixedRotations++;
}

#if WITH_EDITOR
TArray<FString> URigHierarchy::ControlSettingsToPythonCommands(const FRigControlSettings& Settings, const FString& NameSettings)
{
	TArray<FString> Commands;
	Commands.Add(FString::Printf(TEXT("%s = unreal.RigControlSettings()"),
			*NameSettings));
	FString TypeStr;
	switch (Settings.ControlType)
	{
		case ERigControlType::Bool: TypeStr = TEXT("BOOL"); break;							
		case ERigControlType::Float: TypeStr = TEXT("FLOAT"); break;
		case ERigControlType::Integer: TypeStr = TEXT("INTEGER"); break;
		case ERigControlType::Position: TypeStr = TEXT("POSITION"); break;
		case ERigControlType::Rotator: TypeStr = TEXT("ROTATOR"); break;
		case ERigControlType::Scale: TypeStr = TEXT("SCALE"); break;
		case ERigControlType::Transform: TypeStr = TEXT("EULER_TRANSFORM"); break;
		case ERigControlType::EulerTransform: TypeStr = TEXT("EULER_TRANSFORM"); break;
		case ERigControlType::Vector2D: TypeStr = TEXT("VECTOR2D"); break;
		case ERigControlType::TransformNoScale: TypeStr = TEXT("EULER_TRANSFORM"); break;
		default: ensure(false);
	}

	static const TCHAR* TrueText = TEXT("True");
	static const TCHAR* FalseText = TEXT("False");

	TArray<FString> LimitEnabledParts;
	for(const FRigControlLimitEnabled& LimitEnabled : Settings.LimitEnabled)
	{
		LimitEnabledParts.Add(FString::Printf(TEXT("unreal.RigControlLimitEnabled(%s, %s)"),
						   LimitEnabled.bMinimum ? TrueText : FalseText,
						   LimitEnabled.bMaximum ? TrueText : FalseText));
	}
	
	const FString LimitEnabledStr = FString::Join(LimitEnabledParts, TEXT(", "));
	
	Commands.Add(FString::Printf(TEXT("%s.control_type = unreal.RigControlType.%s"),
									*NameSettings,
									*TypeStr));
	Commands.Add(FString::Printf(TEXT("%s.animatable = %s"),
		*NameSettings,
		Settings.bAnimatable ? TrueText : FalseText));
	Commands.Add(FString::Printf(TEXT("%s.display_name = '%s'"),
		*NameSettings,
		*Settings.DisplayName.ToString()));
	Commands.Add(FString::Printf(TEXT("%s.draw_limits = %s"),
		*NameSettings,
		Settings.bDrawLimits ? TrueText : FalseText));
	Commands.Add(FString::Printf(TEXT("%s.shape_color = %s"),
		*NameSettings,
		*RigVMPythonUtils::LinearColorToPythonString(Settings.ShapeColor)));
	Commands.Add(FString::Printf(TEXT("%s.shape_enabled = %s"),
		*NameSettings,
		Settings.bShapeEnabled ? TrueText : FalseText));
	Commands.Add(FString::Printf(TEXT("%s.shape_name = '%s'"),
		*NameSettings,
		*Settings.ShapeName.ToString()));
	Commands.Add(FString::Printf(TEXT("%s.shape_visible = %s"),
		*NameSettings,
		Settings.bShapeVisible ? TrueText : FalseText));
	Commands.Add(FString::Printf(TEXT("%s.is_transient_control = %s"),
		*NameSettings,
		Settings.bIsTransientControl ? TrueText : FalseText));
	Commands.Add(FString::Printf(TEXT("%s.limit_enabled = [%s]"),
		*NameSettings,
		*LimitEnabledStr));
	Commands.Add(FString::Printf(TEXT("%s.primary_axis = unreal.RigControlAxis.%s"),
		*NameSettings,
		Settings.PrimaryAxis == ERigControlAxis::X ? TEXT("X") : Settings.PrimaryAxis == ERigControlAxis::Y ? TEXT("Y") : TEXT("Z")));

	return Commands;
}

#endif