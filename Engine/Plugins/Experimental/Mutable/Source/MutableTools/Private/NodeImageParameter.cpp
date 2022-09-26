// Copyright Epic Games, Inc. All Rights Reserved.


#include "NodeImageParameter.h"
#include "NodeImageParameterPrivate.h"

#include "MutableMath.h"
#include "MemoryPrivate.h"


namespace mu
{

	//---------------------------------------------------------------------------------------------
	// Static initialisation
	//---------------------------------------------------------------------------------------------
    NODE_TYPE NodeImageParameter::Private::s_type =
            NODE_TYPE( "ImageParameter", NodeImage::GetStaticType() );


	//---------------------------------------------------------------------------------------------
	//!
	//---------------------------------------------------------------------------------------------

    MUTABLE_IMPLEMENT_NODE( NodeImageParameter, EType::Parameter, Node, Node::EType::Image)


	//---------------------------------------------------------------------------------------------
	// Node Interface
	//---------------------------------------------------------------------------------------------
    int NodeImageParameter::GetInputCount() const
	{
		return 0;
	}


	//---------------------------------------------------------------------------------------------
    Node* NodeImageParameter::GetInputNode( int ) const
	{
		check( false );
		return 0;
	}


	//---------------------------------------------------------------------------------------------
    void NodeImageParameter::SetInputNode( int, NodePtr )
	{
		check( false );
	}


	//---------------------------------------------------------------------------------------------
	// Own Interface
	//---------------------------------------------------------------------------------------------
    const char* NodeImageParameter::GetName() const
	{
		return m_pD->m_name.c_str();
	}


	//---------------------------------------------------------------------------------------------
    void NodeImageParameter::SetName( const char* strName )
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


	const char* NodeImageParameter::GetUid() const
	{
		return m_pD->m_uid.c_str();
	}


	//---------------------------------------------------------------------------------------------
	void NodeImageParameter::SetUid( const char* strUid )
	{
		if ( strUid )
		{
			m_pD->m_uid = strUid;
		}
		else
		{
			m_pD->m_uid = "";
		}
	}

}


