// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

////////////////////////////////////////////////////////////////////////////////
#define MoveTemp(x) std::move(x)
#define check(x)

////////////////////////////////////////////////////////////////////////////////
template <typename Type>
struct TArray
	: protected std::vector<Type>
{
	using Super = std::vector<Type>;
	using Super::Super;
	using Super::operator [];
	using Super::begin;
	using Super::end;

	void		Add(const Type& Value)				{ return Super::push_back(Value); }
	void		SetNum(int32 Num)					{ return Super::resize(Num); }
	int32		Num() const							{ return int32(Super::size()); }
	Type*		GetData()							{ return &(this->operator [] (0)); }
	const Type* GetData() const						{ return &(this->operator [] (0)); }
	void		Empty()								{ return Super::clear(); }
	void		SetNumUninitialized(int Num)		{ return Super::resize(Num); }
	void		Append(const void* Data, int Num)	{ Super::insert(Super::end(), (const Type*)Data, ((const Type*)Data) + Num); }
};

////////////////////////////////////////////////////////////////////////////////
struct FStringView
	: protected std::basic_string_view<char>
{
	typedef std::basic_string_view<char> Super;
	using Super::Super;
	using Super::operator [];

	const char* GetData() const			{ return Super::data(); }
	uint32 Len() const					{ return uint32(Super::size()); }
	int Compare(const char* Rhs) const	{ return Super::compare(Rhs); }
};

////////////////////////////////////////////////////////////////////////////////
struct FString
	: protected std::string
{
	typedef std::string Super;
	using Super::Super;
	using Super::operator +=;

	const char* operator * () const				{ return Super::c_str(); }
	void operator += (const FStringView& Rhs)	{ Super::operator += ((FStringView::Super&)(Rhs)); }
};



////////////////////////////////////////////////////////////////////////////////
#if TS_USING(TS_PLATFORM_WINDOWS)
struct FWinApiStr
{
	FWinApiStr(const char* Utf8)
	{
		int32 BufferSize = MultiByteToWideChar(CP_UTF8, 0, Utf8, -1, nullptr, 0);
		Buffer = new wchar_t[BufferSize];
		MultiByteToWideChar(CP_UTF8, 0, Utf8, -1, Buffer, BufferSize);
	}

	~FWinApiStr()
	{
		delete[] Buffer;
	}

	operator LPCWSTR () const
	{
		return Buffer;
	}

private:
	wchar_t* Buffer = nullptr;
};
#endif // TS_PLATFORM_WINDOWS

/* vim: set noexpandtab : */
