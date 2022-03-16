// Copyright Epic Games, Inc. All Rights Reserved.

//=============================================================================
// TextureExporterGeneric
//=============================================================================

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Exporters/Exporter.h"
#include "TextureExporterGeneric.generated.h"

UCLASS()
class UTextureExporterGeneric : public UExporter
{
	GENERATED_UCLASS_BODY()

	virtual bool SupportsObject(UObject* Object) const override;
	virtual int32 GetFileCount(UObject* Object) const override;
	virtual FString GetUniqueFilename(const TCHAR* Filename, int32 FileIndex, int32 FileCount) override;
	virtual bool ExportBinary(UObject* Object, const TCHAR* Type, FArchive& Ar, FFeedbackContext* Warn, int32 FileIndex = 0, uint32 PortFlags = 0) override;
	
	// derived class exporter must implement :
	virtual bool SupportsTexture(UTexture2D* Texture) const { return false; };

protected:
	class UTexture2D* GetExportTexture(UObject* Object) const;
};

