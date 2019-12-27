// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once


#define GET_OPTION_VALUE(NAME) FDatasmithSketchUpOptions::GetSingleton().Get##NAME()

#define DEFINE_OPTION(TYPE, NAME) \
	public: \
		TYPE Get##NAME() const { return NAME; } \
		void Set##NAME(TYPE value) { NAME = value; } \
	private: \
		TYPE NAME;

class FDatasmithSketchUpOptions
{
public:

	static FDatasmithSketchUpOptions& GetSingleton();

private:

	FDatasmithSketchUpOptions();

	// No copying or copy assignment allowed for this class.
	FDatasmithSketchUpOptions(FDatasmithSketchUpOptions const&) = delete;
	FDatasmithSketchUpOptions& operator=(FDatasmithSketchUpOptions const&) = delete;

private:

};


inline FDatasmithSketchUpOptions::FDatasmithSketchUpOptions()
{
}
