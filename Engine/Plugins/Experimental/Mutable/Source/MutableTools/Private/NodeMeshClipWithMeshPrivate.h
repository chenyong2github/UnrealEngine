// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NodeMeshPrivate.h"
#include "NodeMeshClipWithMesh.h"
#include "AST.h"

#include "MemoryPrivate.h"
#include "MutableMath.h"


namespace mu
{


    class NodeMeshClipWithMesh::Private : public NodeMesh::Private
	{
	public:

		MUTABLE_DEFINE_CONST_VISITABLE()

	public:

		Private()
		{
		}

		static NODE_TYPE s_type;

		NodeMeshPtr m_pSource;
		NodeMeshPtr m_pClipMesh;

		vector<mu::string> m_tags;

		//!
		void Serialise( OutputArchive& arch ) const
		{
            uint32_t ver = 1;
			arch << ver;

			arch << m_pSource;
			arch << m_pClipMesh;
			arch << m_tags;
		}

		//!
		void Unserialise( InputArchive& arch )
		{
            uint32_t ver;
			arch >> ver;

			arch >> m_pSource;
			arch >> m_pClipMesh;
			arch >> m_tags;
		}

		// NodeMesh::Private interface
        NodeLayoutPtr GetLayout( int index ) const override;

	};


}
