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

	FORCEINLINE_DEBUGGABLE FRigVMMemoryHandle(uint8* InPtr, const FProperty* InProperty,  const FRigVMPropertyPath* InPropertyPath)
		: Ptr(InPtr)
		, Property(InProperty)
		, PropertyPath(InPropertyPath)
	{
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

	FORCEINLINE const FRigVMPropertyPath& GetPropertyPathRef() const
	{
		if(PropertyPath)
		{
			return *PropertyPath;
		}
		return EmptyPropertyPath;
	}

private:

	FORCEINLINE_DEBUGGABLE uint8* GetData_Internal(bool bFollowPropertyPath = false) const
	{
		if(bFollowPropertyPath && PropertyPath != nullptr)
		{
			return PropertyPath->GetData<uint8>(Ptr, Property);
		}
		return Ptr;
	}
	
	uint8* Ptr;
	const FProperty* Property;
	const FRigVMPropertyPath* PropertyPath;
	static const FRigVMPropertyPath EmptyPropertyPath;

	friend class URigVM;
};

#endif

//////////////////////////////////////////////////////////////////////////////
/// Property Management
//////////////////////////////////////////////////////////////////////////////

struct RIGVM_API FRigVMPropertyDescription
{
	FName Name;
	const FProperty* Property;
	FString CPPType;
	UObject* CPPTypeObject;
	TArray<EPinContainerType> Containers;
	FString DefaultValue;

	FRigVMPropertyDescription()
		: Name(NAME_None)
		, Property(nullptr)
		, CPPType()
		, CPPTypeObject(nullptr)
		, Containers()
		, DefaultValue()
	{}
	FRigVMPropertyDescription(const FProperty* InProperty, const FString& InDefaultValue, const FName& InName = NAME_None);
	FRigVMPropertyDescription(const FName& InName, const FString& InCPPType, UObject* InCPPTypeObject, const FString& InDefaultValue);

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

UCLASS()
class RIGVM_API URigVMMemoryStorageGeneratorClass :
	public UClass
{
	GENERATED_BODY()

public:

	URigVMMemoryStorageGeneratorClass()
		: Super()
		, MemoryType(ERigVMMemoryType::Literal)
	{}
	
	// UClass overrides
	void PurgeClass(bool bRecompilingOnLoad) override;
	void Link(FArchive& Ar, bool bRelinkExistingProperties) override;
	void Serialize(FArchive& Ar) override;;

	// URigVMMemoryStorageGeneratorClass specific
	static FString GetClassName(ERigVMMemoryType InMemoryType);
	static URigVMMemoryStorageGeneratorClass* GetStorageClass(UObject* InOuter, ERigVMMemoryType InMemoryType);
	static URigVMMemoryStorageGeneratorClass* CreateStorageClass(
		UObject* InOuter,
		ERigVMMemoryType InMemoryType,
		const TArray<FRigVMPropertyDescription>& InProperties,
		const TArray<FRigVMPropertyPathDescription>& InPropertyPaths = TArray<FRigVMPropertyPathDescription>());

	ERigVMMemoryType GetMemoryType() const { return MemoryType; }
	const TArray<const FProperty*>& GetProperties() const { return LinkedProperties; }
	const TArray<FRigVMPropertyPath>& GetPropertyPaths() const { return PropertyPaths; }

protected:

	static FProperty* AddProperty(URigVMMemoryStorageGeneratorClass* InClass, const FRigVMPropertyDescription& InProperty, FField** LinkToProperty = nullptr);

private:

	void RefreshPropertyPaths();

	ERigVMMemoryType MemoryType;
	TArray<const FProperty*> LinkedProperties;
	TArray<FRigVMPropertyPath> PropertyPaths;
	TArray<FRigVMPropertyPathDescription> PropertyPathDescriptions;

	friend class URigVMMemoryStorage;
	friend class URigVMCompiler;
};

UCLASS()
class RIGVM_API URigVMMemoryStorage : public UObject
{
	GENERATED_BODY()

public:

	//////////////////////////////////////////////////////////////////////////////
	/// Memory Access
	//////////////////////////////////////////////////////////////////////////////

	FORCEINLINE ERigVMMemoryType GetMemoryType() const
	{
		if(URigVMMemoryStorageGeneratorClass* Class = Cast<URigVMMemoryStorageGeneratorClass>(GetClass()))
		{
			return Class->GetMemoryType();
		}
		// empty debug containers don't have a generator class
		return ERigVMMemoryType::Debug;
	}
	
	FORCEINLINE int32 Num() const
	{
		return GetProperties().Num();
	}

	FORCEINLINE bool IsValidIndex(int32 InIndex) const
	{
		return GetProperties().IsValidIndex(InIndex);
	}

	const TArray<const FProperty*>& GetProperties() const;
	const TArray<FRigVMPropertyPath>& GetPropertyPaths() const;

	int32 GetPropertyIndex(const FProperty* InProperty) const;

	int32 GetPropertyIndexByName(const FName& InName) const;

	FORCEINLINE const FProperty* GetProperty(int32 InPropertyIndex) const
	{
		return GetProperties()[InPropertyIndex];
	}

	const FProperty* FindPropertyByName(const FName& InName) const;

	FRigVMOperand GetOperand(int32 InPropertyIndex, int32 InPropertyPathIndex = INDEX_NONE) const;
	FRigVMOperand GetOperandByName(const FName& InName, int32 InPropertyPathIndex = INDEX_NONE) const;
	
	FORCEINLINE bool IsArray(int32 InPropertyIndex) const
	{
		return GetProperty(InPropertyIndex)->IsA<FArrayProperty>();
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
		return GetProperty(InPropertyIndex)->IsA<FMapProperty>();
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
		return GetProperty(InPropertyIndex)->IsA<FSetProperty>();
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
		const FProperty* Property = GetProperty(InPropertyIndex);
		return InPropertyPath.GetData<T>(GetData<uint8>(InPropertyIndex), Property);
	}

	template<typename T>
	FORCEINLINE T* GetDataByName(const FName& InName, const FRigVMPropertyPath& InPropertyPath)
	{
		const int32 PropertyIndex = GetPropertyIndexByName(InName);
		return GetData<T>(PropertyIndex, InPropertyPath);
	}

	template<typename T>
	FORCEINLINE T* GetData(const FRigVMOperand& InOperand)
	{
		const int32 PropertyIndex = InOperand.GetRegisterIndex();
		const int32 PropertyPathIndex = InOperand.GetRegisterOffset();

		check(GetProperties().IsValidIndex(PropertyIndex));
		
		if(PropertyPathIndex == INDEX_NONE)
		{
			return GetData<T>(PropertyIndex);
		}

		check(GetPropertyPaths().IsValidIndex(PropertyPathIndex));
		return GetData<T>(PropertyIndex, GetPropertyPaths()[PropertyPathIndex]); 
	}

	template<typename T>
	FORCEINLINE T& GetRef(int32 InPropertyIndex)
	{
		return *GetData<T>(InPropertyIndex);
	}

	template<typename T>
	FORCEINLINE T& GetRefByName(const FName& InName)
	{
		return *GetDataByName<T>(InName);
	}

	template<typename T>
	FORCEINLINE T& GetRef(int32 InPropertyIndex, const FRigVMPropertyPath& InPropertyPath)
	{
		return *GetData<T>(InPropertyIndex, InPropertyPath);
	}

	template<typename T>
	FORCEINLINE T& GetRefByName(const FName& InName, const FRigVMPropertyPath& InPropertyPath)
	{
		return *GetDataByName<T>(InName, InPropertyPath);
	}

	template<typename T>
	FORCEINLINE T& GetRef(const FRigVMOperand& InOperand)
	{
		return *GetData<T>(InOperand);
	}

	FString GetDataAsString(int32 InPropertyIndex);
	FString GetDataAsString(const FRigVMOperand& InOperand);
	
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

private:
	
	static const TArray<const FProperty*> EmptyProperties;
	static const TArray<FRigVMPropertyPath> EmptyPropertyPaths;
};
