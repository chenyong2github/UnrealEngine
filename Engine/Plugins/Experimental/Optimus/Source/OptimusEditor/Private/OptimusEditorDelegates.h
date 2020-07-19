// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Delegates/Delegate.h"


class UOptimusEditorGraph;


// A collection of delegates used by the Optimus editor.

/// A simple delegate that fires when the Optimus graph has changed in some way.
DECLARE_DELEGATE_OneParam(FOptimusGraphEvent, UOptimusEditorGraph*);
