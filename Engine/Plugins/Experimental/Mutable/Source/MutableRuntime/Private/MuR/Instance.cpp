// Copyright Epic Games, Inc. All Rights Reserved.


#include "MuR/Instance.h"

#include "HAL/LowLevelMemTracker.h"
#include "Misc/AssertionMacros.h"
#include "MuR/InstancePrivate.h"
#include "MuR/MutableMath.h"

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
	int32 Instance::GetDataSize() const
	{
		return 16 + sizeof(Private) + m_pD->m_lods.GetAllocatedSize() + m_pD->m_extensionData.GetAllocatedSize();
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
		return m_lods.Num();
	}


	//---------------------------------------------------------------------------------------------
	int Instance::GetComponentCount( int lod ) const
	{
		return m_pD->GetComponentCount( lod );
	}


	//---------------------------------------------------------------------------------------------
	int Instance::Private::GetComponentCount( int lod ) const
	{
		check( lod>=0 && lod<m_lods.Num() );
        if ( lod>=0 && lod<m_lods.Num() )
        {
            return m_lods[lod].m_components.Num();
        }

        return 0;
	}


    //---------------------------------------------------------------------------------------------
    const char* Instance::GetComponentName( int lod, int comp ) const
    {
        if ( lod>=0 && lod<m_pD->m_lods.Num() &&
             comp>=0 && comp<m_pD->m_lods[lod].m_components.Num() )
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
		if ( lod>=0 && lod<m_pD->m_lods.Num() &&
			 comp>=0 && comp<m_pD->m_lods[lod].m_components.Num() )
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
        if ( lod>=0 && lod<m_lods.Num() &&
             comp>=0 && comp<m_lods[lod].m_components.Num() )
        {
            return m_lods[lod].m_components[comp].m_surfaces.Num();
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
        if ( lod>=0 && lod<m_pD->m_lods.Num() &&
             comp>=0 && comp<m_pD->m_lods[lod].m_components.Num() &&
             surf>=0 && surf<m_pD->m_lods[lod].m_components[comp].m_surfaces.Num() )
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
        if ( lod>=0 && lod<m_pD->m_lods.Num() &&
             comp>=0 && comp<m_pD->m_lods[lod].m_components.Num() &&
             surf>=0 && surf<m_pD->m_lods[lod].m_components[comp].m_surfaces.Num() )
        {
            return m_pD->m_lods[lod].m_components[comp].m_surfaces[surf].InternalId;
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
		if (lod >= 0 && lod < m_pD->m_lods.Num() &&
			comp >= 0 && comp < m_pD->m_lods[lod].m_components.Num())
		{
			for (int i = 0; i < m_pD->m_lods[lod].m_components[comp].m_surfaces.Num(); ++i)
			{
				if (m_pD->m_lods[lod].m_components[comp].m_surfaces[i].InternalId == id)
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
	void Instance::FindBaseSurfaceBySharedId(int32 CompIndex, int32 SharedId, int32& OutSurfaceIndex, int32& OutLODIndex) const
	{
		for (int32 LodIndex = 0; LodIndex < m_pD->m_lods.Num(); LodIndex++)
		{
			if (m_pD->m_lods[LodIndex].m_components.IsValidIndex(CompIndex))
			{
				for (int32 SurfaceIndex = 0; SurfaceIndex < m_pD->m_lods[LodIndex].m_components[CompIndex].m_surfaces.Num(); ++SurfaceIndex)
				{
					if (m_pD->m_lods[LodIndex].m_components[CompIndex].m_surfaces[SurfaceIndex].SharedId == SharedId)
					{
						OutSurfaceIndex = SurfaceIndex;
						OutLODIndex = LodIndex;
						return;
					}
				}
			}

		}

		OutSurfaceIndex = INDEX_NONE;
		OutLODIndex = INDEX_NONE;
	}


	//---------------------------------------------------------------------------------------------
	int32 Instance::GetSharedSurfaceId(int32 LodIndex, int32 CompIndex, int32 SurfaceIndex) const
	{
		if (LodIndex >= 0 && LodIndex < m_pD->m_lods.Num() &&
			CompIndex >= 0 && CompIndex < m_pD->m_lods[LodIndex].m_components.Num() &&
			SurfaceIndex >= 0 && SurfaceIndex < m_pD->m_lods[LodIndex].m_components[CompIndex].m_surfaces.Num())
		{
			return m_pD->m_lods[LodIndex].m_components[CompIndex].m_surfaces[SurfaceIndex].SharedId;
		}
		else
		{
			check(false);
		}

		return 0;
	}
	

    //---------------------------------------------------------------------------------------------
    uint32_t Instance::GetSurfaceCustomId( int lod, int comp, int surf ) const
    {
        if ( lod>=0 && lod<m_pD->m_lods.Num() &&
             comp>=0 && comp<m_pD->m_lods[lod].m_components.Num() &&
             surf>=0 && surf<m_pD->m_lods[lod].m_components[comp].m_surfaces.Num() )
        {
            return m_pD->m_lods[lod].m_components[comp].m_surfaces[surf].ExternalId;
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
		check( lod>=0 && lod<m_lods.Num() );
		check( comp>=0 && comp<m_lods[lod].m_components.Num() );

        return m_lods[lod].m_components[comp].m_meshes.Num();
	}


	//---------------------------------------------------------------------------------------------
    int Instance::GetImageCount( int lod, int comp, int surf ) const
	{
        return m_pD->GetImageCount( lod, comp, surf );
	}


	//---------------------------------------------------------------------------------------------
    int Instance::Private::GetImageCount( int lod, int comp, int surf ) const
	{
		check( lod>=0 && lod<m_lods.Num() );
		check( comp>=0 && comp<m_lods[lod].m_components.Num() );
        check( surf>=0 && surf<m_lods[lod].m_components[comp].m_surfaces.Num() );

        return m_lods[lod].m_components[comp].m_surfaces[surf].m_images.Num();
	}


	//---------------------------------------------------------------------------------------------
    int Instance::GetVectorCount( int lod, int comp, int surf ) const
	{
        return m_pD->GetVectorCount( lod, comp, surf );
	}


	//---------------------------------------------------------------------------------------------
    int Instance::Private::GetVectorCount( int lod, int comp, int surf ) const
	{
		check( lod>=0 && lod<m_lods.Num() );
		check( comp>=0 && comp<m_lods[lod].m_components.Num() );
        check( surf>=0 && surf<m_lods[lod].m_components[comp].m_surfaces.Num() );

        return m_lods[lod].m_components[comp].m_surfaces[surf].m_vectors.Num();
	}


	//---------------------------------------------------------------------------------------------
    int Instance::GetScalarCount( int lod, int comp, int surf ) const
	{
        return m_pD->GetScalarCount( lod, comp, surf );
	}


	//---------------------------------------------------------------------------------------------
    int Instance::Private::GetScalarCount( int lod, int comp, int surf ) const
	{
		check( lod>=0 && lod<m_lods.Num() );
		check( comp>=0 && comp<m_lods[lod].m_components.Num() );
        check( surf>=0 && surf<m_lods[lod].m_components[comp].m_surfaces.Num() );

        return m_lods[lod].m_components[comp].m_surfaces[surf].m_scalars.Num();
	}


    //---------------------------------------------------------------------------------------------
    int Instance::GetStringCount( int lod, int comp, int surf ) const
    {
        return m_pD->GetStringCount( lod, comp, surf );
    }


    //---------------------------------------------------------------------------------------------
    int Instance::Private::GetStringCount( int lod, int comp, int surf ) const
    {
        check( lod >= 0 && lod < m_lods.Num() );
        check( comp >= 0 && comp < m_lods[lod].m_components.Num() );
        check( surf >= 0 && surf < m_lods[lod].m_components[comp].m_surfaces.Num() );

        return m_lods[lod].m_components[comp].m_surfaces[surf].m_strings.Num();
    }


    //---------------------------------------------------------------------------------------------
    RESOURCE_ID Instance::GetMeshId( int lod, int comp, int mesh ) const
    {
        check( lod>=0 && lod<m_pD->m_lods.Num() );
        check( comp>=0 && comp<m_pD->m_lods[lod].m_components.Num() );
        check( mesh>=0 && mesh<m_pD->m_lods[lod].m_components[comp].m_meshes.Num() );

        RESOURCE_ID result = m_pD->m_lods[lod].m_components[comp].m_meshes[mesh].m_meshId;
        return result;
    }


	//---------------------------------------------------------------------------------------------
    const char* Instance::GetMeshName( int lod, int comp, int mesh ) const
	{
		check( lod>=0 && lod<m_pD->m_lods.Num() );
		check( comp>=0 && comp<m_pD->m_lods[lod].m_components.Num() );
        check( mesh>=0 && mesh<m_pD->m_lods[lod].m_components[comp].m_meshes.Num() );

        const char* strResult = m_pD->m_lods[lod].m_components[comp].m_meshes[mesh].m_name.c_str();
		return strResult;
	}


	//---------------------------------------------------------------------------------------------
    RESOURCE_ID Instance::GetImageId( int lod, int comp, int surf, int img ) const
	{
		check( lod>=0 && lod<m_pD->m_lods.Num() );
		check( comp>=0 && comp<m_pD->m_lods[lod].m_components.Num() );
        check( surf>=0 && surf<m_pD->m_lods[lod].m_components[comp].m_surfaces.Num() );
        check( img>=0 && img<m_pD->m_lods[lod].m_components[comp].m_surfaces[surf].m_images.Num() );

        RESOURCE_ID result = m_pD->m_lods[lod].m_components[comp].m_surfaces[surf].m_images[img].m_imageId;
        return result;
	}


	//---------------------------------------------------------------------------------------------
    const char* Instance::GetImageName( int lod, int comp, int surf, int img ) const
	{
		check( lod>=0 && lod<m_pD->m_lods.Num() );
		check( comp>=0 && comp<m_pD->m_lods[lod].m_components.Num() );
        check( surf>=0 && surf<m_pD->m_lods[lod].m_components[comp].m_surfaces.Num() );
        check( img>=0 && img<m_pD->m_lods[lod].m_components[comp].m_surfaces[surf].m_images.Num() );

        const char* strResult = m_pD->m_lods[lod].m_components[comp].m_surfaces[surf].m_images[img].m_name.c_str();
		return strResult;
	}


	//---------------------------------------------------------------------------------------------
    void Instance::GetVector( int lod, int comp, int surf, int vec,
							  float* pX, float* pY, float* pZ, float* pW ) const
	{
		check( lod>=0 && lod<m_pD->m_lods.Num() );
		check( comp>=0 && comp<m_pD->m_lods[lod].m_components.Num() );
        check( surf>=0 && surf<m_pD->m_lods[lod].m_components[comp].m_surfaces.Num() );
        check( vec>=0 && vec<m_pD->m_lods[lod].m_components[comp].m_surfaces[surf].m_vectors.Num() );

        FVector4f r = m_pD->m_lods[lod].m_components[comp].m_surfaces[surf].m_vectors[vec].m_vec;

		if ( pX ) { *pX = r[0]; }
		if ( pY ) { *pY = r[1]; }
		if ( pZ ) { *pZ = r[2]; }
		if ( pW ) { *pW = r[3]; }
	}


	//---------------------------------------------------------------------------------------------
    const char* Instance::GetVectorName( int lod, int comp, int surf, int vec ) const
	{
		check( lod>=0 && lod<m_pD->m_lods.Num() );
		check( comp>=0 && comp<m_pD->m_lods[lod].m_components.Num() );
        check( surf>=0 && surf<m_pD->m_lods[lod].m_components[comp].m_surfaces.Num() );
        check( vec>=0 && vec<m_pD->m_lods[lod].m_components[comp].m_surfaces[surf].m_vectors.Num() );

        const char* strResult = m_pD->m_lods[lod].m_components[comp].m_surfaces[surf].m_vectors[vec].m_name.c_str();
		return strResult;
	}


	//---------------------------------------------------------------------------------------------
    float Instance::GetScalar( int lod, int comp, int surf, int sca ) const
	{
		check( lod>=0 && lod<m_pD->m_lods.Num() );
		check( comp>=0 && comp<m_pD->m_lods[lod].m_components.Num() );
        check( surf>=0 && surf<m_pD->m_lods[lod].m_components[comp].m_surfaces.Num() );
        check( sca>=0 && sca<m_pD->m_lods[lod].m_components[comp].m_surfaces[surf].m_scalars.Num() );

        float result = m_pD->m_lods[lod].m_components[comp].m_surfaces[surf].m_scalars[sca].m_scalar;
		return result;
	}


	//---------------------------------------------------------------------------------------------
    const char* Instance::GetScalarName( int lod, int comp, int surf, int sca ) const
	{
		check( lod>=0 && lod<m_pD->m_lods.Num() );
		check( comp>=0 && comp<m_pD->m_lods[lod].m_components.Num() );
        check( surf>=0 && surf<m_pD->m_lods[lod].m_components[comp].m_surfaces.Num() );
        check( sca>=0 && sca<m_pD->m_lods[lod].m_components[comp].m_surfaces[surf].m_scalars.Num() );

        const char* strResult = m_pD->m_lods[lod].m_components[comp].m_surfaces[surf].m_scalars[sca].m_name.c_str();
		return strResult;
	}


    //---------------------------------------------------------------------------------------------
    const char* Instance::GetString( int lod, int comp, int surf, int str ) const
    {
        check( lod >= 0 && lod < m_pD->m_lods.Num() );
        check( comp >= 0 && comp < m_pD->m_lods[lod].m_components.Num() );
        check( surf >= 0 &&
                        surf < m_pD->m_lods[lod].m_components[comp].m_surfaces.Num() );

        bool valid =
            str >= 0 &&
            str < m_pD->m_lods[lod].m_components[comp].m_surfaces[surf].m_strings.Num();
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
        check( lod >= 0 && lod < m_pD->m_lods.Num() );
        check( comp >= 0 && comp < m_pD->m_lods[lod].m_components.Num() );
        check( surf >= 0 &&
                        surf < m_pD->m_lods[lod].m_components[comp].m_surfaces.Num() );
        bool valid =
            str >= 0 &&
            str < m_pD->m_lods[lod].m_components[comp].m_surfaces[surf].m_strings.Num();
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
	int32 Instance::GetExtensionDataCount() const
	{
		return m_pD->m_extensionData.Num();
	}


    //---------------------------------------------------------------------------------------------
	void Instance::GetExtensionData(int32 Index, ExtensionDataPtrConst& OutExtensionData, const char*& OutName) const
	{
		check(m_pD->m_extensionData.IsValidIndex(Index));

		OutExtensionData = m_pD->m_extensionData[Index].Data;
		OutName = m_pD->m_extensionData[Index].Name.c_str();
	}


    //---------------------------------------------------------------------------------------------
	int Instance::Private::AddLOD()
	{
		int result = m_lods.Num();
		m_lods.Add( INSTANCE_LOD() );
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

        int result = m_lods[lod].m_components.Num();
        m_lods[lod].m_components.Add( INSTANCE_COMPONENT() );
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

        int result = m_lods[lod].m_components[comp].m_surfaces.Num();
        m_lods[lod].m_components[comp].m_surfaces.Add( INSTANCE_SURFACE() );
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
        int result = component.m_meshes.Num();
        component.m_meshes.Emplace( meshId, strName );

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
        int result = surface.m_images.Num();
        surface.m_images.Emplace( imageId, strName );

        return result;
	}


	//---------------------------------------------------------------------------------------------
    int Instance::Private::AddVector( int lod, int comp, int surf, const FVector4f& vec, const char* strName )
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
        int result = surface.m_vectors.Num();
        surface.m_vectors.Emplace( vec, strName );

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
        int result = surface.m_scalars.Num();
        surface.m_scalars.Emplace( sca, strName );

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
        int result = surface.m_strings.Num();
        surface.m_strings.Emplace( str, strName );

        return result;
    }


    //---------------------------------------------------------------------------------------------
	void Instance::Private::AddExtensionData(ExtensionDataPtrConst Data, const char* Name)
	{
		check(Data);
		check(Name);

		NamedExtensionData& Entry = m_extensionData.AddDefaulted_GetRef();
		Entry.Data = Data;
		Entry.Name = Name;

		check(Entry.Name.length() > 0);
	}
}

