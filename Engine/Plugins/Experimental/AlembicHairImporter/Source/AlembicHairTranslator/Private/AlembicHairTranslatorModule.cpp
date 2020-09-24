// Copyright Epic Games, Inc. All Rights Reserved.

#include "AlembicHairTranslatorModule.h"

#include "AlembicHairTranslator.h"
#include "HairStrandsEditor.h"

IMPLEMENT_MODULE(FAlembicHairTranslatorModule, AlembicHairTranslatorModule);

void FAlembicHairTranslatorModule::StartupModule()
{
	FGroomEditor::Get().RegisterHairTranslator<FAlembicHairTranslator>();
}

void FAlembicHairTranslatorModule::ShutdownModule()
{
}

