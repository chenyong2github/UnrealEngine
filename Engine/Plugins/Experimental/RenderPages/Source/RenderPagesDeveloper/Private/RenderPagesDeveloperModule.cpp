// Copyright Epic Games, Inc. All Rights Reserved.

#include "RenderPagesDeveloperModule.h"

#define LOCTEXT_NAMESPACE "RenderPagesDeveloperModule"


void UE::RenderPages::Private::FRenderPagesDeveloperModule::StartupModule() {}
void UE::RenderPages::Private::FRenderPagesDeveloperModule::ShutdownModule() {}


#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(UE::RenderPages::Private::FRenderPagesDeveloperModule, RenderPagesDeveloper)
