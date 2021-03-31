// Copyright Epic Games, Inc. All Rights Reserved.
#ifndef SYMS_LINE_TABLE_INCLUDE_H
#define SYMS_LINE_TABLE_INCLUDE_H

/******************************************************************************
 * File   : syms_line_table.h                                                 *
 * Author : Nikita Smith                                                      *
 * Created: 2020/08/07                                                        *
 ******************************************************************************/

#if 0
typedef struct SymsLineInfo
{
  SymsAddr addr;
  U32 ln;
  U16 col;
  U16 file;
} SymsLineInfo;
#endif

#if 0
typedef struct SymsLinesSec 
{
  U32 ln_min;
  U32 ln_max;
  U32 lines_count;
  SymsAddr addr_min;
  SymsAddr addr_max;
  struct SymsLine *lines;
  struct SymsLinesSec *next;
  struct SymsSourceFile file;
} SymsLinesSec;
#endif

#endif // SYMS_LINE_TABLE_INCLUDE_H
