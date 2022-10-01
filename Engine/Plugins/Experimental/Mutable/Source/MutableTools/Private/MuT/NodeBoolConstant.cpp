// Copyright Epic Games, Inc. All Rights Reserved.


#include "MuT/NodeBoolPrivate.h"

#include "MuR/MutableMath.h"


#define NODE_INPUT_COUNT 	0


namespace mu
{

	//---------------------------------------------------------------------------------------------
	// Static initialisation
	//---------------------------------------------------------------------------------------------
	NODE_TYPE NodeBoolConstant::Private::s_type =
			NODE_TYPE( "BoolConstant", NodeBool::GetStaticType() );


	//---------------------------------------------------------------------------------------------
	//!
	//---------------------------------------------------------------------------------------------

	MUTABLE_IMPLEMENT_NODE( NodeBoolConstant, EType::Constant, Node, Node::EType::Bool);


	//---------------------------------------------------------------------------------------------
	// Node Interface
	//---------------------------------------------------------------------------------------------
	int NodeBoolConstant::GetInputCount() const
	{
		return NODE_INPUT_COUNT;
	}


	//---------------------------------------------------------------------------------------------
    Node* NodeBoolConstant::GetInputNode( int ) const
	{
		check( false );
		return 0;
	}


	//---------------------------------------------------------------------------------------------
    void NodeBoolConstant::SetInputNode( int, NodePtr )
	{
		check( false );
	}


	//---------------------------------------------------------------------------------------------
	// Own Interface
	//---------------------------------------------------------------------------------------------
	bool NodeBoolConstant::GetValue() const
	{
		return m_pD->m_value;
	}

	//---------------------------------------------------------------------------------------------
	void NodeBoolConstant::SetValue( bool v )
	{
		m_pD->m_value = v;
	}


}

#undef NODE_INPUT_COUNT
