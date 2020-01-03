// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

// SketchUp SDK.
#include "DatasmithSketchUpSDKBegins.h"
#include "SketchUpAPI/unicodestring.h"
#include "DatasmithSketchUpSDKCeases.h"


#define SU_GET_STRING(FUNCTION, ENTITY_REF, STRING) \
	{ \
		FDatasmithSketchUpString String; \
		FUNCTION(ENTITY_REF, &String); /* we can ignore the returned SU_RESULT */ \
		wchar_t* WideString = String.GetWideString(); \
        STRING = WideString; \
		delete [] WideString; \
	}

#define SU_USE_STRING(STRING_REF, STRING) \
	{ \
		wchar_t* WideString = FDatasmithSketchUpString::GetWideString(STRING_REF); \
        STRING = WideString; \
		delete [] WideString; \
	}

class FDatasmithSketchUpString
{
public:
	FDatasmithSketchUpString();
	~FDatasmithSketchUpString();

	// No copying or copy assignment allowed for this class.
	FDatasmithSketchUpString(FDatasmithSketchUpString const&) = delete;
	FDatasmithSketchUpString& operator=(FDatasmithSketchUpString const&) = delete;

	// Overload the address-of operator to return the address of the private SketchUp string.
	SUStringRef* operator &();

	// Get a wide string version of the private SketchUp string.
	wchar_t* GetWideString();

	// Get a wide string version of a SketchUp string.
	static wchar_t* GetWideString(
		SUStringRef InSStringRef // valid SketchUp string
	);

private:

	// SketchUp string.
	SUStringRef SStringRef;
};


inline FDatasmithSketchUpString::FDatasmithSketchUpString() :
	SStringRef(SU_INVALID)
{
	SUStringCreate(&SStringRef); // we can ignore the returned SU_RESULT
}

inline FDatasmithSketchUpString::~FDatasmithSketchUpString()
{
	SUStringRelease(&SStringRef); // we can ignore the returned SU_RESULT
}

inline SUStringRef* FDatasmithSketchUpString::operator &()
{
	return &SStringRef;
}

inline wchar_t* FDatasmithSketchUpString::GetWideString()
{
	return GetWideString(SStringRef);
}
