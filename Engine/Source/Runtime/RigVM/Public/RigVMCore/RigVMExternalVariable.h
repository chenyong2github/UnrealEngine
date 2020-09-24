// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RigVMTraits.h"
#include "RigVMMemory.h"

/**
 * The external variable can be used to map external / unowned
 * memory into the VM and back out
 */
struct RIGVM_API FRigVMExternalVariable
{
	FORCEINLINE FRigVMExternalVariable()
		: Name(NAME_None)
		, TypeName(NAME_None)
		, TypeObject(nullptr)
		, bIsArray(false)
		, bIsPublic(false)
		, bIsReadOnly(false)
		, Size(0)
		, Memory(nullptr)
		
	{
	}

	FORCEINLINE static FRigVMExternalVariable Make(FProperty* InProperty, UObject* InContainer)
	{
		check(InProperty);

		FProperty* Property = InProperty;

		FRigVMExternalVariable ExternalVariable;
		ExternalVariable.Name = InProperty->GetFName();
		ExternalVariable.bIsPublic = !InProperty->HasAllPropertyFlags(CPF_DisableEditOnInstance);
		ExternalVariable.bIsReadOnly = InProperty->HasAllPropertyFlags(CPF_BlueprintReadOnly);

		if (InContainer)
		{
			ExternalVariable.Memory = (uint8*)Property->ContainerPtrToValuePtr<uint8>(InContainer);
		}

		FString TypePrefix, TypeSuffix;
		if (FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
		{
			ExternalVariable.bIsArray = true;
			TypePrefix = TEXT("TArray<");
			TypeSuffix = TEXT(">");
			Property = ArrayProperty->Inner;
		}

		ExternalVariable.Size = Property->GetSize();

		if (CastField<FBoolProperty>(Property))
		{
			ExternalVariable.TypeName = TEXT("bool");
		}
		else if (CastField<FIntProperty>(Property))
		{
			ExternalVariable.TypeName = TEXT("int32");
		}
		else if (CastField<FFloatProperty>(Property))
		{
			ExternalVariable.TypeName = TEXT("float");
		}
		else if (CastField<FStrProperty>(Property))
		{
			ExternalVariable.TypeName = TEXT("FString");
		}
		else if (CastField<FNameProperty>(Property))
		{
			ExternalVariable.TypeName = TEXT("FName");
		}
		else if (FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property))
		{
			ExternalVariable.TypeName = EnumProperty->GetEnum()->GetFName();
			ExternalVariable.TypeObject = EnumProperty->GetEnum();
		}
		else if (FByteProperty* ByteProperty = CastField<FByteProperty>(Property))
		{
			if (UEnum* BytePropertyEnum = ByteProperty->Enum)
			{
				ExternalVariable.TypeName = BytePropertyEnum->GetFName();
				ExternalVariable.TypeObject = BytePropertyEnum;
			}
		}
		else if (FStructProperty* StructProperty = CastField<FStructProperty>(Property))
		{
			ExternalVariable.TypeName = *StructProperty->Struct->GetStructCPPName();
			ExternalVariable.TypeObject = StructProperty->Struct;
		}

		return ExternalVariable;
	}

	FORCEINLINE static FRigVMExternalVariable Make(const FName& InName, bool& InValue)
	{
		FRigVMExternalVariable Variable;
		Variable.Name = InName;
		Variable.TypeName = TEXT("bool");
		Variable.TypeObject = nullptr;
		Variable.bIsArray = false;
		Variable.Size = sizeof(bool);
		Variable.Memory = (uint8*)&InValue;
		return Variable;
	}

	FORCEINLINE static FRigVMExternalVariable Make(const FName& InName, TArray<bool>& InValue)
	{
		FRigVMExternalVariable Variable;
		Variable.Name = InName;
		Variable.TypeName = TEXT("bool");
		Variable.TypeObject = nullptr;
		Variable.bIsArray = true;
		Variable.Size = sizeof(bool);
		Variable.Memory = (uint8*)&InValue;
		return Variable;
	}

	FORCEINLINE static FRigVMExternalVariable Make(const FName& InName, int32& InValue)
	{
		FRigVMExternalVariable Variable;
		Variable.Name = InName;
		Variable.TypeName = TEXT("int32");
		Variable.TypeObject = nullptr;
		Variable.bIsArray = false;
		Variable.Size = sizeof(int32);
		Variable.Memory = (uint8*)&InValue;
		return Variable;
	}

	FORCEINLINE static FRigVMExternalVariable Make(const FName& InName, TArray<int32>& InValue)
	{
		FRigVMExternalVariable Variable;
		Variable.Name = InName;
		Variable.TypeName = TEXT("int32");
		Variable.TypeObject = nullptr;
		Variable.bIsArray = true;
		Variable.Size = sizeof(int32);
		Variable.Memory = (uint8*)&InValue;
		return Variable;
	}

	FORCEINLINE static FRigVMExternalVariable Make(const FName& InName, uint8& InValue)
	{
		FRigVMExternalVariable Variable;
		Variable.Name = InName;
		Variable.TypeName = TEXT("uint8");
		Variable.TypeObject = nullptr;
		Variable.bIsArray = false;
		Variable.Size = sizeof(uint8);
		Variable.Memory = (uint8*)&InValue;
		return Variable;
	}

	FORCEINLINE static FRigVMExternalVariable Make(const FName& InName, TArray<uint8>& InValue)
	{
		FRigVMExternalVariable Variable;
		Variable.Name = InName;
		Variable.TypeName = TEXT("uint8");
		Variable.TypeObject = nullptr;
		Variable.bIsArray = true;
		Variable.Size = sizeof(uint8);
		Variable.Memory = (uint8*)&InValue;
		return Variable;
	}

	FORCEINLINE static FRigVMExternalVariable Make(const FName& InName, float& InValue)
	{
		FRigVMExternalVariable Variable;
		Variable.Name = InName;
		Variable.TypeName = TEXT("float");
		Variable.TypeObject = nullptr;
		Variable.bIsArray = false;
		Variable.Size = sizeof(float);
		Variable.Memory = (uint8*)&InValue;
		return Variable;
	}

	FORCEINLINE static FRigVMExternalVariable Make(const FName& InName, TArray<float>& InValue)
	{
		FRigVMExternalVariable Variable;
		Variable.Name = InName;
		Variable.TypeName = TEXT("float");
		Variable.TypeObject = nullptr;
		Variable.bIsArray = true;
		Variable.Size = sizeof(float);
		Variable.Memory = (uint8*)&InValue;
		return Variable;
	}

	FORCEINLINE static FRigVMExternalVariable Make(const FName& InName, FString& InValue)
	{
		FRigVMExternalVariable Variable;
		Variable.Name = InName;
		Variable.TypeName = TEXT("FString");
		Variable.TypeObject = nullptr;
		Variable.bIsArray = false;
		Variable.Size = sizeof(FString);
		Variable.Memory = (uint8*)&InValue;
		return Variable;
	}

	FORCEINLINE static FRigVMExternalVariable Make(const FName& InName, TArray<FString>& InValue)
	{
		FRigVMExternalVariable Variable;
		Variable.Name = InName;
		Variable.TypeName = TEXT("FString");
		Variable.TypeObject = nullptr;
		Variable.bIsArray = true;
		Variable.Size = sizeof(FString);
		Variable.Memory = (uint8*)&InValue;
		return Variable;
	}

	FORCEINLINE static FRigVMExternalVariable Make(const FName& InName, FName& InValue)
	{
		FRigVMExternalVariable Variable;
		Variable.Name = InName;
		Variable.TypeName = TEXT("FName");
		Variable.TypeObject = nullptr;
		Variable.bIsArray = false;
		Variable.Size = sizeof(FName);
		Variable.Memory = (uint8*)&InValue;
		return Variable;
	}

	FORCEINLINE static FRigVMExternalVariable Make(const FName& InName, TArray<FName>& InValue)
	{
		FRigVMExternalVariable Variable;
		Variable.Name = InName;
		Variable.TypeName = TEXT("FName");
		Variable.TypeObject = nullptr;
		Variable.bIsArray = true;
		Variable.Size = sizeof(FName);
		Variable.Memory = (uint8*)&InValue;
		return Variable;
	}

	template <
		typename T,
		typename TEnableIf<TIsEnum<T>::Value>::Type* = nullptr
	>
	FORCEINLINE static FRigVMExternalVariable Make(const FName& InName, T& InValue)
	{
		FRigVMExternalVariable Variable;
		Variable.Name = InName;
		Variable.TypeName = StaticEnum<T>()->GetFName();
		Variable.TypeObject = StaticEnum<T>();
		Variable.bIsArray = false;
		Variable.Size = sizeof(T);
		Variable.Memory = (uint8*)&InValue;
		return Variable;
	}

	template <
		typename T,
		typename TEnableIf<TIsEnum<T>::Value>::Type* = nullptr
	>
	FORCEINLINE static FRigVMExternalVariable Make(const FName& InName, TArray<T>& InValue)
	{
		FRigVMExternalVariable Variable;
		Variable.Name = InName;
		Variable.TypeName = StaticEnum<T>()->GetFName();
		Variable.TypeObject = StaticEnum<T>();
		Variable.bIsArray = true;
		Variable.Size = sizeof(T);
		Variable.Memory = (uint8*)&InValue;
		return Variable;
	}

	template <
		typename T,
		typename TEnableIf<TRigVMIsBaseStructure<T>::Value, T>::Type* = nullptr
	>
	FORCEINLINE static FRigVMExternalVariable Make(const FName& InName, T& InValue)
	{
		FRigVMExternalVariable Variable;
		Variable.Name = InName;
		Variable.TypeName = TBaseStructure<T>::Get()->GetFName();
		Variable.TypeObject = TBaseStructure<T>::Get();
		Variable.bIsArray = false;
		Variable.Size = TBaseStructure<T>::Get()->GetStructureSize();
		Variable.Memory = (uint8*)&InValue;
		return Variable;
	}

	template <
		typename T,
		typename TEnableIf<TRigVMIsBaseStructure<T>::Value, T>::Type* = nullptr
	>
	FORCEINLINE static FRigVMExternalVariable Make(const FName& InName, TArray<T>& InValue)
	{
		FRigVMExternalVariable Variable;
		Variable.Name = InName;
		Variable.TypeName = TBaseStructure<T>::Get()->GetFName();
		Variable.TypeObject = TBaseStructure<T>::Get();
		Variable.bIsArray = true;
		Variable.Size = TBaseStructure<T>::Get()->GetStructureSize();
		Variable.Memory = (uint8*)&InValue;
		return Variable;
	}

	template <
		typename T,
		typename TEnableIf<TModels<CRigVMUStruct, T>::Value>::Type * = nullptr
	>
	FORCEINLINE static FRigVMExternalVariable Make(const FName& InName, T& InValue)
	{
		FRigVMExternalVariable Variable;
		Variable.Name = InName;
		Variable.TypeName = T::StaticStruct()->GetFName();
		Variable.TypeObject = T::StaticStruct();
		Variable.bIsArray = false;
		Variable.Size = T::StaticStruct()->GetStructureSize();
		Variable.Memory = (uint8*)&InValue;
		return Variable;
	}

	template <
		typename T,
		typename TEnableIf<TModels<CRigVMUStruct, T>::Value>::Type * = nullptr
	>
	FORCEINLINE static FRigVMExternalVariable Make(const FName& InName, TArray<T>& InValue)
	{
		FRigVMExternalVariable Variable;
		Variable.Name = InName;
		Variable.TypeName = T::StaticStruct()->GetFName();
		Variable.TypeObject = T::StaticStruct();
		Variable.bIsArray = true;
		Variable.Size = T::StaticStruct()->GetStructureSize();
		Variable.Memory = (uint8*)&InValue;
		return Variable;
	}

	template<typename T>
	FORCEINLINE T GetValue()
	{
		ensure(IsValid() && !bIsArray);
		return *(T*)Memory;
	}

	template<typename T>
	FORCEINLINE void SetValue(const T& InValue)
	{
		ensure(IsValid() && !bIsArray);
		(*(T*)Memory) = InValue;
	}

	template<typename T>
	FORCEINLINE TArray<T> GetArray()
	{
		ensure(IsValid() && bIsArray);
		return *(TArray<T>*)Memory;
	}

	template<typename T>
	FORCEINLINE void SetArray(const TArray<T>& InValue)
	{
		ensure(IsValid() && bIsArray);
		(*(TArray<T>*)Memory) = InValue;
	}

	FORCEINLINE bool IsValid(bool bAllowNullPtr = false) const
	{
		return Name.IsValid() && 
			!Name.IsNone() &&
			TypeName.IsValid() &&
			!TypeName.IsNone() &&
			(bAllowNullPtr || Memory != nullptr);
	}

	FORCEINLINE FRigVMMemoryHandle GetHandle(int32 InOffset = INDEX_NONE) const
	{
		return FRigVMMemoryHandle(
			Memory,
			Size,
			bIsArray ? FRigVMMemoryHandle::FType::Dynamic : FRigVMMemoryHandle::FType::Plain,
			InOffset
		);
	}

	FName Name;
	FName TypeName;
	UObject* TypeObject;
	bool bIsArray;
	bool bIsPublic;
	bool bIsReadOnly;
	int32 Size;
	uint8* Memory;
};
