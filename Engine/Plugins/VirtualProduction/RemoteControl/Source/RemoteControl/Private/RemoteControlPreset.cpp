// Copyright Epic Games, Inc. All Rights Reserved.

#include "RemoteControlPreset.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"
#include "Algo/Transform.h"
#include "Misc/Optional.h"
#include "Components/ActorComponent.h"
#include "GameFramework/Actor.h"
#include "IRemoteControlModule.h"

#define LOCTEXT_NAMESPACE "RemoteControlPreset"

bool FRemoteControlSection::HasSameTopLevelObjects(const TArray<UObject*>& ObjectsToTest)
{
	// Section must have the same objects
	const TArray<UObject*>& SectionObjects = ResolveSectionObjects();
	for (UObject* Object : ObjectsToTest)
	{
		// Components are not supported as top level objects
		if (Object && Object->IsA<UActorComponent>())
		{
			Object = Cast<UActorComponent>(Object)->GetOwner();
		}

		if (!SectionObjects.Contains(Object))
		{
			return false;
		}
	}

	return true;
}

void FRemoteControlSection::Expose(FRemoteControlFunction Function)
{
	ExposedFunctions.Add(MoveTemp(Function));
}

void FRemoteControlSection::Expose(FRemoteControlProperty Property)
{
	ExposedProperties.Add(MoveTemp(Property));
}

void FRemoteControlSection::Unexpose(const FGuid& TargetFieldId)
{
	RemoveField<FRemoteControlProperty>(ExposedProperties, TargetFieldId);
	RemoveField<FRemoteControlFunction>(ExposedFunctions, TargetFieldId);
}

FGuid FRemoteControlSection::FindPropertyId(FName PropertyName)
{
	FGuid Guid;
	for (const FRemoteControlProperty& Property : ExposedProperties)
	{
		if (Property.FieldName == PropertyName)
		{
			Guid = Property.FieldId;
		}
	}

	return Guid;
}

TOptional<FRemoteControlProperty> FRemoteControlSection::GetProperty(const FGuid& FieldId)
{
	TOptional<FRemoteControlProperty> Property;
	if (FRemoteControlProperty* RCProperty = ExposedProperties.FindByHash(GetTypeHash(FieldId), FieldId))
	{
		Property = *RCProperty;
	}
	return Property;
}

TArray<UObject*> FRemoteControlSection::ResolveSectionObjects() const
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

	ensure(ResolvedObjects.Num() == Bindings.Num());
	return ResolvedObjects;
}

void FRemoteControlSection::AddBindings(const TArray<UObject*>& ObjectsToBind)
{
	for (UObject* SectionObject : ObjectsToBind)
	{
		Bindings.Add(FSoftObjectPath{ SectionObject });
	}
}

bool URemoteControlPreset::CanCreateSection(const TArray<UObject*>& SectionObjects)
{
	TArray<UClass*> Classes;
	Classes.Reserve(SectionObjects.Num());
	Algo::Transform(SectionObjects, Classes, [](const UObject* Object) { return Object->GetClass(); });
	return !!UClass::FindCommonBase(Classes);
}

FRemoteControlSection& URemoteControlPreset::CreateSection(const TArray<UObject*>& SectionObjects)
{
	check(SectionObjects.Num() > 0);

	FString Alias = GenerateAliasForObjects(SectionObjects);
	FRemoteControlSection& Section = RemoteControlSections.Add(Alias);
	Section.AddBindings(SectionObjects);
	Section.Alias = Alias;

	TArray<UClass*> Classes;
	Classes.Reserve(SectionObjects.Num());
	Algo::Transform(SectionObjects, Classes, [](const UObject* Object) { return Object->GetClass(); });
	UClass* CommonClass = UClass::FindCommonBase(Classes);

	Section.SectionClass = CommonClass;

	return Section;
}

void URemoteControlPreset::DeleteSection(const FString& SectionName)
{
	RemoteControlSections.Remove(SectionName);
}

void URemoteControlPreset::RenameSection(const FString& SectionName, const FString& NewSectionName)
{
	FRemoteControlSection Section;
	RemoteControlSections.RemoveAndCopyValue(SectionName, Section);
	Section.Alias = NewSectionName;
	RemoteControlSections.Add(NewSectionName, MoveTemp(Section));
}

FString URemoteControlPreset::GenerateAliasForObjects(const TArray<UObject*>& Objects)
{
	FString Alias;
	if (Objects.Num() == 1)
	{
		return MakeUniqueName(LOCTEXT("SectionLabel", "New Section").ToString());
	}
	else if (Objects.Num() != 0)
	{
		return MakeUniqueName(LOCTEXT("GroupedActorsSectionLabel", "Grouped Actors Section").ToString());
	}

	return Alias;
}

FString URemoteControlPreset::MakeUniqueName(const FString& InBase)
{
	uint32 Index = 0;
	FString Candidate;
	do
	{
		Candidate = InBase + TEXT("_") + FString::FromInt(Index++);
	}
	while (RemoteControlSections.Contains(Candidate));

	return Candidate;
}

#undef LOCTEXT_NAMESPACE /* RemoteControlPreset */