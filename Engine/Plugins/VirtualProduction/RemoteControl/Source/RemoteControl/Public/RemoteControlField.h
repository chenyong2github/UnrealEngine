// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "RemoteControlEntity.h"
#include "RemoteControlFieldPath.h"
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
struct REMOTECONTROL_API FRemoteControlField : public FRemoteControlEntity
{
	GENERATED_BODY()

	FRemoteControlField() = default;

	/**
	 * Resolve the field's owners using the section's top level objects.
	 * @param SectionObjects The top level objects of the section.
	 * @return The list of UObjects that own the exposed field.
	 */
	TArray<UObject*> ResolveFieldOwners(const TArray<UObject*>& SectionObjects) const;

public:
	/**
	 * The field's type.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "RemoteControlPreset")
	EExposedFieldType FieldType = EExposedFieldType::Invalid;

	/**
	 * The exposed field's name.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "RemoteControlPreset")
	FName FieldName;


	/**
	 * Path information pointing to this field
	 */
	UPROPERTY()
	FRCFieldPathInfo FieldPathInfo;


#if WITH_EDITORONLY_DATA
	UPROPERTY()
	TArray<FString> ComponentHierarchy_DEPRECATED;

#endif

protected:
	FRemoteControlField(URemoteControlPreset* InPreset, EExposedFieldType InType, FName InLabel, FRCFieldPathInfo FieldPathInfo);

private:
#if WITH_EDITORONLY_DATA
	/**
	 * Resolve the field's owners using the section's top level objects and the deprecated component hierarchy.
	 * @param SectionObjects The top level objects of the section.
	 * @return The list of UObjects that own the exposed field.
	 */
	TArray<UObject*> ResolveFieldOwnersUsingComponentHierarchy(const TArray<UObject*>& SectionObjects) const;
#endif
};

/**
 * Represents a property exposed to remote control.
 */
USTRUCT(BlueprintType)
struct REMOTECONTROL_API FRemoteControlProperty : public FRemoteControlField
{
	GENERATED_BODY()

	FRemoteControlProperty() = default;

	UE_DEPRECATED(4.27, "This constructor is deprecated. Use the other constructor.")
	FRemoteControlProperty(FName InLabel, FRCFieldPathInfo FieldPathInfo, TArray<FString> InComponentHierarchy);

	FRemoteControlProperty(URemoteControlPreset* InPreset, FName InLabel, FRCFieldPathInfo FieldPathInfo);
};

/**
 * Represents a function exposed to remote control.
 */
USTRUCT(BlueprintType)
struct REMOTECONTROL_API FRemoteControlFunction : public FRemoteControlField
{
	GENERATED_BODY()

	FRemoteControlFunction() = default;

	UE_DEPRECATED(4.27, "This constructor is deprecated. Use the other constructor.")
	FRemoteControlFunction(FName InLabel, FRCFieldPathInfo FieldPathInfo, UFunction* InFunction);

	FRemoteControlFunction(URemoteControlPreset* InPreset, FName InLabel, FRCFieldPathInfo FieldPathInfo, UFunction* InFunction);
	 
	/**
	 * The exposed function.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "RemoteControlPreset")
	UFunction* Function = nullptr;

	/**
	 * The function arguments.
	 */
	TSharedPtr<class FStructOnScope> FunctionArguments;

	friend FArchive& operator<<(FArchive& Ar, FRemoteControlFunction& RCFunction);
	bool Serialize(FArchive& Ar);

private:
	/** Parse function metadata to get the function`s default parameters */
	void AssignDefaultFunctionArguments();
};


template<> struct TStructOpsTypeTraits<FRemoteControlFunction> : public TStructOpsTypeTraitsBase2<FRemoteControlFunction>
{
	enum
	{
		WithSerializer = true
	};
};