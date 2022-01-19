// Copyright Epic Games, Inc. All Rights Reserved.

#include "SmartObjectOctree.h"

//----------------------------------------------------------------------//
// FSmartObjectOctreeElement
//----------------------------------------------------------------------//
FSmartObjectOctreeElement::FSmartObjectOctreeElement(const FBoxCenterAndExtent& InBounds, const FSmartObjectHandle& InSmartObjectHandle, const FSmartObjectOctreeIDSharedRef& InSharedOctreeID)
	: Bounds(InBounds)
	, SmartObjectHandle(InSmartObjectHandle)
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

void FSmartObjectOctree::AddNode(const FBoxCenterAndExtent& Bounds, const FSmartObjectHandle& SmartObjectHandle, const FSmartObjectOctreeIDSharedRef& SharedOctreeID)
{
	AddElement(FSmartObjectOctreeElement(Bounds, SmartObjectHandle, SharedOctreeID));
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

