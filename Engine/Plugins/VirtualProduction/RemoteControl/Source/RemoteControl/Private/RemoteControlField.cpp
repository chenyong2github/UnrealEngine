// Copyright Epic Games, Inc. All Rights Reserved.

#include "RemoteControlField.h"

#include "Components/ActorComponent.h"
#include "GameFramework/Actor.h"
#include "RemoteControlObjectVersion.h"
#include "RemoteControlFieldPath.h"
#include "RemoteControlBinding.h"
#include "RemoteControlPreset.h"
#include "RemoteControlPropertyHandle.h"
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

void FRemoteControlField::BindObject(UObject* InObjectToBind)
{
	if (!InObjectToBind)
	{
		return;
	}
	
	if (UClass* ResolvedOwnerClass = GetSupportedBindingClass())
	{
		if (InObjectToBind->GetClass()->IsChildOf(ResolvedOwnerClass))
		{
			FRemoteControlEntity::BindObject(InObjectToBind);
		}
		else if (AActor* Actor = Cast<AActor>(InObjectToBind))
		{
			// Attempt finding a matching component if the object is an actor.
			FRemoteControlEntity::BindObject(Actor->GetComponentByClass(ResolvedOwnerClass));
		}
	}
}

bool FRemoteControlField::CanBindObject(const UObject* InObjectToBind) const
{
	if (InObjectToBind)
	{
		if (UClass* ResolvedOwnerClass = GetSupportedBindingClass())
		{
			if (InObjectToBind->GetClass()->IsChildOf(ResolvedOwnerClass))
			{
				return true;
			}
			
			if (ResolvedOwnerClass->IsChildOf<UActorComponent>())
			{
				if (const AActor* Actor = Cast<AActor>(InObjectToBind))
				{
					return !!Actor->GetComponentByClass(ResolvedOwnerClass);
				}
				return false;
			}
		}
	}
	return false;
}

void FRemoteControlField::PostSerialize(const FArchive& Ar)
{
	if (Ar.IsLoading())
	{
		int32 CustomVersion = Ar.CustomVer(FRemoteControlObjectVersion::GUID);
		if (CustomVersion < FRemoteControlObjectVersion::AddedRebindingFunctionality)
		{
			if (OwnerClass.IsNull())
			{
				OwnerClass = GetSupportedBindingClass();
			}
		}
	}
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
	OwnerClass = GetSupportedBindingClass();

	if (FProperty* Property = GetProperty())
	{
		bIsEditorOnly = Property->HasAnyPropertyFlags(CPF_EditorOnly);
		bIsEditableInPackaged = !Property->HasAnyPropertyFlags(CPF_BlueprintReadOnly);
	}
}

uint32 FRemoteControlProperty::GetUnderlyingEntityIdentifier() const
{
	return FieldPathInfo.PathHash;
}

FProperty* FRemoteControlProperty::GetProperty() const
{
	// Make a copy in order to preserve constness.
	FRCFieldPathInfo FieldPathCopy = FieldPathInfo;
	TArray<UObject*> Objects = GetBoundObjects();
	if (Objects.Num() && FieldPathCopy.Resolve(Objects[0]))
	{
		FRCFieldResolvedData Data = FieldPathCopy.GetResolvedData();
		return Data.Field;
	}
	return nullptr;
}

TSharedPtr<IRemoteControlPropertyHandle> FRemoteControlProperty::GetPropertyHandle() const 
{
	TSharedPtr<FRemoteControlProperty> ThisPtr = Owner->GetExposedEntity<FRemoteControlProperty>(GetId()).Pin();
	FProperty* Property = GetProperty();
	const int32 ArrayIndex = Property->ArrayDim != 1 ? -1 : 0;
	constexpr FProperty* ParentProperty = nullptr;
	const TCHAR* ParentFieldPath = TEXT("");
	return FRemoteControlPropertyHandle::GetPropertyHandle(ThisPtr, Property, ParentProperty, ParentFieldPath, ArrayIndex);
}

bool FRemoteControlProperty::Serialize(FArchive& Ar)
{
	Ar.UsingCustomVersion(FRemoteControlObjectVersion::GUID);
	FRemoteControlProperty::StaticStruct()->SerializeTaggedProperties(Ar, (uint8*)this, FRemoteControlProperty::StaticStruct(), nullptr);

	return true;
}

void FRemoteControlProperty::PostSerialize(const FArchive& Ar)
{
	FRemoteControlField::PostSerialize(Ar);

	if (Ar.IsLoading())
	{
		if (FProperty* Property = GetProperty())
		{
			int32 CustomVersion = Ar.CustomVer(FRemoteControlObjectVersion::GUID);
			if (CustomVersion < FRemoteControlObjectVersion::AddedFieldFlags)
			{
				bIsEditorOnly = Property->HasAnyPropertyFlags(CPF_EditorOnly);
				bIsEditableInPackaged = !Property->HasAnyPropertyFlags(CPF_BlueprintReadOnly);
			}
		}
		
		if (UserMetadata.Num() == 0)
		{
			InitializeMetadata();
		}
	}
}

UClass* FRemoteControlProperty::GetSupportedBindingClass() const
{
	if (UClass* Class = OwnerClass.TryLoadClass<UObject>())
	{
		return Class;	
	}
	
	if (FProperty* Property = GetProperty())
	{
		return Property->GetOwnerClass();
	}
	return nullptr;
}

bool FRemoteControlProperty::IsBound() const
{
	return !!GetProperty();
}

void FRemoteControlProperty::InitializeMetadata()
{
#if WITH_EDITOR
	auto SupportsMinMax = [] (FProperty* Property)
	{
		if (!Property)
		{
			return false;
		}

		if (Property->IsA<FNumericProperty>())
		{
			return !(Property->IsA<FByteProperty>() && CastFieldChecked<FByteProperty>(Property)->IsEnum());
		}

		if (Property->IsA<FStructProperty>())
		{
			UStruct* Struct = CastFieldChecked<FStructProperty>(Property)->Struct;
			return Struct->IsChildOf(TBaseStructure<FVector>::Get())
				|| Struct->IsChildOf(TBaseStructure<FRotator>::Get());
		}

		return false;
	};
	
	if (FProperty* Property = GetProperty())
	{
		if (SupportsMinMax(Property))
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
	, FunctionPath(InFunction)
{
	checkSlow(InFunction);
	FunctionArguments = MakeShared<FStructOnScope>(InFunction);
	InFunction->InitializeStruct(FunctionArguments->GetStructMemory());
	AssignDefaultFunctionArguments();
	OwnerClass = GetSupportedBindingClass();
}

FRemoteControlFunction::FRemoteControlFunction(URemoteControlPreset* InPreset, FName InLabel, FRCFieldPathInfo InFieldPathInfo, UFunction* InFunction, const TArray<URemoteControlBinding*>& InBindings)
	: FRemoteControlField(InPreset, EExposedFieldType::Function, InLabel, MoveTemp(InFieldPathInfo), InBindings)
	, FunctionPath(InFunction)
{
	check(InFunction);
	FunctionArguments = MakeShared<FStructOnScope>(InFunction);
	InFunction->InitializeStruct(FunctionArguments->GetStructMemory());
	AssignDefaultFunctionArguments();
	OwnerClass = GetSupportedBindingClass();

	bIsEditorOnly = InFunction->HasAnyFunctionFlags(FUNC_EditorOnly);
	bIsCallableInPackaged = InFunction->HasAnyFunctionFlags(FUNC_BlueprintCallable);
}

uint32 FRemoteControlFunction::GetUnderlyingEntityIdentifier() const
{
	if (UFunction* Function = GetFunction())
	{
		return GetTypeHash(Function->GetName());
	}
	
	return 0;
}

bool FRemoteControlFunction::Serialize(FArchive& Ar)
{
	if (Ar.IsLoading() || Ar.IsSaving())
	{
		Ar << *this;
	}
	return true;
}

void FRemoteControlFunction::PostSerialize(const FArchive& Ar)
{
	FRemoteControlField::PostSerialize(Ar);

	if (Ar.IsLoading())
	{
		int32 CustomVersion = Ar.CustomVer(FRemoteControlObjectVersion::GUID);

		if (CustomVersion < FRemoteControlObjectVersion::AddedFieldFlags)
		{
			if (UFunction* Function = GetFunction())
			{
				bIsEditorOnly = Function->HasAnyFunctionFlags(FUNC_EditorOnly);
				bIsCallableInPackaged = Function->HasAnyFunctionFlags(FUNC_BlueprintCallable);
			}
		}
	}
}

UClass* FRemoteControlFunction::GetSupportedBindingClass() const
{
	if (UClass* Class = OwnerClass.TryLoadClass<UObject>())
	{
		return Class;	
	}

	if (UFunction* Function = GetFunction())
	{
		return Function->GetOwnerClass();
	}
	return nullptr;
}

bool FRemoteControlFunction::IsBound() const
{
	if (UFunction* Function = GetFunction())
	{
		TArray<UObject*> BoundObjects = GetBoundObjects();
		if (!BoundObjects.Num())
		{
			return false;
		}
		
		if (UClass* SupportedClass = GetSupportedBindingClass())
		{
			return BoundObjects.ContainsByPredicate([SupportedClass](UObject* Object){ return Object->GetClass() && Object->GetClass()->IsChildOf(SupportedClass);});
		}
	}
	
	return false;
}

UFunction* FRemoteControlFunction::GetFunction() const
{
	UObject* ResolvedObject = FunctionPath.ResolveObject();
	
	if (!ResolvedObject)
	{
		ResolvedObject = FunctionPath.TryLoad();
	}

	UFunction* ResolvedFunction = Cast<UFunction>(ResolvedObject);
	if (ResolvedFunction)
	{
		CachedFunction = ResolvedFunction;
	}
	
	return ResolvedFunction;
}

void FRemoteControlFunction::AssignDefaultFunctionArguments()
{
#if WITH_EDITOR
	if (UFunction* Function = GetFunction())
	{
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
	}
#endif
}

FArchive& operator<<(FArchive& Ar, FRemoteControlFunction& RCFunction)
{
	Ar.UsingCustomVersion(FRemoteControlObjectVersion::GUID);

	FRemoteControlFunction::StaticStruct()->SerializeTaggedProperties(Ar, (uint8*)&RCFunction, FRemoteControlFunction::StaticStruct(), nullptr);

	if (Ar.IsLoading())
	{
#if WITH_EDITOR
		if (RCFunction.Function_DEPRECATED && RCFunction.FunctionPath.IsValid())
		{
			RCFunction.FunctionPath = RCFunction.Function_DEPRECATED;
		}
#endif
		
		if (UFunction* Function = RCFunction.GetFunction())
		{
			RCFunction.FunctionArguments = MakeShared<FStructOnScope>(Function);
			Function->SerializeTaggedProperties(Ar, RCFunction.FunctionArguments->GetStructMemory(), Function, nullptr);
		}
	}
	else if (RCFunction.CachedFunction.IsValid())
	{
		RCFunction.CachedFunction->SerializeTaggedProperties(Ar, RCFunction.FunctionArguments->GetStructMemory(), RCFunction.CachedFunction.Get(), nullptr);	
	}
		
	return Ar;
}