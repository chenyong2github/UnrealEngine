// Copyright Epic Games, Inc. All Rights Reserved.


#include "MutableTools/Public/NodeSurfaceNew.h"
#include "MutableTools/Private/NodeSurfaceNewPrivate.h"

#include "MutableTools/Public/NodeMesh.h"
#include "MutableTools/Public/NodeImage.h"
#include "MutableTools/Public/NodeScalar.h"
#include "MutableTools/Public/NodeString.h"
#include "MutableTools/Public/NodeColour.h"

namespace mu
{


	//---------------------------------------------------------------------------------------------
	// Static initialisation
	//---------------------------------------------------------------------------------------------
    NODE_TYPE NodeSurfaceNew::Private::s_type =
            NODE_TYPE( "NewSurface", NodeSurface::GetStaticType() );


	//---------------------------------------------------------------------------------------------
	//---------------------------------------------------------------------------------------------
	//---------------------------------------------------------------------------------------------

    MUTABLE_IMPLEMENT_NODE( NodeSurfaceNew, EType::New, Node, Node::EType::Surface)


	//---------------------------------------------------------------------------------------------
	// Node Interface
	//---------------------------------------------------------------------------------------------
    int NodeSurfaceNew::GetInputCount() const
	{
        return (int)( m_pD->m_meshes.size() + m_pD->m_images.size() + m_pD->m_vectors.size() +
                      m_pD->m_scalars.size() + m_pD->m_strings.size() );
    }


	//---------------------------------------------------------------------------------------------
    mu::Node* NodeSurfaceNew::GetInputNode( int i ) const
	{
		check( i >=0 && i < GetInputCount() );

        std::size_t index = size_t(i);

        if ( index < m_pD->m_meshes.size() )
        {
            return m_pD->m_meshes[index].m_pMesh.get();
        }
		else
		{
            index -= m_pD->m_meshes.size();
        }

        if ( index < m_pD->m_images.size() )
        {
            return m_pD->m_images[index].m_pImage.get();
        }
        else 
		{
            index -= m_pD->m_images.size();
        }

        if ( index < m_pD->m_vectors.size() )
        {
            return m_pD->m_vectors[index].m_pVector.get();
        }
        else
        {
            index -= m_pD->m_vectors.size();
        }

        if ( index < m_pD->m_scalars.size() )
        {
            return m_pD->m_scalars[index].m_pScalar.get();
        }
        else
        {
            index -= m_pD->m_scalars.size();
        }

        if ( index < m_pD->m_strings.size() )
        {
            return m_pD->m_strings[index].m_pString.get();
        }
        else
        {
            index -= m_pD->m_strings.size();
        }

        return nullptr;
    }


	//---------------------------------------------------------------------------------------------
    void NodeSurfaceNew::SetInputNode( int i, NodePtr pNode )
	{
		check( i >=0 && i < GetInputCount());

        std::size_t index = size_t( i );

        if ( index<m_pD->m_meshes.size() )
		{
			m_pD->m_meshes[ index ].m_pMesh = dynamic_cast<NodeMesh*>(pNode.get());
            return;
        }
		else
		{
            index -= m_pD->m_meshes.size();
        }


        if ( index < m_pD->m_images.size())
        {
			m_pD->m_images[index].m_pImage = dynamic_cast<NodeImage*>(pNode.get());
            return;
        }
        else
        {
            index -= m_pD->m_images.size();
        }
        
		if ( index < m_pD->m_vectors.size() )
        {
            m_pD->m_vectors[index].m_pVector = dynamic_cast<NodeColour*>(pNode.get());
            return;
        }
        else
        {
            index -= m_pD->m_vectors.size();
        }

        if ( index < m_pD->m_scalars.size() )
        {
            m_pD->m_scalars[index].m_pScalar = dynamic_cast<NodeScalar*>( pNode.get() );
            return;
        }
        else
        {
            index -= m_pD->m_scalars.size();
        }

        if ( index < m_pD->m_strings.size() )
        {
            m_pD->m_strings[index].m_pString = dynamic_cast<NodeString*>( pNode.get() );
            return;
        }
        else
        {
            index -= m_pD->m_strings.size();
        }
    }


	//---------------------------------------------------------------------------------------------
	// Own Interface
	//---------------------------------------------------------------------------------------------
    const char* NodeSurfaceNew::GetName() const
	{
		const char* strResult = m_pD->m_name.c_str();

		return strResult;
	}


    //---------------------------------------------------------------------------------------------
    void NodeSurfaceNew::SetName( const char* strName )
    {
        if (strName)
        {
            m_pD->m_name = strName;
        }
        else
        {
            m_pD->m_name = "";
        }
    }


    //---------------------------------------------------------------------------------------------
    void NodeSurfaceNew::SetCustomID( uint32_t id )
    {
        m_pD->m_customID = id;
    }


	//---------------------------------------------------------------------------------------------
    int NodeSurfaceNew::GetMeshCount() const
	{
		return (int)m_pD->m_meshes.size();
	}


	//---------------------------------------------------------------------------------------------
    void NodeSurfaceNew::SetMeshCount( int num )
	{
		check( num >=0 );
		m_pD->m_meshes.resize( num );
	}


	//---------------------------------------------------------------------------------------------
    NodeMeshPtr NodeSurfaceNew::GetMesh( int index ) const
	{
		check( index >=0 && index < (int)m_pD->m_meshes.size() );

		return m_pD->m_meshes[ index ].m_pMesh.get();
	}


	//---------------------------------------------------------------------------------------------
    void NodeSurfaceNew::SetMesh( int index, NodeMeshPtr pNode )
	{
		check( index >=0 && index < (int)m_pD->m_meshes.size() );

		m_pD->m_meshes[ index ].m_pMesh = pNode;
	}


	//---------------------------------------------------------------------------------------------
    const char* NodeSurfaceNew::GetMeshName( int index ) const
	{
		check( index >=0 && index < (int)m_pD->m_meshes.size() );

		const char* strResult = m_pD->m_meshes[ index ].m_name.c_str();

		return strResult;
	}


	//---------------------------------------------------------------------------------------------
    void NodeSurfaceNew::SetMeshName( int index, const char* strName )
	{
		check( index >=0 && index < (int)m_pD->m_meshes.size() );

		if (strName)
		{
			m_pD->m_meshes[ index ].m_name = strName;
		}
		else
		{
			m_pD->m_meshes[ index ].m_name = "";
		}
	}


	//---------------------------------------------------------------------------------------------
    int NodeSurfaceNew::GetImageCount() const
	{
		return (int)m_pD->m_images.size();
	}


	//---------------------------------------------------------------------------------------------
    void NodeSurfaceNew::SetImageCount( int num )
	{
		check( num >=0 );
		m_pD->m_images.resize( num );
	}


	//---------------------------------------------------------------------------------------------
    NodeImagePtr NodeSurfaceNew::GetImage( int index ) const
	{
		check( index >=0 && index < (int)m_pD->m_images.size() );

		return m_pD->m_images[ index ].m_pImage.get();
	}


	//---------------------------------------------------------------------------------------------
    void NodeSurfaceNew::SetImage( int index, NodeImagePtr pNode )
	{
		check( index >=0 && index < (int)m_pD->m_images.size() );

		m_pD->m_images[ index ].m_pImage = pNode;
	}


	//---------------------------------------------------------------------------------------------
    const char* NodeSurfaceNew::GetImageName( int index ) const
	{
		check( index >=0 && index < (int)m_pD->m_images.size() );

		const char* strResult = m_pD->m_images[ index ].m_name.c_str();

		return strResult;
	}


	//---------------------------------------------------------------------------------------------
    void NodeSurfaceNew::SetImageName( int index, const char* strName )
	{
		check( index >=0 && index < (int)m_pD->m_images.size() );

		if (strName)
		{
			m_pD->m_images[ index ].m_name = strName;
		}
		else
		{
			m_pD->m_images[ index ].m_name = "";
		}
	}


    //---------------------------------------------------------------------------------------------
    int NodeSurfaceNew::GetImageLayoutIndex( int index ) const
    {
        check( index >= 0 && index < (int)m_pD->m_images.size() );

        return int(m_pD->m_images[index].m_layoutIndex);
    }


	//---------------------------------------------------------------------------------------------
	void NodeSurfaceNew::SetImageLayoutIndex(int index, int layoutIndex)
	{
		check(index >= 0 && index < (int)m_pD->m_images.size());

		m_pD->m_images[index].m_layoutIndex = int8_t(layoutIndex);
	}


	//---------------------------------------------------------------------------------------------
	void NodeSurfaceNew::SetImageAdditionalNames(int index, const char* strMaterialName, const char* strMaterialParameterName )
	{
		if (index >= 0 && index < (int)m_pD->m_images.size())
		{
			m_pD->m_images[index].m_materialName = strMaterialName;
			m_pD->m_images[index].m_materialParameterName = strMaterialParameterName;
		}
		else
		{
			check(false);
		}
	}


    //---------------------------------------------------------------------------------------------
	int NodeSurfaceNew::GetVectorCount() const
	{
		return (int)m_pD->m_vectors.size();
	}


	//---------------------------------------------------------------------------------------------
	void NodeSurfaceNew::SetVectorCount(int num)
	{
		check(num >= 0);
		m_pD->m_vectors.resize(num);
	}


	//---------------------------------------------------------------------------------------------
	NodeColourPtr NodeSurfaceNew::GetVector(int index) const
	{
		check(index >= 0 && index < (int)m_pD->m_vectors.size());

		return m_pD->m_vectors[index].m_pVector.get();
	}


	//---------------------------------------------------------------------------------------------
	void NodeSurfaceNew::SetVector(int index, NodeColourPtr pNode)
	{
		check(index >= 0 && index < (int)m_pD->m_vectors.size());

		m_pD->m_vectors[index].m_pVector = pNode;
	}


	//---------------------------------------------------------------------------------------------
	const char* NodeSurfaceNew::GetVectorName(int index) const
	{
        check(index >= 0 && index < (int)m_pD->m_vectors.size());

		const char* strResult = m_pD->m_vectors[index].m_name.c_str();

		return strResult;
	}


	//---------------------------------------------------------------------------------------------
	void NodeSurfaceNew::SetVectorName(int index, const char* strName)
	{
        check(index >= 0 && index < (int)m_pD->m_vectors.size());

		if (strName)
		{
			m_pD->m_vectors[index].m_name = strName;
		}
		else
		{
			m_pD->m_vectors[index].m_name = "";
		}
	}


    //---------------------------------------------------------------------------------------------
    int NodeSurfaceNew::GetScalarCount() const
    {
        return (int)m_pD->m_scalars.size();
    }


    //---------------------------------------------------------------------------------------------
    void NodeSurfaceNew::SetScalarCount(int num)
    {
        check(num >= 0);
        m_pD->m_scalars.resize(num);
    }


    //---------------------------------------------------------------------------------------------
    NodeScalarPtr NodeSurfaceNew::GetScalar(int index) const
    {
        check(index >= 0 && index < (int)m_pD->m_scalars.size());

        return m_pD->m_scalars[index].m_pScalar.get();
    }


    //---------------------------------------------------------------------------------------------
    void NodeSurfaceNew::SetScalar(int index, NodeScalarPtr pNode)
    {
        check(index >= 0 && index < (int)m_pD->m_scalars.size());

        m_pD->m_scalars[index].m_pScalar = pNode;
    }


    //---------------------------------------------------------------------------------------------
    const char* NodeSurfaceNew::GetScalarName(int index) const
    {
        check(index >= 0 && index < (int)m_pD->m_scalars.size());

        const char* strResult = m_pD->m_scalars[index].m_name.c_str();

        return strResult;
    }


    //---------------------------------------------------------------------------------------------
    void NodeSurfaceNew::SetScalarName(int index, const char* strName)
    {
        check(index >= 0 && index < (int)m_pD->m_scalars.size());

        if (strName)
        {
            m_pD->m_scalars[index].m_name = strName;
        }
        else
        {
            m_pD->m_scalars[index].m_name = "";
        }
    }


    //---------------------------------------------------------------------------------------------
    int NodeSurfaceNew::GetStringCount() const { return (int)m_pD->m_strings.size(); }


    //---------------------------------------------------------------------------------------------
    void NodeSurfaceNew::SetStringCount( int num )
    {
        check( num >= 0 );
        m_pD->m_strings.resize( num );
    }


    //---------------------------------------------------------------------------------------------
    NodeStringPtr NodeSurfaceNew::GetString( int index ) const
    {
        check( index >= 0 && index < (int)m_pD->m_strings.size() );

        return m_pD->m_strings[index].m_pString.get();
    }


    //---------------------------------------------------------------------------------------------
    void NodeSurfaceNew::SetString( int index, NodeStringPtr pNode )
    {
        check( index >= 0 && index < (int)m_pD->m_strings.size() );

        m_pD->m_strings[index].m_pString = pNode;
    }


    //---------------------------------------------------------------------------------------------
    const char* NodeSurfaceNew::GetStringName( int index ) const
    {
        check( index >= 0 && index < (int)m_pD->m_strings.size() );

        const char* strResult = m_pD->m_strings[index].m_name.c_str();

        return strResult;
    }


    //---------------------------------------------------------------------------------------------
    void NodeSurfaceNew::SetStringName( int index, const char* strName )
    {
        check( index >= 0 && index < (int)m_pD->m_strings.size() );

        if ( strName )
        {
            m_pD->m_strings[index].m_name = strName;
        }
        else
        {
            m_pD->m_strings[index].m_name = "";
        }
    }


    //---------------------------------------------------------------------------------------------
    int NodeSurfaceNew::Private::FindImage( const char* strName ) const
	{
		int i = 0;
		for ( vector<IMAGE>::const_iterator it = m_images.begin()
			; it!=m_images.end()
			; ++it, ++i )
		{
			if ( it->m_name == strName )
			{
				return i;
			}
		}

		return -1;
	}


	//---------------------------------------------------------------------------------------------
	int NodeSurfaceNew::Private::FindMesh(const char* strName) const
	{
		int i = 0;
		for (vector<MESH>::const_iterator it = m_meshes.begin()
			; it != m_meshes.end()
			; ++it, ++i)
		{
			if (it->m_name == strName)
			{
				return i;
			}
		}

		return -1;
	}


    //---------------------------------------------------------------------------------------------
    int NodeSurfaceNew::Private::FindVector(const char* strName) const
    {
        int i = 0;
        for (vector<VECTOR>::const_iterator it = m_vectors.begin()
            ; it != m_vectors.end()
            ; ++it, ++i)
        {
            if (it->m_name == strName)
            {
                return i;
            }
        }

        return -1;
    }


    //---------------------------------------------------------------------------------------------
    int NodeSurfaceNew::Private::FindScalar( const char* strName ) const
    {
        int i = 0;
        for ( vector<SCALAR>::const_iterator it = m_scalars.begin(); it != m_scalars.end();
              ++it, ++i )
        {
            if ( it->m_name == strName )
            {
                return i;
            }
        }

        return -1;
    }


    //---------------------------------------------------------------------------------------------
    int NodeSurfaceNew::Private::FindString( const char* strName ) const
    {
        int i = 0;
        for ( vector<STRING>::const_iterator it = m_strings.begin(); it != m_strings.end();
              ++it, ++i )
        {
            if ( it->m_name == strName )
            {
                return i;
            }
        }

        return -1;
    }


    //---------------------------------------------------------------------------------------------
	void NodeSurfaceNew::AddTag(const char* tagName)
	{
		m_pD->m_tags.push_back(tagName);
	}


    //---------------------------------------------------------------------------------------------
    int NodeSurfaceNew::GetTagCount() const
    {
        return int( m_pD->m_tags.size());
    }


    //---------------------------------------------------------------------------------------------
    const char* NodeSurfaceNew::GetTag( int i ) const
    {
        if (i>=0 && i<GetTagCount())
        {
            return m_pD->m_tags[i].c_str();
        }
        return nullptr;
    }

}


