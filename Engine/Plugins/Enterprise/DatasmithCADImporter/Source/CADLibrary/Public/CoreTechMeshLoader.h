// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#ifdef CAD_LIBRARY
#include "CoreTechTypes.h"
#include "CoreTechHelper.h"

#include <vector>

class GPMMesh;
class GPUVSetData;
class GPMFace;
class GPMatrixH;


namespace CADLibrary
{

	class CADLIBRARY_API CoreTechMeshLoader
	{
	public:
		/**
		 * Load a .ct file and populate the CoreTech mesh with the loaded geometry
		 * @note All input are assumed to be valid
		 */
		bool LoadFile(const FString& FileName, FMeshDescription& MeshDescription, const FImportParameters& ImportParameters, FMeshParameters& MeshParameters);

	private:
		void ExtractComponent( CT_OBJECT_ID ObjectID );
		void ExtractInstance( CT_OBJECT_ID ObjectID );
		void ExtractSolid( CT_OBJECT_ID ObjectID );
		void ExtractBranch( CT_OBJECT_ID ObjectID );
		void ExtractLeaf( CT_OBJECT_ID ObjectID );
		void ExtractBody( CT_OBJECT_ID ObjectID );
		void ExtractShell( CT_OBJECT_ID ObjectID );
		void ExtractFace( CT_OBJECT_ID ObjectID );
		void ExtractLoop( CT_OBJECT_ID ObjectID );
		void ExtractCoedge( CT_OBJECT_ID ObjectID );
		void ExtractCurve( CT_OBJECT_ID ObjectID );
		void ExtractPoint( CT_OBJECT_ID ObjectID );

	private:
		TColorMap ColorMap;
		TArray<CT_OBJECT_ID> BodySet;
	};

} // ns CADLibrary

#endif // CAD_LIBRARY
