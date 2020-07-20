// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundGraphCoreModule.h"

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"
#include "MetasoundPrimitives.h"
#include "MetasoundDataReferenceTypes.h"
#include "MetasoundTime.h"
#include "MetasoundFrequency.h"

DEFINE_LOG_CATEGORY(LogMetasoundGraphCore);

REGISTER_METASOUND_DATATYPE(bool, ::Metasound::ELiteralArgType::Boolean)
REGISTER_METASOUND_DATATYPE(int32, ::Metasound::ELiteralArgType::Integer)
REGISTER_METASOUND_DATATYPE(int64, ::Metasound::ELiteralArgType::Integer)
REGISTER_METASOUND_DATATYPE(float, ::Metasound::ELiteralArgType::Float)
REGISTER_METASOUND_DATATYPE(double, ::Metasound::ELiteralArgType::Float)

namespace Metasound
{
	REGISTER_METASOUND_DATATYPE(FBop)
	REGISTER_METASOUND_DATATYPE(FFloatTime)
	REGISTER_METASOUND_DATATYPE(FDoubleTime)
	REGISTER_METASOUND_DATATYPE(FSampleTime)
	REGISTER_METASOUND_DATATYPE(FFrequency)
}

namespace Metasound 
{
	class FMetasoundGraphCoreModule : public IMetasoundGraphCoreModule
	{
	};
}

IMPLEMENT_MODULE(Metasound::FMetasoundGraphCoreModule, MetasoundGraphCore);

