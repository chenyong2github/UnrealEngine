// Copyright Epic Games, Inc. All Rights Reserved.

#include "Exporters/GLTFSkeletalMeshExporter.h"
#include "Engine/SkeletalMesh.h"

UGLTFSkeletalMeshExporter::UGLTFSkeletalMeshExporter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// TODO: uncomment when support is implemented
	// SupportedClass = USkeletalMesh::StaticClass();
}

bool UGLTFSkeletalMeshExporter::AddObject(FGLTFContainerBuilder& Builder, const UObject* Object)
{
	const USkeletalMesh* SkeletalMesh = CastChecked<USkeletalMesh>(Object);
	// TODO: implement
	return true;
}
