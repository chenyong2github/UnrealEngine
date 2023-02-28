// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	ScriptDelegateFwd.h: Delegate forward declarations
=============================================================================*/

#pragma once

#include "UObject/WeakObjectPtrFwd.h"

template <typename TWeakPtr = FWeakObjectPtr> class TScriptDelegate;
template <typename TWeakPtr = FWeakObjectPtr> class TMulticastScriptDelegate;

// Typedef script delegates for convenience.
typedef TScriptDelegate<> FScriptDelegate;
typedef TMulticastScriptDelegate<> FMulticastScriptDelegate;
