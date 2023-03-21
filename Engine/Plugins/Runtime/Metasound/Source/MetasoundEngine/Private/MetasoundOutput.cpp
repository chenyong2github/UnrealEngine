// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundOutput.h"

#include "MetasoundPrimitives.h"

FMetaSoundOutput::FMetaSoundOutput(const FMetaSoundOutput& Other)
{
	Name = Other.Name;
	DataReference = MakeUnique<Metasound::FAnyDataReference>(*Other.DataReference);
}

FMetaSoundOutput& FMetaSoundOutput::operator=(const FMetaSoundOutput& Other)
{
	if (this != &Other)
	{
		this->Name = Other.Name;
		this->DataReference = MakeUnique<Metasound::FAnyDataReference>(*Other.DataReference);
	}

	return *this;
}

bool FMetaSoundOutput::IsValid() const
{
	return nullptr != DataReference;
}

FName FMetaSoundOutput::GetTypeName() const
{
	return IsValid() ? DataReference->GetDataTypeName() : FName();
}

bool UMetasoundOutputBlueprintAccess::IsFloat(const FMetaSoundOutput& Output)
{
	return Output.IsType<float>();
}

float UMetasoundOutputBlueprintAccess::GetFloat(const FMetaSoundOutput& Output, bool& Success)
{
	float Value = 0;
	Success = Output.Get(Value);
	return Value;
}

bool UMetasoundOutputBlueprintAccess::IsInt32(const FMetaSoundOutput& Output)
{
	return Output.IsType<int32>();
}

int32 UMetasoundOutputBlueprintAccess::GetInt32(const FMetaSoundOutput& Output, bool& Success)
{
	int32 Value = 0;
	Success = Output.Get(Value);
	return Value;
}

bool UMetasoundOutputBlueprintAccess::IsBool(const FMetaSoundOutput& Output)
{
	return Output.IsType<bool>();
}

bool UMetasoundOutputBlueprintAccess::GetBool(const FMetaSoundOutput& Output, bool& Success)
{
	bool Value = false;
	Success = Output.Get(Value);
	return Value;
}

bool UMetasoundOutputBlueprintAccess::IsString(const FMetaSoundOutput& Output)
{
	return Output.IsType<FString>();
}

FString UMetasoundOutputBlueprintAccess::GetString(const FMetaSoundOutput& Output, bool& Success)
{
	FString Value;
	Success = Output.Get(Value);
	return Value;
}
