// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/StringFwd.h"

class FCbObject;
class FCbWriter;

namespace UE::DerivedData { class FBuildDefinition; }
namespace UE::DerivedData { class FBuildDefinitionBuilder; }
namespace UE::DerivedData { class FBuildOutput; }
namespace UE::DerivedData { class FBuildOutputBuilder; }
namespace UE::DerivedData { class FCacheRecord; }
namespace UE::DerivedData { class FCacheRecordBuilder; }

namespace UE::DerivedData::Private
{

// Implemented in DerivedDataBuildDefinition.cpp
FBuildDefinitionBuilder CreateBuildDefinition(FStringView Name, FStringView Function);
FBuildDefinition LoadBuildDefinition(FStringView Name, FCbObject&& Definition);

// Implemented in DerivedDataBuildOutput.cpp
FBuildOutputBuilder CreateBuildOutput(FStringView Name, FStringView Function);
FBuildOutput LoadBuildOutput(FStringView Name, FStringView Function, const FCbObject& Output);
FBuildOutput LoadBuildOutput(FStringView Name, FStringView Function, const FCacheRecord& Output);

} // UE::DerivedData::Private
