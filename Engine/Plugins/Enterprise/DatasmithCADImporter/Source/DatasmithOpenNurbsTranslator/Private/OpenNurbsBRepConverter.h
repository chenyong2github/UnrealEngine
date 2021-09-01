// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

class ON_Brep;
class ON_3dVector;

class IOpenNurbsBRepConverter
{
public:
	virtual bool AddBRep(ON_Brep& Brep, const ON_3dVector& Offset) = 0;
};

