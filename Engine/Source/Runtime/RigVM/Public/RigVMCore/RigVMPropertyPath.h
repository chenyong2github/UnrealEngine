// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/UnrealType.h"

struct RIGVM_API FRigVMPropertyPathDescription
{
	int32 PropertyIndex;
	FString RootCPPType;
	FString SegmentPath;
	
	FRigVMPropertyPathDescription()
		: PropertyIndex(INDEX_NONE)
		, RootCPPType()
		, SegmentPath()
	{}

	FRigVMPropertyPathDescription(int32 InPropertyIndex, const FString& InRootCPPType, const FString& InSegmentPath)
		: PropertyIndex(InPropertyIndex)
		, RootCPPType(InRootCPPType)
		, SegmentPath(InSegmentPath)
	{}

	FORCEINLINE friend FArchive& operator<<(FArchive& Ar, FRigVMPropertyPathDescription& Path)
	{
		Ar << Path.PropertyIndex;
		Ar << Path.RootCPPType;
		Ar << Path.SegmentPath;
		return Ar;
	}
};

enum RIGVM_API ERigVMPropertyPathSegmentType
{
	StructMember,
	ArrayElement,
	MapValue
};

struct RIGVM_API FRigVMPropertyPathSegment
{
	ERigVMPropertyPathSegmentType Type;
	FName Name;
	int32 Index;
	const FProperty* Property;
};

class RIGVM_API FRigVMPropertyPath
{
public:

	FRigVMPropertyPath();
	FRigVMPropertyPath(const FProperty* InProperty, const FString& InSegmentPath);
	FRigVMPropertyPath(const FRigVMPropertyPath& InOther);

	FORCEINLINE const FString& ToString() const { return Path; }
	FORCEINLINE int32 Num() const { return Segments.Num(); }
	FORCEINLINE bool IsValid() const { return Num() > 0; }
	FORCEINLINE bool IsEmpty() const { return Num() == 0; }
	FORCEINLINE const FRigVMPropertyPathSegment& operator[](int32 InIndex) const { return Segments[InIndex]; }
	bool IsDirect() const;

	FORCEINLINE bool operator ==(const FRigVMPropertyPath& Other) const
	{
		return GetTypeHash(this) == GetTypeHash(Other);
	}

	FORCEINLINE bool operator !=(const FRigVMPropertyPath& Other) const
	{
		return GetTypeHash(this) != GetTypeHash(Other);
	}

	FORCEINLINE bool operator >(const FRigVMPropertyPath& Other) const
	{
		return GetTypeHash(this) > GetTypeHash(Other);
	}

	FORCEINLINE bool operator <(const FRigVMPropertyPath& Other) const
	{
		return GetTypeHash(this) < GetTypeHash(Other);
	}

	template<typename T>
	FORCEINLINE T* GetData(uint8* InPtr, const FProperty* InProperty) const
	{
		return (T*)GetData_Internal(InPtr, InProperty);
	}

	friend FORCEINLINE uint32 GetTypeHash(const FRigVMPropertyPath& InPropertyPath)
	{
		if(InPropertyPath.IsEmpty())
		{
			return 0;
		}
		
		return HashCombine(
			GetTypeHash(InPropertyPath[0].Property),
			GetTypeHash(InPropertyPath.ToString())
		);
	}

	const FProperty* GetTargetProperty() const;
	
	static FRigVMPropertyPath Empty;

private:

	uint8* GetData_Internal(uint8* InPtr, const FProperty* InProperty) const;

	FString Path;
	TArray<FRigVMPropertyPathSegment> Segments;
};

