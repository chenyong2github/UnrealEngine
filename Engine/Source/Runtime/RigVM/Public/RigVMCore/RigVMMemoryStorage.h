// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "RigVMTraits.h"
#include "RigVMStatistics.h"
#include "RigVMArray.h"
#include "RigVMMemoryCommon.h"
#include "RigVMMemoryStorage.generated.h"

#if !UE_RIGVM_UCLASS_BASED_STORAGE_DISABLED

/**
 * The FRigVMMemoryHandle is used to access the memory used within a URigMemoryStorage.
 */
struct FRigVMMemoryHandle
{
public:

	FORCEINLINE_DEBUGGABLE FRigVMMemoryHandle()
		: Ptr(nullptr)
	{}

private:
	
	uint8* Ptr;

	friend class URigVM;
};

#endif

UCLASS()
class RIGVM_API URigVMMemoryStorage : public UObject
{
	GENERATED_BODY()

public:
	
	struct FPropertyDescription
	{
		FName Name;
		const FProperty* Property;
		FEdGraphPinType PinType;
		FString DefaultValue;
		
		FPropertyDescription(const FProperty* InProperty, const FString& InDefaultValue, const FName& InName = NAME_None);
		FPropertyDescription(const FName& InName, const FEdGraphPinType& InPinType, const FString& InDefaultValue);
	};

	static UClass* CreateStorageClass(UObject* InOuter, const TArray<FPropertyDescription>& InProperties);
	static URigVMMemoryStorage* CreateStorage(UObject* InOuter, const TArray<FPropertyDescription>& InProperties);
};
