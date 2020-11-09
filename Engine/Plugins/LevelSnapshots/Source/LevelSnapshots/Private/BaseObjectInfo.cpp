// Copyright Epic Games, Inc. All Rights Reserved.

#include "BaseObjectInfo.h"

FBaseObjectInfo::FBaseObjectInfo(UObject* TargetObject)
	: ObjectName(TargetObject ? TargetObject->GetFName() : FName())
	, ObjectOuterPathName(TargetObject&& TargetObject->GetOuter() ? TargetObject->GetOuter()->GetPathName() : FString()) // TODO: can optimize GetPathName?
	, ObjectClassPathName(TargetObject ? TargetObject->GetClass()->GetPathName() : FString())
	, ObjectFlags(TargetObject ? (uint32)TargetObject->GetFlags() : 0)
	, InternalObjectFlags(TargetObject ? (uint32)TargetObject->GetInternalFlags() : 0)
	, ObjectAddress((uint64)TargetObject)
	, InternalIndex(TargetObject ? TargetObject->GetUniqueID() : 0)
	, PropertyBlockStart(0)
	, PropertyBlockEnd(0)
{
	
};