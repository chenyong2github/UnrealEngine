// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NodeMeshPrivate.h"
#include "NodePatchMesh.h"
#include "AST.h"

#include "MemoryPrivate.h"


namespace mu
{


	class NodePatchMesh::Private : public Node::Private
	{
	public:

		MUTABLE_DEFINE_CONST_VISITABLE();

	public:

		static NODE_TYPE s_type;

		NodeMeshPtr m_pRemove;
		NodeMeshPtr m_pAdd;

		//!
		void Serialise( OutputArchive& arch ) const
		{
            uint32_t ver = 1;
			arch << ver;

			arch << m_pRemove;
			arch << m_pAdd;
		}

		//!
		void Unserialise( InputArchive& arch )
		{
            uint32_t ver;
			arch >> ver;
            check( ver==1 );

			arch >> m_pRemove;
			arch >> m_pAdd;
		}

	};


}
