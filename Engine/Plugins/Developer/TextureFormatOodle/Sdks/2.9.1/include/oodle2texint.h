//===================================================
// Oodle2 Internals header
// (C) Copyright 1994-2018 RAD Game Tools, Inc.  
//===================================================

#ifndef __OODLE2TEXINT_H_INCLUDED__
#define __OODLE2TEXINT_H_INCLUDED__

#include "texbase.h"

typedef void (OODLE_CALLBACK OodleTex_RecordEntry)(const void *item, size_t size_in_bytes);
OOFUNC1 void OOFUNC2 OodleTex_InstallVisualizerCallback (OodleTex_RecordEntry * callback, int tex_width, int tex_height);

#endif // __OODLE2TEXINT_H_INCLUDED__

