// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#ifdef DATASMITH_SKETCHUP_EXPORTER_DLL
#define DATASMITH_SKETCHUP_EXPORTER_API __declspec(dllexport)
#else
#define DATASMITH_SKETCHUP_EXPORTER_API __declspec(dllimport)
#endif

class SketchUpModelExporterInterface;


DATASMITH_SKETCHUP_EXPORTER_API SketchUpModelExporterInterface* GetSketchUpModelExporterInterface();
