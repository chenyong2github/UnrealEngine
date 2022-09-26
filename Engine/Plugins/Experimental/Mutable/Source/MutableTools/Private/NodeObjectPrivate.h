// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NodePrivate.h"

#include "NodeObject.h"
#include "NodeLayout.h"

namespace mu
{

	class NodeObject::Private : public Node::Private
	{
	public:

        virtual NodeLayoutPtr GetLayout( int lod, int component, int surface, int texture ) const = 0;

	};

}

