// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RigVMDefines.h"

#if UE_RIGVM_DEBUG_TYPEINDEX
class RIGVM_API TRigVMTypeIndex
{
public:
	
	TRigVMTypeIndex()
		: Name(NAME_None)
		, Index(INDEX_NONE)
	{}

	TRigVMTypeIndex(int32 InIndex)
		: Name(NAME_None)
		, Index(InIndex)
	{}

	int32 GetIndex() const { return Index; }
	const FName& GetName() const { return Name; }

	operator int() const
	{
		return Index;
	}

	bool operator ==(const TRigVMTypeIndex& Other) const
	{
		return Index == Other.Index;
	}

	bool operator ==(const int32& Other) const
	{
		return Index == Other;
	}

	bool operator !=(const TRigVMTypeIndex& Other) const
	{
		return Index != Other.Index;
	}

	bool operator !=(const int32& Other) const
	{
		return Index != Other;
	}

	bool operator >(const TRigVMTypeIndex& Other) const
	{
		return Index > Other.Index;
	}

	bool operator >(const int32& Other) const
	{
		return Index > Other;
	}

	bool operator <(const TRigVMTypeIndex& Other) const
	{
		return Index < Other.Index;
	}

	bool operator <(const int32& Other) const
	{
		return Index < Other;
	}

	friend uint32 GetTypeHash(const TRigVMTypeIndex& InIndex)
	{
		return GetTypeHash(InIndex.Index);
	}

protected:
	FName Name;
	int32 Index;

	friend struct FRigVMRegistry; 
};
#else
typedef int32 TRigVMTypeIndex;
#endif
