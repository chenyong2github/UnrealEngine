// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#define NP_CHECKS_AND_ENSURES 1
#if NP_CHECKS_AND_ENSURES
	#define npCheckf(Condition, ...) checkf(Condition, ##__VA_ARGS__)
	#define npEnsure(Condition) ensureAlways(Condition)
	#define npEnsureMsgf(Condition, ...) ensureAlwaysMsgf(Condition, ##__VA_ARGS__)
#else
	#define npCheckf(...)
	#define npEnsure(Condition)
	#define npEnsureMsgf(...)
#endif

#define NP_CHECKS_AND_ENSURES_SLOW !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
#if NP_CHECKS_AND_ENSURES_SLOW
	#define npCheckSlow(Condition) check(Condition)
	#define npCheckfSlow(Condition, ...) checkf(Condition, ##__VA_ARGS__)
	#define npEnsureSlow(Condition) ensureAlways(Condition)
	#define npEnsureMsgfSlow(Condition, ...) ensureAlwaysMsgf(Condition, ##__VA_ARGS__)
#else
	#define npCheckSlow(Condition)
	#define npCheckfSlow(...)
	#define npEnsureSlow(...)
	#define npEnsureMsgfSlow(...)
#endif