// Copyright Epic Games, Inc. All Rights Reserved.


#include "NodeImageSelectColour.h"
#include "NodeImageSelectColourPrivate.h"

#include "NodeColour.h"

#include "ImagePrivate.h"


#define NODE_INPUT_COUNT 	2


namespace mu
{

	//---------------------------------------------------------------------------------------------
	// Static initialisation
	//---------------------------------------------------------------------------------------------
	NODE_TYPE NodeImageSelectColour::Private::s_type =
			NODE_TYPE( "ImageSelectColour", NodeImage::GetStaticType() );

	//---------------------------------------------------------------------------------------------
	//!
	//---------------------------------------------------------------------------------------------

	MUTABLE_IMPLEMENT_NODE( NodeImageSelectColour, EType::SelectColour, Node, Node::EType::Image)


	//---------------------------------------------------------------------------------------------
	// Node Interface
	//---------------------------------------------------------------------------------------------
	int NodeImageSelectColour::GetInputCount() const
	{
		return NODE_INPUT_COUNT;
	}


	//---------------------------------------------------------------------------------------------
	Node* NodeImageSelectColour::GetInputNode( int i ) const
	{
		check( i>=0 && i<NODE_INPUT_COUNT );

		Node* pResult = 0;

		switch (i)
		{
        case 0: pResult = m_pD->m_pColour.get(); break;
        case 1: pResult = m_pD->m_pSource.get(); break;
		}

		return pResult;
	}


	//---------------------------------------------------------------------------------------------
	void NodeImageSelectColour::SetInputNode( int i, NodePtr pNode )
	{
		check( i>=0 && i<NODE_INPUT_COUNT );

		switch (i)
		{
        case 0: m_pD->m_pColour = dynamic_cast<NodeColour*>(pNode.get()); break;
        case 1: m_pD->m_pSource = dynamic_cast<NodeImage*>(pNode.get()); break;
		}
	}


	//---------------------------------------------------------------------------------------------
	// Own Interface
	//---------------------------------------------------------------------------------------------
	NodeColourPtr NodeImageSelectColour::GetColour() const
	{
		return m_pD->m_pColour.get();
	}


	//---------------------------------------------------------------------------------------------
	void NodeImageSelectColour::SetColour( NodeColourPtr pNode )
	{
		m_pD->m_pColour = pNode;
	}


	//---------------------------------------------------------------------------------------------
	NodeImagePtr NodeImageSelectColour::GetSource() const
	{
		return m_pD->m_pSource.get();
	}


	//---------------------------------------------------------------------------------------------
	void NodeImageSelectColour::SetSource( NodeImagePtr pNode )
	{
		m_pD->m_pSource = pNode;
	}



}

#undef NODE_INPUT_COUNT
