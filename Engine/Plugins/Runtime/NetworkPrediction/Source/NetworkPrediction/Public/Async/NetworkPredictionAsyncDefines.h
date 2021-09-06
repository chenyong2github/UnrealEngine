// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

namespace UE_NP
{
#ifndef NP_MAX_ASYNC_MODEL_DEFS
	#define NP_MAX_ASYNC_MODEL_DEFS 16
#endif

const int32 MaxAsyncModelDefs = NP_MAX_ASYNC_MODEL_DEFS;

#ifndef NP_NUM_FRAME_STORAGE
	#define NP_NUM_FRAME_STORAGE 64
#endif

const int32 NumFramesStorage = NP_NUM_FRAME_STORAGE;

#ifndef NP_FRAME_STORAGE_GROWTH
	#define NP_FRAME_STORAGE_GROWTH 8
#endif

const int32 FrameStorageGrowth = NP_FRAME_STORAGE_GROWTH;

#ifndef NP_FRAME_INPUTCMD_BUFFER_SIZE
	#define NP_FRAME_INPUTCMD_BUFFER_SIZE 16
#endif

const int32 InputCmdBufferSize = NP_FRAME_INPUTCMD_BUFFER_SIZE;

};