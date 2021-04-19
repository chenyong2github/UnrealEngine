// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EditorSubsystem.h"
#include "Templates/SubclassOf.h"
#include "UObject/Object.h"

#include "RCPropertyContainer.generated.h"

UCLASS(Transient, Abstract)
class URCPropertyContainerBase : public UObject
{
	GENERATED_BODY()

public:
	/** Sets the value from the incoming raw data */
	void SetValue(const uint8* InData);

	/** Writes to the provided raw data pointer */
	void GetValue(uint8* OutData);
};

/** Minimal information needed to lookup a unique property container class */
USTRUCT()
struct REMOTECONTROLCOMMON_API FRCPropertyContainerKey
{
	GENERATED_BODY()

	UPROPERTY()
	FName ValueTypeName;

	FName ToClassName() const;
};

inline uint64 GetTypeHash(const FRCPropertyContainerKey& InValue) { return GetTypeHash(InValue.ValueTypeName); }
inline bool operator==(const FRCPropertyContainerKey& Lhs, const FRCPropertyContainerKey& Rhs) { return Lhs.ValueTypeName == Rhs.ValueTypeName; }
inline bool operator!=(const FRCPropertyContainerKey& Lhs, const FRCPropertyContainerKey& Rhs) { return Lhs.ValueTypeName != Rhs.ValueTypeName ; }

/** A subsystem to provide and cache dynamically created PropertyContainer classes. */
UCLASS()
class REMOTECONTROLCOMMON_API URCPropertyContainerRegistry final : public UEditorSubsystem
{
	GENERATED_BODY()
	
public:
	/** Creates a (UObject) container, with a single Value property of the given type. */
	URCPropertyContainerBase* CreateContainer(UObject* InOwner, const FName& InValueTypeName, const FProperty* InValueSrcProperty);
	
private:
	UPROPERTY(Transient)
	TMap<FRCPropertyContainerKey, TSubclassOf<URCPropertyContainerBase>> CachedContainerClasses;

	TSubclassOf<URCPropertyContainerBase>& FindOrAddContainerClass(const FName& InValueTypeName, const FProperty* InValueSrcProperty);
};

namespace PropertyContainers
{
	REMOTECONTROLCOMMON_API URCPropertyContainerBase* CreateContainerForProperty(UObject* InOwner, const FProperty* InSrcProperty);
}
