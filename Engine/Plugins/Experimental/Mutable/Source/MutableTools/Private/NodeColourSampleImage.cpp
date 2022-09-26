// Copyright Epic Games, Inc. All Rights Reserved.


#include "NodeColourSampleImage.h"
#include "NodeColourSampleImagePrivate.h"

#include "NodeScalar.h"

#include "ImagePrivate.h"


#define NODE_INPUT_COUNT 	3


namespace mu
{

	//---------------------------------------------------------------------------------------------
	// Static initialisation
	//---------------------------------------------------------------------------------------------
	NODE_TYPE NodeColourSampleImage::Private::s_type =
			NODE_TYPE( "ColourSampleImage", NodeColour::GetStaticType() );


	//---------------------------------------------------------------------------------------------
	//!
	//---------------------------------------------------------------------------------------------

	MUTABLE_IMPLEMENT_NODE( NodeColourSampleImage, EType::SampleImage, Node, Node::EType::Colour)


	//---------------------------------------------------------------------------------------------
	// Node Interface
	//---------------------------------------------------------------------------------------------
	int NodeColourSampleImage::GetInputCount() const
	{
		return NODE_INPUT_COUNT;
	}


	//---------------------------------------------------------------------------------------------
	Node* NodeColourSampleImage::GetInputNode( int i ) const
	{
		check( i>=0 && i<NODE_INPUT_COUNT );

		Node* pResult = 0;

		switch (i)
		{
		case 0: pResult = m_pD->m_pImage.get(); break;
		case 1: pResult = m_pD->m_pX.get(); break;
		case 2: pResult = m_pD->m_pY.get(); break;
		}

		return pResult;
	}


	//---------------------------------------------------------------------------------------------
	void NodeColourSampleImage::SetInputNode( int i, NodePtr pNode )
	{
		check( i>=0 && i<NODE_INPUT_COUNT );

		switch (i)
		{
		case 0: m_pD->m_pImage = dynamic_cast<NodeImage*>(pNode.get()); break;
		case 1: m_pD->m_pX = dynamic_cast<NodeScalar*>(pNode.get()); break;
		case 2: m_pD->m_pY = dynamic_cast<NodeScalar*>(pNode.get()); break;
		}
	}


	//---------------------------------------------------------------------------------------------
	// Own Interface
	//---------------------------------------------------------------------------------------------
	NodeScalarPtr NodeColourSampleImage::GetX() const
	{
		return m_pD->m_pX.get();
	}


	//---------------------------------------------------------------------------------------------
	void NodeColourSampleImage::SetX( NodeScalarPtr pNode )
	{
		m_pD->m_pX = pNode;
	}


	//---------------------------------------------------------------------------------------------
	NodeScalarPtr NodeColourSampleImage::GetY() const
	{
		return m_pD->m_pY.get();
	}


	//---------------------------------------------------------------------------------------------
	void NodeColourSampleImage::SetY( NodeScalarPtr pNode )
	{
		m_pD->m_pY = pNode;
	}


	//---------------------------------------------------------------------------------------------
	NodeImagePtr NodeColourSampleImage::GetImage() const
	{
		return m_pD->m_pImage.get();
	}


	//---------------------------------------------------------------------------------------------
	void NodeColourSampleImage::SetImage( NodeImagePtr pNode )
	{
		m_pD->m_pImage = pNode;
	}



}

#undef NODE_INPUT_COUNT

