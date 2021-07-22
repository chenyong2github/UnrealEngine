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

	//////////////////////////////////////////////////////////////////////////////
	/// Property Management
	//////////////////////////////////////////////////////////////////////////////
	struct RIGVM_API FPropertyDescription
	{
		FName Name;
		const FProperty* Property;
		FString CPPType;
		UObject* CPPTypeObject;
		EPinContainerType ContainerType;
		FString DefaultValue;
		
		FPropertyDescription(const FProperty* InProperty, const FString& InDefaultValue, const FName& InName = NAME_None);
		FPropertyDescription(const FName& InName, const FString& InCPPType, UObject* InCPPTypeObject, const FString& InDefaultValue);

		static FName SanitizeName(const FName& InName);
		void SanitizeName();

		bool HasContainer() const { return ContainerType != EPinContainerType::None; }
		bool IsArray() const { return ContainerType == EPinContainerType::Array; }
		bool IsMap() const { return ContainerType == EPinContainerType::Map; }
		bool IsSet() const { return ContainerType == EPinContainerType::Set; }

		FString GetBaseCPPType() const;
		FString GetArrayElementCPPType() const;
		FString GetSetElementCPPType() const;
		FString GetMapKeyCPPType() const;
		FString GetMapValueCPPType() const;

		static const FString ArrayPrefix;
		static const FString MapPrefix;
		static const FString SetPrefix;
		static const FString ContainerSuffix;
	};

	static UClass* GetStorageClass(UObject* InOuter, ERigVMMemoryType InMemoryType);
	static UClass* CreateStorageClass(UObject* InOuter, ERigVMMemoryType InMemoryType, const TArray<FPropertyDescription>& InProperties);
	static URigVMMemoryStorage* CreateStorage(UObject* InOuter, ERigVMMemoryType InMemoryType);

	//////////////////////////////////////////////////////////////////////////////
	/// Memory Access
	//////////////////////////////////////////////////////////////////////////////

	const TArray<const FProperty*>& GetProperties() const;
	int32 GetPropertyIndex(const FProperty* InProperty) const;
	int32 GetPropertyIndexByName(const FName& InName) const;
	const FProperty* FindPropertyByName(const FName& InName) const;

	template<typename T>
	T* ContainerPtrToValuePtr(int32 InPropertyIndex)
	{
		const TArray<const FProperty*>& Properties = GetProperties();
		if(Properties.IsValidIndex(InPropertyIndex))
		{
			return Properties[InPropertyIndex]->ContainerPtrToValuePtr<T>(this);
		}
		return nullptr;
	}

	template<typename T>
	T* ContainerPtrToValuePtrByName(const FName& InName)
	{
		const int32 PropertyIndex = GetPropertyIndexByName(InName);
		return ContainerPtrToValuePtr<T>(PropertyIndex);
	}

protected:

	TArray<const FProperty*> CachedProperties;

private:
	
	struct FClassInfo
	{
		UClass* LiteralStorageClass;
		UClass* WorkStorageClass;
		UClass* DebugStorageClass;
		
		FClassInfo()
			: LiteralStorageClass(nullptr)
			, WorkStorageClass(nullptr)
			, DebugStorageClass(nullptr)
		{
		}

		UClass** GetClassPtr(ERigVMMemoryType InMemoryType)
		{
			switch(InMemoryType)
			{
				case ERigVMMemoryType::Literal:
				{
					return &LiteralStorageClass;
				}
				case ERigVMMemoryType::Debug:
				{
					return &DebugStorageClass;
				}
				default:
				{
					break;
				}
			}
			return &WorkStorageClass;
		}
	};
	
	static TMap<FString, FClassInfo> PackageToInfo;
};
