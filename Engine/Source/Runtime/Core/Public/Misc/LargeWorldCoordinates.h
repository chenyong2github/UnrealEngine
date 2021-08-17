// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

// Use float based world coordinates throughout. Note: This is unsupported!
#ifndef UE_LARGE_WORLD_COORDINATES_DISABLED
#define UE_LARGE_WORLD_COORDINATES_DISABLED 1
#endif

// Forward declaration of LWC supported core types

#define DECLARE_LWC_TYPE_EX(TYPE, CC, DEFAULT_TYPENAME, DEFAULT_COMPONENT_TYPE)	\
namespace UE { namespace Math { template<typename FReal> struct T##TYPE; } }	\
typedef UE::Math::T##TYPE<float> F##TYPE##CC##f;								\
typedef UE::Math::T##TYPE<double> F##TYPE##CC##d;								\
typedef UE::Math::T##TYPE<DEFAULT_COMPONENT_TYPE> DEFAULT_TYPENAME;

//#define DECLARE_LWC_TYPE_EX_ISPC(TYPE, CC, DEFAULT_TYPENAME, DEFAULT_COMPONENT_TYPE) namespace ispc { struct F##TYPE##CC##f; struct F##TYPE##CC##d; typedef F##TYPE##CC##f DEFAULT_TYPENAME; }	// LWC_TODO: May not be necessary depending on final ispc approach.
#define DECLARE_LWC_TYPE_EX_ISPC(TYPE, CC, DEFAULT_TYPENAME, DEFAULT_COMPONENT_TYPE) namespace ispc { struct DEFAULT_TYPENAME; }

#if UE_LARGE_WORLD_COORDINATES_DISABLED

#define DECLARE_LWC_TYPE(TYPE, CC)		DECLARE_LWC_TYPE_EX(TYPE, CC, F##TYPE, float)
#define DECLARE_LWC_TYPE_ISPC(TYPE, CC) DECLARE_LWC_TYPE_EX_ISPC(TYPE, CC, F##TYPE, float)

#else

#define DECLARE_LWC_TYPE(TYPE, CC)		DECLARE_LWC_TYPE_EX(TYPE, CC, F##TYPE, double)
#define DECLARE_LWC_TYPE_ISPC(TYPE, CC) DECLARE_LWC_TYPE_EX_ISPC(TYPE, CC, F##TYPE, double)

#endif