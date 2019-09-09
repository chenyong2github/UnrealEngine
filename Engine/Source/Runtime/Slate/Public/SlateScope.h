// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

// SLATE_MODULE is defined private to the module in Slate.build.cs
// This allows us to establish a scope that is public within the Slate module itself, but protected from all consumers of the module
#ifdef SLATE_MODULE
#define SLATE_SCOPE public
#else
#define SLATE_SCOPE protected
#endif