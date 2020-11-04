// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundGraphCoreModule.h"

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"
#include "MetasoundBop.h"
#include "MetasoundPrimitives.h"
#include "MetasoundTime.h"
#include "MetasoundFrequency.h"

REGISTER_METASOUND_DATATYPE(Metasound::FBop, "Primitive:Bop", ::Metasound::ELiteralArgType::Boolean)
REGISTER_METASOUND_DATATYPE(Metasound::FFloatTime, "Primitive:Time", ::Metasound::ELiteralArgType::Float)
REGISTER_METASOUND_DATATYPE(Metasound::FDoubleTime, "Primitive:Time:HighResolution", ::Metasound::ELiteralArgType::Float)
REGISTER_METASOUND_DATATYPE(Metasound::FSampleTime, "Primitive:Time:SampleResolution", ::Metasound::ELiteralArgType::Float)
REGISTER_METASOUND_DATATYPE(Metasound::FFrequency, "Primitive:Frequency", ::Metasound::ELiteralArgType::Float)

namespace Metasound 
{
	class FMetasoundStandardNodesModule : public IMetasoundGraphCoreModule
	{
	};
}

IMPLEMENT_MODULE(Metasound::FMetasoundStandardNodesModule, MetasoundStandardNodes);

