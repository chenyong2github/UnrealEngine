// Copyright Epic Games, Inc. All Rights Reserved.


#include "MuT/NodeImageGradient.h"

#include "Misc/AssertionMacros.h"
#include "MuR/MutableMath.h"
#include "MuT/NodeColour.h"
#include "MuT/NodeImageGradientPrivate.h"
#include "MuT/NodePrivate.h"


#define NODE_INPUT_COUNT 	2


namespace mu
{

	//---------------------------------------------------------------------------------------------
	// Static initialisation
	//---------------------------------------------------------------------------------------------
	NODE_TYPE NodeImageGradient::Private::s_type =
			NODE_TYPE( "ImageGradient", NodeImage::GetStaticType() );


	//---------------------------------------------------------------------------------------------
	//!
	//---------------------------------------------------------------------------------------------

	MUTABLE_IMPLEMENT_NODE( NodeImageGradient, EType::Gradient, Node, Node::EType::Image)


	//---------------------------------------------------------------------------------------------
	// Node Interface
	//---------------------------------------------------------------------------------------------
	int NodeImageGradient::GetInputCount() const
	{
		return NODE_INPUT_COUNT;
	}


	//---------------------------------------------------------------------------------------------
	Node* NodeImageGradient::GetInputNode( int i ) const
	{
		check( i>=0 && i<NODE_INPUT_COUNT );

		Node* pResult = 0;

		switch (i)
		{
        case 0: pResult = m_pD->m_pColour0.get(); break;
        case 1: pResult = m_pD->m_pColour1.get(); break;
		}

		return pResult;
	}


	//---------------------------------------------------------------------------------------------
	void NodeImageGradient::SetInputNode( int i, NodePtr pNode )
	{
		check( i>=0 && i<NODE_INPUT_COUNT );

		switch (i)
		{
        case 0: m_pD->m_pColour0 = dynamic_cast<NodeColour*>( pNode.get() ); break;
        case 1: m_pD->m_pColour1 = dynamic_cast<NodeColour*>( pNode.get() ); break;
		}
	}


	//---------------------------------------------------------------------------------------------
	// Own Interface
	//---------------------------------------------------------------------------------------------
	NodeColourPtr NodeImageGradient::GetColour0() const
	{
		return m_pD->m_pColour0.get();
	}


	//---------------------------------------------------------------------------------------------
	void NodeImageGradient::SetColour0( NodeColourPtr pNode )
	{
		m_pD->m_pColour0 = pNode;
	}


	//---------------------------------------------------------------------------------------------
	NodeColourPtr NodeImageGradient::GetColour1() const
	{
		return m_pD->m_pColour1.get();
	}


	//---------------------------------------------------------------------------------------------
	void NodeImageGradient::SetColour1( NodeColourPtr pNode )
	{
		m_pD->m_pColour1 = pNode;
	}


	//---------------------------------------------------------------------------------------------
	int NodeImageGradient::GetSizeX() const
	{
		return m_pD->m_size[0];
	}


	//---------------------------------------------------------------------------------------------
	int NodeImageGradient::GetSizeY() const
	{
		return m_pD->m_size[1];
	}

	//---------------------------------------------------------------------------------------------
	void NodeImageGradient::SetSize( int x, int y )
	{
		m_pD->m_size[0] = x;
		m_pD->m_size[1] = y;
	}

}

#undef NODE_INPUT_COUNT
