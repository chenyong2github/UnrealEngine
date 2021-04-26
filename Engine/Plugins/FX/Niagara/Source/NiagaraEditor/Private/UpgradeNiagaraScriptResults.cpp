// Copyright Epic Games, Inc. All Rights Reserved.

#include "UpgradeNiagaraScriptResults.h"

#include "NiagaraClipboard.h"

template<typename T>
T GetValue(const UNiagaraClipboardFunctionInput* Input)
{
	T Value;
	if (Input == nullptr || Input->InputType.GetSize() != sizeof(T) || Input->Local.Num() != sizeof(T))
	{
		FMemory::Memzero(Value);
	}
	else
	{
		FMemory::Memcpy(&Value, Input->Local.GetData(), sizeof(T));
	}
	return Value;
}

template<typename T>
void SetValue(UNiagaraPythonScriptModuleInput* ModuleInput, T Data)
{
	TArray<uint8> LocalData;
	LocalData.SetNumZeroed(sizeof(T));
	FMemory::Memcpy(LocalData.GetData(), &Data, sizeof(T));
	const UNiagaraClipboardFunctionInput* Input = ModuleInput->Input;
	ModuleInput->Input = UNiagaraClipboardFunctionInput::CreateLocalValue(ModuleInput, Input->InputName, Input->InputType, Input->bEditConditionValue, LocalData);
}

bool UNiagaraPythonScriptModuleInput::IsSet() const
{
	return Input && Input->InputType.IsValid();
}

bool UNiagaraPythonScriptModuleInput::IsLocalValue() const
{
	return IsSet() && Input->ValueMode == ENiagaraClipboardFunctionInputValueMode::Local;
}

float UNiagaraPythonScriptModuleInput::AsFloat() const
{
	if (IsSet() && Input->InputType == FNiagaraTypeDefinition::GetFloatDef())
	{
		FNiagaraVariable LocalInput;
		return GetValue<float>(Input);
	}
	return 0;
}

int32 UNiagaraPythonScriptModuleInput::AsInt() const
{
	if (IsSet() && Input->InputType == FNiagaraTypeDefinition::GetIntDef())
	{
		return GetValue<int32>(Input);
	}
	return 0;
}

bool UNiagaraPythonScriptModuleInput::AsBool() const
{
	if (IsSet() && Input->InputType == FNiagaraTypeDefinition::GetBoolDef())
	{
		if (Input->Local.Num() != sizeof(FNiagaraBool))
		{
			return false;
		}
		FNiagaraBool* BoolStruct = (FNiagaraBool*)Input->Local.GetData();
		return BoolStruct->GetValue();
	}
	return false;
}

FVector2D UNiagaraPythonScriptModuleInput::AsVec2() const
{
	if (IsSet() && Input->InputType == FNiagaraTypeDefinition::GetVec2Def())
	{
		return GetValue<FVector2D>(Input);
	}
	return FVector2D();
}

FVector UNiagaraPythonScriptModuleInput::AsVec3() const
{
	if (IsSet() && Input->InputType == FNiagaraTypeDefinition::GetVec3Def())
	{
		return GetValue<FVector>(Input);
	}
	return FVector();
}

FVector4 UNiagaraPythonScriptModuleInput::AsVec4() const
{
	if (IsSet() && Input->InputType == FNiagaraTypeDefinition::GetVec4Def())
	{
		return GetValue<FVector4>(Input);
	}
	return FVector4();
}

FLinearColor UNiagaraPythonScriptModuleInput::AsColor() const
{
	if (IsSet() && Input->InputType == FNiagaraTypeDefinition::GetColorDef())
	{
		return GetValue<FLinearColor>(Input);
	}
	return FLinearColor();
}

FQuat UNiagaraPythonScriptModuleInput::AsQuat() const
{
	if (IsSet() && Input->InputType == FNiagaraTypeDefinition::GetQuatDef())
	{
		return GetValue<FQuat>(Input);
	}
	return FQuat();
}

FString UNiagaraPythonScriptModuleInput::AsEnum() const
{
	if (IsSet() && Input->InputType.IsEnum())
	{
		int32 Value = GetValue<int32>(Input);
		return Input->InputType.GetEnum()->GetNameStringByValue(Value);
	}
	return FString();
}

UUpgradeNiagaraScriptResults::UUpgradeNiagaraScriptResults()
{
	DummyInput = NewObject<UNiagaraPythonScriptModuleInput>();
}

void UUpgradeNiagaraScriptResults::Init()
{
	// if some of the old inputs are missing in the new inputs we still bring them over, otherwise they can't be set from the script
	for (UNiagaraPythonScriptModuleInput* OldInput : OldInputs)
	{
		UNiagaraPythonScriptModuleInput* NewInput = GetNewInput(OldInput->Input->InputName);
		if (NewInput == nullptr)
		{
			UNiagaraPythonScriptModuleInput* ScriptInput = NewObject<UNiagaraPythonScriptModuleInput>();
			ScriptInput->Input = OldInput->Input;
			NewInputs.Add(ScriptInput);
		}
	}
}

UNiagaraPythonScriptModuleInput* UUpgradeNiagaraScriptResults::GetOldInput(const FString& InputName)
{
	for (UNiagaraPythonScriptModuleInput* ModuleInput : OldInputs)
	{
		if (ModuleInput->Input->InputName == FName(InputName))
		{
			return ModuleInput;
		}
	}
	return DummyInput;
}

void UUpgradeNiagaraScriptResults::SetFloatInput(const FString& InputName, float Value)
{
	UNiagaraPythonScriptModuleInput* ModuleInput = GetNewInput(FName(InputName));
	if (ModuleInput && ModuleInput->Input->InputType == FNiagaraTypeDefinition::GetFloatDef())
	{
		SetValue(ModuleInput, Value);
	}
}

void UUpgradeNiagaraScriptResults::SetIntInput(const FString& InputName, int32 Value)
{
	UNiagaraPythonScriptModuleInput* ModuleInput = GetNewInput(FName(InputName));
	if (ModuleInput && ModuleInput->Input->InputType == FNiagaraTypeDefinition::GetIntDef())
	{
		SetValue(ModuleInput, Value);
	}
}

void UUpgradeNiagaraScriptResults::SetBoolInput(const FString& InputName, bool Value)
{
	UNiagaraPythonScriptModuleInput* ModuleInput = GetNewInput(FName(InputName));
	if (ModuleInput && ModuleInput->Input->InputType == FNiagaraTypeDefinition::GetBoolDef())
	{
		SetValue(ModuleInput, Value);
	}
}

void UUpgradeNiagaraScriptResults::SetVec2Input(const FString& InputName, FVector2D Value)
{
	UNiagaraPythonScriptModuleInput* ModuleInput = GetNewInput(FName(InputName));
	if (ModuleInput && ModuleInput->Input->InputType == FNiagaraTypeDefinition::GetVec2Def())
	{
		SetValue(ModuleInput, Value);
	}
}

void UUpgradeNiagaraScriptResults::SetVec3Input(const FString& InputName, FVector Value)
{
	UNiagaraPythonScriptModuleInput* ModuleInput = GetNewInput(FName(InputName));
	if (ModuleInput && ModuleInput->Input->InputType == FNiagaraTypeDefinition::GetVec3Def())
	{
		SetValue(ModuleInput, Value);
	}
}

void UUpgradeNiagaraScriptResults::SetVec4Input(const FString& InputName, FVector4 Value)
{
	UNiagaraPythonScriptModuleInput* ModuleInput = GetNewInput(FName(InputName));
	if (ModuleInput && ModuleInput->Input->InputType == FNiagaraTypeDefinition::GetVec4Def())
	{
		SetValue(ModuleInput, Value);
	}
}

void UUpgradeNiagaraScriptResults::SetColorInput(const FString& InputName, FLinearColor Value)
{
	UNiagaraPythonScriptModuleInput* ModuleInput = GetNewInput(FName(InputName));
	if (ModuleInput && ModuleInput->Input->InputType == FNiagaraTypeDefinition::GetColorDef())
	{
		SetValue(ModuleInput, Value);
	}
}

void UUpgradeNiagaraScriptResults::SetQuatInput(const FString& InputName, FQuat Value)
{
	UNiagaraPythonScriptModuleInput* ModuleInput = GetNewInput(FName(InputName));
	if (ModuleInput && ModuleInput->Input->InputType == FNiagaraTypeDefinition::GetQuatDef())
	{
		SetValue(ModuleInput, Value);
	}
}

void UUpgradeNiagaraScriptResults::SetEnumInput(const FString& InputName, FString Value)
{
	UNiagaraPythonScriptModuleInput* ModuleInput = GetNewInput(FName(InputName));
	if (ModuleInput && ModuleInput->Input->InputType.IsEnum())
	{
		int32 EnumValue = ModuleInput->Input->InputType.GetEnum()->GetValueByNameString(Value, EGetByNameFlags::ErrorIfNotFound | EGetByNameFlags::CheckAuthoredName);
		SetValue(ModuleInput, EnumValue);
	}
}

UNiagaraPythonScriptModuleInput* UUpgradeNiagaraScriptResults::GetNewInput(const FName& InputName) const
{
	for (UNiagaraPythonScriptModuleInput* ModuleInput : NewInputs)
	{
		if (ModuleInput->Input && ModuleInput->Input->InputName == InputName)
		{
			return ModuleInput;
		}
	}
	return nullptr;
}


