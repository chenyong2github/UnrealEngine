// Copyright Epic Games, Inc. All Rights Reserved.


#include "MuT/NodeScalarConstant.h"
#include "MuT/NodeScalarConstantPrivate.h"

#include "MuR/MutableMath.h"


#define NODE_INPUT_COUNT 	0


namespace mu
{

	//---------------------------------------------------------------------------------------------
	// Static initialisation
	//---------------------------------------------------------------------------------------------
	NODE_TYPE NodeScalarConstant::Private::s_type =
			NODE_TYPE( "ScalarConstant", NodeScalar::GetStaticType() );


	//---------------------------------------------------------------------------------------------
	//!
	//---------------------------------------------------------------------------------------------

	MUTABLE_IMPLEMENT_NODE( NodeScalarConstant, EType::Constant, Node, Node::EType::Scalar)


	//---------------------------------------------------------------------------------------------
	// Node Interface
	//---------------------------------------------------------------------------------------------
	int NodeScalarConstant::GetInputCount() const
	{
		return NODE_INPUT_COUNT;
	}


	//---------------------------------------------------------------------------------------------
    Node* NodeScalarConstant::GetInputNode( int ) const
	{
		check( false );
		return 0;
	}


	//---------------------------------------------------------------------------------------------
    void NodeScalarConstant::SetInputNode( int, NodePtr )
	{
		check( false );
	}


	//---------------------------------------------------------------------------------------------
	// Own Interface
	//---------------------------------------------------------------------------------------------
	float NodeScalarConstant::GetValue() const
	{
		return m_pD->m_value;
	}

	//---------------------------------------------------------------------------------------------
	void NodeScalarConstant::SetValue( float v )
	{
		m_pD->m_value = v;
	}


}

#undef NODE_INPUT_COUNT

