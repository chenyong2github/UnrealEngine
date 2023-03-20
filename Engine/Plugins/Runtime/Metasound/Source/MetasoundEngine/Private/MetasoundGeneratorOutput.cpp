// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundGeneratorOutput.h"

#include "MetasoundPrimitives.h"

FMetasoundGeneratorOutput::FMetasoundGeneratorOutput(const FMetasoundGeneratorOutput& Other)
{
	Name = Other.Name;
	DataReference = MakeUnique<Metasound::FAnyDataReference>(*Other.DataReference);
}

FMetasoundGeneratorOutput& FMetasoundGeneratorOutput::operator=(const FMetasoundGeneratorOutput& Other)
{
	if (this != &Other)
	{
		this->Name = Other.Name;
		this->DataReference = MakeUnique<Metasound::FAnyDataReference>(*Other.DataReference);
	}

	return *this;
}

bool FMetasoundGeneratorOutput::IsValid() const
{
	return nullptr != DataReference;
}

FName FMetasoundGeneratorOutput::GetTypeName() const
{
	return IsValid() ? DataReference->GetDataTypeName() : FName();
}

bool UMetasoundGeneratorOutputBlueprintAccess::IsFloat(const FMetasoundGeneratorOutput& Output)
{
	return Output.IsType<float>();
}

float UMetasoundGeneratorOutputBlueprintAccess::GetFloat(const FMetasoundGeneratorOutput& Output, bool& Success)
{
	float Value = 0;
	Success = Output.Get(Value);
	return Value;
}

bool UMetasoundGeneratorOutputBlueprintAccess::IsInt32(const FMetasoundGeneratorOutput& Output)
{
	return Output.IsType<int32>();
}

int32 UMetasoundGeneratorOutputBlueprintAccess::GetInt32(const FMetasoundGeneratorOutput& Output, bool& Success)
{
	int32 Value = 0;
	Success = Output.Get(Value);
	return Value;
}

bool UMetasoundGeneratorOutputBlueprintAccess::IsBool(const FMetasoundGeneratorOutput& Output)
{
	return Output.IsType<bool>();
}

bool UMetasoundGeneratorOutputBlueprintAccess::GetBool(const FMetasoundGeneratorOutput& Output, bool& Success)
{
	bool Value = false;
	Success = Output.Get(Value);
	return Value;
}

bool UMetasoundGeneratorOutputBlueprintAccess::IsString(const FMetasoundGeneratorOutput& Output)
{
	return Output.IsType<FString>();
}

FString UMetasoundGeneratorOutputBlueprintAccess::GetString(const FMetasoundGeneratorOutput& Output, bool& Success)
{
	FString Value;
	Success = Output.Get(Value);
	return Value;
}
