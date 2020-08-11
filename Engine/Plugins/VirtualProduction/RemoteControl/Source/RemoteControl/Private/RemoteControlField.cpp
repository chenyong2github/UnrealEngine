// Copyright Epic Games, Inc. All Rights Reserved.

#include "RemoteControlField.h"
#include "Components/ActorComponent.h"
#include "GameFramework/Actor.h"
#include "StructDeserializer.h"
#include "StructSerializer.h"

FRemoteControlField::FRemoteControlField(UObject* FieldOwner, EExposedFieldType InType, FName InFieldName) 
	: FieldType(InType)
	, FieldName(InFieldName)
	, FieldId(FGuid::NewGuid())
{
	check(FieldOwner);
	
	// If the field is on an actor component, we need to keep the relative path of the component to its owner
	// in order to allow field grouping.
	if (FieldOwner->IsA<UActorComponent>())
	{
		PathRelativeToOwner = FieldOwner->GetPathName(Cast<UActorComponent>(FieldOwner)->GetOwner());
	}

	FieldOwnerClass = FieldOwner->GetClass();
}

TArray<UObject*> FRemoteControlField::ResolveFieldOwners(const TArray<UObject*>& SectionObjects)
{
	TArray<UObject*> FieldOwners;
	FieldOwners.Reserve(SectionObjects.Num());

	for (UObject* Object : SectionObjects)
	{
		if (!PathRelativeToOwner.IsEmpty())
		{
			if (UObject* ResolvedFieldOwner = FindObject<UObject>(Object, *PathRelativeToOwner))
			{
				FieldOwners.Add(ResolvedFieldOwner);
			}
			else
			{
				// @todo: Change to a log if this situation can occur under normal conditions. (ie. Blueprint reinstanced)
				ensureAlwaysMsgf(false, TEXT("Could not resolve field owner for field %s"), *Object->GetName());
			}
		}
		else
		{
			FieldOwners.Add(Object);
		}
	}

	return FieldOwners;
}

bool FRemoteControlField::operator==(const FRemoteControlField& InField) const
{
	// Functions can be exposed multiple times so we cannot rely on field name and type to differentiate
	if (InField.FieldType == EExposedFieldType::Function)
	{
		return InField.FieldId == FieldId;
	}
	else
	{
		return InField.FieldType == FieldType && InField.FieldName == FieldName;
	}
}

bool FRemoteControlField::operator==(const FGuid& InFieldId) const
{
	return FieldId == InFieldId;
}

uint32 GetTypeHash(const FRemoteControlField& InField)
{
	return GetTypeHash(InField.FieldId);
}

FRemoteControlProperty::FRemoteControlProperty(UObject* FieldOwner, FName InPropertyName)
	: FRemoteControlField(FieldOwner, EExposedFieldType::Property, InPropertyName)
{}

FRemoteControlFunction::FRemoteControlFunction(UObject* FieldOwner, UFunction* InFunction)
	: FRemoteControlField(FieldOwner, EExposedFieldType::Function, InFunction->GetFName())
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

	RCFunction.Function->SerializeTaggedProperties(Ar, RCFunction.FunctionArguments->GetStructMemory(), RCFunction.Function, nullptr);

	return Ar;
}
