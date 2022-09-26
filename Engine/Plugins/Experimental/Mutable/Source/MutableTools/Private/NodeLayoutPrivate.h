// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MutableTools/Private/NodePrivate.h"
#include "NodeLayout.h"
#include "NodeImage.h"
#include "MutableTools/Private/AST.h"
#include "MutableRuntime/Private/MemoryPrivate.h"


namespace mu
{


	class NodeLayout::Private : public Node::Private
	{
	public:

		virtual Layout* GetLayout() = 0;

	};


	class NodeLayoutBlocks::Private : public NodeLayout::Private
	{
	public:

		MUTABLE_DEFINE_CONST_VISITABLE()

	public:

		Private()
		{
			m_pLayout = new Layout();
		}

		static NODE_TYPE s_type;

		LayoutPtr m_pLayout;

		//!
		void Serialise( OutputArchive& arch ) const
		{
            uint32_t ver = 0;
			arch << ver;

			arch << m_pLayout;
		}

		//!
		void Unserialise( InputArchive& arch )
		{
            uint32_t ver;
			arch >> ver;
			check(ver==0);

			arch >> m_pLayout;
		}

        // NodeLayout::Private interface
        Layout* GetLayout() override
        {
            return m_pLayout.get();
        }
    };

}
