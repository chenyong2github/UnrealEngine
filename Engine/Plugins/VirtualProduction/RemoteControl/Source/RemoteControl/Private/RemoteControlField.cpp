// Copyright Epic Games, Inc. All Rights Reserved.

#include "RemoteControlField.h"
#include "Components/ActorComponent.h"
#include "GameFramework/Actor.h"
#include "StructDeserializer.h"
#include "StructSerializer.h"

FRemoteControlField::FRemoteControlField(EExposedFieldType InType, FName InLabel, FFieldPathInfo&& FieldPathInfo)
	: FieldType(InType)
	, FieldName(FieldPathInfo.GetFieldName())
	, Label(InLabel)
	, Id(FGuid::NewGuid())
	, PathRelativeToOwner(FieldPathInfo.GetPathRelativeToOwner())
	, ComponentHierarchy(FieldPathInfo.GetComponentHierarchy())
{
}

TArray<UObject*> FRemoteControlField::ResolveFieldOwners(const TArray<UObject*>& SectionObjects) const
{
	TArray<UObject*> FieldOwners;
	FieldOwners.Reserve(SectionObjects.Num());

	for (UObject* Object : SectionObjects)
	{
		//If component hierarchy is not empty, we need to walk it to find the child object
		if (!ComponentHierarchy.IsEmpty())
		{
			UObject* Outer = Object;
			TArray<FString> Components;
			ComponentHierarchy.ParseIntoArray(Components, TEXT("."));
			for (const FString& Component : Components)
			{
				if (UObject* ResolvedFieldOwner = FindObject<UObject>(Outer, *Component))
				{
					Outer = ResolvedFieldOwner;
				}
				else
				{
					// This can happen when one of the grouped actors has a component named DefaultSceneRoot and one has a component StaticMeshComponent.
					// @todo: Change to a log if this situation can occur under normal conditions. (ie. Blueprint reinstanced)
					ensureAlwaysMsgf(false, TEXT("Could not resolve field owner for field %s"), *Object->GetName());
					Outer = nullptr;
					break;
				}
			}
			
			if (Outer)
			{
				FieldOwners.Add(Outer);
			}
		}
		else
		{
			FieldOwners.Add(Object);
		}
	}

	return FieldOwners;
}

FString FRemoteControlField::GetQualifiedFieldName() const
{
	FString QualifiedFieldName = FieldName.ToString();
	if (!PathRelativeToOwner.IsEmpty())
	{
		QualifiedFieldName = FString::Printf(TEXT("%s.%s"), *PathRelativeToOwner, *QualifiedFieldName);
	}
	return QualifiedFieldName;
}

bool FRemoteControlField::operator==(const FRemoteControlField& InField) const
{
	return InField.Id == Id;
}

bool FRemoteControlField::operator==(FGuid InFieldId) const
{
	return InFieldId == Id;
}

uint32 GetTypeHash(const FRemoteControlField& InField)
{
	return GetTypeHash(InField.Id);
}

FRemoteControlProperty::FRemoteControlProperty(FName InLabel, FFieldPathInfo FieldPathInfo)
	: FRemoteControlField(EExposedFieldType::Property, InLabel, MoveTemp(FieldPathInfo))
{}

FRemoteControlFunction::FRemoteControlFunction(FName InLabel, FFieldPathInfo FieldPathInfo, UFunction* InFunction)
	: FRemoteControlField(EExposedFieldType::Function, InLabel, MoveTemp(FieldPathInfo))
	, Function(InFunction)
{
	FunctionArguments = MakeShared<FStructOnScope>(Function);
	Function->InitializeStruct(FunctionArguments->GetStructMemory());
}

bool FRemoteControlFunction::Serialize(FArchive& Ar)
{
	if (Ar.IsLoading() || Ar.IsSaving())
	{
		Ar << *this;
	}
	return true;
}

FArchive& operator<<(FArchive& Ar, FRemoteControlFunction& RCFunction)
{
	FRemoteControlFunction::StaticStruct()->SerializeTaggedProperties(Ar, (uint8*)&RCFunction, FRemoteControlFunction::StaticStruct(), nullptr);

	if (Ar.IsLoading())
	{
		RCFunction.FunctionArguments = MakeShared<FStructOnScope>(RCFunction.Function);
	}

	if (ensure(RCFunction.Function))
	{
		RCFunction.Function->SerializeTaggedProperties(Ar, RCFunction.FunctionArguments->GetStructMemory(), RCFunction.Function, nullptr);
	}

	return Ar;
}
