// Copyright Epic Games, Inc. All Rights Reserved.


#include "MutableTools/Public/NodeStringParameter.h"
#include "MutableTools/Private/NodeStringParameterPrivate.h"

#include "MutableMath.h"
#include "MemoryPrivate.h"


namespace mu
{

	//---------------------------------------------------------------------------------------------
	// Static initialisation
	//---------------------------------------------------------------------------------------------
	NODE_TYPE NodeStringParameter::Private::s_type =
			NODE_TYPE( "StringParameter", NodeString::GetStaticType() );


	//---------------------------------------------------------------------------------------------
	//!
	//---------------------------------------------------------------------------------------------

	MUTABLE_IMPLEMENT_NODE( NodeStringParameter, EType::Parameter, Node, Node::EType::String)


	//---------------------------------------------------------------------------------------------
	int NodeStringParameter::GetInputCount() const
	{
        return int( m_pD->m_additionalImages.size()
                    +
                    m_pD->m_ranges.size() );
	}


	//---------------------------------------------------------------------------------------------
    Node* NodeStringParameter::GetInputNode( int i ) const
	{
        check( i<GetInputCount() );

        int imageCount = int(m_pD->m_additionalImages.size());
        int rangeCount = int(m_pD->m_ranges.size());
        if (i<imageCount)
        {
            return m_pD->m_additionalImages[i].get();
        }
        else if ( i < imageCount + rangeCount )
        {
            int r = i - imageCount;
            return m_pD->m_ranges[r].get();
        }
        return nullptr;
	}


	//---------------------------------------------------------------------------------------------
    void NodeStringParameter::SetInputNode( int i, NodePtr n )
	{
        check( i<GetInputCount() );
        int imageCount = int(m_pD->m_additionalImages.size());
        int rangeCount = int(m_pD->m_ranges.size());
        if (i<imageCount)
        {
            m_pD->m_additionalImages[i] = dynamic_cast<NodeImage*>(n.get());
        }
        else if ( i < imageCount + rangeCount )
        {
            int r = i - imageCount;
            m_pD->m_ranges[r] = dynamic_cast<NodeRange*>(n.get());
        }
    }


	//---------------------------------------------------------------------------------------------
	const char* NodeStringParameter::GetName() const
	{
		return m_pD->m_name.c_str();
	}


	//---------------------------------------------------------------------------------------------
	void NodeStringParameter::SetName( const char* strName )
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


	const char* NodeStringParameter::GetUid() const
	{
		return m_pD->m_uid.c_str();
	}


	//---------------------------------------------------------------------------------------------
	void NodeStringParameter::SetUid( const char* strUid )
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


	//---------------------------------------------------------------------------------------------
	const char* NodeStringParameter::GetDefaultValue() const
	{
		return m_pD->m_defaultValue.c_str();
	}


	//---------------------------------------------------------------------------------------------
	void NodeStringParameter::SetDefaultValue( const char* v )
	{
		m_pD->m_defaultValue = v?v:"";
	}


	//---------------------------------------------------------------------------------------------
	PARAMETER_DETAILED_TYPE NodeStringParameter::GetDetailedType() const
	{
		return m_pD->m_detailedType;
	}


	//---------------------------------------------------------------------------------------------
	void NodeStringParameter::SetDetailedType( PARAMETER_DETAILED_TYPE t )
	{
		m_pD->m_detailedType = t;
	}


    //---------------------------------------------------------------------------------------------
    void NodeStringParameter::SetRangeCount( int i )
    {
        check(i>=0);
        m_pD->m_ranges.resize(i);
    }


    //---------------------------------------------------------------------------------------------
    void NodeStringParameter::SetRange( int i, NodeRangePtr pRange )
    {
        check( i>=0 && i<int(m_pD->m_ranges.size()) );
        if ( i>=0 && i<int(m_pD->m_ranges.size()) )
        {
            m_pD->m_ranges[i] = pRange;
        }
    }


}


