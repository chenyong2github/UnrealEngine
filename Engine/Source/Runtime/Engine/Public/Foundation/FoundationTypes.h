// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

// FFoundationID is a runtime unique id that is computed from the Hash of Foundation Actor Guid and all its ancestor Foundation Actor Guids.
// Resulting in a different ID for all instances whether they load the same level or not.
using FFoundationID = uint32;

static constexpr FFoundationID InvalidFoundationID = 0;