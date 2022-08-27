// Copyright Epic Games, Inc. All Rights Reserved.

#include "GLTFStaticMeshExporter.h"
#include "Engine/StaticMesh.h"

UGLTFStaticMeshExporter::UGLTFStaticMeshExporter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SupportedClass = UStaticMesh::StaticClass();
}

bool UGLTFStaticMeshExporter::ExportBinary(UObject* Object, const TCHAR* Type, FArchive& Archive, FFeedbackContext* Warn, int32 FileIndex, uint32 PortFlags)
{
	UStaticMesh* StaticMesh = CastChecked<UStaticMesh>(Object);

	if (!FillExportOptions())
	{
		// User cancelled the export
		return false;
	}

	// TODO: implement
	return true;
}
