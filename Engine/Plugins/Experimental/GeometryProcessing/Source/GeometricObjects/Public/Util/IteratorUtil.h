// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

// Port of geometry3cpp iterator_util.h

#pragma once


/**
 * Wrapper around an object of type IteratorT that provides STL
 * iterator-like semantics, that converts from the iteration type
 * (FromType) to a new type (ToType).
 *
 * Conversion is done via a provided mapping function
 */
template<typename FromType, typename ToType, typename IteratorT>
class MappedIterator
{
	using MapFunctionT = TFunction<ToType(FromType)>;

public:
	inline MappedIterator() { }

	inline bool operator==(const MappedIterator& Other) const
	{
		return Cur == Other.Cur;
	}
	inline bool operator!=(const MappedIterator& Other) const 
	{
		return Cur != Other.Cur;
	}

	inline ToType operator*() const 
	{
		return MapFunction(*Cur);
	}

	inline const MappedIterator& operator++() 		// prefix
	{
		Cur++;
		return *this;
	}

	inline MappedIterator(const IteratorT& CurItr, const MapFunctionT& MapFunctionIn)
	{
		Cur = CurItr;
		MapFunction = MapFunctionIn;
	}

	IteratorT Cur;
	MapFunctionT MapFunction;
};







/**
 * Wrapper around an existing iterator that skips over
 * values for which the filter_func returns false.
 */
template<typename ValueType, typename IteratorT>
class FilteredIterator
{
	using FilterFunctionT = TFunction<bool(ValueType)>;

public:
	inline FilteredIterator() { }

	inline bool operator==(const FilteredIterator& Other) const 
	{
		return Cur == Other.Cur;
	}
	inline bool operator!=(const FilteredIterator& Other) const
	{
		return Cur != Other.Cur;
	}

	inline ValueType operator*() const 
	{
		return *Cur;
	}

	inline const FilteredIterator& operator++() 		// prefix
	{
		GotoNextElement();
		return *this;
	}

	inline void GotoNextElement() 
	{
		do {
			Cur++;
		} while (Cur != End && FilterFunc(*Cur) == false);
	}

	inline FilteredIterator(const IteratorT& CurItr, const IteratorT& EndItr, const FilterFunctionT& FilterFuncIn)
	{
		Cur = CurItr;
		End = EndItr;
		this->FilterFunc = FilterFuncIn;
		if (FilterFunc(*Cur) == false)
		{
			GotoNextElement();
		}
	}

	IteratorT Cur;
	IteratorT End;
	FilterFunctionT FilterFunc;
};










/**
 * Wrapper around existing iterator that returns multiple values, of potentially
 * different type, for each value that input iterator returns.
 *
 * This is done via an "expansion" function that takes an int reference which
 * indicates "where" we are in the expansion (eg like a state machine).
 * How you use this value is up to you.
 *
 * When the input is -1, you should interpret this as the "beginning" of
 * handling the input value (ie we have not returned any values yet for
 * this input value)
 *
 * When you are "done" with an input value, set the outgoing int reference to -1
 * and the base iterator will be incremented.
 *
 * If you have more values to return for this input value, set it to some positive
 * number of your choosing.
 *
 * See FDynamicMesh3::VtxTrianglesItr for an example
 */
template<typename OutputType, typename InputType, typename InputIteratorT>
class ExpandIterator
{
	using ExpandFunctionT = TFunction<OutputType(InputType, int&)>;

public:
	inline ExpandIterator() { }

	inline bool operator==(const ExpandIterator& Other) const 
	{
		return Cur == Other.Cur;
	}
	inline bool operator!=(const ExpandIterator & Other) const 
	{
		return Cur != Other.Cur;
	}

	inline OutputType operator*() const 
	{
		return CurValue;
	}

	inline const ExpandIterator& operator++() 		// prefix
	{
		goto_next();
		return *this;
	}

	inline void goto_next() 
	{
		while (Cur != End) 
		{
			CurValue = ExpandFunc(*Cur, CurExpandI);
			if (CurExpandI == -1)
			{
				++Cur;  // done with this base value
			}
			else
			{
				break; // want caller to see current output value
			}
		}
	}

	inline ExpandIterator(const InputIteratorT& CurItr, const InputIteratorT& EndItr, const ExpandFunctionT& ExpandFuncIn)
	{
		Cur = CurItr;
		End = EndItr;
		ExpandFunc = ExpandFuncIn;
		CurExpandI = -1;
		goto_next();
	}

	InputIteratorT Cur;
	InputIteratorT End;
	OutputType CurValue;
	int CurExpandI;
	ExpandFunctionT ExpandFunc;
};



/**
 * Generic "enumerable" object that provides begin/end semantics for an ExpandIterator suitable for use with range-based for.
 * You can either provide begin/end iterators, or another "enumerable" object that has begin()/end() functions.
 */
template<typename OutputType, typename InputType, typename InputIteratorT>
class ExpandEnumerable
{
	using ExpandFunctionT = TFunction<OutputType(InputType, int&)>;
	using ExpandIteratorT = ExpandIterator<OutputType, InputType, InputIteratorT>;

public:
	ExpandFunctionT ExpandFunc;
	InputIteratorT BeginItr, EndItr;

	ExpandEnumerable(const InputIteratorT& BeginIn, const InputIteratorT& EndIn, ExpandFunctionT ExpandFuncIn) 
	{
		this->BeginItr = BeginIn;
		this->EndItr = EndIn;
		this->ExpandFunc = ExpandFuncIn;
	}

	template<typename IteratorSource>
	ExpandEnumerable(const IteratorSource& Source, ExpandFunctionT ExpandFuncIn)
	{
		this->BeginItr = Source.begin();
		this->EndItr = Source.end();
		this->ExpandFunc = ExpandFuncIn;
	}

	ExpandIteratorT begin() 
	{
		return ExpandIteratorT(BeginItr, EndItr, ExpandFunc);
	}

	ExpandIteratorT end()
	{
		return ExpandIteratorT(EndItr, EndItr, ExpandFunc);
	}
};