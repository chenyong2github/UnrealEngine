// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NodeMeshPrivate.h"

#include "NodeMeshConstant.h"
#include "NodeLayout.h"
#include "AST.h"


namespace mu
{

	class NodeMeshConstant::Private : public NodeMesh::Private
	{
	public:

		MUTABLE_DEFINE_CONST_VISITABLE()

	public:

		static NODE_TYPE s_type;

		MeshPtr m_pValue;

		vector<NodeLayoutPtr> m_layouts;

		//!
		void Serialise( OutputArchive& arch ) const
		{
            uint32_t ver = 0;
			arch << ver;

			arch << m_pValue;
			arch << m_layouts;
		}

		//!
		void Unserialise( InputArchive& arch )
		{
            uint32_t ver;
			arch >> ver;
			check(ver==0);

			arch >> m_pValue;
			arch >> m_layouts;

			//if (m_pValue)
			//{
			//	m_pValue->CheckIntegrity();
			//}
        }

		// NodeMesh::Private interface
        NodeLayoutPtr GetLayout( int index ) const override;

	};

}
