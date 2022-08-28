// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Builders/GLTFConvertBuilder.h"

class FGLTFContainerBuilder : public FGLTFConvertBuilder
{
public:

	FGLTFContainerBuilder(const FString& FilePath, const UGLTFExportOptions* ExportOptions, bool bSelectedActorsOnly);

	void Write(FArchive& Archive, FFeedbackContext* Context = GWarn);

private:

	void WriteGlb(FArchive& Archive) const;

	void BundleWebViewer();

	void UpdateWebViewerIndex();

	static bool ReadJsonFile(const FString& FilePath, TSharedPtr<FJsonObject>& JsonObject);
	static bool WriteJsonFile(const FString& FilePath, const TSharedRef<FJsonObject>& JsonObject);
};
