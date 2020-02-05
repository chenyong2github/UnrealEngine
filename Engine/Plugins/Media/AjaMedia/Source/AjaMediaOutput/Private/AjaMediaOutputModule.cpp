// Copyright Epic Games, Inc. All Rights Reserved.

#include "IAjaMediaOutputModule.h"

#include "AjaMediaFrameGrabberProtocol.h"
#include "Modules/ModuleManager.h"

#define LOCTEXT_NAMESPACE "AjaMediaOutput"

DEFINE_LOG_CATEGORY(LogAjaMediaOutput);

class FAjaMediaOutputModule : public IAjaMediaOutputModule
{
};

IMPLEMENT_MODULE(FAjaMediaOutputModule, AjaMediaOutput )

#undef LOCTEXT_NAMESPACE
