// Copyright Epic Games, Inc. All Rights Reserved.


#include "MuT/NodeObjectNew.h"

#include "HAL/PlatformString.h"
#include "Misc/AssertionMacros.h"
#include "MuR/MemoryPrivate.h"
#include "MuT/CompilerPrivate.h"
#include "MuT/NodeComponent.h"
#include "MuT/NodeComponentNew.h"
#include "MuT/NodeLOD.h"
#include "MuT/NodeLODPrivate.h"
#include "MuT/NodeMesh.h"
#include "MuT/NodeMeshConstant.h"
#include "MuT/NodeMeshPrivate.h"
#include "MuT/NodeObjectNewPrivate.h"
#include "MuT/NodePrivate.h"
#include "MuT/NodeSurface.h"
#include "MuT/NodeSurfaceNew.h"

#include <memory>
#include <utility>


namespace mu
{


	//---------------------------------------------------------------------------------------------
	// Static initialisation
	//---------------------------------------------------------------------------------------------
	NODE_TYPE NodeObjectNew::Private::s_type =
			NODE_TYPE( "Object", NodeObjectNew::GetStaticType() );


	//---------------------------------------------------------------------------------------------
	//!
	//---------------------------------------------------------------------------------------------

	MUTABLE_IMPLEMENT_NODE( NodeObjectNew, EType::New, Node, Node::EType::Object )


	//---------------------------------------------------------------------------------------------
	// Node Interface
	//---------------------------------------------------------------------------------------------
	int NodeObjectNew::GetInputCount() const
	{
		return (int)(m_pD->m_lods.size() + m_pD->m_children.size());
	}


	//---------------------------------------------------------------------------------------------
	Node* NodeObjectNew::GetInputNode( int i ) const
	{
		check( i>=0 && i<GetInputCount() );

		Node* pResult = 0;

		if ( i<(int)m_pD->m_lods.size() )
		{
			pResult = m_pD->m_lods[i].get();
		}
		else
		{
			i -= (int)m_pD->m_lods.size();
			pResult = m_pD->m_children[i].get();
		}

		return pResult;
	}


	//---------------------------------------------------------------------------------------------
	void NodeObjectNew::SetInputNode( int i, NodePtr pNode )
	{
		check( i>=0 && i<GetInputCount() );

		if ( i<(int)m_pD->m_lods.size() )
		{
			m_pD->m_lods[i] = dynamic_cast<NodeLOD*>( pNode.get() );
		}
		else
		{
			i -= (int)m_pD->m_lods.size();
			m_pD->m_children[i] = dynamic_cast<NodeObject*>( pNode.get() );
		}
	}


	//---------------------------------------------------------------------------------------------
	// Own Interface
	//---------------------------------------------------------------------------------------------
	const char* NodeObjectNew::GetName() const
	{
		return m_pD->m_name.c_str();
	}


	//---------------------------------------------------------------------------------------------
	void NodeObjectNew::SetName( const char* strName )
	{
		if( strName )
		{
			m_pD->m_name = strName;
		}
		else
		{
			m_pD->m_name = "";
		}
	}


	const char* NodeObjectNew::GetUid() const
	{
		return m_pD->m_uid.c_str();
	}


	//---------------------------------------------------------------------------------------------
	void NodeObjectNew::SetUid( const char* strUid )
	{
		if( strUid )
		{
			m_pD->m_uid = strUid;
		}
		else
		{
			m_pD->m_uid = "";
		}
	}


	//---------------------------------------------------------------------------------------------
	int NodeObjectNew::GetLODCount() const
	{
		return (int)m_pD->m_lods.size();
	}


	//---------------------------------------------------------------------------------------------
	void NodeObjectNew::SetLODCount( int num )
	{
		check( num >=0 );
		m_pD->m_lods.resize( num );
	}


	//---------------------------------------------------------------------------------------------
	NodeLODPtr NodeObjectNew::GetLOD( int index ) const
	{
		check( index >=0 && index < (int)m_pD->m_lods.size() );

		return m_pD->m_lods[ index ].get();
	}


	//---------------------------------------------------------------------------------------------
	void NodeObjectNew::SetLOD( int index, NodeLODPtr pLOD )
	{
		check( index >=0 && index < (int)m_pD->m_lods.size() );

		m_pD->m_lods[ index ] = pLOD;
	}


	//---------------------------------------------------------------------------------------------
	int NodeObjectNew::GetChildCount() const
	{
		return (int)m_pD->m_children.size();
	}


	//---------------------------------------------------------------------------------------------
	void NodeObjectNew::SetChildCount( int num )
	{
		check( num >=0 );
		m_pD->m_children.resize( num );
	}


	//---------------------------------------------------------------------------------------------
	NodeObjectPtr NodeObjectNew::GetChild( int index ) const
	{
		check( index >=0 && index < (int)m_pD->m_children.size() );

		return m_pD->m_children[ index ].get();
	}


	//---------------------------------------------------------------------------------------------
	void NodeObjectNew::SetChild( int index, NodeObjectPtr pObject )
	{
		check( index >=0 && index < (int)m_pD->m_children.size() );

		m_pD->m_children[ index ] = pObject;
	}


	//---------------------------------------------------------------------------------------------
    NodeLayoutPtr NodeObjectNew::Private::GetLayout( int lod,
                                                     int component,
                                                     int surface,
                                                     int //texture
                                                     ) const
	{
		check( lod>=0 && lod<(int)m_lods.size() );

		NodeLayoutPtr pLayout;

		const NodeComponentNew* pComp =
			dynamic_cast<const NodeComponentNew*> ( m_lods[lod]->GetComponent( component ).get() );

        if (pComp)
        {
            const NodeSurfaceNew* pSurface =
                    dynamic_cast<const NodeSurfaceNew*> ( pComp->GetSurface( surface ) );

            if ( pSurface )
            {
                // TODO: Look for the layout index of the given texture
                // TODO: Multiple meshes
                if ( pSurface->GetMeshCount()>0 )
                {
                     const NodeMesh* pMesh =
                             dynamic_cast<const NodeMesh*> ( pSurface->GetMesh( 0 ).get() );

                    if ( pMesh )
                    {
                        NodeMesh::Private* pPrivate =
                                dynamic_cast<NodeMesh::Private*>( pMesh->GetBasePrivate() );

                        pLayout = pPrivate->GetLayout( 0 );
                    }
                }
            }
        }

		// TODO: layout index
		return pLayout;
	}


    //---------------------------------------------------------------------------------------------
    bool NodeObjectNew::Private::HasComponent( const NodeComponent* pComponent ) const
    {
        bool found = false;
        for ( size_t l=0; !found && l<m_lods.size(); ++l )
        {
            found = std::find( m_lods[l]->GetPrivate()->m_components.begin(),
                               m_lods[l]->GetPrivate()->m_components.end(),
                               pComponent )
                    != m_lods[l]->GetPrivate()->m_components.end();
        }

        return found;
    }


	//---------------------------------------------------------------------------------------------
	int NodeObjectNew::GetStateCount() const
	{
		return (int)m_pD->m_states.size();
	}


	//---------------------------------------------------------------------------------------------
	void NodeObjectNew::SetStateCount( int c )
	{
		m_pD->m_states.resize( c );
	}


	//---------------------------------------------------------------------------------------------
	const char* NodeObjectNew::GetStateName( int s ) const
	{
		check( s>=0 && s<GetStateCount() );
		return m_pD->m_states[s].m_name.c_str();
	}


	//---------------------------------------------------------------------------------------------
	void NodeObjectNew::SetStateName( int s, const char* n )
	{
		check( s>=0 && s<GetStateCount() );
		m_pD->m_states[s].m_name = n;
	}


	//---------------------------------------------------------------------------------------------
	bool NodeObjectNew::HasStateParam( int s, const char* param ) const
	{
		check( s>=0 && s<GetStateCount() );
		return std::find( m_pD->m_states[s].m_runtimeParams.begin(),
						  m_pD->m_states[s].m_runtimeParams.end(),
						  param )
				!=
				m_pD->m_states[s].m_runtimeParams.end();
	}


	//---------------------------------------------------------------------------------------------
	void NodeObjectNew::AddStateParam( int s, const char* param )
	{
		check( s>=0 && s<GetStateCount() );

		if (!HasStateParam(s,param))
		{
			m_pD->m_states[s].m_runtimeParams.push_back( param );
		}
	}


	//---------------------------------------------------------------------------------------------
	void NodeObjectNew::RemoveStateParam( int s, const char* param )
	{
		check( s>=0 && s<GetStateCount() );

		vector<string>::iterator it = std::find( m_pD->m_states[s].m_runtimeParams.begin(),
														   m_pD->m_states[s].m_runtimeParams.end(),
														   param );
		if ( it != m_pD->m_states[s].m_runtimeParams.end() )
		{
			m_pD->m_states[s].m_runtimeParams.erase( it );
		}
	}


    //---------------------------------------------------------------------------------------------
    void NodeObjectNew::SetStateProperties( int s, bool avoidRuntimeCompression, bool onlyFirstLOD, int firstLOD )
    {
        check( s>=0 && s<GetStateCount() );

        m_pD->m_states[s].m_optimisation.m_avoidRuntimeCompression = avoidRuntimeCompression;
        m_pD->m_states[s].m_optimisation.m_onlyFirstLOD = onlyFirstLOD;
        m_pD->m_states[s].m_optimisation.m_firstLOD = firstLOD;
    }

}


