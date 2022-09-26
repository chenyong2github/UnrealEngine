// Copyright Epic Games, Inc. All Rights Reserved.

#include "NodeMesh.h"
#include "NodeMeshPrivate.h"

#include "NodeMesh.h"
#include "NodeMeshConstant.h"
#include "NodeMeshFormat.h"
#include "NodeMeshInterpolate.h"
#include "NodeMeshTable.h"
#include "NodeMeshSubtract.h"
#include "NodeMeshTangents.h"
#include "NodeMeshMorph.h"
#include "NodeMeshMakeMorph.h"
#include "NodeMeshSwitch.h"
#include "NodeMeshFragment.h"
#include "NodeMeshTransform.h"
#include "NodeMeshClipMorphPlane.h"
#include "NodeMeshClipWithMesh.h"
#include "NodeMeshApplyPose.h"
#include "NodeMeshVariation.h"
#include "NodeMeshGeometryOperation.h"
#include "NodeMeshReshape.h"
#include "NodeMeshClipDeform.h"

namespace mu
{

	//---------------------------------------------------------------------------------------------
	// Static initialisation
	//---------------------------------------------------------------------------------------------
	static NODE_TYPE s_nodeMeshType = NODE_TYPE( "NodeMesh", Node::GetStaticType() );


	//---------------------------------------------------------------------------------------------
	void NodeMesh::Serialise( const NodeMesh* p, OutputArchive& arch )
	{
        uint32_t ver = 0;
		arch << ver;

		arch << uint32_t(p->Type);
		p->SerialiseWrapper( arch );
    }


    //---------------------------------------------------------------------------------------------
	NodeMeshPtr NodeMesh::StaticUnserialise( InputArchive& arch )
	{
        uint32_t ver;
		arch >> ver;
		check( ver == 0 );

        uint32_t id;
		arch >> id;

		switch (EType(id))
		{
		case EType::Constant:		return NodeMeshConstant::StaticUnserialise( arch ); break;
		case EType::Interpolate:	return NodeMeshInterpolate::StaticUnserialise( arch ); break;
		case EType::Table:			return NodeMeshTable::StaticUnserialise( arch ); break;
		case EType::Subtract:		return NodeMeshSubtract::StaticUnserialise( arch ); break;
		case EType::Format:			return NodeMeshFormat::StaticUnserialise( arch ); break;
		case EType::Tangents:		return NodeMeshTangents::StaticUnserialise( arch ); break;
		case EType::Morph:			return NodeMeshMorph::StaticUnserialise( arch ); break;
		case EType::MakeMorph:		return NodeMeshMakeMorph::StaticUnserialise( arch ); break;
		case EType::Switch:			return NodeMeshSwitch::StaticUnserialise( arch ); break;
        case EType::Fragment:		return NodeMeshFragment::StaticUnserialise( arch ); break;
		case EType::Transform:		return NodeMeshTransform::StaticUnserialise( arch ); break;
		case EType::ClipMorphPlane: return NodeMeshClipMorphPlane::StaticUnserialise( arch ); break;
        case EType::ClipWithMesh:	return NodeMeshClipWithMesh::StaticUnserialise( arch ); break;
        case EType::ApplyPose:		return NodeMeshApplyPose::StaticUnserialise( arch ); break;
		case EType::Variation:		return NodeMeshVariation::StaticUnserialise( arch ); break;
		case EType::GeometryOperation:	return NodeMeshGeometryOperation::StaticUnserialise( arch ); break;
		case EType::Reshape:			return NodeMeshReshape::StaticUnserialise( arch ); break;
		case EType::ClipDeform:			return NodeMeshClipDeform::StaticUnserialise( arch ); break;
		default : check(false);
		}

		return 0;
	}


	//---------------------------------------------------------------------------------------------
	const NODE_TYPE* NodeMesh::GetType() const
	{
		return GetStaticType();
	}


	//---------------------------------------------------------------------------------------------
	const NODE_TYPE* NodeMesh::GetStaticType()
	{
		return &s_nodeMeshType;
	}


}


