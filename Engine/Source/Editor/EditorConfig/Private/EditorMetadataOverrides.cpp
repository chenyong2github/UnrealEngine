// Copyright Epic Games, Inc. All Rights Reserved.

#include "EditorMetadataOverrides.h"

#include "Editor.h"
#include "EditorConfigSubsystem.h"
#include "Misc/StringBuilder.h"
#include "UObject/UnrealType.h"

UEditorMetadataOverrides::UEditorMetadataOverrides()
{
}

void UEditorMetadataOverrides::Initialize(FSubsystemCollectionBase& Collection)
{
	UEditorConfigSubsystem* EditorConfig = Collection.InitializeDependency<UEditorConfigSubsystem>();
	
	TSharedPtr<FEditorConfig> MetadataOverrideConfig = EditorConfig->FindOrLoadConfig(TEXT("MetadataOverrides"));
	LoadFromConfig(MetadataOverrideConfig);
}

bool UEditorMetadataOverrides::LoadFromConfig(TSharedPtr<FEditorConfig> Config)
{
	SourceConfig = Config;
	LoadedMetadata.Classes.Reset();

	if (!SourceConfig.IsValid())
	{
		return false;
	}

	return SourceConfig->TryGetStruct(TEXT("Metadata"), LoadedMetadata);
}

void UEditorMetadataOverrides::Tick(float DeltaTime)
{
	if (bDirty)
	{
		TimeSinceLastSave = TimeSinceLastSave + DeltaTime;

		const float SaveDelaySeconds = 3.0f;
		if (TimeSinceLastSave > SaveDelaySeconds)
		{
			Save();
		}
	}
}

TStatId UEditorMetadataOverrides::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UEditorMetadataOverrides, STATGROUP_Tickables);
}

void UEditorMetadataOverrides::Save()
{
	if (!SourceConfig.IsValid())
	{
		return;
	}

	SourceConfig->SetStruct(TEXT("Metadata"), LoadedMetadata);

	UEditorConfigSubsystem* EditorConfigSubsystem = GEditor->GetEditorSubsystem<UEditorConfigSubsystem>();
	if (EditorConfigSubsystem == nullptr)
	{
		return;
	}

	EditorConfigSubsystem->SaveConfig(SourceConfig, FOnCompletedDelegate::CreateUObject(this, &UEditorMetadataOverrides::OnCompleted));
}

void UEditorMetadataOverrides::OnCompleted(bool bSuccess)
{
	if (bSuccess)
	{
		TimeSinceLastSave = 0;
		bDirty = false;
	}
}

const FMetadataSet* UEditorMetadataOverrides::FindFieldMetadata(const FField* Field, FName Key) const
{
	if (!SourceConfig.IsValid())
	{
		return nullptr;
	}

	const UStruct* OwnerStruct = Field->GetOwnerStruct();
	if (OwnerStruct == nullptr)
	{
		return nullptr;
	}

	const FStructMetadata* StructMetadata = LoadedMetadata.Classes.Find(OwnerStruct->GetFName());
	if (StructMetadata == nullptr)
	{
		return nullptr;
	}

	const FMetadataSet* FieldMetadata = StructMetadata->Fields.Find(Field->GetFName());
	return FieldMetadata;
}
	
FMetadataSet* UEditorMetadataOverrides::FindOrAddFieldMetadata(const FField* Field, FName Key)
{
	if (!SourceConfig.IsValid())
	{
		return nullptr;
	}

	const UStruct* OwnerStruct = Field->GetOwnerStruct();
	if (OwnerStruct == nullptr)
	{
		return nullptr;
	}

	FStructMetadata& StructMetadata = LoadedMetadata.Classes.FindOrAdd(OwnerStruct->GetFName());

	FMetadataSet& FieldMetadata = StructMetadata.Fields.FindOrAdd(Field->GetFName());
	return &FieldMetadata;
}

bool UEditorMetadataOverrides::HasMetadata(const FField* Field, FName Key) const
{
	check(Field != nullptr);

	const FMetadataSet* FieldMetadata = FindFieldMetadata(Field, Key);
	return FieldMetadata != nullptr;
}

bool UEditorMetadataOverrides::GetStringMetadata(const FField* Field, FName Key, FString& OutValue) const
{
	check(Field != nullptr);

	const FMetadataSet* FieldMetadata = FindFieldMetadata(Field, Key);
	if (FieldMetadata == nullptr)
	{
		return false;
	}

	const FString* MetaValue = FieldMetadata->Strings.Find(Key);
	if (MetaValue == nullptr)
	{
		if (!Field->HasMetaData(Key))
		{
			return false;
		}

		OutValue = Field->GetMetaData(Key);
		return true;
	}

	OutValue = *MetaValue;
	return true;
}

void UEditorMetadataOverrides::SetStringMetadata(const FField* Field, FName Key, FStringView Value)
{
	check(Field != nullptr);
		
	FMetadataSet* FieldMetadata = FindOrAddFieldMetadata(Field, Key);
	if (FieldMetadata == nullptr)
	{
		return;
	}

	FieldMetadata->Strings.Add(Key, FString(Value));
	bDirty = true;
}

bool UEditorMetadataOverrides::GetFloatMetadata(const FField* Field, FName Key, float& OutValue) const
{
check(Field != nullptr);

	const FMetadataSet* FieldMetadata = FindFieldMetadata(Field, Key);
	if (FieldMetadata == nullptr)
	{
		return false;
	}

	const float* MetaValue = FieldMetadata->Floats.Find(Key);
	if (MetaValue == nullptr)
	{
		if (!Field->HasMetaData(Key))
		{
			return false;
		}

		OutValue = Field->GetFloatMetaData(Key);
		return true;
	}

	OutValue = *MetaValue;
	return true;
}

void UEditorMetadataOverrides::SetFloatMetadata(const FField* Field, FName Key, float Value)
{
	check(Field != nullptr);
		
	FMetadataSet* FieldMetadata = FindOrAddFieldMetadata(Field, Key);
	if (FieldMetadata == nullptr)
	{
		return;
	}

	FieldMetadata->Floats.Add(Key, Value);
	bDirty = true;
}

bool UEditorMetadataOverrides::GetIntMetadata(const FField* Field, FName Key, int32& OutValue) const
{
	check(Field != nullptr);

	const FMetadataSet* FieldMetadata = FindFieldMetadata(Field, Key);
	if (FieldMetadata == nullptr)
	{
		return false;
	}

	const int32* MetaValue = FieldMetadata->Ints.Find(Key);
	if (MetaValue == nullptr)
	{
		if (!Field->HasMetaData(Key))
		{
			return false;
		}

		OutValue = Field->GetIntMetaData(Key);
		return true;
	}

	OutValue = *MetaValue;
	return true;
}

void UEditorMetadataOverrides::SetIntMetadata(const FField* Field, FName Key, int32 Value)
{
	check(Field != nullptr);
		
	FMetadataSet* FieldMetadata = FindOrAddFieldMetadata(Field, Key);
	if (FieldMetadata == nullptr)
	{
		return;
	}

	FieldMetadata->Ints.Add(Key, Value);
	bDirty = true;
}

bool UEditorMetadataOverrides::GetBoolMetadata(const FField* Field, FName Key, bool& OutValue) const
{
	check(Field != nullptr);

	const FMetadataSet* FieldMetadata = FindFieldMetadata(Field, Key);
	if (FieldMetadata == nullptr)
	{
		return false;
	}

	const bool* MetaValue = FieldMetadata->Bools.Find(Key);
	if (MetaValue == nullptr)
	{
		if (!Field->HasMetaData(Key))
		{
			return false;
		}

		OutValue = Field->GetBoolMetaData(Key);
		return true;
	}

	OutValue = *MetaValue;
	return true;
}
	
void UEditorMetadataOverrides::SetBoolMetadata(const FField* Field, FName Key, bool Value)
{
	check(Field != nullptr);
		
	FMetadataSet* FieldMetadata = FindOrAddFieldMetadata(Field, Key);
	if (FieldMetadata == nullptr)
	{
		return;
	}

	FieldMetadata->Bools.Add(Key, Value);
	bDirty = true;
}

bool UEditorMetadataOverrides::GetClassMetadata(const FField* Field, FName Key, UClass*& OutValue) const
{
	check(Field != nullptr);

	FString ClassName;
	if (!GetStringMetadata(Field, Key, ClassName))
	{
		return false;
	}

	OutValue = FindObject<UClass>(ANY_PACKAGE, *ClassName);
	return true; // we return true here because we did have a value, it just wasn't a valid class name
}

void UEditorMetadataOverrides::SetClassMetadata(const FField* Field, FName Key, UClass* Value)
{
	check(Field != nullptr);

	FString ClassName;
	if (Value != nullptr)
	{
		ClassName = Value->GetName();
	}

	SetStringMetadata(Field, Key, ClassName);
	bDirty = true;
}

void UEditorMetadataOverrides::RemoveMetadata(const FField* Field, FName Key)
{
	if (!SourceConfig.IsValid())
	{
		return;
	}

	const UStruct* OwnerStruct = Field->GetOwnerStruct();
	if (OwnerStruct == nullptr)
	{
		return;
	}

	FStructMetadata* StructMetadata = LoadedMetadata.Classes.Find(OwnerStruct->GetFName());
	if (StructMetadata == nullptr)
	{
		return;
	}

	FMetadataSet* FieldMetadata = StructMetadata->Fields.Find(Field->GetFName());
	if (FieldMetadata == nullptr)
	{
		return;
	}

	FieldMetadata->Ints.Remove(Key);
	FieldMetadata->Bools.Remove(Key);
	FieldMetadata->Floats.Remove(Key);
	FieldMetadata->Strings.Remove(Key);

	bDirty = true;
}
