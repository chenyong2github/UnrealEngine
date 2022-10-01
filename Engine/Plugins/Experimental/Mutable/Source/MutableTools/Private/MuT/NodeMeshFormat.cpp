// Copyright Epic Games, Inc. All Rights Reserved.


#include "MuT/NodeMeshFormat.h"
#include "MuT/NodeMeshFormatPrivate.h"
#include "MuR/MeshPrivate.h"


namespace mu
{

	//---------------------------------------------------------------------------------------------
	// Static initialisation
	//---------------------------------------------------------------------------------------------
	NODE_TYPE NodeMeshFormat::Private::s_type =
			NODE_TYPE( "MeshFormat", NodeMesh::GetStaticType() );


	//---------------------------------------------------------------------------------------------
	//!
	//---------------------------------------------------------------------------------------------

	MUTABLE_IMPLEMENT_NODE( NodeMeshFormat, EType::Format, Node, Node::EType::Mesh)


	//---------------------------------------------------------------------------------------------
	// Node Interface
	//---------------------------------------------------------------------------------------------
	int NodeMeshFormat::GetInputCount() const
	{
		return 1;
	}


	//---------------------------------------------------------------------------------------------
	Node* NodeMeshFormat::GetInputNode( int i ) const
	{
		check( i==0 );
        (void)i;
        return m_pD->m_pSource.get();
	}


	//---------------------------------------------------------------------------------------------
	void NodeMeshFormat::SetInputNode( int i, NodePtr pNode )
	{
		check( i==0 );
        (void)i;
        m_pD->m_pSource = dynamic_cast<NodeMesh*>( pNode.get() );
	}


	//---------------------------------------------------------------------------------------------
	// Own Interface
	//---------------------------------------------------------------------------------------------
	NodeMeshPtr NodeMeshFormat::GetSource() const
	{
		return m_pD->m_pSource;
	}


	//---------------------------------------------------------------------------------------------
	void NodeMeshFormat::SetSource( NodeMesh* pValue )
	{
		m_pD->m_pSource = pValue;
	}


	//---------------------------------------------------------------------------------------------
	FMeshBufferSet& NodeMeshFormat::GetVertexBuffers()
	{
		return m_pD->m_VertexBuffers;
	}


	//---------------------------------------------------------------------------------------------
	FMeshBufferSet& NodeMeshFormat::GetIndexBuffers()
	{
		return m_pD->m_IndexBuffers;
	}


	//---------------------------------------------------------------------------------------------
	FMeshBufferSet& NodeMeshFormat::GetFaceBuffers()
	{
		return m_pD->m_FaceBuffers;
	}


	//---------------------------------------------------------------------------------------------
	void NodeMeshFormat::SetRebuildTangents( bool enabled )
	{
		m_pD->m_rebuildTangents = enabled;
	}


	//---------------------------------------------------------------------------------------------
	NodeLayoutPtr NodeMeshFormat::Private::GetLayout( int index ) const
	{
		NodeLayoutPtr pResult;

		if ( m_pSource )
		{
			NodeMesh::Private* pPrivate =
					dynamic_cast<NodeMesh::Private*>( m_pSource->GetBasePrivate() );

			pResult = pPrivate->GetLayout( index );
		}

		return pResult;
	}

}


