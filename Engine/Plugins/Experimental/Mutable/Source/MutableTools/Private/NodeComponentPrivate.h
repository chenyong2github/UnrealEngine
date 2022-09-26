// Copyright Epic Games, Inc. All Rights Reserved.


#pragma once

#include "NodePrivate.h"
#include "NodeComponent.h"
#include "NodeComponentNew.h"

#include "MemoryPrivate.h"


namespace mu
{
	//---------------------------------------------------------------------------------------------
	//!
	//---------------------------------------------------------------------------------------------
	class NodeComponent::Private : public Node::Private
	{
	public:

		//! Look for the parent component that this node is editing
		virtual const NodeComponentNew::Private* GetParentComponentNew() const = 0;
	};


}

