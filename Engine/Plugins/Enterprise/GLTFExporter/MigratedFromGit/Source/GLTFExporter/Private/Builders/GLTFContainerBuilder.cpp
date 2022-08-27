// Copyright Epic Games, Inc. All Rights Reserved.

#include "Builders/GLTFContainerBuilder.h"
#include "Builders/GLTFContainerUtility.h"

FGLTFContainerBuilder::FGLTFContainerBuilder(const FString& FilePath, const UGLTFExportOptions* ExportOptions, bool bSelectedActorsOnly)
	: FGLTFConvertBuilder(FilePath, ExportOptions, bSelectedActorsOnly)
{
}

void FGLTFContainerBuilder::WriteGlb(FArchive& Archive) const
{
	FBufferArchive JsonData;
	WriteJson(JsonData);

	const TArray<uint8>& BufferData = GetBufferData();

	FGLTFContainerUtility::WriteGlb(Archive, JsonData, BufferData);
}

void FGLTFContainerBuilder::Write(FArchive& Archive, FFeedbackContext* Context)
{
	CompleteAllTasks(Context);

	if (bIsGlbFile)
	{
		WriteGlb(Archive);
	}
	else
	{
		WriteJson(Archive);
	}
}
