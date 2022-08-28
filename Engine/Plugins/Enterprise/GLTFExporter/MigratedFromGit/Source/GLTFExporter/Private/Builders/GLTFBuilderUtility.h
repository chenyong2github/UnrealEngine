// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine.h"

struct FGLTFBuilderUtility
{
	static FString GetMeshName(const UStaticMesh* StaticMesh, int32 LODIndex)
	{
		FString Name;

		if (StaticMesh != nullptr)
		{
			StaticMesh->GetName(Name);
			if (LODIndex != 0) Name += TEXT("_LOD") + FString::FromInt(LODIndex);
		}

		return Name;
	}

	template <typename ElementType, typename AllocatorType>
	static TArray<const ElementType*, AllocatorType> MakeArrayOfPointersConst(const TArray<ElementType*, AllocatorType>& ArrayOfPointers)
	{
		TArray<const ElementType*, AllocatorType> ArrayOfConstPointers;
		ArrayOfConstPointers.AddUninitialized(ArrayOfPointers.Num());

		FMemory::Memcpy(ArrayOfConstPointers.GetData(), ArrayOfPointers.GetData(), ArrayOfPointers.Num() * sizeof(ElementType*));
		return ArrayOfConstPointers;
	}
};
