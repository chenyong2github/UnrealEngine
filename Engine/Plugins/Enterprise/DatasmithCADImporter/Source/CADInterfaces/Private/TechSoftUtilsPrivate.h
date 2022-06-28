// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "TechSoftInterface.h"

namespace CADLibrary
{

namespace TechSoftUtils
{

#ifdef USE_TECHSOFT_SDK
void ExtractAttribute(const A3DMiscAttributeData& AttributeData, TMap<FString, FString>& OutMetaData);
#endif

FString CleanSdkName(const FString& Name);
FString CleanCatiaInstanceSdkName(const FString& Name);
FString CleanCatiaReferenceName(const FString& Name);
FString Clean3dxmlInstanceSdkName(const FString& Name);
FString Clean3dxmlReferenceSdkName(const FString& Name);
FString CleanSwInstanceSdkName(const FString& Name);
FString CleanSwReferenceSdkName(const FString& Name);
FString CleanCreoName(const FString& Name);
bool CheckIfNameExists(TMap<FString, FString>& MetaData);
bool ReplaceOrAddNameValue(TMap<FString, FString>& MetaData, const TCHAR* Key);

} // NS TechSoftUtils

} // CADLibrary

