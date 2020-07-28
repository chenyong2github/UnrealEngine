// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundGraphCoreModule.h"

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"
#include "MetasoundPrimitives.h"
#include "MetasoundDataReferenceTypes.h"
#include "MetasoundTime.h"
#include "MetasoundFrequency.h"
#include "MetasoundWave.h"

REGISTER_METASOUND_DATATYPE(bool, "Primitive:Bool", ::Metasound::ELiteralArgType::Boolean)
REGISTER_METASOUND_DATATYPE(int32, "Primitive:Int32", ::Metasound::ELiteralArgType::Integer)
REGISTER_METASOUND_DATATYPE(int64, "Primitive:Int64", ::Metasound::ELiteralArgType::Integer)
REGISTER_METASOUND_DATATYPE(float, "Primitive:Float", ::Metasound::ELiteralArgType::Float)
REGISTER_METASOUND_DATATYPE(double, "Primitive:Double", ::Metasound::ELiteralArgType::Float)

namespace Metasound
{
	REGISTER_METASOUND_DATATYPE(FBop, "Primitive:Bop", ::Metasound::ELiteralArgType::Boolean)
	REGISTER_METASOUND_DATATYPE(FFloatTime, "Primitive:Time")
	REGISTER_METASOUND_DATATYPE(FDoubleTime, "Primitive:Time:HighResolution")
	REGISTER_METASOUND_DATATYPE(FSampleTime, "Primitive:Time:SampleResolution")
	REGISTER_METASOUND_DATATYPE(FFrequency, "Primitive:Frequency")
	REGISTER_METASOUND_DATATYPE(FWave, "Primitive:Wave")
}

namespace Metasound 
{
	class FMetasoundStandardNodesModule : public IMetasoundGraphCoreModule
	{
	};
}

IMPLEMENT_MODULE(Metasound::FMetasoundStandardNodesModule, MetasoundStandardNodes);

