// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Containers/UnrealString.h"
#include "HAL/Platform.h"
#include "MetasoundRouter.h"
#include "MetasoundVertex.h"
#include "Misc/Guid.h"


namespace Metasound
{
	namespace Frontend
	{
		// String serializable (as key) channel of analyzer or its internal members
		// that can be written to or read from using the Transmission System.
		struct METASOUNDFRONTEND_API FAnalyzerAddress
		{
			static const FString PathSeparator;

			// Active Instance ID to monitor
			uint64 InstanceID = TNumericLimits<uint64>::Max();

			// ID of Node being monitored
			FGuid NodeID;

			// Name of output to monitor (not to be confused with the Analyzer's members,
			// which are specific to the analyzer instance being addressed)
			FVertexName OutputName;

			// DataType of the given channel
			FName DataType;

			// Name of Analyzer
			FName AnalyzerName;

			// Instance ID of analyzer (allowing for multiple analyzer of the same type to be
			// addressed at the same output).
			FGuid AnalyzerInstanceID;

			// Optional name used to specify a channel for a given analyzer's inputs/outputs.
			// If not provided (i.e. 'none'), single input & output are assumed to share
			// the same name. Useful if the analyzer requires outputting multiple analysis values.
			// Can potentially be used as an input as well to modify analyzer settings.
			FName AnalyzerMemberName;

			// Converts AnalyzerAddress to String representation using the PathSeparator
			FString ToString() const;

			// Converts AnalyzerAddress to SendAddress
			FSendAddress ToSendAddress() const;

			static bool ParseKey(const FString& InAnalyzerKey, FAnalyzerAddress& OutAnalyzer);
		};
	} // namespace Frontend
} // namespace Metasound
