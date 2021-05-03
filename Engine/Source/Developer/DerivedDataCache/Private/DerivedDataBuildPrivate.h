// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/StringFwd.h"

class FCbObject;

namespace UE::DerivedData { class FBuildDefinition; }
namespace UE::DerivedData { class FBuildDefinitionBuilder; }

namespace UE::DerivedData::Private
{

// Implemented in DerivedDataBuildDefinition.cpp
FBuildDefinitionBuilder CreateBuildDefinition(FStringView Name, FStringView Function);

// Implemented in DerivedDataBuildDefinition.cpp
FBuildDefinition LoadBuildDefinition(FStringView Name, FCbObject&& Definition);

} // UE::DerivedData::Private
