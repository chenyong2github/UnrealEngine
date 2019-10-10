// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "ClothLODDataNv.h"

UClothLODDataNv::UClothLODDataNv(const FObjectInitializer& Init)
	: Super(Init)
{
	PhysicalMeshData = Init.CreateDefaultSubobject<UClothPhysicalMeshDataNv>(this, FName("ClothPhysicalMeshDataNv"));
}

UClothLODDataNv::~UClothLODDataNv()
{}
