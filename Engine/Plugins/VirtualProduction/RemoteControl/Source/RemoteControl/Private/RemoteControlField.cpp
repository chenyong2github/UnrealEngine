// Copyright Epic Games, Inc. All Rights Reserved.

#include "RemoteControlField.h"
#include "RemoteControlObjectVersion.h"
#include "UObject/Class.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"

FRemoteControlField::FRemoteControlField(URemoteControlPreset* InPreset, EExposedFieldType InType, FName InLabel, FRCFieldPathInfo InFieldPathInfo)
	: FRemoteControlEntity(InPreset, InLabel)
	, FieldType(InType)
	, FieldName(InFieldPathInfo.GetFieldName())
	, FieldPathInfo(MoveTemp(InFieldPathInfo))
{
}

TArray<UObject*> FRemoteControlField::ResolveFieldOwners(const TArray<UObject*>& SectionObjects) const
{
	TArray<UObject*> ResolvedObjects;
#if WITH_EDITORONLY_DATA
	if (ComponentHierarchy_DEPRECATED.Num())
	{
		ResolvedObjects = ResolveFieldOwnersUsingComponentHierarchy(SectionObjects);
	}
	else
#endif
	{
		ResolvedObjects = SectionObjects;
	}

	return ResolvedObjects;
}

#if WITH_EDITORONLY_DATA
TArray<UObject*> FRemoteControlField::ResolveFieldOwnersUsingComponentHierarchy(const TArray<UObject*>& SectionObjects) const
{
	TArray<UObject*> FieldOwners;
	FieldOwners.Reserve(SectionObjects.Num());

	for (UObject* Object : SectionObjects)
	{
		//If component hierarchy is not empty, we need to walk it to find the child object
		if (ComponentHierarchy_DEPRECATED.Num() > 0)
		{
			UObject* Outer = Object;

			for (const FString& Component : ComponentHierarchy_DEPRECATED)
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
#endif

FRemoteControlProperty::FRemoteControlProperty(FName InLabel, FRCFieldPathInfo FieldPathInfo, TArray<FString> InComponentHierarchy)
	: FRemoteControlField(nullptr, EExposedFieldType::Property, InLabel, MoveTemp(FieldPathInfo))
{
}

FRemoteControlProperty::FRemoteControlProperty(URemoteControlPreset* InPreset, FName InLabel, FRCFieldPathInfo FieldPathInfo)
	: FRemoteControlField(InPreset, EExposedFieldType::Property, InLabel, MoveTemp(FieldPathInfo))
{
}

FRemoteControlFunction::FRemoteControlFunction(FName InLabel, FRCFieldPathInfo FieldPathInfo, UFunction* InFunction)
	: FRemoteControlField(nullptr, EExposedFieldType::Function, InLabel, MoveTemp(FieldPathInfo))
	, Function(InFunction)
{
	check(Function);
	FunctionArguments = MakeShared<FStructOnScope>(Function);
	Function->InitializeStruct(FunctionArguments->GetStructMemory());
	AssignDefaultFunctionArguments();
}

FRemoteControlFunction::FRemoteControlFunction(URemoteControlPreset* InPreset, FName InLabel, FRCFieldPathInfo FieldPathInfo, UFunction* InFunction)
	: FRemoteControlField(InPreset, EExposedFieldType::Function, InLabel, MoveTemp(FieldPathInfo))
	, Function(InFunction)
{
	check(Function);
	FunctionArguments = MakeShared<FStructOnScope>(Function);
	Function->InitializeStruct(FunctionArguments->GetStructMemory());
	AssignDefaultFunctionArguments();
}

bool FRemoteControlFunction::Serialize(FArchive& Ar)
{
	if (Ar.IsLoading() || Ar.IsSaving())
	{
		Ar << *this;
	}
	return true;
}

void FRemoteControlFunction::AssignDefaultFunctionArguments()
{
#if WITH_EDITOR
	for (TFieldIterator<FProperty> It(Function); It; ++It)
	{
		if (!It->HasAnyPropertyFlags(CPF_ReturnParm))
		{
			const FName DefaultPropertyKey = *FString::Printf(TEXT("CPP_Default_%s"), *It->GetName());
			const FString& PropertyDefaultValue = Function->GetMetaData(DefaultPropertyKey);
			if (!PropertyDefaultValue.IsEmpty())
			{
				It->ImportText(*PropertyDefaultValue, It->ContainerPtrToValuePtr<uint8>(FunctionArguments->GetStructMemory()), PPF_None, NULL);
			}
		}
	}
#endif
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