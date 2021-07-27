// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "RigVMTraits.h"
#include "RigVMStatistics.h"
#include "RigVMArray.h"
#include "RigVMMemoryCommon.h"
#include "EdGraph/EdGraphNode.h"
#include "RigVMPropertyPath.h"
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
		, Property(nullptr) 
		, PropertyPath(nullptr)
	{}

	FORCEINLINE_DEBUGGABLE FRigVMMemoryHandle(uint8* InPtr, const FProperty* InProperty,  const FRigVMPropertyPath* InPropertyPath = nullptr)
		: Ptr(InPtr)
		, Property(InProperty)
		, PropertyPath(InPropertyPath)
	{
		if(PropertyPath)
		{
			if(PropertyPath->IsDirect())
			{
				Ptr = PropertyPath->GetData<uint8>(Ptr);
				PropertyPath = nullptr;
			}
		}
	}

	FORCEINLINE uint8* GetData(bool bFollowPropertyPath = false)
	{
		return GetData_Internal(bFollowPropertyPath);
	}

	FORCEINLINE const uint8* GetData(bool bFollowPropertyPath = false) const
	{
		return GetData_Internal(bFollowPropertyPath);
	}

	FORCEINLINE const FProperty* GetProperty() const
	{
		return Property;
	}

	FORCEINLINE const FRigVMPropertyPath* GetPropertyPath() const
	{
		return PropertyPath;
	}

private:

	FORCEINLINE uint8* GetData_Internal(bool bFollowPropertyPath = false) const
	{
		if(bFollowPropertyPath && PropertyPath != nullptr)
		{
			return PropertyPath->GetData<uint8>(Ptr);
		}
		return Ptr;
	}
	
	uint8* Ptr;
	const FProperty* Property;
	const FRigVMPropertyPath* PropertyPath;

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
		TArray<EPinContainerType> Containers;
		FString DefaultValue;

		FPropertyDescription()
			: Name(NAME_None)
			, Property(nullptr)
			, CPPType()
			, CPPTypeObject(nullptr)
			, Containers()
			, DefaultValue()
		{}
		FPropertyDescription(const FProperty* InProperty, const FString& InDefaultValue, const FName& InName = NAME_None);
		FPropertyDescription(const FName& InName, const FString& InCPPType, UObject* InCPPTypeObject, const FString& InDefaultValue);

		static FName SanitizeName(const FName& InName);
		void SanitizeName();

		bool IsValid() const { return !Name.IsNone(); }
		int32 NumContainers() const { return Containers.Num(); }
		bool HasContainer() const { return NumContainers() > 0; }
		bool IsArray(int32 InContainerIndex = 0) const { return Containers[InContainerIndex] == EPinContainerType::Array; }
		bool IsMap(int32 InContainerIndex = 0) const { return Containers[InContainerIndex] == EPinContainerType::Map; }
		bool IsSet(int32 InContainerIndex = 0) const { return Containers[InContainerIndex] == EPinContainerType::Set; }

		FString GetBaseCPPType() const;

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

	FORCEINLINE int32 Num() const
	{
		return GetProperties().Num();
	}

	FORCEINLINE bool IsValidIndex(int32 InIndex) const
	{
		return GetProperties().IsValidIndex(InIndex);
	}
	
	const TArray<const FProperty*>& GetProperties() const;

	int32 GetPropertyIndex(const FProperty* InProperty) const;

	int32 GetPropertyIndexByName(const FName& InName) const;

	const FProperty* FindPropertyByName(const FName& InName) const;
	
	FORCEINLINE bool IsArray(int32 InPropertyIndex) const
	{
		return GetProperties()[InPropertyIndex]->IsA<FArrayProperty>();
	}
	
	FORCEINLINE bool IsArrayByName(const FName& InName) const
	{
		if(const FProperty* Property = FindPropertyByName(InName))
		{
			return Property->IsA<FArrayProperty>();
		}
		return false;
	}
	
	FORCEINLINE bool IsMap(int32 InPropertyIndex) const
	{
		return GetProperties()[InPropertyIndex]->IsA<FMapProperty>();
	}
	
	FORCEINLINE bool IsMapByName(const FName& InName) const
	{
		if(const FProperty* Property = FindPropertyByName(InName))
		{
			return Property->IsA<FMapProperty>();
		}
		return false;
	}
	
	FORCEINLINE bool IsSet(int32 InPropertyIndex) const
	{
		return GetProperties()[InPropertyIndex]->IsA<FSetProperty>();
	}
	
	FORCEINLINE bool IsSetByName(const FName& InName) const
	{
		if(const FProperty* Property = FindPropertyByName(InName))
		{
			return Property->IsA<FSetProperty>();
		}
		return false;
	}

	template<typename T>
	FORCEINLINE T* GetData(int32 InPropertyIndex)
	{
		const TArray<const FProperty*>& Properties = GetProperties();
		if(Properties.IsValidIndex(InPropertyIndex))
		{
			return Properties[InPropertyIndex]->ContainerPtrToValuePtr<T>(this);
		}
		return nullptr;
	}

	template<typename T>
	FORCEINLINE T* GetDataByName(const FName& InName)
	{
		const int32 PropertyIndex = GetPropertyIndexByName(InName);
		return GetData<T>(PropertyIndex);
	}

	template<typename T>
	FORCEINLINE T* GetData(int32 InPropertyIndex, const FRigVMPropertyPath& InPropertyPath)
	{
		return InPropertyPath.GetData<T>(GetData<uint8>(InPropertyIndex));
	}

	template<typename T>
	FORCEINLINE T* GetDataByName(const FName& InName, const FRigVMPropertyPath& InPropertyPath)
	{
		const int32 PropertyIndex = GetPropertyIndexByName(InName);
		return GetData<T>(PropertyIndex, InPropertyPath);
	}

	FString GetDataAsString(int32 InPropertyIndex);
	
	FORCEINLINE FString GetDataAsStringByName(const FName& InName)
	{
		const int32 PropertyIndex = GetPropertyIndexByName(InName);
		return GetDataAsString(PropertyIndex);
	}

	bool SetDataFromString(int32 InPropertyIndex, const FString& InValue);

	FORCEINLINE bool SetDataFromStringByName(const FName& InName, const FString& InValue)
	{
		const int32 PropertyIndex = GetPropertyIndexByName(InName);
		return SetDataFromString(PropertyIndex, InValue);
	}

#if !UE_RIGVM_UCLASS_BASED_STORAGE_DISABLED
	
	FRigVMMemoryHandle GetHandle(int32 InPropertyIndex, const FRigVMPropertyPath* InPropertyPath = nullptr);

	FORCEINLINE FRigVMMemoryHandle GetHandleByName(const FName& InName, const FRigVMPropertyPath* InPropertyPath = nullptr)
	{
		const int32 PropertyIndex = GetPropertyIndexByName(InName);
		return GetHandle(PropertyIndex, InPropertyPath);
	}
	
#endif
	
	static bool CopyProperty(
		const FProperty* InTargetProperty,
		uint8* InTargetPtr,
		const FProperty* InSourceProperty,
		const uint8* InSourcePtr);

	static bool CopyProperty(
		const FProperty* InTargetProperty,
		uint8* InTargetPtr,
		const FRigVMPropertyPath& InTargetPropertyPath,
		const FProperty* InSourceProperty,
		const uint8* InSourcePtr,
		const FRigVMPropertyPath& InSourcePropertyPath);

	static bool CopyProperty(
		URigVMMemoryStorage* InTargetStorage,
		int32 InTargetPropertyIndex,
		const FRigVMPropertyPath& InTargetPropertyPath,
		URigVMMemoryStorage* InSourceStorage,
		int32 InSourcePropertyIndex,
		const FRigVMPropertyPath& InSourcePropertyPath);

#if !UE_RIGVM_UCLASS_BASED_STORAGE_DISABLED
	
	static bool CopyProperty(
		FRigVMMemoryHandle& TargetHandle,
		FRigVMMemoryHandle& SourceHandle);
	
#endif

protected:

	static FProperty* AddProperty(UClass* InClass, const FPropertyDescription& InProperty, bool bPurge = false, bool bLink = true, FField** LinkToProperty = nullptr);

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
