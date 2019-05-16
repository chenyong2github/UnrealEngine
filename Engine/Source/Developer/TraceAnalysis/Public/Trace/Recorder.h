// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Templates/SharedPointer.h"

namespace Trace
{

class IStore;

TRACEANALYSIS_API void Recorder_StartServer(TSharedRef<IStore> TraceStore);

}
