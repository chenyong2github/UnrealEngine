// Copyright Epic Games, Inc. All Rights Reserved.


#include "MuR/Instance.h"

#include "MuR/Platform.h"

#include "MuR/InstancePrivate.h"
#include "MuR/ImagePrivate.h"
#include "MuR/MeshPrivate.h"


namespace mu
{

	//---------------------------------------------------------------------------------------------
	//---------------------------------------------------------------------------------------------
	//---------------------------------------------------------------------------------------------
	Instance::Instance()
	{
		LLM_SCOPE_BYNAME(TEXT("MutableRuntime"));
		m_pD = new Private();
	}


	//---------------------------------------------------------------------------------------------
	Instance::~Instance()
	{
		LLM_SCOPE_BYNAME(TEXT("MutableRuntime"));
        check( m_pD );
		delete m_pD;
		m_pD = 0;
	}


	//---------------------------------------------------------------------------------------------
	Instance::Private* Instance::GetPrivate() const
	{
		return m_pD;
	}


    //---------------------------------------------------------------------------------------------
    InstancePtr Instance::Clone() const
    {
		LLM_SCOPE_BYNAME(TEXT("MutableRuntime"));
        InstancePtr pResult = new Instance();

        *pResult->GetPrivate() = *m_pD;

        return pResult;
    }


    //---------------------------------------------------------------------------------------------
    Instance::ID Instance::GetId() const
    {
        return m_pD->m_id;
    }


    //---------------------------------------------------------------------------------------------
    int Instance::GetLODCount() const
    {
        return m_pD->GetLODCount();
    }


	//---------------------------------------------------------------------------------------------
	int Instance::Private::GetLODCount() const
	{
		return (int)m_lods.size();
	}


	//---------------------------------------------------------------------------------------------
	int Instance::GetComponentCount( int lod ) const
	{
		return m_pD->GetComponentCount( lod );
	}


	//---------------------------------------------------------------------------------------------
	int Instance::Private::GetComponentCount( int lod ) const
	{
		check( lod>=0 && lod<(int)m_lods.size() );
        if ( lod>=0 && lod<(int)m_lods.size() )
        {
            return (int)m_lods[lod].m_components.size();
        }

        return 0;
	}


    //---------------------------------------------------------------------------------------------
    const char* Instance::GetComponentName( int lod, int comp ) const
    {
        if ( lod>=0 && lod<(int)m_pD->m_lods.size() &&
             comp>=0 && comp<(int)m_pD->m_lods[lod].m_components.size() )
        {
            return m_pD->m_lods[lod].m_components[comp].m_name.c_str();
        }
		else
		{
			check(false);
		}

        return "";
    }

	
	//---------------------------------------------------------------------------------------------
	uint16 Instance::GetComponentId( int lod, int comp ) const
	{
		if ( lod>=0 && lod<(int)m_pD->m_lods.size() &&
			 comp>=0 && comp<(int)m_pD->m_lods[lod].m_components.size() )
		{
			return m_pD->m_lods[lod].m_components[comp].m_id;
		}
		else
		{
			check(false);
		}

		return 0;
	}


    //---------------------------------------------------------------------------------------------
    int Instance::GetSurfaceCount( int lod, int comp ) const
    {
        return m_pD->GetSurfaceCount( lod, comp );
    }


    //---------------------------------------------------------------------------------------------
    int Instance::Private::GetSurfaceCount( int lod, int comp ) const
    {
        if ( lod>=0 && lod<(int)m_lods.size() &&
             comp>=0 && comp<(int)m_lods[lod].m_components.size() )
        {
            return (int)m_lods[lod].m_components[comp].m_surfaces.size();
        }
		else
		{
			check(false);
		}

        return 0;
    }


	//---------------------------------------------------------------------------------------------
    const char* Instance::GetSurfaceName( int lod, int comp, int surf ) const
    {
        if ( lod>=0 && lod<(int)m_pD->m_lods.size() &&
             comp>=0 && comp<(int)m_pD->m_lods[lod].m_components.size() &&
             surf>=0 && surf<(int)m_pD->m_lods[lod].m_components[comp].m_surfaces.size() )
        {
            return m_pD->m_lods[lod].m_components[comp].m_surfaces[surf].m_name.c_str();
        }
		else
		{
			check(false);
		}

        return "";
    }


    //---------------------------------------------------------------------------------------------
    uint32_t Instance::GetSurfaceId( int lod, int comp, int surf ) const
    {
        if ( lod>=0 && lod<(int)m_pD->m_lods.size() &&
             comp>=0 && comp<(int)m_pD->m_lods[lod].m_components.size() &&
             surf>=0 && surf<(int)m_pD->m_lods[lod].m_components[comp].m_surfaces.size() )
        {
            return m_pD->m_lods[lod].m_components[comp].m_surfaces[surf].m_internalID;
        }
		else
		{
			check(false);
		}

        return 0;
    }


    //---------------------------------------------------------------------------------------------
    int Instance::FindSurfaceById( int lod, int comp, uint32_t id ) const
    {
		if (lod >= 0 && lod < (int)m_pD->m_lods.size() &&
			comp >= 0 && comp < (int)m_pD->m_lods[lod].m_components.size())
		{
			for (int i = 0; i < (int)m_pD->m_lods[lod].m_components[comp].m_surfaces.size(); ++i)
			{
				if (m_pD->m_lods[lod].m_components[comp].m_surfaces[i].m_internalID == id)
				{
					return i;
				}
			}
		}
		else
		{
			check(false);
		}

        return -1;
    }


    //---------------------------------------------------------------------------------------------
    uint32_t Instance::GetSurfaceCustomId( int lod, int comp, int surf ) const
    {
        if ( lod>=0 && lod<(int)m_pD->m_lods.size() &&
             comp>=0 && comp<(int)m_pD->m_lods[lod].m_components.size() &&
             surf>=0 && surf<(int)m_pD->m_lods[lod].m_components[comp].m_surfaces.size() )
        {
            return m_pD->m_lods[lod].m_components[comp].m_surfaces[surf].m_customID;
        }
		else
		{
			check(false);
		}

        return 0;
    }


	//---------------------------------------------------------------------------------------------
    int Instance::GetMeshCount( int lod, int comp ) const
	{
        return m_pD->GetMeshCount( lod, comp );
	}


	//---------------------------------------------------------------------------------------------
    int Instance::Private::GetMeshCount( int lod, int comp ) const
	{
		check( lod>=0 && lod<(int)m_lods.size() );
		check( comp>=0 && comp<(int)m_lods[lod].m_components.size() );

        return (int)m_lods[lod].m_components[comp].m_meshes.size();
	}


	//---------------------------------------------------------------------------------------------
    int Instance::GetImageCount( int lod, int comp, int surf ) const
	{
        return m_pD->GetImageCount( lod, comp, surf );
	}


	//---------------------------------------------------------------------------------------------
    int Instance::Private::GetImageCount( int lod, int comp, int surf ) const
	{
		check( lod>=0 && lod<(int)m_lods.size() );
		check( comp>=0 && comp<(int)m_lods[lod].m_components.size() );
        check( surf>=0 && surf<(int)m_lods[lod].m_components[comp].m_surfaces.size() );

        return (int)m_lods[lod].m_components[comp].m_surfaces[surf].m_images.size();
	}


	//---------------------------------------------------------------------------------------------
    int Instance::GetVectorCount( int lod, int comp, int surf ) const
	{
        return m_pD->GetVectorCount( lod, comp, surf );
	}


	//---------------------------------------------------------------------------------------------
    int Instance::Private::GetVectorCount( int lod, int comp, int surf ) const
	{
		check( lod>=0 && lod<(int)m_lods.size() );
		check( comp>=0 && comp<(int)m_lods[lod].m_components.size() );
        check( surf>=0 && surf<(int)m_lods[lod].m_components[comp].m_surfaces.size() );

        return (int)m_lods[lod].m_components[comp].m_surfaces[surf].m_vectors.size();
	}


	//---------------------------------------------------------------------------------------------
    int Instance::GetScalarCount( int lod, int comp, int surf ) const
	{
        return m_pD->GetScalarCount( lod, comp, surf );
	}


	//---------------------------------------------------------------------------------------------
    int Instance::Private::GetScalarCount( int lod, int comp, int surf ) const
	{
		check( lod>=0 && lod<(int)m_lods.size() );
		check( comp>=0 && comp<(int)m_lods[lod].m_components.size() );
        check( surf>=0 && surf<(int)m_lods[lod].m_components[comp].m_surfaces.size() );

        return (int)m_lods[lod].m_components[comp].m_surfaces[surf].m_scalars.size();
	}


    //---------------------------------------------------------------------------------------------
    int Instance::GetStringCount( int lod, int comp, int surf ) const
    {
        return m_pD->GetStringCount( lod, comp, surf );
    }


    //---------------------------------------------------------------------------------------------
    int Instance::Private::GetStringCount( int lod, int comp, int surf ) const
    {
        check( lod >= 0 && lod < (int)m_lods.size() );
        check( comp >= 0 && comp < (int)m_lods[lod].m_components.size() );
        check( surf >= 0 && surf < (int)m_lods[lod].m_components[comp].m_surfaces.size() );

        return (int)m_lods[lod].m_components[comp].m_surfaces[surf].m_strings.size();
    }


    //---------------------------------------------------------------------------------------------
    RESOURCE_ID Instance::GetMeshId( int lod, int comp, int mesh ) const
    {
        check( lod>=0 && lod<(int)m_pD->m_lods.size() );
        check( comp>=0 && comp<(int)m_pD->m_lods[lod].m_components.size() );
        check( mesh>=0 && mesh<(int)m_pD->m_lods[lod].m_components[comp].m_meshes.size() );

        RESOURCE_ID result = m_pD->m_lods[lod].m_components[comp].m_meshes[mesh].m_meshId;
        return result;
    }


	//---------------------------------------------------------------------------------------------
    const char* Instance::GetMeshName( int lod, int comp, int mesh ) const
	{
		check( lod>=0 && lod<(int)m_pD->m_lods.size() );
		check( comp>=0 && comp<(int)m_pD->m_lods[lod].m_components.size() );
        check( mesh>=0 && mesh<(int)m_pD->m_lods[lod].m_components[comp].m_meshes.size() );

        const char* strResult = m_pD->m_lods[lod].m_components[comp].m_meshes[mesh].m_name.c_str();
		return strResult;
	}


	//---------------------------------------------------------------------------------------------
    RESOURCE_ID Instance::GetImageId( int lod, int comp, int surf, int img ) const
	{
		check( lod>=0 && lod<(int)m_pD->m_lods.size() );
		check( comp>=0 && comp<(int)m_pD->m_lods[lod].m_components.size() );
        check( surf>=0 && surf<(int)m_pD->m_lods[lod].m_components[comp].m_surfaces.size() );
        check( img>=0 && img<(int)m_pD->m_lods[lod].m_components[comp].m_surfaces[surf].m_images.size() );

        RESOURCE_ID result = m_pD->m_lods[lod].m_components[comp].m_surfaces[surf].m_images[img].m_imageId;
        return result;
	}


	//---------------------------------------------------------------------------------------------
    const char* Instance::GetImageName( int lod, int comp, int surf, int img ) const
	{
		check( lod>=0 && lod<(int)m_pD->m_lods.size() );
		check( comp>=0 && comp<(int)m_pD->m_lods[lod].m_components.size() );
        check( surf>=0 && surf<(int)m_pD->m_lods[lod].m_components[comp].m_surfaces.size() );
        check( img>=0 && img<(int)m_pD->m_lods[lod].m_components[comp].m_surfaces[surf].m_images.size() );

        const char* strResult = m_pD->m_lods[lod].m_components[comp].m_surfaces[surf].m_images[img].m_name.c_str();
		return strResult;
	}


	//---------------------------------------------------------------------------------------------
    void Instance::GetVector( int lod, int comp, int surf, int vec,
							  float* pX, float* pY, float* pZ, float* pW ) const
	{
		check( lod>=0 && lod<(int)m_pD->m_lods.size() );
		check( comp>=0 && comp<(int)m_pD->m_lods[lod].m_components.size() );
        check( surf>=0 && surf<(int)m_pD->m_lods[lod].m_components[comp].m_surfaces.size() );
        check( vec>=0 && vec<(int)m_pD->m_lods[lod].m_components[comp].m_surfaces[surf].m_vectors.size() );

        vec4<float> r = m_pD->m_lods[lod].m_components[comp].m_surfaces[surf].m_vectors[vec].m_vec;

		if ( pX ) { *pX = r[0]; }
		if ( pY ) { *pY = r[1]; }
		if ( pZ ) { *pZ = r[2]; }
		if ( pW ) { *pW = r[3]; }
	}


	//---------------------------------------------------------------------------------------------
    const char* Instance::GetVectorName( int lod, int comp, int surf, int vec ) const
	{
		check( lod>=0 && lod<(int)m_pD->m_lods.size() );
		check( comp>=0 && comp<(int)m_pD->m_lods[lod].m_components.size() );
        check( surf>=0 && surf<(int)m_pD->m_lods[lod].m_components[comp].m_surfaces.size() );
        check( vec>=0 && vec<(int)m_pD->m_lods[lod].m_components[comp].m_surfaces[surf].m_vectors.size() );

        const char* strResult = m_pD->m_lods[lod].m_components[comp].m_surfaces[surf].m_vectors[vec].m_name.c_str();
		return strResult;
	}


	//---------------------------------------------------------------------------------------------
    float Instance::GetScalar( int lod, int comp, int surf, int sca ) const
	{
		check( lod>=0 && lod<(int)m_pD->m_lods.size() );
		check( comp>=0 && comp<(int)m_pD->m_lods[lod].m_components.size() );
        check( surf>=0 && surf<(int)m_pD->m_lods[lod].m_components[comp].m_surfaces.size() );
        check( sca>=0 && sca<(int)m_pD->m_lods[lod].m_components[comp].m_surfaces[surf].m_scalars.size() );

        float result = m_pD->m_lods[lod].m_components[comp].m_surfaces[surf].m_scalars[sca].m_scalar;
		return result;
	}


	//---------------------------------------------------------------------------------------------
    const char* Instance::GetScalarName( int lod, int comp, int surf, int sca ) const
	{
		check( lod>=0 && lod<(int)m_pD->m_lods.size() );
		check( comp>=0 && comp<(int)m_pD->m_lods[lod].m_components.size() );
        check( surf>=0 && surf<(int)m_pD->m_lods[lod].m_components[comp].m_surfaces.size() );
        check( sca>=0 && sca<(int)m_pD->m_lods[lod].m_components[comp].m_surfaces[surf].m_scalars.size() );

        const char* strResult = m_pD->m_lods[lod].m_components[comp].m_surfaces[surf].m_scalars[sca].m_name.c_str();
		return strResult;
	}


    //---------------------------------------------------------------------------------------------
    const char* Instance::GetString( int lod, int comp, int surf, int str ) const
    {
        check( lod >= 0 && lod < (int)m_pD->m_lods.size() );
        check( comp >= 0 && comp < (int)m_pD->m_lods[lod].m_components.size() );
        check( surf >= 0 &&
                        surf < (int)m_pD->m_lods[lod].m_components[comp].m_surfaces.size() );

        bool valid =
            str >= 0 &&
            str < (int)m_pD->m_lods[lod].m_components[comp].m_surfaces[surf].m_strings.size();
        check(valid);

        if (valid)
        {
            const char* result = m_pD->m_lods[lod]
                                     .m_components[comp]
                                     .m_surfaces[surf]
                                     .m_strings[str]
                                     .m_string.c_str();
            return result;
        }

        return "";
    }


    //---------------------------------------------------------------------------------------------
    const char* Instance::GetStringName( int lod, int comp, int surf, int str ) const
    {
        check( lod >= 0 && lod < (int)m_pD->m_lods.size() );
        check( comp >= 0 && comp < (int)m_pD->m_lods[lod].m_components.size() );
        check( surf >= 0 &&
                        surf < (int)m_pD->m_lods[lod].m_components[comp].m_surfaces.size() );
        bool valid =
            str >= 0 &&
            str < (int)m_pD->m_lods[lod].m_components[comp].m_surfaces[surf].m_strings.size();
        check( valid );

        if (valid)
        {
            const char* strResult =
                m_pD->m_lods[lod].m_components[comp].m_surfaces[surf].m_strings[str].m_name.c_str();
            return strResult;
        }

        return "";
    }


    //---------------------------------------------------------------------------------------------
	int Instance::Private::AddLOD()
	{
		int result = (int)m_lods.size();
		m_lods.push_back( INSTANCE_LOD() );
		return result;
	}


    //---------------------------------------------------------------------------------------------
    int Instance::Private::AddComponent( int lod )
    {
        // Automatically create the necessary lods and components
        while ( lod>=GetLODCount() )
        {
            AddLOD();
        }

        int result = (int)m_lods[lod].m_components.size();
        m_lods[lod].m_components.push_back( INSTANCE_COMPONENT() );
        return result;
    }


    //---------------------------------------------------------------------------------------------
    int Instance::Private::AddSurface( int lod, int comp )
    {
        // Automatically create the necessary lods and components
        while ( lod>=GetLODCount() )
        {
            AddLOD();
        }
        while ( comp>=GetComponentCount(lod) )
        {
            AddComponent(lod);
        }

        int result = (int)m_lods[lod].m_components[comp].m_surfaces.size();
        m_lods[lod].m_components[comp].m_surfaces.push_back( INSTANCE_SURFACE() );
        return result;
    }


    //---------------------------------------------------------------------------------------------
    void Instance::Private::SetComponentName( int lod, int comp, const char* strName )
    {
        // Automatically create the necessary lods and components
        while ( lod>=GetLODCount() )
        {
            AddLOD();
        }
        while ( comp>=GetComponentCount(lod) )
        {
            AddComponent( lod );
        }

        INSTANCE_COMPONENT& component = m_lods[lod].m_components[comp];
        if (strName)
        {
            component.m_name = strName;
        }
        else
        {
            component.m_name = "";
        }
    }


    //---------------------------------------------------------------------------------------------
    void Instance::Private::SetSurfaceName( int lod, int comp, int surf, const char* strName )
    {
        // Automatically create the necessary lods and components
        while ( lod>=GetLODCount() )
        {
            AddLOD();
        }
        while ( comp>=GetComponentCount(lod) )
        {
            AddComponent( lod );
        }
        while ( surf>=GetSurfaceCount(lod, comp) )
        {
            AddSurface( lod, comp );
        }

        INSTANCE_SURFACE& surface = m_lods[lod].m_components[comp].m_surfaces[surf];
        if (strName)
        {
            surface.m_name = strName;
        }
        else
        {
            surface.m_name = "";
        }
    }


	//---------------------------------------------------------------------------------------------
    int Instance::Private::AddMesh( int lod, int comp,
                                    RESOURCE_ID meshId,
                                    const char* strName )
	{
		// Automatically create the necessary lods and components
		while ( lod>=GetLODCount() )
		{
			AddLOD();
		}
		while ( comp>=GetComponentCount(lod) )
		{
			AddComponent( lod );
		}

		INSTANCE_COMPONENT& component = m_lods[lod].m_components[comp];
        int result = (int)component.m_meshes.size();
        component.m_meshes.emplace_back( meshId, strName );

		return result;
	}


	//---------------------------------------------------------------------------------------------
    int Instance::Private::AddImage( int lod, int comp, int surf,
                                     RESOURCE_ID imageId,
                                     const char* strName )
	{
		// Automatically create the necessary lods and components
		while ( lod>=GetLODCount() )
		{
			AddLOD();
		}
		while ( comp>=GetComponentCount(lod) )
		{
			AddComponent( lod );
		}
        while ( surf>=GetSurfaceCount(lod, comp) )
        {
            AddSurface( lod, comp );
        }

        INSTANCE_SURFACE& surface = m_lods[lod].m_components[comp].m_surfaces[surf];
        int result = (int)surface.m_images.size();
        surface.m_images.emplace_back( imageId, strName );

        return result;
	}


	//---------------------------------------------------------------------------------------------
    int Instance::Private::AddVector( int lod, int comp, int surf, const vec4<float>& vec, const char* strName )
	{
		// Automatically create the necessary lods and components
		while ( lod>=GetLODCount() )
		{
			AddLOD();
		}
		while ( comp>=GetComponentCount(lod) )
		{
			AddComponent( lod );
		}
        while ( surf>=GetSurfaceCount(lod, comp) )
        {
            AddSurface( lod, comp );
        }

        INSTANCE_SURFACE& surface = m_lods[lod].m_components[comp].m_surfaces[surf];
        int result = (int)surface.m_vectors.size();
        surface.m_vectors.emplace_back( vec, strName );

        return result;
	}


    //---------------------------------------------------------------------------------------------
    int Instance::Private::AddScalar( int lod, int comp, int surf, float sca, const char* strName )
    {
        // Automatically create the necessary lods and components
        while ( lod >= GetLODCount() )
        {
            AddLOD();
        }
        while ( comp >= GetComponentCount( lod ) )
        {
            AddComponent( lod );
        }
        while ( surf >= GetSurfaceCount( lod, comp ) )
        {
            AddSurface( lod, comp );
        }

        INSTANCE_SURFACE& surface = m_lods[lod].m_components[comp].m_surfaces[surf];
        int result = (int)surface.m_scalars.size();
        surface.m_scalars.emplace_back( sca, strName );

        return result;
    }


    //---------------------------------------------------------------------------------------------
    int Instance::Private::AddString(
        int lod, int comp, int surf, const char* str, const char* strName )
    {
        // Automatically create the necessary lods and components
        while ( lod >= GetLODCount() )
        {
            AddLOD();
        }
        while ( comp >= GetComponentCount( lod ) )
        {
            AddComponent( lod );
        }
        while ( surf >= GetSurfaceCount( lod, comp ) )
        {
            AddSurface( lod, comp );
        }

        INSTANCE_SURFACE& surface = m_lods[lod].m_components[comp].m_surfaces[surf];
        int result = (int)surface.m_strings.size();
        surface.m_strings.emplace_back( str, strName );

        return result;
    }
}

