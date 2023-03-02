// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	ScriptDelegateFwd.h: Delegate forward declarations
=============================================================================*/

#pragma once

#include "UObject/WeakObjectPtrFwd.h"

namespace UE::Core::Private
{
	struct TScriptDelegateDefault;
}

template <typename Dummy = UE::Core::Private::TScriptDelegateDefault> class TScriptDelegate;
template <typename Dummy = UE::Core::Private::TScriptDelegateDefault> class TMulticastScriptDelegate;

// Typedef script delegates for convenience.
typedef TScriptDelegate<> FScriptDelegate;
typedef TMulticastScriptDelegate<> FMulticastScriptDelegate;
