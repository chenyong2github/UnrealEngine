// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HAL/MemoryBase.h"

////////////////////////////////////////////////////////////////////////////////
template <typename T>
struct TMiniArray
{
				TMiniArray(FMalloc* InMalloc) : Malloc(InMalloc) {}
				~TMiniArray() { Malloc->Free(Data); }
	void		Add(T const& Item);
	void		Insert(T const& Item, int32 Index);
	void		RemoveAt(int32 Index);
	void		MakeRoom();
	T*			begin()		{ return Data; }
	T*			end()		{ return Data + Num; }
	FMalloc*	Malloc;
	int32		Num = 0;
	int32		Max = 0;
	T*			Data = nullptr;
};

////////////////////////////////////////////////////////////////////////////////
template <typename T>
inline void TMiniArray<T>::MakeRoom()   
{
	if (Num + 1 >= Max)
	{
		Max += 8;
		Data = (T*)(Malloc->Realloc(Data, sizeof(T) * Max));
	}
}

////////////////////////////////////////////////////////////////////////////////
template <typename T>
inline void TMiniArray<T>::Add(T const& Item)
{
	MakeRoom();
	Data[Num] = Item;
	++Num;
}

////////////////////////////////////////////////////////////////////////////////
template <typename T>
inline void TMiniArray<T>::Insert(T const& Item, int32 Index)
{
	MakeRoom();
	for (int i = Num - 1; i > Index; --i)
	{
		Data[i] = Data[i - 1];
	}
	Data[Index] = Item;
	++Num;
}       

////////////////////////////////////////////////////////////////////////////////
template <typename T>
inline void TMiniArray<T>::RemoveAt(int32 Index)
{
	for (int i = Index + 1; i < Num; ++i)
	{
		Data[i - 1] = Data[i];
	}
	--Num;
}
