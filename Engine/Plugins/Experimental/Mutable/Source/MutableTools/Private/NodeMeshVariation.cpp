// Copyright Epic Games, Inc. All Rights Reserved.


#include "NodeMeshVariation.h"
#include "NodeMeshVariationPrivate.h"


namespace mu
{

    //---------------------------------------------------------------------------------------------
    // Static initialisation
    //---------------------------------------------------------------------------------------------
    NODE_TYPE NodeMeshVariation::Private::s_type =
        NODE_TYPE( "MeshVariation", NodeMesh::GetStaticType() );


    //---------------------------------------------------------------------------------------------
    //---------------------------------------------------------------------------------------------
    //---------------------------------------------------------------------------------------------

    MUTABLE_IMPLEMENT_NODE( NodeMeshVariation, EType::Variation, Node, Node::EType::Mesh)


    //---------------------------------------------------------------------------------------------
    int NodeMeshVariation::GetInputCount() const { return 1 + int( m_pD->m_variations.size() ); }


    //---------------------------------------------------------------------------------------------
    Node* NodeMeshVariation::GetInputNode( int i ) const
    {
        check( i >= 0 && i < GetInputCount() );

        if ( i == 0 )
        {
            return m_pD->m_defaultMesh.get();
        }
        i -= 1;

        if ( i < int( m_pD->m_variations.size() ) )
        {
            return m_pD->m_variations[i].m_mesh.get();
        }
        i -= int( m_pD->m_variations.size() );

        return nullptr;
    }


    //---------------------------------------------------------------------------------------------
    void NodeMeshVariation::SetInputNode( int i, NodePtr pNode )
    {
        check( i >= 0 && i < GetInputCount() );

        if ( i == 0 )
        {
            m_pD->m_defaultMesh = dynamic_cast<NodeMesh*>( pNode.get() );
            return;
        }

        i -= 1;
        if ( i < int( m_pD->m_variations.size() ) )
        {

            m_pD->m_variations[i].m_mesh = dynamic_cast<NodeMesh*>( pNode.get() );
            return;
        }
        i -= (int)m_pD->m_variations.size();
    }


    //---------------------------------------------------------------------------------------------
    // Own Interface
    //---------------------------------------------------------------------------------------------
    void NodeMeshVariation::SetDefaultMesh( NodeMesh* p ) { m_pD->m_defaultMesh = p; }


    //---------------------------------------------------------------------------------------------
    int NodeMeshVariation::GetVariationCount() const { return int( m_pD->m_variations.size() ); }


    //---------------------------------------------------------------------------------------------
    void NodeMeshVariation::SetVariationCount( int num )
    {
        check( num >= 0 );
        m_pD->m_variations.resize( num );
    }

    //---------------------------------------------------------------------------------------------
    void NodeMeshVariation::SetVariationTag( int index, const char* strTag )
    {
        check( index >= 0 && index < (int)m_pD->m_variations.size() );
        check( strTag );

        if ( strTag )
        {
            m_pD->m_variations[index].m_tag = strTag;
        }
        else
        {
            m_pD->m_variations[index].m_tag = "";
        }
    }


    //---------------------------------------------------------------------------------------------
    void NodeMeshVariation::SetVariationMesh( int index, NodeMesh* pNode )
    {
        check( index >= 0 && index < (int)m_pD->m_variations.size() );

        m_pD->m_variations[index].m_mesh = pNode;
    }


} // namespace mu
