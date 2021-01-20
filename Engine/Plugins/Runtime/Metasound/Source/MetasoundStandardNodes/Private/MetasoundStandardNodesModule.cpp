// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundGraphCoreModule.h"

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"
#include "MetasoundBop.h"
#include "MetasoundPrimitives.h"
#include "MetasoundTime.h"
#include "MetasoundFrequency.h"

REGISTER_METASOUND_DATATYPE(Metasound::FTrigger, "Primitive:Trigger", ::Metasound::ELiteralType::Boolean)
REGISTER_METASOUND_DATATYPE(Metasound::FFloatTime, "Primitive:Time", ::Metasound::ELiteralType::Float)
REGISTER_METASOUND_DATATYPE(Metasound::FDoubleTime, "Primitive:Time:HighResolution", ::Metasound::ELiteralType::Float)
REGISTER_METASOUND_DATATYPE(Metasound::FSampleTime, "Primitive:Time:SampleResolution", ::Metasound::ELiteralType::Float)
REGISTER_METASOUND_DATATYPE(Metasound::FFrequency, "Primitive:Frequency", ::Metasound::ELiteralType::Float)

namespace Metasound 
{
	class FMetasoundStandardNodesModule : public IMetasoundGraphCoreModule
	{
	};
}

IMPLEMENT_MODULE(Metasound::FMetasoundStandardNodesModule, MetasoundStandardNodes);

