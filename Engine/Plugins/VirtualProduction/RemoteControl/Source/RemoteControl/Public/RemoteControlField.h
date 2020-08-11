// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Misc/Guid.h"
#include "RemoteControlField.generated.h"

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
	TArray<UObject*> ResolveFieldOwners(const TArray<UObject*>& SectionObjects);

	bool operator==(const FRemoteControlField& InField) const;

	bool operator==(const FGuid& InFieldId) const;

	friend uint32 GetTypeHash(const FRemoteControlField& InField);

public:
	/**
	 * The field's type.
	 */
	UPROPERTY()
	EExposedFieldType FieldType = EExposedFieldType::Invalid;

	/**
	 * The field's name.
	 */
	UPROPERTY()
	FName FieldName;

	/**
	 * The class of the field's owner.
	 */
	UPROPERTY()
	UClass* FieldOwnerClass;

	/**
	 * The ID of the field.
	 */
	UPROPERTY()
	FGuid FieldId;
protected:
	FRemoteControlField(UObject* FieldOwner, EExposedFieldType InType, FName InFieldName);

	/**
	 * If not empty, holds the field's path relative to its owner.
	 */
	UPROPERTY()
	FString PathRelativeToOwner;
};

/**
 * Represents a property exposed to remote control.
 */
USTRUCT(BlueprintType)
struct REMOTECONTROL_API FRemoteControlProperty : public FRemoteControlField
{
	GENERATED_BODY()

	FRemoteControlProperty() = default;

	FRemoteControlProperty(UObject* FieldOwner, FName InPropertyName);
};

/**
 * Represents a function exposed to remote control.
 */
USTRUCT(BlueprintType)	
struct REMOTECONTROL_API FRemoteControlFunction : public FRemoteControlField
{
	GENERATED_BODY()

	FRemoteControlFunction() = default;

	FRemoteControlFunction(UObject* FieldOwner, UFunction* InFunction);
	 
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