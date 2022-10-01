// Copyright Epic Games, Inc. All Rights Reserved.


#include "MuT/NodeImageLayerColour.h"
#include "MuT/NodeImageLayerColourPrivate.h"

#include "MuT/NodeColour.h"

#include "MuR/ImagePrivate.h"


#define NODE_INPUT_COUNT 	3


namespace mu
{

	//---------------------------------------------------------------------------------------------
	// Static initialisation
	//---------------------------------------------------------------------------------------------
	NODE_TYPE NodeImageLayerColour::Private::s_type =
			NODE_TYPE( "ImageLayerColour", NodeImage::GetStaticType() );


	//---------------------------------------------------------------------------------------------
	//!
	//---------------------------------------------------------------------------------------------

	MUTABLE_IMPLEMENT_NODE( NodeImageLayerColour, EType::LayerColour, Node, Node::EType::Image);


	//---------------------------------------------------------------------------------------------
	// Node Interface
	//---------------------------------------------------------------------------------------------
	int NodeImageLayerColour::GetInputCount() const
	{
		return NODE_INPUT_COUNT;
	}


	//---------------------------------------------------------------------------------------------
	Node* NodeImageLayerColour::GetInputNode( int i ) const
	{
		check( i>=0 && i<NODE_INPUT_COUNT );

		Node* pResult = 0;

		switch (i)
		{
        case 0: pResult = m_pD->m_pBase.get(); break;
        case 1: pResult = m_pD->m_pMask.get(); break;
        case 2: pResult = m_pD->m_pColour.get(); break;
		}

		return pResult;
	}


	//---------------------------------------------------------------------------------------------
	void NodeImageLayerColour::SetInputNode( int i, NodePtr pNode )
	{
		check( i>=0 && i<NODE_INPUT_COUNT );

		switch (i)
		{
        case 0: m_pD->m_pBase = dynamic_cast<NodeImage*>( pNode.get() ); break;
        case 1: m_pD->m_pMask = dynamic_cast<NodeImage*>( pNode.get() ); break;
        case 2: m_pD->m_pColour = dynamic_cast<NodeColour*>( pNode.get() ); break;
		}
	}


	//---------------------------------------------------------------------------------------------
	// Own Interface
	//---------------------------------------------------------------------------------------------
	NodeImagePtr NodeImageLayerColour::GetBase() const
	{
		return m_pD->m_pBase.get();
	}

	//---------------------------------------------------------------------------------------------
	void NodeImageLayerColour::SetBase( NodeImagePtr pNode )
	{
		m_pD->m_pBase = pNode;
	}

	//---------------------------------------------------------------------------------------------
	NodeImagePtr NodeImageLayerColour::GetMask() const
	{
		return m_pD->m_pMask.get();
	}

	//---------------------------------------------------------------------------------------------
	void NodeImageLayerColour::SetMask( NodeImagePtr pNode )
	{
		m_pD->m_pMask = pNode;
	}

	//---------------------------------------------------------------------------------------------
	NodeColourPtr NodeImageLayerColour::GetColour() const
	{
		return m_pD->m_pColour.get();
	}

	//---------------------------------------------------------------------------------------------
	void NodeImageLayerColour::SetColour( NodeColourPtr pNode )
	{
		m_pD->m_pColour = pNode;
	}


	//---------------------------------------------------------------------------------------------
	EBlendType NodeImageLayerColour::GetBlendType() const
	{
		return m_pD->m_type;
	}


	//---------------------------------------------------------------------------------------------
	void NodeImageLayerColour::SetBlendType(EBlendType t)
	{
		m_pD->m_type = t;
	}

}

#undef NODE_INPUT_COUNT

