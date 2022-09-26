// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NodeMeshPrivate.h"
#include "NodeMeshTable.h"
#include "NodeLayout.h"
#include "Table.h"
#include "AST.h"

#include "MemoryPrivate.h"


namespace mu
{


	class NodeMeshTable::Private : public NodeMesh::Private
	{
	public:

		MUTABLE_DEFINE_CONST_VISITABLE()

	public:

		static NODE_TYPE s_type;

		string m_parameterName;
		TablePtr m_pTable;
		string m_columnName;

		vector<NodeLayoutPtr> m_layouts;

		//!
		void Serialise( OutputArchive& arch ) const
		{
            uint32_t ver = 1;
			arch << ver;

			arch << m_parameterName;
			arch << m_pTable;
			arch << m_columnName;
			arch << m_layouts;
		}

		//!
		void Unserialise( InputArchive& arch )
		{
            uint32_t ver;
			arch >> ver;
            check(ver==1);

			arch >> m_parameterName;
			arch >> m_pTable;
			arch >> m_columnName;
			arch >> m_layouts;
		}

		// NodeMesh::Private interface
        NodeLayoutPtr GetLayout( int index ) const override;

	};

}
