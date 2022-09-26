// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NodePrivate.h"

#include "NodeMesh.h"
#include "NodeLayout.h"


namespace mu
{


	class NodeMesh::Private : public Node::Private
	{
	public:

		virtual NodeLayoutPtr GetLayout( int index ) const = 0;

	};

}
