// Copyright Epic Games, Inc. All Rights Reserved.


#include "NodeBoolPrivate.h"

#include "MutableMath.h"
#include "MemoryPrivate.h"


namespace mu
{

	//---------------------------------------------------------------------------------------------
	// Static initialisation
	//---------------------------------------------------------------------------------------------
	NODE_TYPE NodeBoolParameter::Private::s_type =
			NODE_TYPE( "BoolParameter", NodeBool::GetStaticType() );


	//---------------------------------------------------------------------------------------------
	//!
	//---------------------------------------------------------------------------------------------

	MUTABLE_IMPLEMENT_NODE( NodeBoolParameter, EType::Parameter, Node, Node::EType::Bool);


	//---------------------------------------------------------------------------------------------
	// Node Interface
	//---------------------------------------------------------------------------------------------
	int NodeBoolParameter::GetInputCount() const
	{
        return int( m_pD->m_ranges.size() );
    }


	//---------------------------------------------------------------------------------------------
    Node* NodeBoolParameter::GetInputNode( int i ) const
	{
        check( i<GetInputCount() );
        if (i<GetInputCount())
        {
            return m_pD->m_ranges[i].get();
        }
        return nullptr;
	}


	//---------------------------------------------------------------------------------------------
    void NodeBoolParameter::SetInputNode( int i , NodePtr n )
	{
        check( i<GetInputCount() );
        if (i<GetInputCount())
        {
            m_pD->m_ranges[i] = dynamic_cast<NodeRange*>(n.get());
        }
    }


	//---------------------------------------------------------------------------------------------
	// Own Interface
	//---------------------------------------------------------------------------------------------
	const char* NodeBoolParameter::GetName() const
	{
		return m_pD->m_name.c_str();
	}


	//---------------------------------------------------------------------------------------------
	void NodeBoolParameter::SetName( const char* strName )
	{
		if ( strName )
		{
			m_pD->m_name = strName;
		}
		else
		{
			m_pD->m_name = "";
		}
	}


	//---------------------------------------------------------------------------------------------
	bool NodeBoolParameter::GetDefaultValue() const
	{
		return m_pD->m_defaultValue;
	}


	//---------------------------------------------------------------------------------------------
	void NodeBoolParameter::SetDefaultValue( bool v )
	{
		m_pD->m_defaultValue = v;
	}


    //---------------------------------------------------------------------------------------------
    void NodeBoolParameter::SetRangeCount( int i )
    {
        check(i>=0);
        m_pD->m_ranges.resize(i);
    }


    //---------------------------------------------------------------------------------------------
    void NodeBoolParameter::SetRange( int i, NodeRangePtr pRange )
    {
        check( i>=0 && i<int(m_pD->m_ranges.size()) );
        if ( i>=0 && i<int(m_pD->m_ranges.size()) )
        {
            m_pD->m_ranges[i] = pRange;
        }
    }

}


