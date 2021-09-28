// Copyright Epic Games, Inc. All Rights Reserved.

#include "StateTreeVariable.h"
#include "StateTreeVariableDesc.h"
#include "StateTreeVariableLayout.h"
#include "StateTreeConstantStorage.h"

#define LOCTEXT_NAMESPACE "StateTreeEditor"

#if WITH_EDITORONLY_DATA
// Make sure the supported variable types are POD and can fit into the storage.
static_assert(sizeof(bool) <= sizeof(FStateTreeVariable::Value) && TIsPODType<bool>::Value, "bool does not fit into the Value storage or is not POD type.");
static_assert(sizeof(int32) <= sizeof(FStateTreeVariable::Value) && TIsPODType<int32>::Value, "int32 does not fit into the Value storage or is not POD type.");
static_assert(sizeof(float) <= sizeof(FStateTreeVariable::Value) && TIsPODType<float>::Value, "float does not fit into the Value storage or is not POD type.");
static_assert(sizeof(FVector) <= sizeof(FStateTreeVariable::Value) && TIsPODType<FVector>::Value, "FVector does not fit into the Value storage or is not POD type.");
#endif

FStateTreeVariable::FStateTreeVariable()
{
#if WITH_EDITORONLY_DATA
	BindingMode = EStateTreeVariableBindingMode::Any;
	BaseClass = nullptr;
	FMemory::Memzero(Value);
#endif
	Handle = FStateTreeHandle::Invalid;
	Type = EStateTreeVariableType::Void;
}

FStateTreeVariable::FStateTreeVariable(const EStateTreeVariableBindingMode InMode, const EStateTreeVariableType InType, TSubclassOf<UObject> InBaseClass)
{
#if WITH_EDITORONLY_DATA
	BindingMode = InMode;
	BaseClass = InBaseClass;
	FMemory::Memzero(Value);
	if (BindingMode == EStateTreeVariableBindingMode::Definition)
	{
		ID = FGuid::NewGuid();
	}
#endif
	Handle = FStateTreeHandle::Invalid;
	Type = InType;
}

FStateTreeVariable::FStateTreeVariable(const EStateTreeVariableBindingMode InMode, const FName& InName, const EStateTreeVariableType InType, TSubclassOf<UObject> InBaseClass)
{
#if WITH_EDITORONLY_DATA
	BindingMode = InMode;
	BaseClass = InBaseClass;
	FMemory::Memzero(Value);
	if (BindingMode == EStateTreeVariableBindingMode::Definition)
	{
		ID = FGuid::NewGuid();
	}
	Name = InName;
#endif
	Handle = FStateTreeHandle::Invalid;
	Type = InType;
}

#if WITH_EDITOR
FStateTreeVariableDesc FStateTreeVariable::AsVariableDesc() const
{
	FStateTreeVariableDesc Desc;
	Desc.Name = Name;
	Desc.ID = ID;
	Desc.BaseClass = BaseClass;
	Desc.Offset = 0;
	Desc.Type = Type;
	return Desc;
}

FText FStateTreeVariable::GetDescription() const
{
	if (IsBound())
	{
		return  FText::FromName(Name);
	}

	switch (Type)
	{
	case EStateTreeVariableType::Void:
		return FText(LOCTEXT("Void", "Void"));
	case EStateTreeVariableType::Bool:
		return GetBoolValue() ? FText(LOCTEXT("True", "True")) : FText(LOCTEXT("False", "False"));
	case EStateTreeVariableType::Int:
		return FText::AsNumber(GetIntValue());
	case EStateTreeVariableType::Float:
		return FText::AsNumber(GetFloatValue());
	case EStateTreeVariableType::Vector:
		return GetVectorValue().ToText();
	case EStateTreeVariableType::Object:
		return FText(LOCTEXT("Empty", "Empty"));
	default:
		return FText(LOCTEXT("Undefined", "Undefined"));
		break;
	}
}

bool FStateTreeVariable::ResolveHandle(const FStateTreeVariableLayout& Variables, FStateTreeConstantStorage& Constants)
{
	if (IsBound())
	{
		Handle = Variables.GetVariableHandle(ID);
	}
	else
	{
		switch (Type)
		{
		case EStateTreeVariableType::Bool:
			Handle = Constants.AddConstantBool(GetBoolValue());
			break;
		case EStateTreeVariableType::Float:
			Handle = Constants.AddConstantFloat(GetFloatValue());
			break;
		case EStateTreeVariableType::Int:
			Handle = Constants.AddConstantInt(GetIntValue());
			break;
		case EStateTreeVariableType::Vector:
			Handle = Constants.AddConstantVector(GetVectorValue());
			break;
		default:
			Handle = FStateTreeHandle::Invalid;
			break;
		}
	}
	return true;
}

void FStateTreeVariable::InitializeFromDesc(EStateTreeVariableBindingMode InMode, const FStateTreeVariableDesc& Desc)
{
	BindingMode = InMode;
	Type = Desc.Type;
	BaseClass = Desc.BaseClass;
	if (BindingMode == EStateTreeVariableBindingMode::Definition)
	{
		// Defines parameter
		ID = Desc.ID;
		Name = Desc.Name;
	}
	else
	{
		// Refers to parameter
		ID.Invalidate();
		Name = FName();
	}
	FMemory::Memzero(Value);
}

bool FStateTreeVariable::HasSameType(const FStateTreeVariable& Other) const
{
	return Other.Type == Type && Other.BaseClass == BaseClass;
}

bool FStateTreeVariable::CopyValueFrom(FStateTreeVariable& Other)
{
	if (!HasSameType(Other))
	{
		return false;
	}

	ID = Other.ID;
	Name = Other.Name;
	FMemory::Memcpy(Value, Other.Value, FStateTreeVariable::ValueStorageSizeBytes);

	return true;
}

#endif
