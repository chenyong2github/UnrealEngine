// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	GenericOctreePublic.h: Generic octree definition.
=============================================================================*/

#pragma once

#include "CoreMinimal.h"

/** 
 *	An identifier for an element in the octree. 
 */
class FOctreeElementId
{
public:

	template<typename,typename>
	friend class TOctree;

	/** Default constructor. */
	FOctreeElementId()
		:	NodeIndex(INDEX_NONE)
		,	ElementIndex(INDEX_NONE)
	{}

	/** @return a boolean value representing whether the id is NULL. */
	bool IsValidId() const
	{
		return NodeIndex != INDEX_NONE;
	}

private:

	/** The node the element is in. */
	uint32 NodeIndex;

	/** The index of the element in the node's element array. */
	int32 ElementIndex;

	/** Initialization constructor. */
	FOctreeElementId(uint32 InNodeIndex, int32 InElementIndex)
		:	NodeIndex(InNodeIndex)
		,	ElementIndex(InElementIndex)
	{}

	/** Implicit conversion to the element index. */
	operator int32() const
	{
		return ElementIndex;
	}
};
