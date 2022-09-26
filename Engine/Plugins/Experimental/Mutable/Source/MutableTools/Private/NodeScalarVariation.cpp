// Copyright Epic Games, Inc. All Rights Reserved.


#include "NodeScalarVariation.h"
#include "NodeScalarVariationPrivate.h"


namespace mu
{

    //---------------------------------------------------------------------------------------------
    // Static initialisation
    //---------------------------------------------------------------------------------------------
    NODE_TYPE NodeScalarVariation::Private::s_type =
        NODE_TYPE( "ScalarVariation", NodeScalar::GetStaticType() );


    //---------------------------------------------------------------------------------------------
    //---------------------------------------------------------------------------------------------
    //---------------------------------------------------------------------------------------------

    MUTABLE_IMPLEMENT_NODE( NodeScalarVariation, EType::Variation, Node, Node::EType::Scalar)


    //---------------------------------------------------------------------------------------------
    int NodeScalarVariation::GetInputCount() const { return 1 + int( m_pD->m_variations.size() ); }


    //---------------------------------------------------------------------------------------------
    Node* NodeScalarVariation::GetInputNode( int i ) const
    {
        check( i >= 0 && i < GetInputCount() );

        if ( i == 0 )
        {
            return m_pD->m_defaultScalar.get();
        }
        i -= 1;

        if ( i < int( m_pD->m_variations.size() ) )
        {
            return m_pD->m_variations[i].m_scalar.get();
        }
        i -= int( m_pD->m_variations.size() );

        return nullptr;
    }


    //---------------------------------------------------------------------------------------------
    void NodeScalarVariation::SetInputNode( int i, NodePtr pNode )
    {
        check( i >= 0 && i < GetInputCount() );

        if ( i == 0 )
        {
            m_pD->m_defaultScalar = dynamic_cast<NodeScalar*>( pNode.get() );
            return;
        }

        i -= 1;
        if ( i < int( m_pD->m_variations.size() ) )
        {

            m_pD->m_variations[i].m_scalar = dynamic_cast<NodeScalar*>( pNode.get() );
            return;
        }
        i -= (int)m_pD->m_variations.size();
    }


    //---------------------------------------------------------------------------------------------
    // Own Interface
    //---------------------------------------------------------------------------------------------
    void NodeScalarVariation::SetDefaultScalar( NodeScalar* p ) { m_pD->m_defaultScalar = p; }


    //---------------------------------------------------------------------------------------------
    int NodeScalarVariation::GetVariationCount() const { return int( m_pD->m_variations.size() ); }


    //---------------------------------------------------------------------------------------------
    void NodeScalarVariation::SetVariationCount( int num )
    {
        check( num >= 0 );
        m_pD->m_variations.resize( num );
    }

    //---------------------------------------------------------------------------------------------
    void NodeScalarVariation::SetVariationTag( int index, const char* strTag )
    {
        check( index >= 0 && index < (int)m_pD->m_variations.size() );
        check( strTag );

        if ( strTag )
        {
            m_pD->m_variations[index].m_tag = strTag;
        }
        else
        {
            m_pD->m_variations[index].m_tag = "";
        }
    }


    //---------------------------------------------------------------------------------------------
    void NodeScalarVariation::SetVariationScalar( int index, NodeScalar* pNode )
    {
        check( index >= 0 && index < (int)m_pD->m_variations.size() );

        m_pD->m_variations[index].m_scalar = pNode;
    }


} // namespace mu
