// Copyright Epic Games, Inc. All Rights Reserved.


#include "MuT/NodeMeshClipWithMesh.h"
#include "MuT/NodeMeshClipWithMeshPrivate.h"
#include "MuR/MeshPrivate.h"


#define NODE_INPUT_COUNT 	1


namespace mu
{

	//---------------------------------------------------------------------------------------------
	// Static initialisation
	//---------------------------------------------------------------------------------------------
    NODE_TYPE NodeMeshClipWithMesh::Private::s_type =
            NODE_TYPE( "MeshClipMorphPlane", NodeMesh::GetStaticType() );


	//---------------------------------------------------------------------------------------------
	//!
	//---------------------------------------------------------------------------------------------

    MUTABLE_IMPLEMENT_NODE( NodeMeshClipWithMesh, EType::ClipWithMesh, Node, Node::EType::Mesh)


	//---------------------------------------------------------------------------------------------
	// Node Interface
	//---------------------------------------------------------------------------------------------
    int NodeMeshClipWithMesh::GetInputCount() const
	{
		return NODE_INPUT_COUNT;
	}


	//---------------------------------------------------------------------------------------------
    Node* NodeMeshClipWithMesh::GetInputNode( int i ) const
	{
		check( i>=0 && i<NODE_INPUT_COUNT );
        (void)i;
        return m_pD->m_pSource.get();
	}


	//---------------------------------------------------------------------------------------------
    void NodeMeshClipWithMesh::SetInputNode( int i, NodePtr pNode )
	{
		check( i>=0 && i<NODE_INPUT_COUNT );
		if (i==0)
		{
			m_pD->m_pSource = dynamic_cast<NodeMesh*>( pNode.get() );
		}
	}


	//---------------------------------------------------------------------------------------------
    const char* NodeMeshClipWithMesh::GetInputName( int i ) const
	{
		check( i>=0 && i<NODE_INPUT_COUNT );
        (void)i;
        return "Source";
	}


	//---------------------------------------------------------------------------------------------
    const NODE_TYPE* NodeMeshClipWithMesh::GetInputType( int i ) const
	{
		check( i>=0 && i<NODE_INPUT_COUNT );
        (void)i;
        return NodeMesh::GetStaticType();
	}


	//---------------------------------------------------------------------------------------------
	// Own Interface
	//---------------------------------------------------------------------------------------------
    NodeMeshPtr NodeMeshClipWithMesh::GetSource() const
	{
		return m_pD->m_pSource;
	}


    //---------------------------------------------------------------------------------------------
    void NodeMeshClipWithMesh::SetSource(NodeMesh* p)
    {
        m_pD->m_pSource = p;
    }


	//---------------------------------------------------------------------------------------------
	void NodeMeshClipWithMesh::SetClipMesh(NodeMesh* p)
	{
		m_pD->m_pClipMesh = p;
	}


	//---------------------------------------------------------------------------------------------
    NodeLayoutPtr NodeMeshClipWithMesh::Private::GetLayout( int index ) const
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

	//---------------------------------------------------------------------------------------------
	void NodeMeshClipWithMesh::AddTag(const char* tagName)
	{
		m_pD->m_tags.push_back(tagName);
	}
}

#undef NODE_INPUT_COUNT
