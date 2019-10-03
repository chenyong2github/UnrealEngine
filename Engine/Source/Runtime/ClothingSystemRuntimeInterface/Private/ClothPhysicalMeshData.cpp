// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "ClothPhysicalMeshData.h"

UClothPhysicalMeshDataBase::UClothPhysicalMeshDataBase()
	: NumFixedVerts(0)
	, MaxBoneWeights(0)
{}

UClothPhysicalMeshDataBase::~UClothPhysicalMeshDataBase()
{}

void UClothPhysicalMeshDataBase::Reset(const int32 InNumVerts)
{
	Vertices.Reset();
	Normals.Reset();
#if WITH_EDITORONLY_DATA
	VertexColors.Reset();
#endif // #if WITH_EDITORONLY_DATA
	InverseMasses.Reset();
	BoneData.Reset();

	Vertices.AddDefaulted(InNumVerts);
	Normals.AddDefaulted(InNumVerts);
#if WITH_EDITORONLY_DATA
	VertexColors.AddDefaulted(InNumVerts);
#endif //#if WITH_EDITORONLY_DATA

	InverseMasses.AddDefaulted(InNumVerts);
	BoneData.AddDefaulted(InNumVerts);

	NumFixedVerts = 0;
	MaxBoneWeights = 0;
}

TArray<float>* UClothPhysicalMeshDataBase::GetFloatArray(const uint32 Id) const
{
	check(IdToArray.Contains(Id));
	return IdToArray[Id];
}

TArray<uint32> UClothPhysicalMeshDataBase::GetFloatArrayIds() const
{
	TArray<uint32> Keys; 
	IdToArray.GetKeys(Keys);
	return Keys;
}

TArray<TArray<float>*> UClothPhysicalMeshDataBase::GetFloatArrays() const
{
	TArray<TArray<float>*> Values;
	IdToArray.GenerateValueArray(Values);
	return Values;
}

void UClothPhysicalMeshDataBase::RegisterFloatArray(
	const uint32 Id,
	TArray<float> *Array)
{
	check(Id != INDEX_NONE);
	check(Array != nullptr);
	check(!IdToArray.Contains(Id) || IdToArray[Id] == Array);
	IdToArray.Add(Id, Array);
}
