// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if defined(_Analysis_assume_)
#define stb_assume(expr) _Analysis_assume_(expr)
#else
#define stb_assume(expr)
#endif