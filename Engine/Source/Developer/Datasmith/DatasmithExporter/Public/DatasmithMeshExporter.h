// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "DatasmithExportOptions.h"
#include "Templates/SharedPointer.h"

class FDatasmithMesh;
class FDatasmithMeshExporterImpl;
class IDatasmithMeshElement;
struct FMeshDescription;

class DATASMITHEXPORTER_API FDatasmithMeshExporter
{
public:
	/**
	 * Exports the DatasmithMesh as a UObject.
	 *
	 * @param Filepath		The path where the resulting file will be written
	 * @param Filename		The name of the file to export to, without any path or extension
	 * @param Mesh	        The mesh to export
	 * @param CollisionMesh An optional collision mesh
	 * @param LightmapUV	The UV generation export option
	 *
	 * @return				A IDatasmithMeshElement that refers to the exported file
	 */
	TSharedPtr< IDatasmithMeshElement > ExportToUObject( const TCHAR* Filepath, const TCHAR* Filename, FDatasmithMesh& Mesh, FDatasmithMesh* CollisionMesh, EDSExportLightmapUV LightmapUV );

	/**
	 * @return The error that happened during the last export, if any
	 */
	FString GetLastError() const { return LastError; }

private:
	void PreExport( FDatasmithMesh& DatasmithMesh, const TCHAR* Filepath, const TCHAR* Filename, EDSExportLightmapUV LightmapUV );
	void PostExport( const FDatasmithMesh& DatasmithMesh, TSharedRef< IDatasmithMeshElement > MeshElement );

	void CreateDefaultUVs( FDatasmithMesh& DatasmithMesh );
	void RegisterStaticMeshAttributes( FMeshDescription& MeshDescription);

	FString LastError;
};
