// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NodeMeshApplyPose.h"
#include "NodeScalar.h"
#include "NodeMeshPrivate.h"
#include "AST.h"

namespace mu
{


    class NodeMeshApplyPose::Private : public NodeMesh::Private
	{
	public:

		MUTABLE_DEFINE_CONST_VISITABLE()

	public:

		static NODE_TYPE s_type;

        NodeMeshPtr m_pBase;
        NodeMeshPtr m_pPose;

		//!
		void Serialise( OutputArchive& arch ) const
		{
            uint32_t ver = 0;
			arch << ver;

            arch << m_pBase;
            arch << m_pPose;
		}

		//!
		void Unserialise( InputArchive& arch )
		{
            uint32_t ver;
			arch >> ver;
			check(ver==0);

            arch >> m_pBase;
            arch >> m_pPose;
        }


        // NodeMesh::Private interface
        NodeLayoutPtr GetLayout( int index ) const override;

	};


}

