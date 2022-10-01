// Copyright Epic Games, Inc. All Rights Reserved.


#include "MuT/NodeScalarArithmeticOperation.h"
#include "NodeScalarArithmeticOperationPrivate.h"

#include "MuT/NodeScalar.h"

#include "MuR/ImagePrivate.h"


#define NODE_INPUT_COUNT 	2


namespace mu
{

	//---------------------------------------------------------------------------------------------
	// Static initialisation
	//---------------------------------------------------------------------------------------------
    NODE_TYPE NodeScalarArithmeticOperation::Private::s_type =
            NODE_TYPE( "ScalarArithmenticOperation", NodeScalar::GetStaticType() );


	//---------------------------------------------------------------------------------------------
	//!
	//---------------------------------------------------------------------------------------------

    MUTABLE_IMPLEMENT_NODE( NodeScalarArithmeticOperation, EType::ArithmeticOperation, Node, Node::EType::Scalar)


    //---------------------------------------------------------------------------------------------
    const char* NodeScalarArithmeticOperation::s_opTypeName[] =
    {
        "Add",
        "Subtract",
        "Multiply",
        "Divide"
    };


	//---------------------------------------------------------------------------------------------
	// Node Interface
	//---------------------------------------------------------------------------------------------
    int NodeScalarArithmeticOperation::GetInputCount() const
	{
		return NODE_INPUT_COUNT;
	}


	//---------------------------------------------------------------------------------------------
    Node* NodeScalarArithmeticOperation::GetInputNode( int i ) const
	{
		check( i>=0 && i<NODE_INPUT_COUNT );

		Node* pResult = 0;

		switch (i)
		{
		case 0: pResult = m_pD->m_pA.get(); break;
		case 1: pResult = m_pD->m_pB.get(); break;
		}

		return pResult;
	}


	//---------------------------------------------------------------------------------------------
    void NodeScalarArithmeticOperation::SetInputNode( int i, NodePtr pNode )
	{
		check( i>=0 && i<NODE_INPUT_COUNT );

		switch (i)
		{
        case 0: m_pD->m_pA = dynamic_cast<NodeScalar*>(pNode.get()); break;
        case 1: m_pD->m_pB = dynamic_cast<NodeScalar*>(pNode.get()); break;
		}
	}


	//---------------------------------------------------------------------------------------------
	// Own Interface
	//---------------------------------------------------------------------------------------------
    NodeScalarArithmeticOperation::OPERATION NodeScalarArithmeticOperation::GetOperation() const
	{
		return m_pD->m_operation;
	}


	//---------------------------------------------------------------------------------------------
    void NodeScalarArithmeticOperation::SetOperation(NodeScalarArithmeticOperation::OPERATION o)
	{
		m_pD->m_operation = o;
	}


    NodeScalarPtr NodeScalarArithmeticOperation::GetA() const
	{
		return m_pD->m_pA.get();
	}


	//---------------------------------------------------------------------------------------------
    void NodeScalarArithmeticOperation::SetA(NodeScalarPtr pNode)
	{
		m_pD->m_pA = pNode;
	}


	//---------------------------------------------------------------------------------------------
    NodeScalarPtr NodeScalarArithmeticOperation::GetB() const
	{
		return m_pD->m_pB.get();
	}


	//---------------------------------------------------------------------------------------------
    void NodeScalarArithmeticOperation::SetB( NodeScalarPtr pNode )
	{
		m_pD->m_pB = pNode;
	}

}

#undef NODE_INPUT_COUNT

