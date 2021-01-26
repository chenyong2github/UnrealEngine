// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundGraphCoreModule.h"

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"
#include "MetasoundBop.h"
#include "MetasoundPrimitives.h"
#include "MetasoundTime.h"
#include "MetasoundFrequency.h"
#include "MetasoundGain.h"

REGISTER_METASOUND_DATATYPE(Metasound::FTrigger, "Trigger", ::Metasound::ELiteralType::Boolean)
REGISTER_METASOUND_DATATYPE(Metasound::FFloatTime, "Time", ::Metasound::ELiteralType::Float)
REGISTER_METASOUND_DATATYPE(Metasound::FDoubleTime, "Time:HighResolution", ::Metasound::ELiteralType::Float)
REGISTER_METASOUND_DATATYPE(Metasound::FSampleTime, "Time:SampleResolution", ::Metasound::ELiteralType::Float)
REGISTER_METASOUND_DATATYPE(Metasound::FFrequency, "Frequency", ::Metasound::ELiteralType::Float)
REGISTER_METASOUND_DATATYPE(Metasound::FGain, "Gain", ::Metasound::ELiteralType::Float)

namespace Metasound 
{
	class FMetasoundStandardNodesModule : public IMetasoundGraphCoreModule
	{
	};
}

IMPLEMENT_MODULE(Metasound::FMetasoundStandardNodesModule, MetasoundStandardNodes);

