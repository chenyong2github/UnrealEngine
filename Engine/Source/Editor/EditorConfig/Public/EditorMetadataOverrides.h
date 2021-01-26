// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EditorConfig.h"
#include "EditorSubsystem.h"
#include "TickableEditorObject.h"

#include "EditorMetadataOverrides.generated.h"

USTRUCT()
struct FMetadataSet
{
	GENERATED_BODY()

	// map of metadata key to metadata value
	UPROPERTY()
	TMap<FName, FString> Strings;
		
	UPROPERTY()
	TMap<FName, bool> Bools;

	UPROPERTY()
	TMap<FName, int32> Ints;
		
	UPROPERTY()
	TMap<FName, float> Floats;
};

USTRUCT()
struct FStructMetadata
{
	GENERATED_BODY()

	// map of field name to field metadata
	UPROPERTY()
	TMap<FName, FMetadataSet> Fields;

	UPROPERTY()
	FMetadataSet StructMetadata;
};

USTRUCT()
struct FMetadataConfig
{
	GENERATED_BODY()
		
	// map of class name to class metadata
	UPROPERTY()
	TMap<FName, FStructMetadata> Classes;
};

UCLASS()
class EDITORCONFIG_API UEditorMetadataOverrides : 
	public UEditorSubsystem, 
	public FTickableEditorObject
{ 
	GENERATED_BODY()

public:
	UEditorMetadataOverrides();
	virtual ~UEditorMetadataOverrides() {}

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	bool LoadFromConfig(TSharedPtr<FEditorConfig> Config);
	void Save();

	bool HasMetadata(const FField* Field, FName Key) const;

	bool GetStringMetadata(const FField* Field, FName Key, FString& OutValue) const;
	void SetStringMetadata(const FField* Field, FName Key, FStringView Value);

	bool GetFloatMetadata(const FField* Field, FName Key, float& OutValue) const;
	void SetFloatMetadata(const FField* Field, FName Key, float Value);

	bool GetIntMetadata(const FField* Field, FName Key, int32& OutValue) const;
	void SetIntMetadata(const FField* Field, FName Key, int32 Value);

	bool GetBoolMetadata(const FField* Field, FName Key, bool& OutValue) const;
	void SetBoolMetadata(const FField* Field, FName Key, bool Value);

	bool GetClassMetadata(const FField* Field, FName Key, UClass*& OutValue) const;
	void SetClassMetadata(const FField* Field, FName Key, UClass* Value);

	void RemoveMetadata(const FField* Field, FName Key);

	void Tick(float DeltaTime) override;
	TStatId GetStatId() const override;

private:
	const FMetadataSet* FindFieldMetadata(const FField* Field, FName Key) const;
	FMetadataSet* FindOrAddFieldMetadata(const FField* Field, FName Key);

	void OnCompleted(bool bSuccess);

private:
	TSharedPtr<FEditorConfig> SourceConfig;
	FMetadataConfig LoadedMetadata;

	TAtomic<bool> bDirty { false };
	TAtomic<float> TimeSinceLastSave { 0 };
};
