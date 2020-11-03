// Copyright Epic Games, Inc. All Rights Reserved.

#include "RemoteControlPreset.h"

#include "Algo/ForEach.h"
#include "Algo/Transform.h"
#include "Components/ActorComponent.h"
#include "GameFramework/Actor.h"
#include "IRemoteControlModule.h"
#include "Misc/Optional.h"
#include "StructDeserializer.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"
#include "UObject/StructOnScope.h"

#if WITH_EDITOR
#include "Editor.h"
#endif

#define LOCTEXT_NAMESPACE "RemoteControlPreset" 

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
		return CreateGroup(NAME_DefaultLayoutGroup, DefaultGroupId);
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

	return Group;
}

FRemoteControlPresetGroup& FRemoteControlPresetLayout::CreateGroup()
{
	return CreateGroup(MakeUniqueName(NAME_DefaultNewGroup, [this](FName Candidate) { return !!GetGroupByName(Candidate); }), FGuid::NewGuid());
}

FRemoteControlPresetGroup* FRemoteControlPresetLayout::FindGroupFromField(FGuid FieldId)
{
	if (FRCCachedFieldData* CachedData = Owner->FieldCache.Find(FieldId))
	{
		return GetGroup(CachedData->LayoutGroupId);
	}

	return nullptr;
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
	}
	else
	{
		DragOriginGroup->AccessFields().RemoveAt(DragOriginFieldIndex);

		DragTargetFieldIndex = DragTargetFieldIndex == INDEX_NONE ? 0 : DragTargetFieldIndex;
		DragTargetGroup->AccessFields().Insert(FieldSwapArgs.DraggedFieldId, DragTargetFieldIndex);

		Owner->CacheLayoutData();
		OnFieldDeletedDelegate.Broadcast(FieldSwapArgs.OriginGroupId, FieldSwapArgs.DraggedFieldId, DragOriginFieldIndex);
		OnFieldAddedDelegate.Broadcast(FieldSwapArgs.TargetGroupId, FieldSwapArgs.DraggedFieldId, DragTargetFieldIndex);
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
	}
}

void FRemoteControlPresetLayout::RenameGroup(FGuid GroupId, FName NewGroupName)
{
	if (FRemoteControlPresetGroup* Group = GetGroup(GroupId))
	{
		Group->Name = NewGroupName;
		OnGroupRenamedDelegate.Broadcast(GroupId, NewGroupName);
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
	}
}

void FRemoteControlPresetLayout::InsertFieldAt(FGuid GroupId, FGuid FieldId, int32 Index)
{
	if (FRemoteControlPresetGroup* Group = GetGroup(GroupId))
	{
		Group->AccessFields().Insert(FieldId, Index);
		OnFieldAddedDelegate.Broadcast(GroupId, FieldId, Index);
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
	}
}

URemoteControlPreset* FRemoteControlPresetLayout::GetOwner()
{
	return Owner.Get();
}

TOptional<FRemoteControlProperty> FRemoteControlTarget::ExposeProperty(FRCFieldPathInfo FieldPathInfo, TArray<FString> ComponentHierarchy, const FString& DesiredDisplayName, FGuid GroupId)
{
	FName FieldLabel = Owner->GenerateUniqueFieldLabel(Alias, DesiredDisplayName);
	FRemoteControlProperty RCField(FieldLabel, MoveTemp(FieldPathInfo), MoveTemp(ComponentHierarchy));
	ExposedProperties.Add(MoveTemp(RCField));

	URemoteControlPreset::FExposeInfo ExposeInfo;
	ExposeInfo.Alias = Alias;
	ExposeInfo.FieldId = RCField.Id;
	ExposeInfo.LayoutGroupId = GroupId;
	Owner->OnExpose(MoveTemp(ExposeInfo));

	return RCField;
}

TOptional<FRemoteControlFunction> FRemoteControlTarget::ExposeFunction(FString RelativeFieldPath, const FString& DesiredDisplayName, FGuid GroupId)
{
	TOptional<FRemoteControlFunction> RCFunction;

	// Right now only top level function exposing is supported,
	// We could allow exposing functions on components by resolving the target objects
	if (!ensure(!RelativeFieldPath.Contains(TEXT("."))))
	{
		return RCFunction;
	}

	
	FRCFieldPathInfo Path{ MoveTemp(RelativeFieldPath) };
	if (UFunction* Function = Class->FindFunctionByName(Path.GetFieldName()))
	{
		FName FieldLabel = Owner->GenerateUniqueFieldLabel(Alias, DesiredDisplayName);
		RCFunction = FRemoteControlFunction{ FieldLabel, MoveTemp(Path), Function };
		ExposedFunctions.Add(*RCFunction);

		URemoteControlPreset::FExposeInfo ExposeInfo;
		ExposeInfo.Alias = Alias;
		ExposeInfo.FieldId = RCFunction->Id;
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
			Label = RCProperty.Label;
		}
	}

	if (Label == NAME_None)
	{
		for (const FRemoteControlFunction& RCFunction : ExposedFunctions)
		{
			if (RCFunction.FieldName == FieldName)
			{
				Label = RCFunction.Label;
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
			Label = RCProperty.Label;
		}
	}

	if (Label == NAME_None)
	{
		for (const FRemoteControlFunction& RCFunction : ExposedFunctions)
		{
			if (RCFunction.FieldPathInfo.IsEqual(Path))
			{
				Label = RCFunction.Label;
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

TOptional<FExposedProperty> FRemoteControlTarget::ResolveExposedProperty(FGuid PropertyId) const
{
	TOptional<FExposedProperty> OptionalExposedProperty;
	check(Class);

	if (TOptional<FRemoteControlProperty> RCProperty = GetProperty(PropertyId))
	{
		TArray<UObject*> FieldOwners = RCProperty->ResolveFieldOwners(ResolveBoundObjects());
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

TOptional<FExposedFunction> FRemoteControlTarget::ResolveExposedFunction(FGuid FunctionId) const
{
	TOptional<FExposedFunction> OptionalExposedFunction;
	check(Class);
	if (TOptional<FRemoteControlFunction> RCFunction = GetFunction(FunctionId))
	{
		FExposedFunction ExposedFunction;
		ExposedFunction.Function = RCFunction->Function;
		ExposedFunction.DefaultParameters = RCFunction->FunctionArguments;
		ExposedFunction.OwnerObjects = ResolveBoundObjects();
		OptionalExposedFunction = MoveTemp(ExposedFunction);
	}

	return OptionalExposedFunction;
}

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

bool FRemoteControlTarget::HasBoundObjects(const TArray<UObject*>& ObjectsToTest)
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

bool FRemoteControlTarget::CanBindObjects(const TArray<UObject*>& ObjectsToTest)
{
	if (!Class && Bindings.Num() == 0)
	{
		// If no target class is set, then anything can be exposed.
		return true;
	}

	UClass* OwnersCommonBase = FindCommonBase(ObjectsToTest);
	return OwnersCommonBase && OwnersCommonBase->IsChildOf(Class);
}

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

URemoteControlPreset::URemoteControlPreset()
	: Layout(FRemoteControlPresetLayout{ this })
{
}

void URemoteControlPreset::PostInitProperties()
{
	Super::PostInitProperties();
	if (!HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject))
	{
		RegisterDelegates();
		IRemoteControlModule::Get().RegisterPreset(GetFName(), this);
	}
}

void URemoteControlPreset::PostLoad()
{
	Super::PostLoad();
	IRemoteControlModule::Get().RegisterPreset(GetFName(), this);

	RegisterDelegates();

	FieldCache.Reset();
	CacheFieldsData();
	CacheFieldLayoutData();
}

void URemoteControlPreset::BeginDestroy()
{
	Super::BeginDestroy();

	UnregisterDelegates();
	IRemoteControlModule::Get().UnregisterPreset(GetFName());
}

void URemoteControlPreset::PostRename(UObject* OldOuter, const FName OldName)
{
	Super::PostRename(OldOuter, OldName);
	IRemoteControlModule::Get().UnregisterPreset(OldName);
	IRemoteControlModule::Get().RegisterPreset(GetFName(), this);
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

TOptional<FRemoteControlFunction> URemoteControlPreset::GetFunction(FName FunctionLabel) const
{
	return GetFunction(GetFieldId(FunctionLabel));
}

TOptional<FRemoteControlFunction> URemoteControlPreset::GetFunction(FGuid FunctionId) const
{
	TOptional<FRemoteControlFunction> Function;
	if (const FRCCachedFieldData* CachedData = FieldCache.Find(FunctionId))
	{
		if (const FRemoteControlTarget* Target = RemoteControlTargets.Find(CachedData->OwnerObjectAlias))
		{
			Function = Target->GetFunction(FunctionId);
		}
	}

	return Function;
}

TOptional<FRemoteControlProperty> URemoteControlPreset::GetProperty(FName PropertyLabel) const
{
	return GetProperty(GetFieldId(PropertyLabel));
}

TOptional<FRemoteControlProperty> URemoteControlPreset::GetProperty(FGuid PropertyId) const
{
	TOptional<FRemoteControlProperty> Property;
	if (const FRCCachedFieldData* CachedData = FieldCache.Find(PropertyId))
	{
		if (const FRemoteControlTarget* Target = RemoteControlTargets.Find(CachedData->OwnerObjectAlias))
		{
			Property = Target->GetProperty(PropertyId);
		}
	}

	return Property;
}

void URemoteControlPreset::RenameField(FName OldFieldLabel, FName NewFieldLabel)
{
	if (OldFieldLabel == NewFieldLabel)
	{
		return;
	}

	ensure(!NameToGuidMap.Contains(NewFieldLabel));

	Modify();

	if (FRemoteControlField* Field = GetFieldPtr(GetFieldId(OldFieldLabel)))
	{
		Field->Label = NewFieldLabel;
	}

	FGuid FieldId = GetFieldId(OldFieldLabel);
	NameToGuidMap.Remove(OldFieldLabel);
	NameToGuidMap.Add(NewFieldLabel, FieldId);

	OnPresetFieldRenamed.Broadcast(this, OldFieldLabel, NewFieldLabel);
}

TOptional<FExposedProperty> URemoteControlPreset::ResolveExposedProperty(FName PropertyLabel) const
{
	TOptional<FExposedProperty> OptionalExposedProperty;
	if (const FRCCachedFieldData* Data = FieldCache.Find(GetFieldId(PropertyLabel)))
	{
		OptionalExposedProperty = RemoteControlTargets.FindChecked(Data->OwnerObjectAlias).ResolveExposedProperty(GetFieldId(PropertyLabel));
	}

	return OptionalExposedProperty;
}

TOptional<FExposedFunction> URemoteControlPreset::ResolveExposedFunction(FName FunctionLabel) const
{
	TOptional<FExposedFunction> OptionalExposedFunction;
	if (const FRCCachedFieldData* Data = FieldCache.Find(GetFieldId(FunctionLabel)))
	{
		OptionalExposedFunction = RemoteControlTargets.FindChecked(Data->OwnerObjectAlias).ResolveExposedFunction(GetFieldId(FunctionLabel));
	}

	return OptionalExposedFunction;
}

void URemoteControlPreset::Unexpose(FName FieldLabel)
{
	FGuid FieldId = GetFieldId(FieldLabel);
	Unexpose(FieldId);
}

void URemoteControlPreset::Unexpose(const FGuid& FieldId)
{
	if (FRCCachedFieldData* Data = FieldCache.Find(FieldId))
	{
		RemoteControlTargets.FindChecked(Data->OwnerObjectAlias).Unexpose(FieldId);
	}
}

FName URemoteControlPreset::CreateTarget(const TArray<UObject*>& TargetObjects)
{
	if (TargetObjects.Num() == 0)
	{
		return NAME_None;
	}

	FName Alias = GenerateAliasForObjects(TargetObjects);
	FRemoteControlTarget Target{ this };
	Target.BindObjects(TargetObjects);
	Target.Alias = Alias;

	TArray<UClass*> Classes;
	Classes.Reserve(TargetObjects.Num());
	Algo::Transform(TargetObjects, Classes, [](const UObject* Object) { return Object->GetClass(); });
	UClass* CommonClass = UClass::FindCommonBase(Classes);

	Target.Class = CommonClass;

	RemoteControlTargets.Add(Alias, MoveTemp(Target));

	return Alias;
}

void URemoteControlPreset::DeleteTarget(FName TargetName)
{	
	if (FRemoteControlTarget* Target = RemoteControlTargets.Find(TargetName))
	{
		for (auto It = Target->ExposedProperties.CreateConstIterator(); It; ++It)
		{
			Unexpose(It->Label);
		}
	}

	RemoteControlTargets.Remove(TargetName);
}

void URemoteControlPreset::RenameTarget(FName TargetName, FName NewTargetName)
{
	FRemoteControlTarget Target;
	RemoteControlTargets.RemoveAndCopyValue(TargetName, Target);
	Target.Alias = NewTargetName;
	RemoteControlTargets.Add(NewTargetName, MoveTemp(Target));
}

void URemoteControlPreset::CacheLayoutData()
{
	CacheFieldLayoutData();
}

TArray<UObject*> URemoteControlPreset::ResolvedBoundObjects(FName FieldLabel)
{
	if (const FRCCachedFieldData* Data = FieldCache.Find(GetFieldId(FieldLabel)))
	{
		FRemoteControlTarget& Target = RemoteControlTargets.FindChecked(Data->OwnerObjectAlias);
		return Target.ResolveBoundObjects();
	}

	return TArray<UObject*>();
}

void URemoteControlPreset::NotifyExposedPropertyChanged(FName PropertyLabel)
{
	if (TOptional<FRemoteControlProperty> ExposedProperty = GetProperty(PropertyLabel))
	{
		OnExposedPropertyChanged().Broadcast(this, ExposedProperty.GetValue());
	}
}

FRemoteControlField* URemoteControlPreset::GetFieldPtr(FGuid FieldId)
{
	if (const FRCCachedFieldData* CachedData = FieldCache.Find(FieldId))
	{
		if (FRemoteControlTarget* Target = RemoteControlTargets.Find(CachedData->OwnerObjectAlias))
		{
			if (FRemoteControlField* Field = Target->ExposedProperties.FindByHash(GetTypeHash(FieldId), FieldId))
			{
				return Field;
			}

			return Target->ExposedFunctions.FindByHash(GetTypeHash(FieldId), FieldId);
		}
	}

	return nullptr;
}

TOptional<FRemoteControlField> URemoteControlPreset::GetField(FGuid FieldId) const
{
	TOptional<FRemoteControlField> Field;

	if (TOptional<FRemoteControlProperty> Property = GetProperty(FieldId))
	{
		Field = *Property;
	}
	else if (TOptional<FRemoteControlFunction> Function = GetFunction(FieldId))
	{
		Field = *Function;
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
			return MakeUniqueName(TargetObject->GetFName(), Functor);
		}
	}
	else
	{
		ensure(false);
		return NAME_None;
	}
}

FName URemoteControlPreset::GenerateUniqueFieldLabel(FName Alias, const FString& BaseName)
{
	auto NamePoolContainsFunctor = [this](FName Candidate) { return FieldCache.Contains(GetFieldId(Candidate)); };
	return MakeUniqueName(FName(FString::Printf(TEXT("%s (%s)"), *BaseName, *Alias.ToString())), NamePoolContainsFunctor);
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
		NameToGuidMap.Add(Field->Label, Info.FieldId);
		Layout.AddField(FieldGroupId, Info.FieldId);
		OnPropertyExposed().Broadcast(this, Field->Label);
	}
}

void URemoteControlPreset::OnUnexpose(FGuid UnexposedFieldId)
 {
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

	if (FieldLabel != NAME_None)
	{
		OnPropertyUnexposed().Broadcast(this, FieldLabel);
	}
}

void URemoteControlPreset::CacheFieldsData()
{
	for (TTuple<FName, FRemoteControlTarget>& Target : RemoteControlTargets)
	{
		FieldCache.Reserve(FieldCache.Num() + Target.Value.ExposedProperties.Num() + Target.Value.ExposedFunctions.Num());
		auto CacheField = [this, TargetName = Target.Key](const FRemoteControlField& Field)
		{
			FRCCachedFieldData& CachedData = FieldCache.FindOrAdd(Field.Id);
			CachedData.OwnerObjectAlias = TargetName;
			NameToGuidMap.Add(Field.Label, Field.Id);
		};

		Algo::ForEach(Target.Value.ExposedProperties, CacheField);
		Algo::ForEach(Target.Value.ExposedFunctions, CacheField);
	}
}

void URemoteControlPreset::CacheFieldLayoutData()
{
	for (FRemoteControlPresetGroup& Group : Layout.AccessGroups())
	{
		for (auto It = Group.AccessFields().CreateIterator(); It; ++It)
		{
			if (FRCCachedFieldData* CachedData = FieldCache.Find(*It))
			{
				CachedData->LayoutGroupId = Group.Id;
			}
			else
			{
				//It.RemoveCurrent();
			}
		}
	}
}

void URemoteControlPreset::OnObjectPropertyChanged(UObject* Object, struct FPropertyChangedEvent& Event)
{
	//Objects modified should have run through the preobjectmodified. If interesting, they will be cached
	for (auto Iter = PreObjectModifiedCache.CreateIterator(); Iter; ++Iter)
	{
		FGuid& PropertyId = Iter.Key();
		FPreObjectModifiedCache& CacheEntry = Iter.Value();

		if (CacheEntry.Object == Object
			&& CacheEntry.Property == Event.Property)
		{
			if (TOptional<FRemoteControlProperty> Property = GetProperty(PropertyId))
			{
				OnExposedPropertyChanged().Broadcast(this, Property.GetValue());
				Iter.RemoveCurrent();
			}
		}
	}
}

void URemoteControlPreset::OnPreObjectPropertyChanged(UObject* Object, const class FEditPropertyChain& PropertyChain)
{
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

	//If the modified object is a component, get the owner. Target bindings will point to Actors at the top level
	UObject* Binding = Object;
	if (UActorComponent* Component = Cast<UActorComponent>(Object))
	{
		Binding = Component->GetOwner();
	}

	for (TPair<FName, FRemoteControlTarget>& Pair : RemoteControlTargets)
	{
		FRemoteControlTarget& Target = Pair.Value;
		TArray<UObject*> BoundedObj({ Binding });
		if (Target.HasBoundObjects(BoundedObj))
		{
			for (const FRemoteControlProperty& Property : Target.ExposedProperties)
			{
				//If this property is already cached, skip it
				if (PreObjectModifiedCache.Contains(Property.Id))
				{

					continue;
				}

				if (TOptional<FExposedProperty> ExposedProprety = Target.ResolveExposedProperty(Property.Id))
				{
					bool bHasFound = false;
					PropertyNode* Current = Tail;
					while (Current && bHasFound == false)
					{
						//Verify if the exposed property was changed
						if (ExposedProprety->Property == Current->GetValue())
						{
							bHasFound = true;

							FPreObjectModifiedCache& NewEntry = PreObjectModifiedCache.FindOrAdd(Property.Id);
							NewEntry.Object = Object;
							NewEntry.Property = PropertyChain.GetActiveNode()->GetValue();
							NewEntry.MemberProperty = PropertyChain.GetActiveMemberNode()->GetValue();
						}

						//Go backward to walk up the property hierarchy to see if an owning property is exposed
						Current = Current->GetPrevNode();
					}
				}
			}
		}
	}
}

void URemoteControlPreset::RegisterDelegates()
{
#if WITH_EDITOR
	UnregisterDelegates();
	FCoreUObjectDelegates::OnObjectPropertyChanged.AddUObject(this, &URemoteControlPreset::OnObjectPropertyChanged);
	FCoreUObjectDelegates::OnPreObjectPropertyChanged.AddUObject(this, &URemoteControlPreset::OnPreObjectPropertyChanged);
#endif
}

void URemoteControlPreset::UnregisterDelegates()
{
#if WITH_EDITOR
	FCoreUObjectDelegates::OnPreObjectPropertyChanged.RemoveAll(this);
	FCoreUObjectDelegates::OnObjectPropertyChanged.RemoveAll(this);
#endif
}

#undef LOCTEXT_NAMESPACE /* RemoteControlPreset */ 
