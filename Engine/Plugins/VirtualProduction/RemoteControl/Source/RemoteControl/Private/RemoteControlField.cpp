// Copyright Epic Games, Inc. All Rights Reserved.

#include "RemoteControlField.h"
#include "RemoteControlObjectVersion.h"
#include "RemoteControlFieldPath.h"
#include "RemoteControlBinding.h"
#include "UObject/Class.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"

FRemoteControlField::FRemoteControlField(URemoteControlPreset* InPreset, EExposedFieldType InType, FName InLabel, FRCFieldPathInfo InFieldPathInfo, const TArray<URemoteControlBinding*> InBindings)
	: FRemoteControlEntity(InPreset, InLabel, InBindings)
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

TArray<UObject*> FRemoteControlField::ResolveFieldOwners() const
{
	TArray<UObject*> Objects;
	Objects.Reserve(Bindings.Num());
	for (const TWeakObjectPtr<URemoteControlBinding>& Obj : Bindings)
	{
		if (Obj.IsValid())
		{
			if (UObject* ResolvedObject = Obj->Resolve())
			{
				Objects.Add(ResolvedObject);	
			}
		}
	}
	
	return Objects;
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

FName FRemoteControlProperty::MetadataKey_Min = "Min";
FName FRemoteControlProperty::MetadataKey_Max = "Max";

FRemoteControlProperty::FRemoteControlProperty(FName InLabel, FRCFieldPathInfo FieldPathInfo, TArray<FString> InComponentHierarchy)
	: FRemoteControlField(nullptr, EExposedFieldType::Property, InLabel, MoveTemp(FieldPathInfo), {})
{
}

FRemoteControlProperty::FRemoteControlProperty(URemoteControlPreset* InPreset, FName InLabel, FRCFieldPathInfo InFieldPathInfo, const TArray<URemoteControlBinding*>& InBindings)
	: FRemoteControlField(InPreset, EExposedFieldType::Property, InLabel, MoveTemp(InFieldPathInfo), InBindings)
{
	InitializeMetadata();
}

FProperty* FRemoteControlProperty::GetProperty() const
{
	// Make a copy in order to preserve constness.
	FRCFieldPathInfo FieldPathCopy = FieldPathInfo;
	TArray<UObject*> Objects = ResolveFieldOwners();
	if (Objects.Num() && FieldPathCopy.Resolve(Objects[0]))
	{
		FRCFieldResolvedData Data = FieldPathCopy.GetResolvedData();
		return Data.Field;
	}
	return nullptr;
}

void FRemoteControlProperty::PostSerialize(FArchive& Ar)
{
	if (Ar.IsLoading())
	{
		if (!UserMetadata.Contains(MetadataKey_Min)
			&& !UserMetadata.Contains(MetadataKey_Max))
		{
			InitializeMetadata();
		}
	}
}

void FRemoteControlProperty::InitializeMetadata()
{
#if WITH_EDITOR
	if (FProperty* Property = GetProperty())
	{
		if (Property->IsA<FNumericProperty>())
		{
			const FString& UIMin = Property->GetMetaData(TEXT("UIMin"));
			const FString& UIMax = Property->GetMetaData(TEXT("UIMax"));
			const FString& ClampMin = Property->GetMetaData(TEXT("ClampMin"));
			const FString& ClampMax = Property->GetMetaData(TEXT("ClampMax"));

			const FString& NewMinEntry = !UIMin.IsEmpty() ? UIMin : ClampMin;
			const FString& NewMaxEntry = !UIMax.IsEmpty() ? UIMax : ClampMax;

			// Add the metadata even if empty in case the user wants to specify it themself.
			UserMetadata.FindOrAdd(MetadataKey_Min) = NewMinEntry;
			UserMetadata.FindOrAdd(MetadataKey_Max) = NewMaxEntry;
		}
	}
#endif
}

FRemoteControlFunction::FRemoteControlFunction(FName InLabel, FRCFieldPathInfo FieldPathInfo, UFunction* InFunction)
	: FRemoteControlField(nullptr, EExposedFieldType::Function, InLabel, MoveTemp(FieldPathInfo), {})
	, Function(InFunction)
{
	check(Function);
	FunctionArguments = MakeShared<FStructOnScope>(Function);
	Function->InitializeStruct(FunctionArguments->GetStructMemory());
	AssignDefaultFunctionArguments();
}

FRemoteControlFunction::FRemoteControlFunction(URemoteControlPreset* InPreset, FName InLabel, FRCFieldPathInfo InFieldPathInfo, UFunction* InFunction, const TArray<URemoteControlBinding*>& InBindings)
	: FRemoteControlField(InPreset, EExposedFieldType::Function, InLabel, MoveTemp(InFieldPathInfo), InBindings)
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