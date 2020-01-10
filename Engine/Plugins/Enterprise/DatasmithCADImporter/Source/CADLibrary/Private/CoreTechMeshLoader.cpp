// Copyright Epic Games, Inc. All Rights Reserved.
#include "CoreTechMeshLoader.h"

#ifdef CAD_LIBRARY
#include "CoreTechTypes.h"

namespace CADLibrary
{

bool CoreTechMeshLoader::LoadFile(const FString& FileName, FMeshDescription& MeshDescription, const FImportParameters& ImportParameters, FMeshParameters& MeshParameters)
{
	CoreTechSessionBase Session(TEXT("CoreTechMeshLoader::LoadFile"), ImportParameters.MetricUnit);
	if (!Session.IsSessionValid())
	{
		return false;
	}

	CT_FLAGS LoadingFlags = CT_LOAD_FLAGS_USE_DEFAULT | CT_LOAD_FLAGS_READ_META_DATA;
	CT_IO_ERROR Result = IO_OK;

	CT_OBJECT_ID MainObjectID;
	Result = CTKIO_LoadFile(*FileName, MainObjectID, LoadingFlags);

	// Something wrong happened during the load, abort
	if (Result != IO_OK)
	{
		return false;
	}

	Result = Repair(MainObjectID, ImportParameters.StitchingTechnique);
	Result = SetCoreTechTessellationState(ImportParameters);

	bool bNeedSwapOrientation = ImportParameters.StitchingTechnique != EStitchingTechnique::StitchingSew ? MeshParameters.bNeedSwapOrientation : false;

	// Parse CoreTech model's hierarchy to collect body objects.
	ExtractComponent(MainObjectID);

	// Nothing useful in the file, abort
	if (BodySet.Num() == 0)
	{
		return false;
	}

	Tessellate(MainObjectID, ImportParameters, MeshDescription, MeshParameters);
	return true;
}

void CoreTechMeshLoader::ExtractComponent( CT_OBJECT_ID ObjectID )
{
	if( !CT_OBJECT_IO::IsA(ObjectID, CT_COMPONENT_TYPE) )
	{
		return;
	}

	if( ! CT_OBJECT_IO::IsVisible( ObjectID ))
	{
		return;
	}

	CT_LIST_IO ObjectList;
	CT_IO_ERROR Error = CT_COMPONENT_IO::AskChildren(ObjectID, ObjectList);
	if (Error != IO_OK || ObjectList.IsEmpty())
	{
		return;
	}

	ObjectList.IteratorInitialize();

	CT_OBJECT_ID ChildID;
	while( (ChildID = ObjectList.IteratorIter()) != 0 )
	{
		if (!CT_OBJECT_IO::IsVisible(ChildID))
		{
			continue;
		}

		if (CT_OBJECT_IO::IsA( ChildID, CT_INSTANCE_TYPE ))
		{
			ExtractInstance( ChildID );
		}
		else if (CT_OBJECT_IO::IsA( ChildID, CT_SOLID_TYPE ))
		{
			ExtractSolid( ChildID );
		}
		else if (CT_OBJECT_IO::IsA( ChildID, CT_BODY_TYPE ))
		{
			ExtractBody( ChildID );
		}
		else if (CT_OBJECT_IO::IsA( ChildID, CT_CURVE_TYPE ))
		{
			ExtractCurve( ChildID );
		}
		else if (CT_OBJECT_IO::IsA( ChildID, CT_POINT_TYPE ) )
		{
			ExtractPoint( ChildID );
		}
	}
}

void CoreTechMeshLoader::ExtractInstance( CT_OBJECT_ID ObjectID )
{
	if (!CT_OBJECT_IO::IsA(ObjectID, CT_INSTANCE_TYPE))
	{
		return;
	}

	CT_DOUBLE Matrix[16] =
	{
		1., 0., 0., 0.,
		0., 1., 0., 0.,
		0., 0., 1., 0.,
		0., 0., 0., 1.
	};

	CT_IO_ERROR Error = CT_INSTANCE_IO::AskTransformation( ObjectID, Matrix, CT_MATRIX_ALL );

	CT_OBJECT_ID ChildID;
	Error = CT_INSTANCE_IO::AskComponentInstanceLevel( ObjectID, ChildID );

	if( Error == IO_OK && ChildID != 0 )
	{
		// There is geometry altered by assembly level operations
	}
	else
	{
		// Get the default children
		Error = CT_INSTANCE_IO::AskChild( ObjectID, ChildID );
	}

	if (ChildID == 0)
	{
		return;
	}

	if (CT_OBJECT_IO::IsA( ChildID, CT_UNLOADED_PART_TYPE ))
	{
		return;
	}

	//CHECK_ERROR_NO_LOG(Error);
	if (Error == IO_OK && ChildID != 0)
	{
		ExtractComponent( ChildID);
	}
}

void CoreTechMeshLoader::ExtractSolid( CT_OBJECT_ID ObjectID )
{
	if (!CT_OBJECT_IO::IsA(ObjectID, CT_SOLID_TYPE))
	{
		return;
	}

	CT_LIST_IO BodyList;
	CT_IO_ERROR Error = CT_SOLID_IO::AskResultBodies( ObjectID, BodyList );
	if (Error != IO_OK)
	{
		return;
	}

	if( BodyList.Count() > 0 )
	{
		BodyList.IteratorInitialize();

		CT_OBJECT_ID BodyID;
		while ( (BodyID = BodyList.IteratorIter()) != 0 )
		{
			ExtractBody(BodyID);
		}
	}
	else
	{
		const CT_UINT32 NodeCount = CT_SOLID_IO::AskNodesCount( ObjectID );

		for( CT_UINT32 NodeIndex = 0; NodeIndex < NodeCount; ++NodeIndex )
		{
			CT_OBJECT_ID NodeID;
			if( CT_SOLID_IO::AskIthNode( ObjectID, NodeIndex, NodeID) == IO_OK )
			{
				ExtractBranch( NodeID );
			}
		}
	}
}

void CoreTechMeshLoader::ExtractBranch( CT_OBJECT_ID ObjectID )
{
	if (!CT_OBJECT_IO::IsA(ObjectID, CT_HBRANCH_TYPE))
	{
		return;
	}

	CT_UINT32 NodeCount = CT_HBRANCH_IO::AskNodesCount( ObjectID );

	for( CT_UINT32 NodeIndex = 0; NodeIndex < NodeCount; ++NodeIndex )
	{
		CT_OBJECT_ID NodeID;
		if( CT_HBRANCH_IO::AskIthNode( ObjectID, NodeIndex, NodeID ) == IO_OK )
		{
			if( CT_OBJECT_IO::IsA( NodeID, CT_HBRANCH_TYPE ) )
			{
				ExtractBranch( NodeID );
			}
			else
			{
				ExtractLeaf( NodeID );
			}
		}
	}
}

void CoreTechMeshLoader::ExtractLeaf( CT_OBJECT_ID ObjectID )
{
	if (!CT_OBJECT_IO::IsA(ObjectID, CT_HLEAF_TYPE))
	{
		return;
	}

	if( CT_HLEAF_IO::IsOfType( ObjectID, "GEOMETRY" ) )
	{
		CT_OBJECT_ID  GeomID = 0;

		CT_IO_ERROR Error = CT_HLEAF_IO::AskGeometry( ObjectID, GeomID );

		if( Error == IO_OK && GeomID != 0 )
		{
			if (CT_OBJECT_IO::IsA(GeomID, CT_BODY_TYPE))
			{
				ExtractBody( GeomID );
			}
			else if( CT_OBJECT_IO::IsA( GeomID, CT_SHELL_TYPE ) )
			{
				ExtractShell( GeomID );
			}
			else if( CT_OBJECT_IO::IsA( GeomID, CT_FACE_TYPE ) )
			{
				ExtractFace( GeomID );
			}
			else if( CT_OBJECT_IO::IsA( GeomID, CT_CURVE_TYPE ) )
			{
				ExtractCurve( GeomID );
			}
			else if( CT_OBJECT_IO::IsA( GeomID, CT_POINT_TYPE ) )
			{
				ExtractPoint( GeomID );
			}
		}
	}
}

void CoreTechMeshLoader::ExtractBody( CT_OBJECT_ID ObjectID )
{
	if (!CT_OBJECT_IO::IsA(ObjectID, CT_BODY_TYPE))
	{
		return;
	}

	BodySet.Add(ObjectID);
}

void CoreTechMeshLoader::ExtractShell( CT_OBJECT_ID ObjectID )
{
	if (!CT_OBJECT_IO::IsA(ObjectID, CT_SHELL_TYPE))
	{
		return;
	}
}

void CoreTechMeshLoader::ExtractFace( CT_OBJECT_ID ObjectID )
{
	if (!CT_OBJECT_IO::IsA(ObjectID, CT_FACE_TYPE))
	{
		return;
	}

}

void CoreTechMeshLoader::ExtractLoop( CT_OBJECT_ID ObjectID )
{
	if (!CT_OBJECT_IO::IsA(ObjectID, CT_LOOP_TYPE))
	{
		return;
	}
}

void CoreTechMeshLoader::ExtractCoedge( CT_OBJECT_ID ObjectID )
{
	if (!CT_OBJECT_IO::IsA(ObjectID, CT_COEDGE_TYPE))
	{
		return;
	}
}

void CoreTechMeshLoader::ExtractCurve( CT_OBJECT_ID ObjectID )
{
	if (!CT_OBJECT_IO::IsA(ObjectID, CT_CURVE_TYPE))
	{
		return;
	}
}

void CoreTechMeshLoader::ExtractPoint( CT_OBJECT_ID ObjectID )
{
}

} // ns CADLibrary

#endif // CAD_LIBRARY
