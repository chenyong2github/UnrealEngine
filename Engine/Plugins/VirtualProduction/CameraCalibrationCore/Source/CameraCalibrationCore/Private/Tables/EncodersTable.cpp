// Copyright Epic Games, Inc. All Rights Reserved.


#include "Tables/EncodersTable.h"


int32 FEncodersTable::GetNumFocusPoints() const
{
	return Focus.GetNumKeys();
}

float FEncodersTable::GetFocusInput(int32 Index) const
{
	return Focus.Keys[Index].Time;
}

float FEncodersTable::GetFocusValue(int32 Index) const
{
	return Focus.Keys[Index].Value;
}

int32 FEncodersTable::GetNumIrisPoints() const
{
	return Iris.GetNumKeys();
}

float FEncodersTable::GetIrisInput(int32 Index) const
{
	return Iris.Keys[Index].Time;
}

float FEncodersTable::GetIrisValue(int32 Index) const
{
	return Iris.Keys[Index].Value;
}