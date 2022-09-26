// Copyright Epic Games, Inc. All Rights Reserved.


#include "NodeComponentEdit.h"
#include "NodeComponentEditPrivate.h"

#include "NodeSurface.h"


namespace mu
{


	//---------------------------------------------------------------------------------------------
	// Static initialisation
	//---------------------------------------------------------------------------------------------
	NODE_TYPE NodeComponentEdit::Private::s_type =
			NODE_TYPE( "EditComponent", NodeComponent::GetStaticType() );


	//---------------------------------------------------------------------------------------------
	//---------------------------------------------------------------------------------------------
	//---------------------------------------------------------------------------------------------

    MUTABLE_IMPLEMENT_NODE( NodeComponentEdit, EType::Edit, Node, Node::EType::Component)


    //---------------------------------------------------------------------------------------------
    // Node Interface
    //---------------------------------------------------------------------------------------------
    int NodeComponentEdit::GetInputCount() const
    {
        return (int) m_pD->m_surfaces.size();
    }


    //---------------------------------------------------------------------------------------------
    Node* NodeComponentEdit::GetInputNode( int i ) const
    {
        check( i >=0 && i < GetInputCount() );

        NodePtr pResult;

        if ( i<(int)m_pD->m_surfaces.size() )
        {
            pResult = m_pD->m_surfaces[i];
        }

        return pResult.get();
    }


    //---------------------------------------------------------------------------------------------
    void NodeComponentEdit::SetInputNode( int i, NodePtr pNode )
    {
        check( i >=0 && i < GetInputCount() );

        if ( i<(int)m_pD->m_surfaces.size() )
        {
            m_pD->m_surfaces[ i ] = dynamic_cast<NodeSurface*>(pNode.get());
        }
    }


	//---------------------------------------------------------------------------------------------
	// Own Interface
	//---------------------------------------------------------------------------------------------
    void NodeComponentEdit::SetParent( NodeComponent* p )
	{
		m_pD->m_pParent = p;
	}


	//---------------------------------------------------------------------------------------------
    NodeComponent* NodeComponentEdit::GetParent() const
	{
        return m_pD->m_pParent.get();
	}


    //---------------------------------------------------------------------------------------------
    int NodeComponentEdit::GetSurfaceCount() const
    {
        return (int)m_pD->m_surfaces.size();
    }


    //---------------------------------------------------------------------------------------------
    void NodeComponentEdit::SetSurfaceCount( int num )
    {
        check( num >=0 );
        m_pD->m_surfaces.resize( num );
    }


    //---------------------------------------------------------------------------------------------
    NodeSurface* NodeComponentEdit::GetSurface( int index ) const
    {
        check( index >=0 && index < (int)m_pD->m_surfaces.size() );

        return m_pD->m_surfaces[ index ].get();
    }


    //---------------------------------------------------------------------------------------------
    void NodeComponentEdit::SetSurface( int index, NodeSurface* pNode )
    {
        check( index >=0 && index < (int)m_pD->m_surfaces.size() );

        m_pD->m_surfaces[ index ] = pNode;
    }

}


