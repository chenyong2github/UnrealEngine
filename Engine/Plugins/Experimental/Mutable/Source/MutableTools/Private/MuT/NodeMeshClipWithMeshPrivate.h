// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuT/NodeMeshPrivate.h"
#include "MuT/NodeMeshClipWithMesh.h"
#include "MuT/AST.h"

#include "MuR/MemoryPrivate.h"
#include "MuR/MutableMath.h"


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
