// Copyright Epic Games, Inc. All Rights Reserved.

#include "BaseObjectInfo.h"

FBaseObjectInfo::FBaseObjectInfo(UObject* TargetObject)
	: SoftObjectPath(TargetObject)
	, ObjectName(TargetObject ? TargetObject->GetFName() : FName())
	, ObjectOuterPathName(TargetObject&& TargetObject->GetOuter() ? TargetObject->GetOuter()->GetPathName() : FString()) // TODO: can optimize GetPathName?
	, ObjectClassPathName(TargetObject ? TargetObject->GetClass()->GetPathName() : FString())
	, ObjectAddress((uint64)TargetObject)
	, PropertyBlockStart(0)
	, PropertyBlockEnd(0)
{
	
};