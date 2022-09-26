// Copyright Epic Games, Inc. All Rights Reserved.


#include "MutableTools/Public/NodeSurfaceVariation.h"
#include "MutableTools/Private/NodeSurfaceVariationPrivate.h"

#include "MutableTools/Public/NodeMesh.h"
#include "MutableTools/Public/NodeImage.h"


namespace mu
{


	//---------------------------------------------------------------------------------------------
	// Static initialisation
	//---------------------------------------------------------------------------------------------
    NODE_TYPE NodeSurfaceVariation::Private::s_type =
            NODE_TYPE( "SurfaceVariation", NodeSurface::GetStaticType() );


	//---------------------------------------------------------------------------------------------
	//---------------------------------------------------------------------------------------------
	//---------------------------------------------------------------------------------------------

    MUTABLE_IMPLEMENT_NODE( NodeSurfaceVariation, EType::Variation, Node, Node::EType::Surface)


	//---------------------------------------------------------------------------------------------
	// Node Interface
	//---------------------------------------------------------------------------------------------
    int NodeSurfaceVariation::GetInputCount() const
	{
        size_t c = m_pD->m_defaultSurfaces.size();
        c += m_pD->m_defaultModifiers.size();
        for (const auto& v : m_pD->m_variations)
		{
            c += v.m_surfaces.size();
            c += v.m_modifiers.size();
        }

		return (int)c;
	}


	//---------------------------------------------------------------------------------------------
    Node* NodeSurfaceVariation::GetInputNode( int i ) const
	{
		check( i >=0 && i < GetInputCount() );

        if ( i<(int)m_pD->m_defaultSurfaces.size())
		{
            return m_pD->m_defaultSurfaces[i].get();
		}
        i -= (int)m_pD->m_defaultSurfaces.size();

        if ( i<(int)m_pD->m_defaultModifiers.size())
        {
            return m_pD->m_defaultModifiers[i].get();
        }
        i -= (int)m_pD->m_defaultModifiers.size();

        for (const auto& v : m_pD->m_variations)
        {
            if (i < (int)v.m_surfaces.size())
            {
                return v.m_surfaces[i].get();
            }
            i -= (int)v.m_surfaces.size();

            if (i < (int)v.m_modifiers.size())
            {
                return v.m_modifiers[i].get();
            }
            i -= (int)v.m_modifiers.size();
        }

		return nullptr;
	}


	//---------------------------------------------------------------------------------------------
    void NodeSurfaceVariation::SetInputNode( int i, NodePtr pNode )
	{
		check( i >=0 && i <  GetInputCount());

        if (i<(int)m_pD->m_defaultSurfaces.size())
		{
            m_pD->m_defaultSurfaces[i] = dynamic_cast<NodeSurface*>(pNode.get());
			return;
		}

        i -= (int)m_pD->m_defaultSurfaces.size();
        if (i<(int)m_pD->m_defaultModifiers.size())
        {

            m_pD->m_defaultModifiers[i] = dynamic_cast<NodeModifier*>(pNode.get());
            return;
        }
        i -= (int)m_pD->m_defaultModifiers.size();

        for (auto& v : m_pD->m_variations)
        {
            if (i < (int)v.m_surfaces.size())
            {
                v.m_surfaces[i] = dynamic_cast<NodeSurface*>(pNode.get());
                return;
            }
            i -= (int)v.m_surfaces.size();

            if (i < (int)v.m_modifiers.size())
            {
                v.m_modifiers[i] = dynamic_cast<NodeModifier*>(pNode.get());
                return;
            }
            i -= (int)v.m_modifiers.size();
        }
	}


	//---------------------------------------------------------------------------------------------
	// Own Interface
	//---------------------------------------------------------------------------------------------
    void NodeSurfaceVariation::AddDefaultSurface( NodeSurface* p )
    {
        m_pD->m_defaultSurfaces.push_back(p);
    }


    //---------------------------------------------------------------------------------------------
    void NodeSurfaceVariation::AddDefaultModifier( NodeModifier* p )
    {
        m_pD->m_defaultModifiers.push_back(p);
    }


    //---------------------------------------------------------------------------------------------
    int NodeSurfaceVariation::GetVariationCount() const
	{
		return (int)m_pD->m_variations.size();
	}


	//---------------------------------------------------------------------------------------------
    void NodeSurfaceVariation::SetVariationCount( int num )
	{
		check( num >=0 );
		m_pD->m_variations.resize( num );
	}


    //---------------------------------------------------------------------------------------------
    void NodeSurfaceVariation::SetVariationType(VariationType type )
    {
        m_pD->m_type = type;
    }


	//---------------------------------------------------------------------------------------------
	void NodeSurfaceVariation::SetVariationTag(int index, const char* strTag)
	{
		check(index >= 0 && index < (int)m_pD->m_variations.size());
		check(strTag);

        if (strTag)
        {
            m_pD->m_variations[index].m_tag = strTag;
        }
        else
        {
            m_pD->m_variations[index].m_tag = "";
        }
	}


	//---------------------------------------------------------------------------------------------
	void NodeSurfaceVariation::AddVariationSurface(int index, NodeSurface* pNode)
	{
		check(index >= 0 && index < (int)m_pD->m_variations.size());

		m_pD->m_variations[index].m_surfaces.push_back(pNode);
	}


    //---------------------------------------------------------------------------------------------
    void NodeSurfaceVariation::AddVariationModifier(int index, NodeModifier* pModifier)
    {
        check(index >= 0 && index < (int)m_pD->m_variations.size());

        m_pD->m_variations[index].m_modifiers.push_back(pModifier);
    }

}


