// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "RemoteControlField.generated.h"

/**
 * Holds a path to a field
 * The full path up to the owning object is described.
 * The object (component) hierarchy is also available to build the full path
 */
struct FFieldPathInfo
{
	FFieldPathInfo() = default;

	FFieldPathInfo(FString&& RelativePath)
		: FullRelativePath(MoveTemp(RelativePath))
		, LastDotIndex(INDEX_NONE)
	{
		FullRelativePath.FindLastChar(TEXT('.'), LastDotIndex);
	}
	
	FString GetFieldName() const
	{
		return FullRelativePath.RightChop(LastDotIndex + 1);
	}

	FString GetPathRelativeToOwner() const
	{
		return FullRelativePath.Left(LastDotIndex);
	}

	FString GetComponentHierarchy() const
	{
		//Convert array representation to dotted string
		FString ComponentHierarchy;
		if (ComponentChain.Num() > 0)
		{
			ComponentHierarchy += ComponentChain[0];
		}

		for (int32 Index = 1; Index < ComponentChain.Num(); ++Index)
		{
			ComponentHierarchy += TEXT(".");
			ComponentHierarchy += ComponentChain[Index];
		}

		return ComponentHierarchy;
	}

	//Property path up to UObject owner (component, actor, etc...) Struct1.Struct2.Var
	FString FullRelativePath;

	/** Component hierarchy above the property (SceneComponent, NestedComponent1, NestedComponent2*/
	TArray<FString> ComponentChain;

private:
	int32 LastDotIndex = INDEX_NONE;
};

/**
 * The type of the exposed field.
 */
UENUM()
enum class EExposedFieldType : uint8
{
	Invalid,
	Property,
	Function
};

/**
 * Represents a property or function that has been exposed to remote control.
 */
USTRUCT(BlueprintType)
struct REMOTECONTROL_API FRemoteControlField
{
	GENERATED_BODY()

	FRemoteControlField() = default;

	/**
	 * Resolve the field's owners using the section's top level objects.
	 * @param SectionObjects The top level objects of the section.
	 * @return The list of UObjects that own the exposed field.
	 */
	TArray<UObject*> ResolveFieldOwners(const TArray<UObject*>& SectionObjects) const;

	bool operator==(const FRemoteControlField& InField) const;
	bool operator==(FGuid InFieldId) const;
	friend uint32 GetTypeHash(const FRemoteControlField& InField);

	/** Returns the field name including its relative path to owner [path.fieldname] */
	FString GetQualifiedFieldName() const;

public:
	/**
	 * The field's type.
	 */
	UPROPERTY()
	EExposedFieldType FieldType = EExposedFieldType::Invalid;

	/**
	 * The exposed field's name.
	 */
	UPROPERTY()
	FName FieldName;

	/**
	 * This RemoteControlField's display name.
	 */
	UPROPERTY()
	FName Label;

	/** 
	 * Unique identifier for this field.
	 */
	UPROPERTY()
	FGuid Id;

	/**
	 * If not empty, holds the field's path relative to its owner.
	 */
	UPROPERTY()
	FString PathRelativeToOwner;

	UPROPERTY()
	FString ComponentHierarchy;

	/**
	 * Metadata for this field.
	 */
	UPROPERTY()
	TMap<FString, FString> Metadata;
	
protected:
	FRemoteControlField(EExposedFieldType InType, FName InLabel, FFieldPathInfo&& FieldPathInfo);
};

/**
 * Represents a property exposed to remote control.
 */
USTRUCT(BlueprintType)
struct REMOTECONTROL_API FRemoteControlProperty : public FRemoteControlField
{
	GENERATED_BODY()

	FRemoteControlProperty() = default;
	FRemoteControlProperty(FName InLabel, FFieldPathInfo FieldPathInfo);
};

/**
 * Represents a function exposed to remote control.
 */
USTRUCT(BlueprintType)	
struct REMOTECONTROL_API FRemoteControlFunction : public FRemoteControlField
{
	GENERATED_BODY()

	FRemoteControlFunction() = default;

	FRemoteControlFunction(FName InLabel, FFieldPathInfo FieldPathInfo, UFunction* InFunction);
	 
	/**
	 * The exposed function.
	 */
	UPROPERTY()
	UFunction* Function = nullptr;

	/**
	 * The function arguments.
	 */
	TSharedPtr<class FStructOnScope> FunctionArguments;

	friend FArchive& operator<<(FArchive& Ar, FRemoteControlFunction& RCFunction);
	bool Serialize(FArchive& Ar);
};

template<> struct TStructOpsTypeTraits<FRemoteControlFunction> : public TStructOpsTypeTraitsBase2<FRemoteControlFunction>
{
	enum
	{
		WithSerializer = true
	};
};