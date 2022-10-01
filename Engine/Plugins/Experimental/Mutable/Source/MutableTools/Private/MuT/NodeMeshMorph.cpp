// Copyright Epic Games, Inc. All Rights Reserved.


#include "MuT/NodeMeshMorph.h"
#include "MuT/NodeMeshMorphPrivate.h"

#include "MuT/NodeScalar.h"

#include "MuR/MeshPrivate.h"



namespace mu
{

	//---------------------------------------------------------------------------------------------
	// Static initialisation
	//---------------------------------------------------------------------------------------------
	NODE_TYPE NodeMeshMorph::Private::s_type =
			NODE_TYPE( "MeshMorph", NodeMesh::GetStaticType() );


	//---------------------------------------------------------------------------------------------
	//!
	//---------------------------------------------------------------------------------------------

	MUTABLE_IMPLEMENT_NODE( NodeMeshMorph, EType::Morph, Node, Node::EType::Mesh)


	//---------------------------------------------------------------------------------------------
	// Node Interface
	//---------------------------------------------------------------------------------------------
	int NodeMeshMorph::GetInputCount() const
	{
		return 2 + (int)m_pD->m_morphs.size();
	}


	//---------------------------------------------------------------------------------------------
	Node* NodeMeshMorph::GetInputNode( int i ) const
	{
		check( i>=0 && i<GetInputCount() );

		Node* pResult = 0;

		switch (i)
		{
		case 0:
			pResult = m_pD->m_pFactor.get();
			break;

		case 1:
			pResult = m_pD->m_pBase.get();
			break;

		default:
			pResult = m_pD->m_morphs[i-2].get();
			break;
		}

		return pResult;
	}


	//---------------------------------------------------------------------------------------------
	void NodeMeshMorph::SetInputNode( int i, NodePtr pNode )
	{
		check( i>=0 && i<GetInputCount() );

		switch (i)
		{
		case 0:
			m_pD->m_pFactor = dynamic_cast<NodeScalar*>(pNode.get());
			break;

		case 1:
			m_pD->m_pBase = dynamic_cast<NodeMesh*>(pNode.get());
			break;

		default:
			m_pD->m_morphs[i-2] = dynamic_cast<NodeMesh*>(pNode.get());
			break;
		}
	}


	//---------------------------------------------------------------------------------------------
	// Own Interface
	//---------------------------------------------------------------------------------------------
	NodeScalarPtr NodeMeshMorph::GetFactor() const
	{
		return m_pD->m_pFactor.get();
	}


	//---------------------------------------------------------------------------------------------
	void NodeMeshMorph::SetFactor( NodeScalarPtr pNode )
	{
		m_pD->m_pFactor = pNode;
	}


	//---------------------------------------------------------------------------------------------
	NodeMeshPtr NodeMeshMorph::GetBase() const
	{
		return m_pD->m_pBase.get();
	}


	//---------------------------------------------------------------------------------------------
	void NodeMeshMorph::SetBase( NodeMeshPtr pNode )
	{
		m_pD->m_pBase = pNode;
	}


	//---------------------------------------------------------------------------------------------
	NodeMeshPtr NodeMeshMorph::GetMorph( int t ) const
	{
		check( t>=0 && t<(int)m_pD->m_morphs.size() );

		return m_pD->m_morphs[t].get();
	}


	//---------------------------------------------------------------------------------------------
	void NodeMeshMorph::SetMorph( int t, NodeMeshPtr pNode )
	{
		check( t>=0 && t<(int)m_pD->m_morphs.size() );
		m_pD->m_morphs[t] = pNode;
	}

	//---------------------------------------------------------------------------------------------
	void NodeMeshMorph::SetMorphCount( int t )
	{
		m_pD->m_morphs.resize(t);
	}

	//---------------------------------------------------------------------------------------------
	int NodeMeshMorph::GetMorphCount() const
	{
		return (int)m_pD->m_morphs.size();
	}

	//---------------------------------------------------------------------------------------------
	void NodeMeshMorph::SetMorphIndicesAreRelative( bool relative )
	{
		m_pD->m_vertexIndicesAreRelative = relative;
	}

	//---------------------------------------------------------------------------------------------
	void NodeMeshMorph::SetReshapeSkeleton(bool bEnable)
	{
		m_pD->m_reshapeSkeleton = bEnable;
	}	

	//---------------------------------------------------------------------------------------------
	void NodeMeshMorph::SetDeformAllBones(bool bEnable)
	{
		m_pD->m_deformAllBones = bEnable;
	}

	//---------------------------------------------------------------------------------------------
	void NodeMeshMorph::SetDeformAllPhysics(bool bEnable)
	{
		m_pD->m_deformAllPhysics = bEnable;
	}

	//---------------------------------------------------------------------------------------------
	void NodeMeshMorph::AddBoneToDeform(const char* BoneName)
	{
		m_pD->m_bonesToDeform.Emplace(BoneName);
	}

	void NodeMeshMorph::AddPhysicsBodyToDeform(const char* BoneName)
	{
		m_pD->m_physicsToDeform.Emplace(BoneName);
	}

	//---------------------------------------------------------------------------------------------
	void NodeMeshMorph::SetReshapePhysicsVolumes(bool bEnable)
	{
		m_pD->m_reshapePhysicsVolumes = bEnable;
	}

	//---------------------------------------------------------------------------------------------
	NodeLayoutPtr NodeMeshMorph::Private::GetLayout( int index ) const
	{
		NodeLayoutPtr pResult;

		if ( m_pBase )
		{
			NodeMesh::Private* pPrivate =
					dynamic_cast<NodeMesh::Private*>( m_pBase->GetBasePrivate() );

			pResult = pPrivate->GetLayout( index );
		}

		return pResult;
	}


}


