// Copyright Epic Games, Inc. All Rights Reserved.

// usage
//
// General purpose Scene Graph Type definitions

#ifndef EVAL_GRAPH_CONNECTION_TYPE
#error EVAL_GRAPH_CONNECTION_TYPE macro is undefined.
#endif

// NOTE: new types must be added at the bottom to keep serialization from breaking

EVAL_GRAPH_CONNECTION_TYPE(int, Integer)
EVAL_GRAPH_CONNECTION_TYPE(TSharedPtr<FManagedArrayCollection>, ManagedArrayCollection)

// NOTE: new types must be added at the bottom to keep serialization from breaking


#undef EVAL_GRAPH_CONNECTION_TYPE
