// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "RemoteControlField.generated.h"


/** Small container for the resolved data of a remote control field segment */
struct REMOTECONTROL_API FRCFieldResolvedData
{
	/** Type of that segment owner */
	UStruct* Struct = nullptr;

	/** Container address of this segment */
	void* ContainerAddress = nullptr;
	
	/** Resolved field for this segment */
	FProperty* Field = nullptr;
};

/** RemoteControl Path segment holding a property layer */
USTRUCT()
struct REMOTECONTROL_API FRCFieldPathSegment
{
	GENERATED_BODY()

public:
	FRCFieldPathSegment() = default;

	/** Builds a segment from a name. */
	FRCFieldPathSegment(FStringView SegmentName);

	/** Returns true if a Field was found for a given owner */
	bool IsResolved() const;

	/** 
	 * Converts this segment to a string 
	 * FieldName, FieldName[Index]
	 * If bDuplicateContainer is asked, format will be different if its indexed
	 * FieldName.FieldName[Index]  -> This is to bridge for PathToProperty 
	 */
	FString ToString(bool bDuplicateContainer = false) const;


private:
	
	/** Reset resolved pointers */
	void ClearResolvedData();

public:

	/** Name of the segment */
	UPROPERTY()
	FName Name;

	/** Container index if any. */
	UPROPERTY()
	int32 ArrayIndex = INDEX_NONE;
	
	/** Resolved Data of the segment */
	FRCFieldResolvedData ResolvedData;
};


/**
 * Holds a path information to a field
 * Have facilities to resolve for a given owner
 */
 USTRUCT()
struct REMOTECONTROL_API FRCFieldPathInfo
{
	GENERATED_BODY()

public:
	FRCFieldPathInfo() = default;

	/** 
	 * Builds a path info from a string of format with '.' delimiters
	 * Optionally can reduce duplicates when dealing with containers
	 * If true -> Struct.ArrayName.ArrayName[2].Member will collapse to Struct.ArrayName[2].Member
	 * This is when being used with PathToProperty
	 */
	FRCFieldPathInfo(const FString& PathInfo, bool bSkipDuplicates = false);

public:

	/** Go through each segment and finds the property associated + container address for a given UObject owner */
	bool Resolve(UObject* Owner);

	/** Returns true if last segment was resolved */
	bool IsResolved() const;

	/** Returns true if the hash of the string corresponds to the string we were built with */
	bool IsEqual(FStringView OtherPath) const;

	/** Returns true if hash of both PathInfo matches */
	bool IsEqual(const FRCFieldPathInfo& OtherPath) const;

	/** 
	 * Converts this PathInfo to a string
	 * Walks the full path by default
	 * If EndSegment is not none, will stop at the desired segment 
	 */
	FString ToString(int32 EndSegment = INDEX_NONE) const;

	/**
	 * Converts this PathInfo to a string of PathToProperty format
	 * Struct.ArrayName.ArrayName[Index]
	 * If EndSegment is not none, will stop at the desired segment
	 */
	FString ToPathPropertyString(int32 EndSegment = INDEX_NONE) const;

	/** Returns the number of segment in this path */
	int32 GetSegmentCount() const { return Segments.Num(); }

	/** Gets a segment from this path */
	const FRCFieldPathSegment& GetFieldSegment(int32 Index) const;

	/** 
	 * Returns the resolved data of the last segment
	 * If last segment is not resolved, data won't be valid
	 */
	FRCFieldResolvedData GetResolvedData() const;

	/** Returns last segment's name */
	FName GetFieldName() const;

	/** Builds a property change event from all the segments */
	FPropertyChangedEvent ToPropertyChangedEvent(EPropertyChangeType::Type InChangeType = EPropertyChangeType::Unspecified) const;

	/** Builds an EditPropertyChain from the segments */
	void ToEditPropertyChain(FEditPropertyChain& OutPropertyChain) const;

private:

	/** Recursively resolves all segment until the final one */
	bool ResolveInternalRecursive(UStruct* OwnerType, void* ContainerAddress, int32 SegmentIndex);

public:

	/** List of segments to point to a given field */
	UPROPERTY()
	TArray<FRCFieldPathSegment> Segments;

	/** Hash created from the string we were built from to quickly compare to paths */
	UPROPERTY()
	uint32 PathHash = 0;
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
	 * Path information pointing to this field
	 */
	UPROPERTY()
	FRCFieldPathInfo FieldPathInfo;

	/**
	 * Component hierarchy of this field starting after the actor owner
	 */
	UPROPERTY()
	TArray<FString> ComponentHierarchy;

	/**
	 * Metadata for this field.
	 */
	UPROPERTY()
	TMap<FString, FString> Metadata;

protected:
	FRemoteControlField(EExposedFieldType InType, FName InLabel, FRCFieldPathInfo FieldPathInfo, TArray<FString> InComponentHierarchy);
};

/**
 * Represents a property exposed to remote control.
 */
USTRUCT(BlueprintType)
struct REMOTECONTROL_API FRemoteControlProperty : public FRemoteControlField
{
	GENERATED_BODY()

	FRemoteControlProperty() = default;
	FRemoteControlProperty(FName InLabel, FRCFieldPathInfo FieldPathInfo, TArray<FString> InComponentHierarchy);
};

/**
 * Represents a function exposed to remote control.
 */
USTRUCT(BlueprintType)	
struct REMOTECONTROL_API FRemoteControlFunction : public FRemoteControlField
{
	GENERATED_BODY()

	FRemoteControlFunction() = default;

	FRemoteControlFunction(FName InLabel, FRCFieldPathInfo FieldPathInfo, UFunction* InFunction);
	 
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