// Copyright Epic Games, Inc. All Rights Reserved.


#include "MuT/ModelReport.h"

#include "Misc/AssertionMacros.h"
#include "MuR/MemoryPrivate.h"
#include "MuT/ModelReportPrivate.h"

namespace mu
{

	//---------------------------------------------------------------------------------------------
	//---------------------------------------------------------------------------------------------
	//---------------------------------------------------------------------------------------------
	ModelReport::ModelReport()
	{
		m_pD = new Private();
	}


	//---------------------------------------------------------------------------------------------
	ModelReport::~ModelReport()
	{
        check( m_pD );
		delete m_pD;
		m_pD = 0;
	}


	//---------------------------------------------------------------------------------------------
	ModelReport::Private* ModelReport::GetPrivate() const
	{
		return m_pD;
	}


	//---------------------------------------------------------------------------------------------
	int ModelReport::GetStateCount() const
	{
		return (int)m_pD->m_states.size();
	}


	//---------------------------------------------------------------------------------------------
	int ModelReport::GetLODCount( int state ) const
	{
		int res = 0;

		if ( state>=0 && state<(int)m_pD->m_states.size() )
		{
			res = (int)m_pD->m_states[state].m_lods.size();
		}
		else
		{
			check(false);
		}

		return res;
	}


	//---------------------------------------------------------------------------------------------
	int ModelReport::GetComponentCount( int state, int lod ) const
	{
		int res = 0;

		if ( state>=0 && state<(int)m_pD->m_states.size() )
		{
			res = (int)m_pD->m_states[state].m_lods[lod].m_components.size();
		}
		else
		{
			check(false);
		}

		return res;
	}


	//---------------------------------------------------------------------------------------------
	const char* ModelReport::GetComponentName( int state, int lod, int comp ) const
	{
		const char* strRes = 0;

		if ( state>=0 && state<(int)m_pD->m_states.size()
			 && comp>=0 && comp<(int)m_pD->m_states[state].m_lods[lod].m_components.size()
			 )
		{
			strRes = m_pD->m_states[state].m_lods[lod].m_components[comp].m_name.c_str();
		}
		else
		{
			check(false);
		}

		return strRes;
	}


	//---------------------------------------------------------------------------------------------
	int ModelReport::GetComponentImageCount( int state, int lod, int comp ) const
	{
		int res = 0;

		if ( state>=0 && state<(int)m_pD->m_states.size()
			 && comp>=0 && comp<(int)m_pD->m_states[state].m_lods[lod].m_components.size()
			 )
		{
			res = (int)m_pD->m_states[state].m_lods[lod].m_components[comp].m_images.size();
		}
		else
		{
			check(false);
		}

		return res;
	}


	//---------------------------------------------------------------------------------------------
	const char* ModelReport::GetImageName( int state, int lod, int comp, int image ) const
	{
		const char* res = 0;

		if ( state>=0 && state<(int)m_pD->m_states.size()
			 && comp>=0 && comp<(int)m_pD->m_states[state].m_lods[lod].m_components.size()
			 && image>=0 && image<(int)m_pD->m_states[state].m_lods[lod].m_components[comp].m_images.size() )
		{
			res = m_pD->m_states[state].m_lods[lod].m_components[comp].m_images[image].m_name.c_str();
		}
		else
		{
			check(false);
		}

		return res;
	}


	//---------------------------------------------------------------------------------------------
	const char* ModelReport::GetImageFragmentCode( int state, int lod, int comp, int image ) const
	{
		const char* res = 0;

		if ( state>=0 && state<(int)m_pD->m_states.size()
			 && comp>=0 && comp<(int)m_pD->m_states[state].m_lods[lod].m_components.size()
			 && image>=0 && image<(int)m_pD->m_states[state].m_lods[lod].m_components[comp].m_images.size() )
		{
			res = m_pD->m_states[state].m_lods[lod].m_components[comp].m_images[image].m_fragmentCode.c_str();
		}
		else
		{
			check(false);
		}

		return res;
	}


	//---------------------------------------------------------------------------------------------
	int ModelReport::GetFragmentSourceImageCount( int state, int lod, int comp, int image ) const
	{
		int result = 0;

		if ( state>=0 && state<(int)m_pD->m_states.size()
			 && comp>=0 && comp<(int)m_pD->m_states[state].m_lods[lod].m_components.size()
			 && image>=0 && image<(int)m_pD->m_states[state].m_lods[lod].m_components[comp].m_images.size() )
		{
			result = (int)m_pD->m_states[state].m_lods[lod].m_components[comp].m_images[image].m_sourceImages.size();
		}
		else
		{
			check(false);
		}

		return result;
	}


    //---------------------------------------------------------------------------------------------
    const char* ModelReport::GetFragmentSourceImageName( int state, int lod, int comp,
                                                                   int image,
                                                                   int source ) const
    {
        const char* strRes = 0;

        if ( state>=0 && state<(int)m_pD->m_states.size()
             && comp>=0 && comp<(int)m_pD->m_states[state].m_lods[lod].m_components.size()
             && image>=0 && image<(int)m_pD->m_states[state].m_lods[lod].m_components[comp].m_images.size()
             && source>=0
             && source<(int)m_pD->m_states[state].m_lods[lod].m_components[comp].m_images[image].m_sourceImages.size() )
        {
            strRes = m_pD->m_states[state].m_lods[lod].m_components[comp].m_images[image].m_sourceImages[source].c_str();
        }
		else
		{
			check(false);
		}

        return strRes;
    }


	//---------------------------------------------------------------------------------------------
	int ModelReport::GetFragmentSourceVectorCount( int state, int lod, int comp, int image ) const
	{
		int result = 0;

		if ( state>=0 && state<(int)m_pD->m_states.size()
			 && comp>=0 && comp<(int)m_pD->m_states[state].m_lods[lod].m_components.size()
			 && image>=0 && image<(int)m_pD->m_states[state].m_lods[lod].m_components[comp].m_images.size() )
		{
			result = (int)m_pD->m_states[state].m_lods[lod].m_components[comp].m_images[image].m_sourceVectors.size();
		}
		else
		{
			check(false);
		}

		return result;
	}


	//---------------------------------------------------------------------------------------------
	const char* ModelReport::GetFragmentSourceVectorName( int state, int lod, int comp,
																	int image,
																	int source ) const
	{
		const char* strRes = 0;

		if ( state>=0 && state<(int)m_pD->m_states.size()
			 && comp>=0 && comp<(int)m_pD->m_states[state].m_lods[lod].m_components.size()
			 && image>=0 && image<(int)m_pD->m_states[state].m_lods[lod].m_components[comp].m_images.size()
			 && source>=0
			 && source<(int)m_pD->m_states[state].m_lods[lod].m_components[comp].m_images[image].m_sourceVectors.size() )
		{
			strRes = m_pD->m_states[state].m_lods[lod].m_components[comp].m_images[image].m_sourceVectors[source].c_str();
		}
		else
		{
			check(false);
		}

		return strRes;
	}


	//---------------------------------------------------------------------------------------------
	int ModelReport::GetFragmentSourceScalarCount( int state, int lod, int comp,
																	  int image ) const
	{
		int result = 0;

		if ( state>=0 && state<(int)m_pD->m_states.size()
			 && comp>=0 && comp<(int)m_pD->m_states[state].m_lods[lod].m_components.size()
			 && image>=0 && image<(int)m_pD->m_states[state].m_lods[lod].m_components[comp].m_images.size() )
		{
			result = (int)m_pD->m_states[state].m_lods[lod].m_components[comp].m_images[image].m_sourceScalars.size();
		}
		else
		{
			check(false);
		}

		return result;
	}


	//---------------------------------------------------------------------------------------------
	const char* ModelReport::GetFragmentSourceScalarName( int state, int lod, int comp,
																	int image,
																	int source ) const
	{
		const char* strRes = 0;

		if ( state>=0 && state<(int)m_pD->m_states.size()
			 && comp>=0 && comp<(int)m_pD->m_states[state].m_lods[lod].m_components.size()
			 && image>=0 && image<(int)m_pD->m_states[state].m_lods[lod].m_components[comp].m_images.size()
			 && source>=0
			 && source<(int)m_pD->m_states[state].m_lods[lod].m_components[comp].m_images[image].m_sourceScalars.size() )
		{
			strRes = m_pD->m_states[state].m_lods[lod].m_components[comp].m_images[image].m_sourceScalars[source].c_str();
		}
		else
		{
			check(false);
		}

		return strRes;
	}

}

