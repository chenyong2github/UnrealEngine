// Copyright Epic Games, Inc. All Rights Reserved.

#include "K2Node_UpdateVirtualSubjectDataTyped.h"
#include "Misc/AssertionMacros.h"
#include "VirtualSubjects/LiveLinkBlueprintVirtualSubject.h"

#define LOCTEXT_NAMESPACE "K2Node_UpdateVirtualSubjectDataTyped"


FText UK2Node_UpdateVirtualSubjectStaticData::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	return LOCTEXT("NodeTitle_Static", "Update Virtual Subject Static Data");
}

UScriptStruct* UK2Node_UpdateVirtualSubjectStaticData::GetStructTypeFromRole(ULiveLinkRole* Role) const
{
	check(Role);
	return Role->GetStaticDataStruct();
}

FName UK2Node_UpdateVirtualSubjectStaticData::GetUpdateFunctionName() const
{
	return GET_FUNCTION_NAME_CHECKED(ULiveLinkBlueprintVirtualSubject, UpdateVirtualSubjectStaticData_Internal);
}

FText UK2Node_UpdateVirtualSubjectStaticData::GetStructPinName() const
{
	return LOCTEXT("StaticPinName", "Static Data");
}

FText UK2Node_UpdateVirtualSubjectFrameData::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	return LOCTEXT("NodeTitle_Frame", "Update Virtual Subject Frame Data");
}

UScriptStruct* UK2Node_UpdateVirtualSubjectFrameData::GetStructTypeFromRole(ULiveLinkRole* Role) const
{
	check(Role);
	return Role->GetFrameDataStruct();
}

FName UK2Node_UpdateVirtualSubjectFrameData::GetUpdateFunctionName() const
{
	return GET_FUNCTION_NAME_CHECKED(ULiveLinkBlueprintVirtualSubject, UpdateVirtualSubjectFrameData_Internal);
}

FText UK2Node_UpdateVirtualSubjectFrameData::GetStructPinName() const
{
	return LOCTEXT("FramePinName", "Frame Data");
}

#undef LOCTEXT_NAMESPACE