// Copyright Epic Games, Inc. All Rights Reserved.

#include "RemoteControlPreset.h"

#include "Algo/Find.h"
#include "Algo/ForEach.h"
#include "Algo/Transform.h"
#include "Components/ActorComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/Actor.h"
#include "HAL/IConsoleManager.h"
#include "IRemoteControlModule.h"
#include "Misc/CoreDelegates.h"
#include "Misc/Optional.h"
#include "RemoteControlExposeRegistry.h"
#include "RemoteControlFieldPath.h"
#include "RemoteControlActor.h"
#include "RemoteControlBinding.h"
#include "RemoteControlLogger.h"
#include "RemoteControlObjectVersion.h"
#include "RemoteControlPresetRebindingManager.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"
#if WITH_EDITOR
#include "AnalyticsEventAttribute.h"
#include "Editor.h"
#include "EngineAnalytics.h"
#include "Engine/Blueprint.h"
#include "TimerManager.h"
#include "UObject/PackageReload.h"
#endif

URemoteControlPreset::FOnPostLoadRemoteControlPreset URemoteControlPreset::OnPostLoadRemoteControlPreset;

#define LOCTEXT_NAMESPACE "RemoteControlPreset"

static TAutoConsoleVariable<int32> CVarRemoteControlEnablePropertyWatchInEditor(TEXT("RemoteControl.EnablePropertyWatchInEditor"), 0, TEXT("Whether or not to manually compare certain properties to detect property changes while in editor."));
static TAutoConsoleVariable<int32> CVarRemoteControlFramesBetweenPropertyWatch(TEXT("RemoteControl.FramesBetweenPropertyWatch"), 5, TEXT("The number of frames between every property value comparison when manually watching for property changes."));

namespace
{
	FGuid DefaultGroupId = FGuid(0x5DFBC958, 0xF3B311EA, 0x9A3F00EE, 0xFB2CA371);
	FName NAME_DefaultLayoutGroup = FName("Default Group");
	FName NAME_DefaultNewGroup = FName("New Group");
	const FString DefaultObjectPrefix = TEXT("Default__");

	UClass* FindCommonBase(const TArray<UObject*>& ObjectsToTest)
	{
		TArray<UClass*> Classes;
		Classes.Reserve(ObjectsToTest.Num());
		Algo::TransformIf(ObjectsToTest, Classes, [](const UObject* Object) { return !!Object; }, [](const UObject* Object) { return Object->GetClass(); });
		return UClass::FindCommonBase(Classes);
	}

	/** Create a unique name. */
	FName MakeUniqueName(FName InBase, TFunctionRef<bool(FName)> NamePoolContains)
	{
		// Try using the field name itself
		if (!NamePoolContains(InBase))
		{
			return InBase;
		}

		// Then try the field name with a suffix
		for (uint32 Index = 1; Index < 1000; ++Index)
		{
			const FName Candidate = FName(*FString::Printf(TEXT("%s (%d)"), *InBase.ToString(), Index));
			if (!NamePoolContains(Candidate))
			{
				return Candidate;
			}
		}

		// Something went wrong if we end up here.
		checkNoEntry();
		return NAME_None;
	}

	FName GenerateExposedFieldLabel(const FString& FieldName, UObject* FieldOwner)
	{
		FName OutputName;
		
		if (ensure(FieldOwner))
		{
			FString ObjectName;
	#if WITH_EDITOR
			if (AActor* Actor = Cast<AActor>(FieldOwner))
			{
				ObjectName = Actor->GetActorLabel();
			}
			else if(UActorComponent* Component = Cast<UActorComponent>(FieldOwner))
			{
				ObjectName = Component->GetOwner()->GetActorLabel();
			}
			else
	#endif
			{
				// Get the class name when dealing with BP libraries and subsystems. 
				ObjectName = FieldOwner->HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject) ? FieldOwner->GetClass()->GetName() : FieldOwner->GetName();
			}

			OutputName = *FString::Printf(TEXT("%s (%s)"), *FieldName, *ObjectName);
		}
		return OutputName;
	}
}

FRemoteControlPresetExposeArgs::FRemoteControlPresetExposeArgs()
	: GroupId(DefaultGroupId)
{
}

FRemoteControlPresetExposeArgs::FRemoteControlPresetExposeArgs(FString InLabel, FGuid InGroupId)
	: Label(MoveTemp(InLabel))
	, GroupId(MoveTemp(InGroupId))
{
}

const TArray<FGuid>& FRemoteControlPresetGroup::GetFields() const
{
	return Fields;
}

TArray<FGuid>& FRemoteControlPresetGroup::AccessFields()
{
	return Fields;
}

FRemoteControlPresetLayout::FRemoteControlPresetLayout(URemoteControlPreset* OwnerPreset)
	: Owner(OwnerPreset)
{
	Groups.Emplace(NAME_DefaultLayoutGroup, DefaultGroupId);
}

FRemoteControlPresetGroup& FRemoteControlPresetLayout::GetDefaultGroup()
{
	if (FRemoteControlPresetGroup* Group = GetGroup(DefaultGroupId))
	{
		return *Group;
	}
	else
	{
		return CreateGroupInternal(NAME_DefaultLayoutGroup, DefaultGroupId);
	}
}

FRemoteControlPresetGroup* FRemoteControlPresetLayout::GetGroup(FGuid GroupId)
{
	return Groups.FindByPredicate([GroupId](const FRemoteControlPresetGroup& Group) { return Group.Id == GroupId; });
}

FRemoteControlPresetGroup* FRemoteControlPresetLayout::GetGroupByName(FName GroupName)
{
	return Groups.FindByPredicate([GroupName](const FRemoteControlPresetGroup& Group) { return Group.Name == GroupName; });
}

FRemoteControlPresetGroup& FRemoteControlPresetLayout::CreateGroup(FName GroupName, FGuid GroupId)
{
	FRemoteControlPresetGroup& Group = Groups.Emplace_GetRef(GroupName, GroupId);
	Owner->CacheLayoutData();
	OnGroupAddedDelegate.Broadcast(Group);
	Owner->OnPresetLayoutModified().Broadcast(Owner.Get());
	return Group;
}

FRemoteControlPresetGroup& FRemoteControlPresetLayout::CreateGroup(FName GroupName)
{
	if (GroupName == NAME_None)
	{
		GroupName = NAME_DefaultNewGroup;
	}

	return CreateGroupInternal(MakeUniqueName(GroupName, [this](FName Candidate) { return !!GetGroupByName(Candidate); }), FGuid::NewGuid());
}

FRemoteControlPresetGroup* FRemoteControlPresetLayout::FindGroupFromField(FGuid FieldId)
{
	if (FRCCachedFieldData* CachedData = Owner->FieldCache.Find(FieldId))
	{
		return GetGroup(CachedData->LayoutGroupId);
	}

	return nullptr;
}

bool FRemoteControlPresetLayout::MoveField(FGuid FieldId, FGuid TargetGroupId)
{
	FFieldSwapArgs FieldSwapArgs;
	FieldSwapArgs.DraggedFieldId = FieldId;
	FGuid OriginGroupId = FindGroupFromField(FieldId)->Id;

	if (OriginGroupId.IsValid() && TargetGroupId.IsValid())
	{
		FieldSwapArgs.OriginGroupId = MoveTemp(OriginGroupId);
		FieldSwapArgs.TargetGroupId = MoveTemp(TargetGroupId);
		SwapFields(FieldSwapArgs);
		return true;
	}
	return false;
}

void FRemoteControlPresetLayout::SwapGroups(FGuid OriginGroupId, FGuid TargetGroupId)
{
	FRemoteControlPresetGroup* OriginGroup = GetGroup(OriginGroupId);
	FRemoteControlPresetGroup* TargetGroup = GetGroup(TargetGroupId);

	if (OriginGroup && TargetGroup)
	{
		int32 OriginGroupIndex = Groups.IndexOfByKey(*OriginGroup);
		int32 TargetGroupIndex = Groups.IndexOfByKey(*TargetGroup);

		if (TargetGroupIndex > OriginGroupIndex)
		{
			TargetGroupIndex += 1;
		}
		else
		{
			OriginGroupIndex += 1;
		}

		Groups.Insert(FRemoteControlPresetGroup{ *OriginGroup }, TargetGroupIndex);
		Groups.Swap(TargetGroupIndex, OriginGroupIndex);
		Groups.RemoveAt(OriginGroupIndex);

		TArray<FGuid> GroupIds;
		GroupIds.Reserve(Groups.Num());
		Algo::Transform(Groups, GroupIds, &FRemoteControlPresetGroup::Id);
		OnGroupOrderChangedDelegate.Broadcast(GroupIds);
		Owner->OnPresetLayoutModified().Broadcast(Owner.Get());
	}
}

void FRemoteControlPresetLayout::SwapFields(const FFieldSwapArgs& FieldSwapArgs)
{
	FRemoteControlPresetGroup* DragOriginGroup = GetGroup(FieldSwapArgs.OriginGroupId);
	FRemoteControlPresetGroup* DragTargetGroup = GetGroup(FieldSwapArgs.TargetGroupId);

	if (!DragOriginGroup || !DragTargetGroup)
	{
		return;
	}

	int32 DragOriginFieldIndex = DragOriginGroup->AccessFields().IndexOfByKey(FieldSwapArgs.DraggedFieldId);
	int32 DragTargetFieldIndex = DragTargetGroup->AccessFields().IndexOfByKey(FieldSwapArgs.TargetFieldId);

	if (DragOriginFieldIndex == INDEX_NONE)
	{
		return;
	}

	if (FieldSwapArgs.OriginGroupId == FieldSwapArgs.TargetGroupId && DragTargetFieldIndex != INDEX_NONE)
	{
		if (DragTargetFieldIndex > DragOriginFieldIndex)
		{
			DragTargetFieldIndex += 1;
		}
		else
		{
			DragOriginFieldIndex += 1;
		}

		// Here we don't want to trigger add/delete delegates since the fields just get moved around.
		TArray<FGuid>& Fields = DragTargetGroup->AccessFields();
		Fields.Insert(FieldSwapArgs.DraggedFieldId, DragTargetFieldIndex);
		Fields.Swap(DragTargetFieldIndex, DragOriginFieldIndex);
		Fields.RemoveAt(DragOriginFieldIndex);
	
		Owner->CacheLayoutData();
		OnFieldOrderChangedDelegate.Broadcast(FieldSwapArgs.TargetGroupId, Fields);
		Owner->OnPresetLayoutModified().Broadcast(Owner.Get());
	}
	else
	{
		DragOriginGroup->AccessFields().RemoveAt(DragOriginFieldIndex);

		DragTargetFieldIndex = DragTargetFieldIndex == INDEX_NONE ? 0 : DragTargetFieldIndex;
		DragTargetGroup->AccessFields().Insert(FieldSwapArgs.DraggedFieldId, DragTargetFieldIndex);

		Owner->CacheLayoutData();
		OnFieldDeletedDelegate.Broadcast(FieldSwapArgs.OriginGroupId, FieldSwapArgs.DraggedFieldId, DragOriginFieldIndex);
		OnFieldAddedDelegate.Broadcast(FieldSwapArgs.TargetGroupId, FieldSwapArgs.DraggedFieldId, DragTargetFieldIndex);
		Owner->OnPresetLayoutModified().Broadcast(Owner.Get());
	}
}

void FRemoteControlPresetLayout::DeleteGroup(FGuid GroupId)
{

	int32 Index = Groups.IndexOfByPredicate([GroupId](const FRemoteControlPresetGroup& Group) { return Group.Id == GroupId; });
	if (Index != INDEX_NONE)
	{
		FRemoteControlPresetGroup DeletedGroup = MoveTemp(Groups[Index]);
		Groups.RemoveAt(Index);

		for (const FGuid& FieldId : DeletedGroup.GetFields())
		{
			Owner->Unexpose(FieldId);
		}
		
		Owner->CacheLayoutData();

		OnGroupDeletedDelegate.Broadcast(MoveTemp(DeletedGroup));
		Owner->OnPresetLayoutModified().Broadcast(Owner.Get());
	}
}

void FRemoteControlPresetLayout::RenameGroup(FGuid GroupId, FName NewGroupName)
{
	if (FRemoteControlPresetGroup* Group = GetGroup(GroupId))
	{
		Group->Name = NewGroupName;
		OnGroupRenamedDelegate.Broadcast(GroupId, NewGroupName);
		Owner->OnPresetLayoutModified().Broadcast(Owner.Get());
	}
}

const TArray<FRemoteControlPresetGroup>& FRemoteControlPresetLayout::GetGroups() const
{
	return Groups;
}

TArray<FRemoteControlPresetGroup>& FRemoteControlPresetLayout::AccessGroups()
{
	return Groups;
}

void FRemoteControlPresetLayout::AddField(FGuid GroupId, FGuid FieldId)
{
	if (FRemoteControlPresetGroup* Group = GetGroup(GroupId))
	{
		Group->AccessFields().Add(FieldId);
		OnFieldAddedDelegate.Broadcast(GroupId, FieldId, Group->AccessFields().Num() - 1);
		Owner->OnPresetLayoutModified().Broadcast(Owner.Get());
	}
}

void FRemoteControlPresetLayout::InsertFieldAt(FGuid GroupId, FGuid FieldId, int32 Index)
{
	if (FRemoteControlPresetGroup* Group = GetGroup(GroupId))
	{
		Group->AccessFields().Insert(FieldId, Index);
		OnFieldAddedDelegate.Broadcast(GroupId, FieldId, Index);
		Owner->OnPresetLayoutModified().Broadcast(Owner.Get());
	}
}

void FRemoteControlPresetLayout::RemoveField(FGuid GroupId, FGuid FieldId)
{
	if (FRemoteControlPresetGroup* Group = GetGroup(GroupId))
	{
		int32 Index = Group->AccessFields().IndexOfByKey(FieldId);
		RemoveFieldAt(GroupId, Index);
	}
}

void FRemoteControlPresetLayout::RemoveFieldAt(FGuid GroupId, int32 Index)
{
	if (FRemoteControlPresetGroup* Group = GetGroup(GroupId))
	{
		
		TArray<FGuid>& Fields = Group->AccessFields();
		if(!Fields.IsValidIndex(Index))
		{
			return;
		}

		FGuid FieldId = Fields[Index];
		Fields.RemoveAt(Index);

		OnFieldDeletedDelegate.Broadcast(GroupId, FieldId, Index);
		Owner->OnPresetLayoutModified().Broadcast(Owner.Get());
	}
}

URemoteControlPreset* FRemoteControlPresetLayout::GetOwner()
{
	return Owner.Get();
}

FRemoteControlPresetGroup& FRemoteControlPresetLayout::CreateGroupInternal(FName GroupName, FGuid GroupId)
{
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	return CreateGroup(GroupName, GroupId);
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
}

TOptional<FRemoteControlProperty> FRemoteControlTarget::ExposeProperty(FRCFieldPathInfo FieldPathInfo, const FString& DesiredDisplayName, FGuid GroupId, bool bAppendAliasToLabel)
{
	FName FieldLabel = Owner->GenerateUniqueFieldLabel(Alias, DesiredDisplayName, bAppendAliasToLabel);
	FRemoteControlProperty RCField(Owner.Get(), FieldLabel, MoveTemp(FieldPathInfo), {});
	ExposedProperties.Add(RCField);

	URemoteControlPreset::FExposeInfo ExposeInfo;
	ExposeInfo.Alias = Alias;
	ExposeInfo.FieldId = RCField.GetId();
	ExposeInfo.LayoutGroupId = GroupId;
	Owner->OnExpose(MoveTemp(ExposeInfo));

	return RCField;
}

TOptional<FRemoteControlProperty> FRemoteControlTarget::ExposeProperty(FRCFieldPathInfo FieldPathInfo, TArray<FString> ComponentHierarchy, const FString& DesiredDisplayName, FGuid GroupId)
{
	return ExposeProperty(MoveTemp(FieldPathInfo), DesiredDisplayName, MoveTemp(GroupId));
}

TOptional<FRemoteControlFunction> FRemoteControlTarget::ExposeFunction(FString RelativeFieldPath, const FString& DesiredDisplayName, FGuid GroupId, bool bAppendAliasToLabel)
{
	TOptional<FRemoteControlFunction> RCFunction;

	FRCFieldPathInfo Path{ MoveTemp(RelativeFieldPath) };
	if (UFunction* Function = Class->FindFunctionByName(Path.GetFieldName()))
	{
		FName FieldLabel = Owner->GenerateUniqueFieldLabel(Alias, DesiredDisplayName, bAppendAliasToLabel);
		RCFunction = FRemoteControlFunction{ Owner.Get(), FieldLabel, MoveTemp(Path), Function, {}};
		ExposedFunctions.Add(*RCFunction);

		URemoteControlPreset::FExposeInfo ExposeInfo;
		ExposeInfo.Alias = Alias;
		ExposeInfo.FieldId = RCFunction->GetId();
		ExposeInfo.LayoutGroupId = GroupId;
		Owner->OnExpose(MoveTemp(ExposeInfo));
	}

	return RCFunction;
}

void FRemoteControlTarget::Unexpose(FGuid FieldId)
{
	RemoveField<FRemoteControlProperty>(ExposedProperties, FieldId);
	RemoveField<FRemoteControlFunction>(ExposedFunctions, FieldId);

	Owner->OnUnexpose(FieldId);
}

FName FRemoteControlTarget::FindFieldLabel(FName FieldName) const
{
	FName Label = NAME_None;

	for (const FRemoteControlProperty& RCProperty : ExposedProperties)
	{
		if (RCProperty.FieldName == FieldName)
		{
			Label = RCProperty.GetLabel();
		}
	}

	if (Label == NAME_None)
	{
		for (const FRemoteControlFunction& RCFunction : ExposedFunctions)
		{
			if (RCFunction.FieldName == FieldName)
			{
				Label = RCFunction.GetLabel();
			}
		}
	}

	return Label;
}

FName FRemoteControlTarget::FindFieldLabel(const FRCFieldPathInfo& Path) const
{
	FName Label = NAME_None;

	for (const FRemoteControlProperty& RCProperty : ExposedProperties)
	{
		if (RCProperty.FieldPathInfo.IsEqual(Path))
		{
			Label = RCProperty.GetLabel();
		}
	}

	if (Label == NAME_None)
	{
		for (const FRemoteControlFunction& RCFunction : ExposedFunctions)
		{
			if (RCFunction.FieldPathInfo.IsEqual(Path))
			{
				Label = RCFunction.GetLabel();
			}
		}
	}

	return Label;
}

TOptional<FRemoteControlProperty> FRemoteControlTarget::GetProperty(FGuid PropertyId) const
{
	TOptional<FRemoteControlProperty> Field;
	if (const FRemoteControlProperty* RCField = ExposedProperties.FindByHash(GetTypeHash(PropertyId), PropertyId))
	{
		Field = *RCField;
	}
	return Field;
}

TOptional<FRemoteControlFunction> FRemoteControlTarget::GetFunction(FGuid FunctionId) const
{
	TOptional<FRemoteControlFunction> Field;
	if (const FRemoteControlFunction* RCField = ExposedFunctions.FindByHash(GetTypeHash(FunctionId), FunctionId))
	{
		Field = *RCField;
	}
	return Field;
}

PRAGMA_DISABLE_DEPRECATION_WARNINGS
TOptional<FExposedProperty> FRemoteControlTarget::ResolveExposedProperty(FGuid PropertyId) const
{
	TOptional<FExposedProperty> OptionalExposedProperty;
	ensure(Class);

	if (TOptional<FRemoteControlProperty> RCProperty = GetProperty(PropertyId))
	{
		TArray<UObject*> FieldOwners = RCProperty->GetBoundObjects();
		if (FieldOwners.Num() && FieldOwners[0])
		{
			//Always resolve exposed property. We might have been pointing to an array element that has been deleted
			if (RCProperty->FieldPathInfo.Resolve(FieldOwners[0]))
			{
				OptionalExposedProperty = FExposedProperty{ RCProperty->FieldPathInfo.GetResolvedData().Field, FieldOwners };
			}
		}
	}

	return OptionalExposedProperty;
}
PRAGMA_ENABLE_DEPRECATION_WARNINGS

PRAGMA_DISABLE_DEPRECATION_WARNINGS
TOptional<FExposedFunction> FRemoteControlTarget::ResolveExposedFunction(FGuid FunctionId) const
{
	TOptional<FExposedFunction> OptionalExposedFunction;
	ensure(Class);
	if (TOptional<FRemoteControlFunction> RCFunction = GetFunction(FunctionId))
	{
		FExposedFunction ExposedFunction;
		ExposedFunction.Function = RCFunction->GetFunction();
		ExposedFunction.DefaultParameters = RCFunction->FunctionArguments;
		ExposedFunction.OwnerObjects = ResolveBoundObjects();
		OptionalExposedFunction = MoveTemp(ExposedFunction);
	}

	return OptionalExposedFunction;
}
PRAGMA_ENABLE_DEPRECATION_WARNINGS

TArray<UObject*> FRemoteControlTarget::ResolveBoundObjects() const
{
	TArray<UObject*> ResolvedObjects;
	ResolvedObjects.Reserve(Bindings.Num());
	for (const FSoftObjectPath& Path : Bindings)
	{
		if (UObject* Obj = Path.ResolveObject())
		{
#if WITH_EDITOR
			if (Obj->IsA<AActor>() && GEditor && GEditor->PlayWorld)
			{
				if (AActor* SimWorldActor = EditorUtilities::GetSimWorldCounterpartActor(Cast<AActor>(Obj)))
				{
					Obj = SimWorldActor;
				}
			}
#endif		
			ResolvedObjects.Add(Obj);
		}
	}

	return ResolvedObjects;
}

void FRemoteControlTarget::BindObjects(const TArray<UObject*>& ObjectsToBind)
{
	if (CanBindObjects(ObjectsToBind))
	{
		for (UObject* TargetObject : ObjectsToBind)
		{
			Bindings.Add(FSoftObjectPath{ TargetObject });
		}
	}
}

bool FRemoteControlTarget::HasBoundObjects(const TArray<UObject*>& ObjectsToTest) const
{
	const TArray<UObject*>& TargetObjects = ResolveBoundObjects();
	for (UObject* Object : ObjectsToTest)
	{
		if (!TargetObjects.Contains(Object))
		{
			return false;
		}
	}

	return true;
}

bool FRemoteControlTarget::HasBoundObject(const UObject* ObjectToTest) const
{
	return ResolveBoundObjects().Contains(ObjectToTest);
}

bool FRemoteControlTarget::CanBindObjects(const TArray<UObject*>& ObjectsToTest) const
{
	if (!Class && Bindings.Num() == 0)
	{
		// If no target class is set, then anything can be exposed.
		return true;
	}

	UClass* OwnersCommonBase = FindCommonBase(ObjectsToTest);
	return OwnersCommonBase && OwnersCommonBase->IsChildOf(Class);
}

PRAGMA_DISABLE_DEPRECATION_WARNINGS
FProperty* FRemoteControlTarget::FindPropertyRecursive(UStruct* Container, TArray<FString>& DesiredPropertyPath) const
{
	if (DesiredPropertyPath.Num() <= 0)
	{
		return nullptr;
	}

	const FString DesiredPropertyName = DesiredPropertyPath[0];
	for (TFieldIterator<FProperty> It(Container, EFieldIteratorFlags::IncludeSuper, EFieldIteratorFlags::ExcludeDeprecated, EFieldIteratorFlags::ExcludeInterfaces); It; ++It)
	{
		if (It->GetName() == DesiredPropertyName)
		{
			if (DesiredPropertyPath.Num() == 1)
			{
				return *It;
			}
			else
			{
				//We're looking for nested property so might be a struct or array
				if (FStructProperty* StructProp = CastField<FStructProperty>(*It))
				{
					DesiredPropertyPath.RemoveAt(0, 1, false);
					return FindPropertyRecursive(StructProp->Struct, DesiredPropertyPath);
				}
			}
		}
	}

	return nullptr;
}
PRAGMA_ENABLE_DEPRECATION_WARNINGS

URemoteControlPreset::URemoteControlPreset()
	: Layout(FRemoteControlPresetLayout{ this })
	, PresetId(FGuid::NewGuid())
	, RebindingManager(MakePimpl<FRemoteControlPresetRebindingManager>())
{
	Registry = CreateDefaultSubobject<URemoteControlExposeRegistry>(FName("ExposeRegistry"));
}

void URemoteControlPreset::PostInitProperties()
{
	Super::PostInitProperties();
	if (!HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject))
	{
		RegisterDelegates();
	}
}

#if WITH_EDITOR
void URemoteControlPreset::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	if (PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(URemoteControlPreset, Metadata))
	{
		OnMetadataModified().Broadcast(this);
	}
}
#endif /*WITH_EDITOR*/

void URemoteControlPreset::PostLoad()
{
	Super::PostLoad();

	OnPostLoadRemoteControlPreset.Broadcast(this);

	RegisterDelegates();

	CacheFieldsData();

	CacheFieldLayoutData();

	InitializeEntitiesMetadata();

	RegisterEntityDelegates();

	CreatePropertyWatchers();
}

void URemoteControlPreset::PostDuplicate(bool bDuplicateForPIE)
{
	Super::PostDuplicate(bDuplicateForPIE);
	if (!bDuplicateForPIE)
	{
		PresetId = FGuid::NewGuid();
	}
}

void URemoteControlPreset::BeginDestroy()
{
	UnregisterDelegates();
	Super::BeginDestroy();
}

FName URemoteControlPreset::GetOwnerAlias(FGuid FieldId) const
{
	if (const FRCCachedFieldData* CachedData = FieldCache.Find(FieldId))
	{
		return CachedData->OwnerObjectAlias;
	}

	return NAME_None;
}

FGuid URemoteControlPreset::GetFieldId(FName FieldLabel) const
{
	if (const FGuid* Guid = NameToGuidMap.Find(FieldLabel))
	{
		return *Guid;
	}
	return FGuid();
}

TWeakPtr<FRemoteControlActor> URemoteControlPreset::ExposeActor(AActor* Actor, FRemoteControlPresetExposeArgs Args)
{
	check(Actor);

#if WITH_EDITOR
	const TCHAR* DesiredName = Args.Label.IsEmpty() ? *Actor->GetActorLabel() : *Args.Label;
#else
	const TCHAR* DesiredName = Args.Label.IsEmpty() ? *Actor->GetName() : *Args.Label;
#endif

	FText LogText = FText::Format(LOCTEXT("ExposedActor", "Exposed actor ({0})"), FText::FromString(Actor->GetName()));
    FRemoteControlLogger::Get().Log(TEXT("RemoteControlPreset"), [Text = MoveTemp(LogText)](){ return Text; });

	FRemoteControlActor RCActor{this, Registry->GenerateUniqueLabel(DesiredName), { FindOrAddBinding(Actor)} };
	return StaticCastSharedPtr<FRemoteControlActor>(Expose(MoveTemp(RCActor), FRemoteControlActor::StaticStruct(), Args.GroupId));
}

TWeakPtr<FRemoteControlProperty> URemoteControlPreset::ExposeProperty(UObject* Object, FRCFieldPathInfo FieldPath, FRemoteControlPresetExposeArgs Args)
{
	if (!Object)
	{
		return nullptr;
	}

	if (!FieldPath.Resolve(Object))
	{
		return nullptr;
	}

	FProperty* Property = FieldPath.GetResolvedData().Field;
	check(Property);

	FString FieldName;

#if WITH_EDITOR
	FieldName = Property->GetDisplayNameText().ToString();
#else
	FieldName = FieldPath.GetFieldName().ToString();
#endif

	FName DesiredName = *Args.Label;

	if (DesiredName == NAME_None)
	{
		FString ObjectName;
#if WITH_EDITOR
		if (AActor* Actor = Cast<AActor>(Object))
		{
			ObjectName = Actor->GetActorLabel();
		}
		else if(UActorComponent* Component = Cast<UActorComponent>(Object))
		{
			ObjectName = Component->GetOwner()->GetActorLabel();
		}
		else
#endif
		{
			ObjectName = Object->GetName();
		}

		DesiredName = *FString::Printf(TEXT("%s (%s)"), *FieldName, *ObjectName);
	}

	FRemoteControlProperty RCProperty{ this, Registry->GenerateUniqueLabel(DesiredName), MoveTemp(FieldPath), { FindOrAddBinding(Object) } };

	TSharedPtr<FRemoteControlProperty> RCPropertyPtr = StaticCastSharedPtr<FRemoteControlProperty>(Expose(MoveTemp(RCProperty), FRemoteControlProperty::StaticStruct(), Args.GroupId));

	RCPropertyPtr->EnableEditCondition();

	if (PropertyShouldBeWatched(RCPropertyPtr))
	{
		CreatePropertyWatcher(RCPropertyPtr);
	}

	FText LogText = FText::Format(LOCTEXT("ExposedProperty", "Exposed property ({0}) on object {1}"), FText::FromString(RCPropertyPtr->FieldPathInfo.ToString()), FText::FromString(Object->GetPathName()));
	FRemoteControlLogger::Get().Log(TEXT("RemoteControlPreset"), [Text = MoveTemp(LogText)](){ return Text; });

	return RCPropertyPtr;
}

TWeakPtr<FRemoteControlFunction> URemoteControlPreset::ExposeFunction(UObject* Object, UFunction* Function, FRemoteControlPresetExposeArgs Args)
{
	if (!Object || !Function || !Object->GetClass() || !Object->GetClass()->FindFunctionByName(Function->GetFName()))
	{
		return nullptr;
	}

	FName DesiredName = *Args.Label;

	if (DesiredName == NAME_None)
	{
		FString FunctionName;
#if WITH_EDITOR
		FunctionName = Function->GetDisplayNameText().ToString(); 
#else
		FunctionName = Function->GetName();
#endif
		
		DesiredName = GenerateExposedFieldLabel(FunctionName, Object);
	}

	FRemoteControlFunction RCFunction{ this, Registry->GenerateUniqueLabel(DesiredName), Function->GetName(), Function, { FindOrAddBinding(Object) } };
	TSharedPtr<FRemoteControlFunction> RCFunctionPtr = StaticCastSharedPtr<FRemoteControlFunction>(Expose(MoveTemp(RCFunction), FRemoteControlFunction::StaticStruct(), Args.GroupId));

	RegisterOnCompileEvent(RCFunctionPtr);

	FText LogText = FText::Format(LOCTEXT("ExposedFunction", "Exposed function ({0}) on object {1}"), FText::FromString(Function->GetPathName()), FText::FromString(Object->GetPathName()));
	FRemoteControlLogger::Get().Log(TEXT("RemoteControlPreset"), [Text = MoveTemp(LogText)](){ return Text; });
	
	return RCFunctionPtr;
}

TSharedPtr<FRemoteControlEntity> URemoteControlPreset::Expose(FRemoteControlEntity&& Entity, UScriptStruct* EntityType, const FGuid& GroupId)
{
	Registry->Modify();

#if WITH_EDITOR
	if (FEngineAnalytics::IsAvailable())
	{
		TArray<FAnalyticsEventAttribute> EventAttributes;
		check(EntityType);
		EventAttributes.Add(FAnalyticsEventAttribute(TEXT("ExposedEntityType"), EntityType->GetName()));
		FEngineAnalytics::GetProvider().RecordEvent(TEXT("RemoteControl.EntityExposed"), EventAttributes);	
	}
#endif
	
	TSharedPtr<FRemoteControlEntity> RCEntity = Registry->AddExposedEntity(MoveTemp(Entity), EntityType);
	InitializeEntityMetadata(RCEntity);
	
	RCEntity->OnEntityModifiedDelegate.BindUObject(this, &URemoteControlPreset::OnEntityModified);
	FRemoteControlPresetGroup* Group = Layout.GetGroup(GroupId);
	if (!Group)
	{
		Group = &Layout.GetDefaultGroup();
	}

	FRCCachedFieldData& CachedData = FieldCache.Add(RCEntity->GetId());
	CachedData.LayoutGroupId = Group->Id;

	Layout.AddField(Group->Id, RCEntity->GetId());
	
	OnEntityExposed().Broadcast(this, RCEntity->GetId());

	return RCEntity;
}

URemoteControlBinding* URemoteControlPreset::FindOrAddBinding(const TSoftObjectPtr<UObject>& Object)
{
	if (!ensureAlways(Object.ToString().Len()))
	{
		return nullptr;
	}
	
	for (URemoteControlBinding* Binding : Bindings)
	{
		if (Binding->IsBound(Object))
		{
			return Binding;
		}
	}

	URemoteControlBinding* NewBinding = nullptr;

	if (UObject* ResolvedObject = Object.Get())
	{
		if (ResolvedObject->GetTypedOuter<ULevel>())
        {
        	NewBinding = NewObject<URemoteControlLevelDependantBinding>(this);	
        }
        else
        {
        	NewBinding = NewObject<URemoteControlLevelIndependantBinding>(this);
        }
		
		NewBinding->SetBoundObject(Object);
	}
	else
	{
		// Object is not currently loaded, we have to parse the path manually to find the level path.
		FString Path = Object.ToString();
		static const FString PersistentLevelText = TEXT(":PersistentLevel.");
		int32 PersistentLevelIndex = Path.Find(PersistentLevelText);
		if (PersistentLevelIndex != INDEX_NONE)
		{
			TSoftObjectPtr<ULevel> Level = TSoftObjectPtr<ULevel>{ FSoftObjectPath{ Path.Left(PersistentLevelIndex + PersistentLevelText.Len() - 1) } };
			URemoteControlLevelDependantBinding* LevelDependantBinding = NewObject<URemoteControlLevelDependantBinding>(this);
			LevelDependantBinding->SetBoundObject(Level, Object);
			NewBinding = LevelDependantBinding;
		}
		else
		{
			NewBinding = NewObject<URemoteControlLevelIndependantBinding>(this);
			NewBinding->SetBoundObject(Object);
		}
		
	}

	if (NewBinding)
	{
		Bindings.Add(NewBinding);
	}
	
	return NewBinding;
}

void URemoteControlPreset::OnEntityModified(const FGuid& EntityId)
{
	PerFrameUpdatedEntities.Add(EntityId);
	PerFrameModifiedProperties.Add(EntityId);
}

void URemoteControlPreset::InitializeEntitiesMetadata()
{
	for (const TSharedPtr<FRemoteControlEntity>& Entity : Registry->GetExposedEntities())
	{
		InitializeEntityMetadata(Entity);
	}
}

void URemoteControlPreset::InitializeEntityMetadata(const TSharedPtr<FRemoteControlEntity>& Entity)
{
	if (!Entity)
	{
		return;
	}
    	
    const TMap<FName, FEntityMetadataInitializer>& Initializers = IRemoteControlModule::Get().GetDefaultMetadataInitializers();
    for (const TPair<FName, FEntityMetadataInitializer>& Entry : Initializers)
    {
    	if (Entry.Value.IsBound())
    	{
    		// Don't reset the metadata entry if already present.
    		if (!Entity->UserMetadata.Contains(Entry.Key))
    		{
    			Entity->UserMetadata.Add(Entry.Key, Entry.Value.Execute(this, Entity->GetId()));
    		}
    	}
    }
}

void URemoteControlPreset::RegisterEntityDelegates()
{
	for (const TSharedPtr<FRemoteControlEntity>& Entity : Registry->GetExposedEntities())
	{
		Entity->OnEntityModifiedDelegate.BindUObject(this, &URemoteControlPreset::OnEntityModified);

		if (Entity->GetStruct() == FRemoteControlFunction::StaticStruct())
		{
			RegisterOnCompileEvent(StaticCastSharedPtr<FRemoteControlFunction>(Entity));
		}
	}
}

void URemoteControlPreset::RegisterOnCompileEvent(const TSharedPtr<FRemoteControlFunction>& RCFunction)
{
#if WITH_EDITOR
	if (UFunction* UnderlyingFunction = RCFunction->GetFunction())
	{
		if (UBlueprintGeneratedClass* BPClass = Cast<UBlueprintGeneratedClass>(UnderlyingFunction->GetOwnerClass()))
		{
			if (UBlueprint* Blueprint = Cast<UBlueprint>(BPClass->ClassGeneratedBy))
			{
				if (!BlueprintsWithRegisteredDelegates.Contains(Blueprint))
				{
					BlueprintsWithRegisteredDelegates.Emplace(Blueprint);
					Blueprint->OnCompiled().AddUObject(this, &URemoteControlPreset::OnBlueprintRecompiled);
				}
			}
		}
	}
#endif
}

void URemoteControlPreset::CreatePropertyWatcher(const TSharedPtr<FRemoteControlProperty>& RCProperty)
{
	if (ensure(RCProperty))
	{
		if (!PropertyWatchers.Contains(RCProperty->GetId()))
		{
			FRCPropertyWatcher Watcher{RCProperty, FSimpleDelegate::CreateLambda([this, WeakProperty = TWeakPtr<FRemoteControlProperty>(RCProperty)]()
			{
				if (TSharedPtr<FRemoteControlProperty> PinnedProperty = WeakProperty.Pin())
				{
					PerFrameModifiedProperties.Add(PinnedProperty->GetId());
				}
			})};
			
			PropertyWatchers.Add(RCProperty->GetId(), MoveTemp(Watcher));
		}
	}
}

bool URemoteControlPreset::PropertyShouldBeWatched(const TSharedPtr<FRemoteControlProperty>& RCProperty) const
{
#if WITH_EDITOR
	if (GEditor && !CVarRemoteControlEnablePropertyWatchInEditor.GetValueOnAnyThread())
	{
		// Don't use property watchers in editor unless explicitely specified.
		return false;
	}
#endif

	// If we are not running in editor, we need to watch all properties as there is no object modified callback.
	if (!GIsEditor)
	{
		return true;	
	}
	
	static const TSet<FName> WatchedPropertyNames =
		{
			UStaticMeshComponent::GetRelativeLocationPropertyName(),
			UStaticMeshComponent::GetRelativeRotationPropertyName(),
			UStaticMeshComponent::GetRelativeScale3DPropertyName()
		};
	
	return RCProperty && WatchedPropertyNames.Contains(RCProperty->FieldName);
}

void URemoteControlPreset::CreatePropertyWatchers()
{
	for (const TSharedPtr<FRemoteControlProperty>& ExposedProperty : Registry->GetExposedEntities<FRemoteControlProperty>())
	{
		if (PropertyShouldBeWatched(ExposedProperty))
		{
			CreatePropertyWatcher(ExposedProperty);
		}
	}
}

TOptional<FRemoteControlFunction> URemoteControlPreset::GetFunction(FName FunctionLabel) const
{
PRAGMA_DISABLE_DEPRECATION_WARNINGS
	return GetFunction(GetExposedEntityId(FunctionLabel));
PRAGMA_ENABLE_DEPRECATION_WARNINGS
}

TOptional<FRemoteControlFunction> URemoteControlPreset::GetFunction(FGuid FunctionId) const
{
	TOptional<FRemoteControlFunction> OptionalFunction;
	if (TSharedPtr<FRemoteControlFunction> RCFunction = Registry->GetExposedEntity<FRemoteControlFunction>(FunctionId))
	{
		OptionalFunction = *RCFunction;
	}

	return OptionalFunction;
}

TOptional<FRemoteControlProperty> URemoteControlPreset::GetProperty(FName PropertyLabel) const
{
PRAGMA_DISABLE_DEPRECATION_WARNINGS
	return GetProperty(GetExposedEntityId(PropertyLabel));
PRAGMA_ENABLE_DEPRECATION_WARNINGS
}

TOptional<FRemoteControlProperty> URemoteControlPreset::GetProperty(FGuid PropertyId) const
{
	TOptional<FRemoteControlProperty> OptionalProperty;
	if (TSharedPtr<FRemoteControlProperty> RCProperty = Registry->GetExposedEntity<FRemoteControlProperty>(PropertyId))
	{
		OptionalProperty = *RCProperty;
	}

	return OptionalProperty;
}

void URemoteControlPreset::RenameField(FName OldFieldLabel, FName NewFieldLabel)
{
	RenameExposedEntity(GetExposedEntityId(OldFieldLabel), NewFieldLabel);
}

PRAGMA_DISABLE_DEPRECATION_WARNINGS
TOptional<FExposedProperty> URemoteControlPreset::ResolveExposedProperty(FName PropertyLabel) const
{
	TOptional<FExposedProperty> OptionalExposedProperty;
	if (TSharedPtr<FRemoteControlProperty> RCProp = Registry->GetExposedEntity<FRemoteControlProperty>(GetExposedEntityId(PropertyLabel)))
	{
		OptionalExposedProperty = FExposedProperty();
		OptionalExposedProperty->OwnerObjects = RCProp->GetBoundObjects();
		OptionalExposedProperty->Property = RCProp->GetProperty();
	}

	return OptionalExposedProperty;
}
PRAGMA_ENABLE_DEPRECATION_WARNINGS

PRAGMA_DISABLE_DEPRECATION_WARNINGS
TOptional<FExposedFunction> URemoteControlPreset::ResolveExposedFunction(FName FunctionLabel) const
{
	TOptional<FExposedFunction> OptionalExposedFunction;
	if (TSharedPtr<FRemoteControlFunction> RCProp = Registry->GetExposedEntity<FRemoteControlFunction>(GetExposedEntityId(FunctionLabel)))
	{
		OptionalExposedFunction = FExposedFunction();
		OptionalExposedFunction->DefaultParameters = RCProp->FunctionArguments;
		OptionalExposedFunction->OwnerObjects = RCProp->GetBoundObjects();
		OptionalExposedFunction->Function = RCProp->GetFunction();
	}

	return OptionalExposedFunction;
}
PRAGMA_ENABLE_DEPRECATION_WARNINGS

void URemoteControlPreset::Unexpose(FName EntityLabel)
{
	Unexpose(GetExposedEntityId(EntityLabel));
}

void URemoteControlPreset::Unexpose(const FGuid& EntityId)
{
	if (EntityId.IsValid() && Registry->GetExposedEntity(EntityId).IsValid())
	{
		OnEntityUnexposedDelegate.Broadcast(this, EntityId);
		Registry->Modify();
		Registry->RemoveExposedEntity(EntityId);
		FRCCachedFieldData CachedData = FieldCache.FindChecked(EntityId);
		Layout.RemoveField(CachedData.LayoutGroupId, EntityId);
		FieldCache.Remove(EntityId);
		PropertyWatchers.Remove(EntityId);
	}
}

FName URemoteControlPreset::CreateTarget(const TArray<UObject*>& TargetObjects)
{
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	return CreateAndGetTarget(TargetObjects).Alias;
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
}

PRAGMA_DISABLE_DEPRECATION_WARNINGS
FRemoteControlTarget& URemoteControlPreset::CreateAndGetTarget(const TArray<UObject*>& TargetObjects)
{
	check(TargetObjects.Num() != 0);

	FName Alias = GenerateAliasForObjects(TargetObjects);
	FRemoteControlTarget Target{ this };
	Target.BindObjects(TargetObjects);
	Target.Alias = Alias;

	TArray<UClass*> Classes;
	Classes.Reserve(TargetObjects.Num());
	Algo::Transform(TargetObjects, Classes, [](const UObject* Object) { return Object->GetClass(); });
	UClass* CommonClass = UClass::FindCommonBase(Classes);

	Target.Class = CommonClass;

	return RemoteControlTargets.Add(Alias, MoveTemp(Target));
}
PRAGMA_ENABLE_DEPRECATION_WARNINGS

void URemoteControlPreset::DeleteTarget(FName TargetName)
{	
PRAGMA_DISABLE_DEPRECATION_WARNINGS
	if (FRemoteControlTarget* Target = RemoteControlTargets.Find(TargetName))
	{
		for (auto It = Target->ExposedProperties.CreateConstIterator(); It; ++It)
		{
			Unexpose(It->GetLabel());
		}
	}

	RemoteControlTargets.Remove(TargetName);
PRAGMA_ENABLE_DEPRECATION_WARNINGS
}

void URemoteControlPreset::RenameTarget(FName TargetName, FName NewTargetName)
{
PRAGMA_DISABLE_DEPRECATION_WARNINGS
	FRemoteControlTarget Target;
	RemoteControlTargets.RemoveAndCopyValue(TargetName, Target);
	Target.Alias = NewTargetName;
	RemoteControlTargets.Add(NewTargetName, MoveTemp(Target));
PRAGMA_ENABLE_DEPRECATION_WARNINGS
}

void URemoteControlPreset::CacheLayoutData()
{
	CacheFieldLayoutData();
}

TArray<UObject*> URemoteControlPreset::ResolvedBoundObjects(FName FieldLabel)
{
	TArray<UObject*> Objects;

	if (TSharedPtr<FRemoteControlField> Field = Registry->GetExposedEntity<FRemoteControlField>(GetExposedEntityId(FieldLabel)))
	{
		Objects = Field->GetBoundObjects();
	}

	return Objects;
}

void URemoteControlPreset::RebindUnboundEntities()
{
	RebindingManager->Rebind(this);
	Algo::Transform(Registry->GetExposedEntities(), PerFrameUpdatedEntities, [](const TSharedPtr<FRemoteControlEntity>& Entity) { return Entity->GetId(); });
}

void URemoteControlPreset::NotifyExposedPropertyChanged(FName PropertyLabel)
{
	if (TSharedPtr<FRemoteControlProperty> ExposedProperty = GetExposedEntity<FRemoteControlProperty>(GetExposedEntityId(PropertyLabel)).Pin())
	{
		PerFrameModifiedProperties.Add(ExposedProperty->GetId());
	}
}

void URemoteControlPreset::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);

	Ar.UsingCustomVersion(FRemoteControlObjectVersion::GUID);
	int32 CustomVersion = Ar.CustomVer(FRemoteControlObjectVersion::GUID);

	if (Ar.IsLoading())
	{
		if (CustomVersion < FRemoteControlObjectVersion::BeforeCustomVersionWasAdded)
		{
			// Create new targets and remove the component chain from the fields.
			ConvertFieldsToRemoveComponentChain();
		}

		if (CustomVersion < FRemoteControlObjectVersion::ConvertRCFieldsToRCEntities)
		{
			ConvertFieldsToEntities();
		}

		if (CustomVersion < FRemoteControlObjectVersion::ConvertTargetsToBindings)
		{
			ConvertTargetsToBindings();
		}

		if (!PresetId.IsValid())
		{
			PresetId = FGuid::NewGuid();
		}
	}
}

FRemoteControlField* URemoteControlPreset::GetFieldPtr(FGuid FieldId)
{
	return Registry->GetExposedEntity<FRemoteControlField>(FieldId).Get();
}

void URemoteControlPreset::ConvertFieldsToRemoveComponentChain()
{
PRAGMA_DISABLE_DEPRECATION_WARNINGS
#if WITH_EDITOR
	CacheFieldsData();

	auto GroupPropertiesByObjects = [this]()
	{
		TMap<UObject*, TSet<FRemoteControlProperty>> ObjectAndProperties;

		for (const TTuple<FName, FRemoteControlTarget>& Tuple : RemoteControlTargets)
		{
			const FRemoteControlTarget& Target = Tuple.Value;
			for (const FRemoteControlProperty& Property : Target.ExposedProperties)
			{
				if (Property.ComponentHierarchy_DEPRECATED.Num())
				{
					TArray<UObject*> TargetObjects = Property.GetBoundObjects();
					for (UObject* TargetObject : TargetObjects)
					{
						ObjectAndProperties.FindOrAdd(TargetObject).Add(Property);
					}
				}
			}
		}

		return ObjectAndProperties;
	};

	auto MovePropertiesToTarget = [this](TSet<FRemoteControlProperty>&& Properties, FRemoteControlTarget& DestinationTarget)
	{
		for (FRemoteControlProperty& Property : Properties)
		{
			FRCCachedFieldData& CachedPropData = FieldCache.FindChecked(Property.GetId());
			FRemoteControlTarget& OwnerTarget = RemoteControlTargets.FindChecked(CachedPropData.OwnerObjectAlias);

			if (OwnerTarget.Alias != DestinationTarget.Alias)
			{
				OwnerTarget.RemoveField<FRemoteControlProperty>(OwnerTarget.ExposedProperties, Property.GetId());
				Property.ComponentHierarchy_DEPRECATED.Empty();
				DestinationTarget.ExposedProperties.Add(MoveTemp(Property));
				CachedPropData.OwnerObjectAlias = DestinationTarget.Alias;
			}
		}
	};

	auto FindExistingDestinationTarget = [this](UObject* Object) -> FRemoteControlTarget*
	{
		TTuple<FName, FRemoteControlTarget>* Target = Algo::FindByPredicate(RemoteControlTargets,
			[Object](const TTuple<FName, FRemoteControlTarget>& Target)
			{
				return Target.Value.HasBoundObject(Object);
			});

		return Target ? &Target->Value : nullptr;
	};

	auto RegroupPropertiesInTargets = [this, &MovePropertiesToTarget, &FindExistingDestinationTarget](TMap<UObject*, TSet<FRemoteControlProperty>>&& ObjectAndProperties)
	{
		
		for (TTuple<UObject*, TSet<FRemoteControlProperty>>& ObjectMapEntry : ObjectAndProperties)
		{
			FRemoteControlTarget* DestinationTarget = FindExistingDestinationTarget(ObjectMapEntry.Key);

			if (!DestinationTarget)
			{
				DestinationTarget = &CreateAndGetTarget({ ObjectMapEntry.Key });
			}

			MovePropertiesToTarget(MoveTemp(ObjectMapEntry.Value), *DestinationTarget);
		}
	};

	auto RemoveEmptyTargets = [this]()
	{
		for (auto It = RemoteControlTargets.CreateIterator(); It; ++It)
		{
			if (It.Value().ExposedProperties.Num() == 0 && It.Value().ExposedFunctions.Num() == 0)
			{
				It.RemoveCurrent();
			}
		}
	};

	// Convert fields to not use the component chain anymore.
	RegroupPropertiesInTargets(GroupPropertiesByObjects());
	RemoveEmptyTargets();
#endif
PRAGMA_ENABLE_DEPRECATION_WARNINGS
}

void URemoteControlPreset::ConvertFieldsToEntities()
{
#if WITH_EDITOR
	// Convert properties and functions to inherit from FRemoteControlEntities while preserving their old data.
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	for (TTuple<FName, FRemoteControlTarget>& Tuple : RemoteControlTargets)
	{
		for (FRemoteControlProperty& Property : Tuple.Value.ExposedProperties)
		{
			Property.Owner = this;
		}

		for (FRemoteControlFunction& Function : Tuple.Value.ExposedFunctions)
		{
			Function.Owner = this;
		}
	}
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
#endif
}

void URemoteControlPreset::ConvertTargetsToBindings()
{
#if WITH_EDITOR
	// Convert targets to bindings, and put everything in the registry.
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	for (TTuple<FName, FRemoteControlTarget>& Tuple : RemoteControlTargets)
	{
		TArray<URemoteControlBinding*> NewBindings;
		NewBindings.Reserve(Tuple.Value.Bindings.Num());
		for (const FSoftObjectPath& Path : Tuple.Value.Bindings)
		{
			// Load the asset 
			if (URemoteControlBinding* Binding = FindOrAddBinding(TSoftObjectPtr<UObject>{Path}))
			{
				Binding->Name = Tuple.Value.Alias.ToString();
				NewBindings.Add(Binding);
			}
		}

		for (FRemoteControlProperty& Property : Tuple.Value.ExposedProperties)
		{
			Property.Bindings.Append(NewBindings);
			Registry->AddExposedEntity(MoveTemp(Property), FRemoteControlProperty::StaticStruct());
		}

		for (FRemoteControlFunction& Function : Tuple.Value.ExposedFunctions)
		{
			Function.Bindings.Append(NewBindings);
			Registry->AddExposedEntity(MoveTemp(Function), FRemoteControlFunction::StaticStruct());
		}
	}

	RemoteControlTargets.Reset();

	for (TSharedPtr<FRemoteControlActor> RCActor : Registry->GetExposedEntities<FRemoteControlActor>())
	{
		RCActor->Bindings = { FindOrAddBinding(TSoftObjectPtr<UObject>{ RCActor->Path }) }; 
	}
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
#endif
}

TSharedPtr<const FRemoteControlEntity> URemoteControlPreset::FindEntityById(const FGuid& EntityId, const UScriptStruct* EntityType) const
{
	return Registry->GetExposedEntity(EntityId, EntityType);
}

TSharedPtr<FRemoteControlEntity> URemoteControlPreset::FindEntityById(const FGuid& EntityId, const UScriptStruct* EntityType)
{
	return Registry->GetExposedEntity(EntityId, EntityType);
}

FGuid URemoteControlPreset::GetExposedEntityId(FName EntityLabel) const
{
	if (const FGuid* FoundGuid = NameToGuidMap.Find(EntityLabel))
	{
		return *FoundGuid;
	}
	return Registry->GetExposedEntityId(EntityLabel);
}

TArray<TSharedPtr<FRemoteControlEntity>> URemoteControlPreset::GetEntities(UScriptStruct* EntityType)
{
	return Registry->GetExposedEntities(EntityType);
}

TArray<TSharedPtr<const FRemoteControlEntity>> URemoteControlPreset::GetEntities(UScriptStruct* EntityType) const
{
	return const_cast<const URemoteControlExposeRegistry*>(Registry)->GetExposedEntities(EntityType);
}

const UScriptStruct* URemoteControlPreset::GetExposedEntityType(const FGuid& ExposedEntityId) const
{
	return Registry->GetExposedEntityType(ExposedEntityId);
}

FName URemoteControlPreset::RenameExposedEntity(const FGuid& ExposedEntityId, FName NewLabel)
{
	FName AssignedLabel = Registry->RenameExposedEntity(ExposedEntityId, NewLabel);
	PerFrameUpdatedEntities.Add(ExposedEntityId);
	return AssignedLabel;
}

bool URemoteControlPreset::IsExposed(const FGuid& ExposedEntityId) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(URemoteControlPreset::IsExposed);
	if (FieldCache.Contains(ExposedEntityId))
	{
		return true;
	}
	return Registry->GetExposedEntity(ExposedEntityId).IsValid();
}

TOptional<FRemoteControlField> URemoteControlPreset::GetField(FGuid FieldId) const
{
	TOptional<FRemoteControlField> Field;

	if (TSharedPtr<FRemoteControlField> FieldPtr = Registry->GetExposedEntity<FRemoteControlField>(FieldId))
	{
		Field = *FieldPtr;
	}

	return Field;
}

FName URemoteControlPreset::GenerateAliasForObjects(const TArray<UObject*>& Objects)
{
	auto Functor = [this](FName Candidate) { return RemoteControlTargets.Contains(Candidate); };

	if (Objects.Num() == 1 && Objects[0])
	{
		UObject* TargetObject = Objects[0];
		if (TargetObject->HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject))
		{
			return MakeUniqueName(FName(*TargetObject->GetName().RightChop(DefaultObjectPrefix.Len())), Functor);
		}
		else
		{
			FName Name;
			if (TargetObject->IsA<UActorComponent>())
			{
				UActorComponent* Component = Cast<UActorComponent>(TargetObject);
				Name = *Component->GetPathName(Component->GetOwner()->GetOuter());
			}
			else
			{
				Name = TargetObject->GetFName();
			}

			return MakeUniqueName(Name, Functor);
		}
	}
	else
	{
		ensure(false);
		return NAME_None;
	}
}

FName URemoteControlPreset::GenerateUniqueFieldLabel(FName Alias, const FString& BaseName, bool bAppendAlias)
{
	auto NamePoolContainsFunctor = [this](FName Candidate) { return Registry->GetExposedEntity(GetExposedEntityId(Candidate)).IsValid(); };
	FName BaseCandidate = bAppendAlias ? FName(*FString::Printf(TEXT("%s (%s)"), *BaseName, *Alias.ToString())) : FName(*FString::Printf(TEXT("%s"), *BaseName));
	return MakeUniqueName(BaseCandidate, NamePoolContainsFunctor);
}

void URemoteControlPreset::OnExpose(const FExposeInfo& Info)
{
	FRCCachedFieldData CachedData;

	FGuid FieldGroupId = !Info.LayoutGroupId.IsValid() ? DefaultGroupId : Info.LayoutGroupId;
	FRemoteControlPresetGroup* Group = Layout.GetGroup(FieldGroupId);
	if (!Group)
	{
		Group = &Layout.GetDefaultGroup();
	}

	CachedData.LayoutGroupId = FieldGroupId;
	CachedData.OwnerObjectAlias = Info.Alias;
	FieldCache.FindOrAdd(Info.FieldId) = CachedData;

	if (FRemoteControlField* Field = GetFieldPtr(Info.FieldId))
	{
		NameToGuidMap.Add(Field->GetLabel(), Info.FieldId);
		Layout.AddField(FieldGroupId, Info.FieldId);
		OnEntityExposed().Broadcast(this, Info.FieldId);
	}
}

void URemoteControlPreset::OnUnexpose(FGuid UnexposedFieldId)
 {
	OnEntityUnexposed().Broadcast(this, UnexposedFieldId);
	
	FRCCachedFieldData CachedData = FieldCache.FindChecked(UnexposedFieldId);

	Layout.RemoveField(CachedData.LayoutGroupId, UnexposedFieldId);

	FieldCache.Remove(UnexposedFieldId);

	FName FieldLabel;
	for (auto It = NameToGuidMap.CreateIterator(); It; ++It)
	{
		if (It.Value() == UnexposedFieldId)
		{
			FieldLabel = It.Key();
			It.RemoveCurrent();
		}
	}
}

void URemoteControlPreset::CacheFieldsData()
{
	if (FieldCache.Num())
	{
		return;
	}

	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	for (TTuple<FName, FRemoteControlTarget>& Target : RemoteControlTargets)
	{
		FieldCache.Reserve(FieldCache.Num() + Target.Value.ExposedProperties.Num() + Target.Value.ExposedFunctions.Num());
		auto CacheField = [this, TargetName = Target.Key](const FRemoteControlField& Field)
		{
			FRCCachedFieldData& CachedData = FieldCache.FindOrAdd(Field.GetId());
			CachedData.OwnerObjectAlias = TargetName;
			NameToGuidMap.Add(Field.GetLabel(), Field.GetId());
		};

		Algo::ForEach(Target.Value.ExposedProperties, CacheField);
		Algo::ForEach(Target.Value.ExposedFunctions, CacheField);
	}
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
}

void URemoteControlPreset::CacheFieldLayoutData()
{
	for (FRemoteControlPresetGroup& Group : Layout.AccessGroups())
	{
		for (auto It = Group.AccessFields().CreateIterator(); It; ++It)
		{
			FieldCache.FindOrAdd(*It).LayoutGroupId = Group.Id;
		}
	}
}

void URemoteControlPreset::OnObjectPropertyChanged(UObject* Object, struct FPropertyChangedEvent& Event)
{
	// Objects modified should have run through the preobjectmodified. If interesting, they will be cached
	TRACE_CPUPROFILER_EVENT_SCOPE(URemoteControlPreset::OnObjectPropertyChanged);
 
	if (Event.Property == nullptr)
	{
		if(Event.MemberProperty == nullptr)
		{
			// When no property is passed to OnObjectPropertyChanged (such as by LevelSnapshot->Restore()), let's assume they all changed since we don't have more context.
			for (TSharedPtr<FRemoteControlProperty> Property : Registry->GetExposedEntities<FRemoteControlProperty>())
			{
				if (Property->GetBoundObjects().Contains(Object))
				{
					PerFrameModifiedProperties.Add(Property->GetId());
				}
			}
		}
	}
	else
	{
		for (auto Iter = PreObjectsModifiedCache.CreateIterator(); Iter; ++Iter)
		{
			FGuid& PropertyId = Iter.Key();
			FPreObjectsModifiedCache& CacheEntry = Iter.Value();
 
			if (CacheEntry.Objects.Contains(Object)
                && CacheEntry.Property == Event.Property)
			{
				if (TSharedPtr<FRemoteControlProperty> Property = Registry->GetExposedEntity<FRemoteControlProperty>(PropertyId))
				{
					UE_LOG(LogRemoteControl, VeryVerbose, TEXT("(%s) Change detected on %s::%s"), *GetName(), *Object->GetName(), *Event.Property->GetName());
					PerFrameModifiedProperties.Add(Property->GetId());
					Iter.RemoveCurrent();
				}
			}
		}
	}
 
	for (auto Iter = PreObjectsModifiedActorCache.CreateIterator(); Iter; ++Iter)
	{
		FGuid& ActorId = Iter.Key();
		FPreObjectsModifiedCache& CacheEntry = Iter.Value();
 
		if (CacheEntry.Objects.Contains(Object)
            && CacheEntry.Property == Event.Property)
		{
			if (TSharedPtr<FRemoteControlActor> RCActor = GetExposedEntity<FRemoteControlActor>(ActorId).Pin())
			{
				OnActorPropertyModified().Broadcast(this, *RCActor, Object, CacheEntry.MemberProperty);
				Iter.RemoveCurrent();
			}
		}
	}
}

void URemoteControlPreset::OnPreObjectPropertyChanged(UObject* Object, const class FEditPropertyChain& PropertyChain)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(URemoteControlPreset::OnPreObjectPropertyChanged);
	using PropertyNode = TDoubleLinkedList<FProperty*>::TDoubleLinkedListNode;

	//Quick validation of the property chain
	PropertyNode* Head = PropertyChain.GetHead();
	if (!Head || !Head->GetValue())
	{
		return;
	}

	PropertyNode* Tail = PropertyChain.GetTail();
	if (!Tail || !Tail->GetValue())
	{
		return;
	}

	for (const TSharedPtr<FRemoteControlEntity>& Entity : Registry->GetExposedEntities(FRemoteControlActor::StaticStruct()))
	{
		if (TSharedPtr<FRemoteControlActor> RCActor = StaticCastSharedPtr<FRemoteControlActor>(Entity))
		{
			FString ActorPath = RCActor->Path.ToString();
			if (Object->GetPathName() == ActorPath || Object->GetTypedOuter<AActor>()->GetPathName() == ActorPath)
			{
				FPreObjectsModifiedCache& CacheEntry = PreObjectsModifiedActorCache.FindOrAdd(RCActor->GetId());
				
				// Don't recreate entries for a property we have already cached
				// or if the property was already cached by a child component.
				
				bool bParentObjectCached = CacheEntry.Objects.ContainsByPredicate([Object](UObject* InObjectToCompare){ return InObjectToCompare->GetTypedOuter<AActor>() == Object; }); 
				if (CacheEntry.Property == PropertyChain.GetActiveNode()->GetValue()
					|| CacheEntry.MemberProperty == PropertyChain.GetActiveMemberNode()->GetValue()
					|| bParentObjectCached)
				{
					continue;
				}
				
				CacheEntry.Objects.AddUnique(Object);
				CacheEntry.Property = PropertyChain.GetActiveNode()->GetValue();
				CacheEntry.MemberProperty = PropertyChain.GetActiveMemberNode()->GetValue();
			}
		}
	}

	for (TSharedPtr<FRemoteControlProperty> RCProperty : Registry->GetExposedEntities<FRemoteControlProperty>())
	{
		//If this property is already cached, skip it
		if (PreObjectsModifiedCache.Contains(RCProperty->GetId()))
		{
			continue;
		}
		
		TArray<UObject*> BoundObjects = RCProperty->GetBoundObjects();
		if (BoundObjects.Num() == 0)
		{
			continue;
		}

		for (UObject* BoundObject : BoundObjects)
		{
			if (BoundObject == Object || BoundObject->GetOuter() == Object)
			{
				if (FProperty* ExposedProperty = RCProperty->GetProperty())
				{
					bool bHasFound = false;
					PropertyNode* Current = Tail;
					while (Current && bHasFound == false)
					{
						//Verify if the exposed property was changed
						if (ExposedProperty == Current->GetValue())
						{
							bHasFound = true;

							FPreObjectsModifiedCache& NewEntry = PreObjectsModifiedCache.FindOrAdd(RCProperty->GetId());
							NewEntry.Objects.AddUnique(Object);
							NewEntry.Property = PropertyChain.GetActiveNode()->GetValue();
							NewEntry.MemberProperty = PropertyChain.GetActiveMemberNode()->GetValue();
						}

						// Go backward to walk up the property hierarchy to see if an owning property is exposed.
						Current = Current->GetPrevNode();
					}
				}
			}
		}
	}
}

void URemoteControlPreset::RegisterDelegates()
{
	UnregisterDelegates();

#if WITH_EDITOR
	FCoreUObjectDelegates::OnObjectPropertyChanged.AddUObject(this, &URemoteControlPreset::OnObjectPropertyChanged);

		
	FCoreUObjectDelegates::OnPreObjectPropertyChanged.AddUObject(this, &URemoteControlPreset::OnPreObjectPropertyChanged);

	if (GEngine)
	{
		GEngine->OnLevelActorDeleted().AddUObject(this, &URemoteControlPreset::OnActorDeleted);
	}

	FEditorDelegates::PostPIEStarted.AddUObject(this, &URemoteControlPreset::OnPieEvent);
	FEditorDelegates::EndPIE.AddUObject(this, &URemoteControlPreset::OnPieEvent);
	
	if (GEditor)
	{
		GEditor->OnObjectsReplaced().AddUObject(this, &URemoteControlPreset::OnReplaceObjects);
	}

	FEditorDelegates::MapChange.AddUObject(this, &URemoteControlPreset::OnMapChange);

	FCoreUObjectDelegates::OnPackageReloaded.AddUObject(this, &URemoteControlPreset::OnPackageReloaded);
#endif

	FCoreDelegates::OnBeginFrame.AddUObject(this, &URemoteControlPreset::OnBeginFrame);
	FCoreDelegates::OnEndFrame.AddUObject(this, &URemoteControlPreset::OnEndFrame);
}

void URemoteControlPreset::UnregisterDelegates()
{
	FCoreDelegates::OnBeginFrame.RemoveAll(this);
	FCoreDelegates::OnEndFrame.RemoveAll(this);

#if WITH_EDITOR
	FCoreUObjectDelegates::OnPackageReloaded.RemoveAll(this);

	for (TWeakObjectPtr<UBlueprint> Blueprint : BlueprintsWithRegisteredDelegates)
	{
		if (Blueprint.IsValid())
		{
			Blueprint->OnCompiled().RemoveAll(this);
		}
	}
	
	FEditorDelegates::MapChange.RemoveAll(this);

	if (GEditor)
	{
		GEditor->OnObjectsReplaced().RemoveAll(this);
	}
	
	FEditorDelegates::EndPIE.RemoveAll(this);
	FEditorDelegates::PostPIEStarted.RemoveAll(this);

	if (GEngine)
	{
		GEngine->OnLevelActorDeleted().RemoveAll(this);
	}

	FCoreUObjectDelegates::OnPreObjectPropertyChanged.RemoveAll(this);
	FCoreUObjectDelegates::OnObjectPropertyChanged.RemoveAll(this);
#endif
}

#if WITH_EDITOR
void URemoteControlPreset::OnActorDeleted(AActor* Actor)
{
	UWorld* World = Actor->GetWorld();

	TSet<URemoteControlBinding*> ModifiedBindings;
	
	if (World && !World->IsPreviewWorld())
	{
		for (auto It = Bindings.CreateIterator(); It; ++It)
		{
			UObject* ResolvedObject = (*It)->Resolve();
			if (ResolvedObject && (Actor == ResolvedObject || Actor == ResolvedObject->GetTypedOuter<AActor>()))
			{
				Modify();
				(*It)->Modify();
				(*It)->UnbindObject(ResolvedObject);
				ModifiedBindings.Add(*It);

				if (!(*It)->IsValid())
				{
					It.RemoveCurrent();
				}
			}
		}
	}

	for (TSharedPtr<FRemoteControlEntity> Entity : Registry->GetExposedEntities<FRemoteControlEntity>())
	{
		if (Entity)
		{
			for (auto It = Entity->Bindings.CreateIterator(); It; ++It)
			{
				if (ModifiedBindings.Contains(It->Get()))
				{
					PerFrameUpdatedEntities.Add(Entity->GetId());
					It.RemoveCurrent();
					break;
				}
			}
		}
	}
}

void URemoteControlPreset::OnPieEvent(bool)
{
	GEditor->GetTimerManager()->SetTimerForNextTick(FTimerDelegate::CreateLambda([PresetPtr = TWeakObjectPtr<URemoteControlPreset>{this}]()
	{
		if (PresetPtr.IsValid() && PresetPtr->Registry)
		{
			for (TSharedPtr<FRemoteControlEntity> Entity : PresetPtr->Registry->GetExposedEntities<FRemoteControlEntity>())
			{
				PresetPtr->PerFrameUpdatedEntities.Add(Entity->GetId());
			}	
		}
	}));
}

void URemoteControlPreset::OnReplaceObjects(const TMap<UObject*, UObject*>& ReplacementObjectMap)
{
	TSet<URemoteControlBinding*> ModifiedBindings;
	
	for (URemoteControlBinding* Binding : Bindings)
	{
		UObject* NewObject = nullptr;

		if (UObject* Replacement = ReplacementObjectMap.FindRef(Binding->Resolve()))
		{
			NewObject = Replacement;
		}

		if (NewObject)
		{
			ModifiedBindings.Add(Binding);
			Modify();
			Binding->Modify();
			Binding->SetBoundObject(NewObject);
		}
	}

	for (const TSharedPtr<FRemoteControlField>& Entity : Registry->GetExposedEntities<FRemoteControlField>())
	{
		for (TWeakObjectPtr<URemoteControlBinding> Binding : Entity->Bindings)
		{
			if (!Binding.IsValid())
			{
				continue;
			}
				
			if (ModifiedBindings.Contains(Binding.Get()) || ReplacementObjectMap.FindKey(Binding->Resolve()))
			{
				PerFrameUpdatedEntities.Add(Entity->GetId());
			}
		}
	}
}

void URemoteControlPreset::OnMapChange(uint32)
{
	// Delay the refresh in order for the old actors to be invalid.
	GEditor->GetTimerManager()->SetTimerForNextTick(FTimerDelegate::CreateLambda([PresetPtr = TWeakObjectPtr<URemoteControlPreset>{this}]()
	{
		if (PresetPtr.IsValid() && PresetPtr->Registry)
		{
			Algo::Transform(PresetPtr->Registry->GetExposedEntities(), PresetPtr->PerFrameUpdatedEntities, [](const TSharedPtr<FRemoteControlEntity>& Entity) { return Entity->GetId(); });
			Algo::Transform(PresetPtr->Registry->GetExposedEntities<FRemoteControlProperty>(), PresetPtr->PerFrameModifiedProperties, [](const TSharedPtr<FRemoteControlProperty>& RCProp) { return RCProp->GetId(); });
		}
	}));
}

void URemoteControlPreset::OnBlueprintRecompiled(UBlueprint* Blueprint)
{
	if (!Blueprint)
	{
		return;
	}

	for (TSharedPtr<FRemoteControlFunction> RCFunction : Registry->GetExposedEntities<FRemoteControlFunction>())
	{
		if (UClass* Class = RCFunction->GetSupportedBindingClass())
		{
			if (Class->ClassGeneratedBy == Blueprint)
			{
				if (UFunction* OldFunction = RCFunction->GetFunction())
				{
					UClass* NewClass = Blueprint->GeneratedClass;
					if (!!NewClass->FindFunctionByName(OldFunction->GetFName()))
					{
						RCFunction->RegenerateArguments();
						PerFrameUpdatedEntities.Add(RCFunction->GetId());
					}
				}
			}
		}
	}
}

void URemoteControlPreset::OnPackageReloaded(EPackageReloadPhase Phase, FPackageReloadedEvent* Event)
{
	if (Phase == EPackageReloadPhase::PrePackageFixup && Event)
	{
		URemoteControlPreset* RepointedPreset = nullptr;
		if (Event->GetRepointedObject<URemoteControlPreset>(this, RepointedPreset) && RepointedPreset)
		{
			RepointedPreset->OnEntityExposedDelegate = OnEntityExposedDelegate;
			RepointedPreset->OnEntityUnexposedDelegate = OnEntityUnexposedDelegate;
			RepointedPreset->OnEntitiesUpdatedDelegate = OnEntitiesUpdatedDelegate;
			RepointedPreset->OnPropertyChangedDelegate = OnPropertyChangedDelegate;
			RepointedPreset->OnPropertyExposedDelegate = OnPropertyExposedDelegate;
			RepointedPreset->OnPropertyUnexposedDelegate = OnPropertyUnexposedDelegate;
			RepointedPreset->OnPresetFieldRenamed = OnPresetFieldRenamed;
			RepointedPreset->OnMetadataModifiedDelegate = OnMetadataModifiedDelegate;
			RepointedPreset->OnActorPropertyModifiedDelegate = OnActorPropertyModifiedDelegate;
			RepointedPreset->OnPresetLayoutModifiedDelegate = OnPresetLayoutModifiedDelegate;
		}
	}
}
#endif

void URemoteControlPreset::OnBeginFrame()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(URemoteControlPreset::OnBeginFrame);
	PropertyChangeWatchFrameCounter++;

	if (PropertyChangeWatchFrameCounter == CVarRemoteControlFramesBetweenPropertyWatch.GetValueOnGameThread() - 1)
	{
		PropertyChangeWatchFrameCounter = 0;
		for (TPair<FGuid, FRCPropertyWatcher>& Entry : PropertyWatchers)
		{
			Entry.Value.CheckForChange();
		}
	}
}

void URemoteControlPreset::OnEndFrame()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(URemoteControlPreset::OnEndFrame);
	if (PerFrameUpdatedEntities.Num())
	{
		OnEntitiesUpdatedDelegate.Broadcast(this, PerFrameUpdatedEntities);
		PerFrameUpdatedEntities.Empty();
	}

	if (PerFrameModifiedProperties.Num())
	{
		OnPropertyChangedDelegate.Broadcast(this, PerFrameModifiedProperties);
		PerFrameModifiedProperties.Empty();
	}
}

URemoteControlPreset::FRCPropertyWatcher::FRCPropertyWatcher(const TSharedPtr<FRemoteControlProperty>& InWatchedProperty, FSimpleDelegate&& InOnWatchedValueChanged)
	: OnWatchedValueChanged(MoveTemp(InOnWatchedValueChanged))
	, WatchedProperty(InWatchedProperty)
{
	if (TOptional<FRCFieldResolvedData> ResolvedData = GetWatchedPropertyResolvedData())
	{
		SetLastFrameValue(*ResolvedData);
	}
}

void URemoteControlPreset::FRCPropertyWatcher::CheckForChange()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FRCPropertyWatcher::CheckForChange);
	if (TOptional<FRCFieldResolvedData> ResolvedData = GetWatchedPropertyResolvedData())
	{
		if (ensure(ResolvedData->Field && ResolvedData->ContainerAddress))
		{
			const void* NewValueAddress = ResolvedData->Field->ContainerPtrToValuePtr<void>(ResolvedData->ContainerAddress);
			if (NewValueAddress && (ResolvedData->Field->GetSize() != LastFrameValue.Num() || !ResolvedData->Field->Identical(LastFrameValue.GetData(), NewValueAddress)))
			{
				SetLastFrameValue(*ResolvedData);
				OnWatchedValueChanged.ExecuteIfBound();
			}
		}
	}
}

TOptional<FRCFieldResolvedData> URemoteControlPreset::FRCPropertyWatcher::GetWatchedPropertyResolvedData() const
{
	TOptional<FRCFieldResolvedData> ResolvedData;
	
	if (TSharedPtr<FRemoteControlProperty> RCProperty = WatchedProperty.Pin())
	{
		if (!RCProperty->FieldPathInfo.IsResolved())
		{
			// In theory all objects should have the same value if they have an exposed property.
			TArray<UObject*> Objects = RCProperty->GetBoundObjects();
			if (Objects.Num() != 0)
			{
				RCProperty->FieldPathInfo.Resolve(Objects[0]);
			}
		}

		if (RCProperty->FieldPathInfo.IsResolved())
		{
			ResolvedData = RCProperty->FieldPathInfo.GetResolvedData();
		}
	}

	return ResolvedData;
}

void URemoteControlPreset::FRCPropertyWatcher::SetLastFrameValue(const FRCFieldResolvedData& ResolvedData)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FRCPropertyWatcher::SetLastFrameValue);
	checkSlow(ResolvedData.Field);
	checkSlow(ResolvedData.ContainerAddress);
	
	const void* NewValueAddress = ResolvedData.Field->ContainerPtrToValuePtr<void>(ResolvedData.ContainerAddress);
	LastFrameValue.SetNumUninitialized(ResolvedData.Field->GetSize());
	ResolvedData.Field->CopyCompleteValue(LastFrameValue.GetData(), NewValueAddress);
}

#undef LOCTEXT_NAMESPACE /* RemoteControlPreset */ 
