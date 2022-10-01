// Copyright Epic Games, Inc. All Rights Reserved.


#include "MuT/NodeObjectGroup.h"
#include "NodeObjectGroupPrivate.h"

#include "MuT/NodeLayout.h"


namespace mu
{


	//---------------------------------------------------------------------------------------------
	// Static initialisation
	//---------------------------------------------------------------------------------------------
	NODE_TYPE NodeObjectGroup::Private::s_type =
            NODE_TYPE( "ObjectGroup", NodeObjectGroup::GetStaticType() );


	//---------------------------------------------------------------------------------------------
	//!
	//---------------------------------------------------------------------------------------------

	MUTABLE_IMPLEMENT_NODE( NodeObjectGroup, EType::Group, Node, Node::EType::Object)


	//---------------------------------------------------------------------------------------------
	// Node Interface
	//---------------------------------------------------------------------------------------------
	int NodeObjectGroup::GetInputCount() const
	{
		return (int)m_pD->m_children.size();
	}


	//---------------------------------------------------------------------------------------------
	Node* NodeObjectGroup::GetInputNode( int i ) const
	{
		check( i>=0 && i<GetInputCount() );

		Node* pResult = 0;

		if ( i<(int)m_pD->m_children.size() )
		{
			pResult = m_pD->m_children[i].get();
		}

		return pResult;
	}


	//---------------------------------------------------------------------------------------------
	void NodeObjectGroup::SetInputNode( int i, NodePtr pNode )
	{
		check( i>=0 && i<GetInputCount() );

		if ( i<(int)m_pD->m_children.size() )
		{
			m_pD->m_children[i] = dynamic_cast<NodeObject*>( pNode.get() );
		}
	}


	//---------------------------------------------------------------------------------------------
	// Own Interface
	//---------------------------------------------------------------------------------------------
	const char* NodeObjectGroup::GetName() const
	{
		return m_pD->m_name.c_str();
	}


	//---------------------------------------------------------------------------------------------
	void NodeObjectGroup::SetName( const char* strName )
	{
		if( strName )
		{
			m_pD->m_name = strName;
		}
		else
		{
			m_pD->m_name = "";
		}
	}


	const char* NodeObjectGroup::GetUid() const
	{
		return m_pD->m_uid.c_str();
	}


	//---------------------------------------------------------------------------------------------
	void NodeObjectGroup::SetUid( const char* strUid )
	{
		if( strUid )
		{
			m_pD->m_uid = strUid;
		}
		else
		{
			m_pD->m_uid = "";
		}
	}


	//---------------------------------------------------------------------------------------------
	NodeObjectGroup::CHILD_SELECTION NodeObjectGroup::GetSelectionType() const
	{
		return m_pD->m_type;
	}


	//---------------------------------------------------------------------------------------------
	void NodeObjectGroup::SetSelectionType( CHILD_SELECTION t )
	{
		m_pD->m_type = t;
	}


	//---------------------------------------------------------------------------------------------
	int NodeObjectGroup::GetChildCount() const
	{
		return (int)m_pD->m_children.size();
	}


	//---------------------------------------------------------------------------------------------
	void NodeObjectGroup::SetChildCount( int num )
	{
		check( num >=0 );
		m_pD->m_children.resize( num );
	}


	//---------------------------------------------------------------------------------------------
	NodeObjectPtr NodeObjectGroup::GetChild( int index ) const
	{
		check( index >=0 && index < (int)m_pD->m_children.size() );

		return m_pD->m_children[ index ].get();
	}


	//---------------------------------------------------------------------------------------------
	void NodeObjectGroup::SetChild( int index, NodeObjectPtr pObject )
	{
		check( index >=0 && index < (int)m_pD->m_children.size() );

		m_pD->m_children[ index ] = pObject;
	}


	//---------------------------------------------------------------------------------------------
    NodeLayoutPtr NodeObjectGroup::Private::GetLayout(int lod, int component, int surface, int texture ) const
	{
		NodeLayoutPtr pLayout;

		for ( size_t i=0; !pLayout && i<m_children.size(); ++i )
		{
			if (m_children[i])
			{
				NodeObject::Private* pPrivate =
						dynamic_cast<NodeObject::Private*>( m_children[i]->GetBasePrivate() );

                pLayout = pPrivate->GetLayout( lod, component, surface, texture );
			}
		}

		// TODO: layout index
		return pLayout;
	}


}


