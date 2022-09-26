// Copyright Epic Games, Inc. All Rights Reserved.


#pragma once


#include "NodeComponentPrivate.h"
#include "NodeComponentNew.h"
#include "NodePatchImagePrivate.h"

#include "NodeMesh.h"
#include "NodePatchMesh.h"
#include "NodeImage.h"

#include "MemoryPrivate.h"


namespace mu
{

	//---------------------------------------------------------------------------------------------
	//!
	//---------------------------------------------------------------------------------------------
	class NodeComponentNew::Private : public NodeComponent::Private
	{
	public:

		MUTABLE_DEFINE_CONST_VISITABLE()

	public:

		static NODE_TYPE s_type;

		string m_name;

		uint16 m_id = 0;

        vector<NodeSurfacePtr> m_surfaces;

		//!
		void Serialise( OutputArchive& arch ) const
		{
            uint32_t ver = 3;
			
			arch << ver;

			arch << m_name;
			arch << m_id;
            arch << m_surfaces;
        }

		//!
		void Unserialise( InputArchive& arch )
		{
            uint32_t ver;
			arch >> ver;
			check(ver >= 2);

			arch >> m_name;

			if (ver >= 3)
			{
				arch >> m_id;
			}

            arch >> m_surfaces;
		}

		// NodeComponent::Private interface
        const NodeComponentNew::Private* GetParentComponentNew() const override
		{
			return this;
		}

	};

}

