// Copyright Epic Games, Inc. All Rights Reserved.

#include "SmartObjectOctree.h"

//----------------------------------------------------------------------//
// FSmartObjectOctreeElement
//----------------------------------------------------------------------//
FSmartObjectOctreeElement::FSmartObjectOctreeElement(const FBoxCenterAndExtent& InBounds, const FSmartObjectID& InSmartObjectID, const FSmartObjectOctreeIDSharedRef& InSharedOctreeID)
	: Bounds(InBounds)
	, SmartObjectID(InSmartObjectID)
	, SharedOctreeID(InSharedOctreeID)
{
}

//----------------------------------------------------------------------//
// FSmartObjectOctree
//----------------------------------------------------------------------//
FSmartObjectOctree::FSmartObjectOctree()
	: FSmartObjectOctree(FVector::ZeroVector, 0)
{

}

FSmartObjectOctree::FSmartObjectOctree(const FVector& Origin, float Radius)
	: TOctree2<FSmartObjectOctreeElement, FSmartObjectOctreeSemantics>(Origin, Radius)
{
}

FSmartObjectOctree::~FSmartObjectOctree()
{
}

void FSmartObjectOctree::AddNode(const FBoxCenterAndExtent& Bounds, const FSmartObjectID& SmartObjectID, const FSmartObjectOctreeIDSharedRef& SharedOctreeID)
{
	AddElement(FSmartObjectOctreeElement(Bounds, SmartObjectID, SharedOctreeID));
}

void FSmartObjectOctree::UpdateNode(const FOctreeElementId2& Id, const FBox& NewBounds)
{
	FSmartObjectOctreeElement ElementCopy = GetElementById(Id);
	RemoveElement(Id);
	ElementCopy.Bounds = NewBounds;
	AddElement(ElementCopy);
}

void FSmartObjectOctree::RemoveNode(const FOctreeElementId2& Id)
{
	RemoveElement(Id);
}

//----------------------------------------------------------------------//
// FSmartObjectOctreeSemantics
//----------------------------------------------------------------------//
void FSmartObjectOctreeSemantics::SetElementId(const FSmartObjectOctreeElement& Element, FOctreeElementId2 Id)
{
	Element.SharedOctreeID->ID = Id;
}

