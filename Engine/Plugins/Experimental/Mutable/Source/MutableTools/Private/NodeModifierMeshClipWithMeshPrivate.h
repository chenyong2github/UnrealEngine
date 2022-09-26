// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NodeModifierPrivate.h"
#include "NodeModifierMeshClipWithMesh.h"
#include "NodeMesh.h"
#include "AST.h"

#include "MemoryPrivate.h"
#include "MutableMath.h"


namespace mu
{

    class NodeModifierMeshClipWithMesh::Private : public NodeModifier::Private
	{
	public:

		MUTABLE_DEFINE_CONST_VISITABLE()

	public:

		Private()
		{
		}

		static NODE_TYPE s_type;

		//! 
		NodeMeshPtr m_clipMesh;

		//!
		void Serialise( OutputArchive& arch ) const
		{
			NodeModifier::Private::Serialise(arch);

            uint32_t ver = 0;
			arch << ver;

			arch << m_clipMesh;
		}

		//!
		void Unserialise( InputArchive& arch )
		{
			NodeModifier::Private::Unserialise( arch );
			
            uint32_t ver;
			arch >> ver;
            check(ver<=0);

			arch >> m_clipMesh;
		}

	};


}
