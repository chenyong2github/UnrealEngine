// Copyright Epic Games, Inc. All Rights Reserved.


#include "MuT/NodeImageMipmap.h"
#include "MuT/NodeImageMipmapPrivate.h"

#include "MuT/NodeScalar.h"

#include "MuR/ImagePrivate.h"


#define NODE_INPUT_COUNT 	1


namespace mu
{

	//---------------------------------------------------------------------------------------------
	// Static initialisation
	//---------------------------------------------------------------------------------------------
    NODE_TYPE NodeImageMipmap::Private::s_type =
            NODE_TYPE( "ImageMipmap", NodeImage::GetStaticType() );


	//---------------------------------------------------------------------------------------------
	//!
	//---------------------------------------------------------------------------------------------

    MUTABLE_IMPLEMENT_NODE( NodeImageMipmap, EType::Mipmap, Node, Node::EType::Image)


	//---------------------------------------------------------------------------------------------
	// Node Interface
	//---------------------------------------------------------------------------------------------
    int NodeImageMipmap::GetInputCount() const
	{
		return NODE_INPUT_COUNT;
	}


	//---------------------------------------------------------------------------------------------
    Node* NodeImageMipmap::GetInputNode( int i ) const
	{
		check( i>=0 && i<NODE_INPUT_COUNT );

		Node* pResult = 0;

		switch (i)
		{
        case 0: pResult = m_pD->m_pSource.get(); break;
		}

		return pResult;
	}


	//---------------------------------------------------------------------------------------------
    void NodeImageMipmap::SetInputNode( int i, NodePtr pNode )
	{
		check( i>=0 && i<NODE_INPUT_COUNT );

		switch (i)
		{
        case 0: m_pD->m_pSource = dynamic_cast<NodeImage*>(pNode.get()); break;
		}
	}


	//---------------------------------------------------------------------------------------------
	// Own Interface
	//---------------------------------------------------------------------------------------------
    NodeImagePtr NodeImageMipmap::GetSource() const
	{
		return m_pD->m_pSource.get();
	}


	//---------------------------------------------------------------------------------------------
    void NodeImageMipmap::SetSource( NodeImagePtr pNode )
	{
		m_pD->m_pSource = pNode;
	}

	//---------------------------------------------------------------------------------------------
	void NodeImageMipmap::SetMipmapGenerationSettings( EMipmapFilterType filterType,
													   EAddressMode addressMode,
													   float sharpenFactor,
													   bool mipDitherAlpha )
	{
		m_pD->m_settings = FMipmapGenerationSettings{ sharpenFactor, filterType, addressMode, mipDitherAlpha };
	}

}

#undef NODE_INPUT_COUNT
