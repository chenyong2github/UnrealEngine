// Copyright Epic Games, Inc. All Rights Reserved.

#include "GLTFSkeletalMeshExporter.h"
#include "Engine/SkeletalMesh.h"

UGLTFSkeletalMeshExporter::UGLTFSkeletalMeshExporter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SupportedClass = USkeletalMesh::StaticClass();
}

bool UGLTFSkeletalMeshExporter::ExportBinary(UObject* Object, const TCHAR* Type, FArchive& Archive, FFeedbackContext* Warn, int32 FileIndex, uint32 PortFlags)
{
	USkeletalMesh* SkeletalMesh = CastChecked<USkeletalMesh>(Object);

	if (!FillExportOptions())
	{
		// User cancelled the export
		return false;
	}

	// TODO: implement
	return true;
}
