// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

class AlDagNode;

// Defined the reference in which the object has to be defined
enum class EAliasObjectReference
{
	LocalReference,  
	ParentReference, 
	WorldReference, 
};

class IAliasBRepConverter
{
public:
	virtual bool AddBRep(AlDagNode& DagNode, EAliasObjectReference ObjectReference) = 0;
};

