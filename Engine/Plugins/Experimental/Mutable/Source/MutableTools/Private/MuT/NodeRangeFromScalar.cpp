// Copyright Epic Games, Inc. All Rights Reserved.


#include "MuT/NodeRangeFromScalar.h"
#include "MuT/NodeRangeFromScalarPrivate.h"

#include "MuT/NodeScalar.h"

#include "MuR/ImagePrivate.h"


#define NODE_INPUT_COUNT 	1


namespace mu
{

	//---------------------------------------------------------------------------------------------
	// Static initialisation
	//---------------------------------------------------------------------------------------------
    NODE_TYPE NodeRangeFromScalar::Private::s_type =
            NODE_TYPE( "RangeFromScalar", NodeRange::GetStaticType() );


	//---------------------------------------------------------------------------------------------
	//!
	//---------------------------------------------------------------------------------------------

    MUTABLE_IMPLEMENT_NODE( NodeRangeFromScalar, EType::FromScalar, Node, Node::EType::Range)


	//---------------------------------------------------------------------------------------------
	// Node Interface
	//---------------------------------------------------------------------------------------------
    int NodeRangeFromScalar::GetInputCount() const
	{
		return NODE_INPUT_COUNT;
	}


	//---------------------------------------------------------------------------------------------
    Node* NodeRangeFromScalar::GetInputNode( int i ) const
	{
		check( i>=0 && i<NODE_INPUT_COUNT );

        Node* pResult = nullptr;

		switch (i)
		{
        case 0: pResult = m_pD->m_pSize.get(); break;
		}

		return pResult;
	}


	//---------------------------------------------------------------------------------------------
    void NodeRangeFromScalar::SetInputNode( int i, NodePtr pNode )
	{
		check( i>=0 && i<NODE_INPUT_COUNT );

		switch (i)
		{
        case 0: m_pD->m_pSize = dynamic_cast<NodeScalar*>(pNode.get()); break;
		}
	}


	//---------------------------------------------------------------------------------------------
	// Own Interface
	//---------------------------------------------------------------------------------------------
	Ptr<NodeScalar> NodeRangeFromScalar::GetSize() const
	{
        return m_pD->m_pSize.get();
	}


	//---------------------------------------------------------------------------------------------
    void NodeRangeFromScalar::SetSize( const Ptr<NodeScalar>& pNode )
	{
        m_pD->m_pSize = pNode;
	}


	//---------------------------------------------------------------------------------------------
    const char* NodeRangeFromScalar::GetName() const
	{
        return m_pD->m_name.c_str();
	}


	//---------------------------------------------------------------------------------------------
    void NodeRangeFromScalar::SetName( const char* strName )
	{
        if (!strName)
        {
            m_pD->m_name.clear();
        }
        else
        {
            m_pD->m_name = strName;
        }
	}

}

#undef NODE_INPUT_COUNT

