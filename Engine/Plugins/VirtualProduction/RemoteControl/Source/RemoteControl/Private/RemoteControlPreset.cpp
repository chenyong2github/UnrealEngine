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

void FRemoteControlPresetGroup::AddField(FGuid FieldId)
{
	Fields.Add(FieldId);
	if (RemoteControlPreset::FRemoteControlCache* Cache = GetPresetCache())
	{
		Cache->FindOrAdd(FieldId).LayoutGroupId = Id;
	}
}

void FRemoteControlPresetGroup::InsertFieldAt(FGuid FieldId, int32 Index)
{
	if (URemoteControlPreset* Preset = GetOwnerPreset())
	{
		Preset->Modify();
	}

	Fields.Insert(FieldId, Index);
	if (RemoteControlPreset::FRemoteControlCache* Cache = GetPresetCache())
	{
		Cache->FindOrAdd(FieldId).LayoutGroupId = Id;
	}
}

void FRemoteControlPresetGroup::RemoveField(FGuid FieldId)
{
	int32 Index = Fields.IndexOfByKey(FieldId);
	RemoveFieldAt(Index);
}

void FRemoteControlPresetGroup::RemoveFieldAt(int32 Index)
{
	if (Fields.IsValidIndex(Index))
	{
		if (URemoteControlPreset* Preset = GetOwnerPreset())
		{
			Preset->Modify();
		}

		FGuid FieldId = Fields[Index];

		bool bLastFieldOccurence = true;
		for (auto It = Fields.CreateIterator(); It; ++It)
		{
			if (*It == FieldId)
			{
				if (It.GetIndex() != Index)
				{
					bLastFieldOccurence = false;
				}
				else
				{
					It.RemoveCurrent();
				}
			}
		}

		if (!bLastFieldOccurence)
		{
			// If the item was removed but is not the last occurrence, this means the field should still appear in the group.
			return;
		}

		if (RemoteControlPreset::FRemoteControlCache* Cache = GetPresetCache())
		{
			if (RemoteControlPreset::FCachedFieldData* CachedData = Cache->Find(FieldId))
			{
				// Make sure we don't clear the layout group of a field that's been moved to another group.
				if (CachedData->LayoutGroupId == Id)
				{
					CachedData->LayoutGroupId.Invalidate();
				}
			}
		}
	}
}

void FRemoteControlPresetGroup::SwapFields(int32 FirstIndex, int32 SecondIndex)
{
	Fields.Swap(FirstIndex, SecondIndex);
}

const TArray<FGuid>& FRemoteControlPresetGroup::GetFields() const
{
	return Fields;
}

RemoteControlPreset::FRemoteControlCache* FRemoteControlPresetGroup::GetPresetCache() const
{
	if (OwnerLayout)
	{
		if (URemoteControlPreset* OwnerPreset = OwnerLayout->GetOwner())
		{
			return &OwnerPreset->FieldCache;
		}
	}
	return nullptr;
}

URemoteControlPreset* FRemoteControlPresetGroup::GetOwnerPreset() const
{
	if (OwnerLayout)
	{
		return OwnerLayout->GetOwner();
	}

	return nullptr;
}

FRemoteControlPresetLayout::FRemoteControlPresetLayout(URemoteControlPreset* OwnerPreset)
	: Owner(OwnerPreset)
{
	FRemoteControlPresetGroup Group(NAME_DefaultLayoutGroup, this);
	Group.Id = DefaultGroupId;
	Groups.Add(MoveTemp(Group));
}

FRemoteControlPresetGroup& FRemoteControlPresetLayout::GetDefaultGroup()
{
	if (FRemoteControlPresetGroup* Group = GetGroup(DefaultGroupId))
	{
		return *Group;
	}
	else
	{
		FRemoteControlPresetGroup* NewGroup = CreateGroup(NAME_DefaultLayoutGroup);
		NewGroup->Id = DefaultGroupId;
		return *NewGroup;
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

FRemoteControlPresetGroup* FRemoteControlPresetLayout::CreateGroup(FName GroupName)
{
	checkSlow(Owner.IsValid());
	Owner->Modify();

	if (GroupName == NAME_None)
	{
		GroupName = MakeUniqueName(NAME_DefaultNewGroup, [this](FName Candidate) { return !!GetGroupByName(Candidate); });
	}

	FRemoteControlPresetGroup& Group = Groups.Emplace_GetRef(GroupName, this);
	Owner->CacheLayoutData();
	return &Group;
}

FRemoteControlPresetGroup* FRemoteControlPresetLayout::FindGroupFromField(FGuid FieldId)
{
	if (RemoteControlPreset::FCachedFieldData* CachedData = Owner->FieldCache.Find(FieldId))
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

	int32 DragOriginFieldIndex = DragOriginGroup->Fields.IndexOfByKey(FieldSwapArgs.DraggedFieldId);
	int32 DragTargetFieldIndex = DragTargetGroup->Fields.IndexOfByKey(FieldSwapArgs.TargetFieldId);

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

		DragTargetGroup->InsertFieldAt(FieldSwapArgs.DraggedFieldId, DragTargetFieldIndex);
		DragTargetGroup->SwapFields(DragTargetFieldIndex, DragOriginFieldIndex);
		DragTargetGroup->RemoveFieldAt(DragOriginFieldIndex);
	}
	else
	{
		if (DragTargetFieldIndex == INDEX_NONE)
		{
			DragTargetGroup->AddField(FieldSwapArgs.DraggedFieldId);
		}
		else
		{
			DragTargetGroup->InsertFieldAt(FieldSwapArgs.DraggedFieldId, DragTargetFieldIndex);
		}
		DragOriginGroup->RemoveFieldAt(DragOriginFieldIndex);
	}
}

void FRemoteControlPresetLayout::DeleteGroup(FGuid GroupId)
{
	checkSlow(Owner.IsValid());
	Owner->Modify();

	int32 Index = Groups.IndexOfByPredicate([GroupId](const FRemoteControlPresetGroup& Group) { return Group.Id == GroupId; });
	if (Index != INDEX_NONE)
	{
		Groups.RemoveAt(Index);
	}

	Owner->CacheLayoutData();
}

void FRemoteControlPresetLayout::RenameGroup(FGuid GroupId, FName NewGroupName)
{
	checkSlow(Owner.IsValid());
	Owner->Modify();

	if (FRemoteControlPresetGroup* Group = GetGroup(GroupId))
	{
		Group->Name = NewGroupName;
	}
	Owner->CacheLayoutData();
}

void FRemoteControlPresetLayout::PostSerialize(const FArchive& Ar)
{
	for (FRemoteControlPresetGroup& Group : Groups)
	{
		Group.OwnerLayout = this;
	}
}

const TArray<FRemoteControlPresetGroup>& FRemoteControlPresetLayout::GetGroups() const
{
	return Groups;
}

URemoteControlPreset* FRemoteControlPresetLayout::GetOwner()
{
	return Owner.Get();
}

TOptional<FRemoteControlProperty> FRemoteControlTarget::ExposeProperty(FFieldPathInfo FieldPathInfo, const FString& DesiredDisplayName, FGuid GroupId)
{
	Owner->Modify();

	FName FieldLabel = Owner->GenerateUniqueFieldLabel(Alias, DesiredDisplayName);
	FRemoteControlProperty RCField(FieldLabel, MoveTemp(FieldPathInfo));
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

	Owner->Modify();

	FFieldPathInfo Path{ MoveTemp(RelativeFieldPath) };
	if (UFunction* Function = Class->FindFunctionByName(FName(Path.GetFieldName())))
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
	Owner->Modify();

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
			TArray<FString> Path;
			RCProperty->PathRelativeToOwner.ParseIntoArray(Path, TEXT("."));
			Path.Add(RCProperty->FieldName.ToString());
			FProperty* Property = FindPropertyRecursive(Cast<UStruct>(FieldOwners[0]->GetClass()), Path);
			if (Property)
			{
				OptionalExposedProperty = FExposedProperty{ Property, FieldOwners };
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
			ResolvedObjects.Add(Obj);
		}
		else
		{
			UE_LOG(LogRemoteControl, Error, TEXT("Object %s could not be loaded."), *Path.ToString());
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
		IRemoteControlModule::Get().RegisterPreset(GetFName(), this);
	}
}

void URemoteControlPreset::PostLoad()
{
	Super::PostLoad();
	IRemoteControlModule::Get().RegisterPreset(GetFName(), this);

#if WITH_EDITOR
	FCoreUObjectDelegates::OnObjectPropertyChanged.AddUObject(this, &URemoteControlPreset::OnObjectPropertyChanged);
	FCoreUObjectDelegates::OnPreObjectPropertyChanged.AddUObject(this, &URemoteControlPreset::OnPreObjectPropertyChanged);
#endif //WITH_EDITOR

	FieldCache.Reset();
	CacheFieldsData();
	CacheFieldLayoutData();
}

void URemoteControlPreset::BeginDestroy()
{
	Super::BeginDestroy();

#if WITH_EDITOR
	FCoreUObjectDelegates::OnPreObjectPropertyChanged.RemoveAll(this);
	FCoreUObjectDelegates::OnObjectPropertyChanged.RemoveAll(this);
#endif //WITH_EDITOR

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
	if (const RemoteControlPreset::FCachedFieldData* CachedData = FieldCache.Find(FieldId))
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
	if (const RemoteControlPreset::FCachedFieldData* CachedData = FieldCache.Find(FunctionId))
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
	if (const RemoteControlPreset::FCachedFieldData* CachedData = FieldCache.Find(PropertyId))
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

	if (FRemoteControlField* Field = GetField(GetFieldId(OldFieldLabel)))
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
	if (const RemoteControlPreset::FCachedFieldData* Data = FieldCache.Find(GetFieldId(PropertyLabel)))
	{
		OptionalExposedProperty = RemoteControlTargets.FindChecked(Data->OwnerObjectAlias).ResolveExposedProperty(GetFieldId(PropertyLabel));
	}

	return OptionalExposedProperty;
}

TOptional<FExposedFunction> URemoteControlPreset::ResolveExposedFunction(FName FunctionLabel) const
{
	TOptional<FExposedFunction> OptionalExposedFunction;
	if (const RemoteControlPreset::FCachedFieldData* Data = FieldCache.Find(GetFieldId(FunctionLabel)))
	{
		OptionalExposedFunction = RemoteControlTargets.FindChecked(Data->OwnerObjectAlias).ResolveExposedFunction(GetFieldId(FunctionLabel));
	}

	return OptionalExposedFunction;
}

void URemoteControlPreset::Unexpose(FName FieldLabel)
{
	FGuid FieldId = GetFieldId(FieldLabel);
	if (RemoteControlPreset::FCachedFieldData* Data = FieldCache.Find(FieldId))
	{
		RemoteControlTargets.FindChecked(Data->OwnerObjectAlias).Unexpose(FieldId);
		FieldCache.Remove(FieldId);
		NameToGuidMap.Remove(FieldLabel);

		OnPropertyUnexposed().Broadcast(this, FieldLabel);
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
	if (const RemoteControlPreset::FCachedFieldData* Data = FieldCache.Find(GetFieldId(FieldLabel)))
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

FRemoteControlField* URemoteControlPreset::GetField(FGuid FieldId)
{
	if (const RemoteControlPreset::FCachedFieldData* CachedData = FieldCache.Find(FieldId))
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
	RemoteControlPreset::FCachedFieldData CachedData;

	FGuid FieldGroupId = !Info.LayoutGroupId.IsValid() ? DefaultGroupId : Info.LayoutGroupId;
	FRemoteControlPresetGroup* Group = Layout.GetGroup(FieldGroupId);
	if (!Group)
	{
		Group = &Layout.GetDefaultGroup();
	}

	Group->InsertFieldAt(Info.FieldId, Group->GetFields().Num());

	CachedData.LayoutGroupId = Group->Id;
	CachedData.OwnerObjectAlias = Info.Alias;
	FieldCache.FindOrAdd(Info.FieldId) = CachedData;

	if (FRemoteControlField* Field = GetField(Info.FieldId))
	{
		NameToGuidMap.Add(Field->Label, Info.FieldId);
		OnPropertyExposed().Broadcast(this, Field->Label);
	}
}

void URemoteControlPreset::OnUnexpose(FGuid UnexposedFieldId)
{
	RemoteControlPreset::FCachedFieldData CachedData = FieldCache.FindAndRemoveChecked(UnexposedFieldId);
	if (FRemoteControlPresetGroup* FieldGroup = Layout.GetGroup(CachedData.LayoutGroupId))
	{
		FieldGroup->RemoveField(UnexposedFieldId);
	}

	for (auto It = NameToGuidMap.CreateIterator(); It; ++It)
	{
		if (It.Value() == UnexposedFieldId)
		{
			It.RemoveCurrent();
		}
	}
}

void URemoteControlPreset::CacheFieldsData()
{
	for (TTuple<FName, FRemoteControlTarget>& Target : RemoteControlTargets)
	{
		FieldCache.Reserve(FieldCache.Num() + Target.Value.ExposedProperties.Num() + Target.Value.ExposedFunctions.Num());
		auto CacheField = [this, TargetName = Target.Key](const FRemoteControlField& Field)
		{
			RemoteControlPreset::FCachedFieldData& CachedData = FieldCache.FindOrAdd(Field.Id);
			CachedData.OwnerObjectAlias = TargetName;
			NameToGuidMap.Add(Field.Label, Field.Id);
		};

		Algo::ForEach(Target.Value.ExposedProperties, CacheField);
		Algo::ForEach(Target.Value.ExposedFunctions, CacheField);
	}
}

void URemoteControlPreset::CacheFieldLayoutData()
{
	for (const FRemoteControlPresetGroup& Group : Layout.GetGroups())
	{
		for (FGuid FieldId : Group.GetFields())
		{
			RemoteControlPreset::FCachedFieldData& CachedData = FieldCache.FindOrAdd(FieldId);
			CachedData.LayoutGroupId = Group.Id;
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
			&& CacheEntry.Property == Event.Property
			&& CacheEntry.MemberProperty == Event.MemberProperty)
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

#undef LOCTEXT_NAMESPACE /* RemoteControlPreset */ 
