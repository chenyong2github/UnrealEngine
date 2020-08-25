// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

// FLevelInstanceID is a runtime unique id that is computed from the Hash of LevelInstance Actor Guid and all its ancestor LevelInstance Actor Guids.
// Resulting in a different ID for all instances whether they load the same level or not.
using FLevelInstanceID = uint32;

static constexpr FLevelInstanceID InvalidLevelInstanceID = 0;