// Copyright Epic Games, Inc. All Rights Reserved.

#include "Framework/Dispatcher.h"
#include "DispatcherImpl.h"

#include "PhysicsSolver.h"
#include "ChaosSolversModule.h"
#include "ChaosStats.h"
#include "PhysicsCoreTypes.h"

void LexFromString(Chaos::EThreadingMode& OutValue, const TCHAR* InString) 
{
	OutValue = Chaos::EThreadingMode::Invalid;

	if(FCString::Stricmp(InString, TEXT("DedicatedThread")) == 0)
	{
		OutValue = Chaos::EThreadingMode::DedicatedThread;
	}
	else if(FCString::Stricmp(InString, TEXT("TaskGraph")) == 0) 
	{
		OutValue = Chaos::EThreadingMode::TaskGraph;
	}
	else if(FCString::Stricmp(InString, TEXT("SingleThread")) == 0)
	{
		OutValue = Chaos::EThreadingMode::SingleThread;
	}
}

FString LexToString(const Chaos::EThreadingMode InValue)
{
	switch(InValue)
	{
	case Chaos::EThreadingMode::DedicatedThread:
		return TEXT("DedicatedThread");
	case Chaos::EThreadingMode::TaskGraph:
		return TEXT("TaskGraph");
	case Chaos::EThreadingMode::SingleThread:
		return TEXT("SingleThread");
	default:
		break;
	}

	return TEXT("");
}

template class Chaos::FDispatcher<EChaosThreadingMode::DedicatedThread>;
template class Chaos::FDispatcher<EChaosThreadingMode::SingleThread>;
template class Chaos::FDispatcher<EChaosThreadingMode::TaskGraph>;
