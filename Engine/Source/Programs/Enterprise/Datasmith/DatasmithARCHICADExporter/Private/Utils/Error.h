// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AddonTools.h"

#include <stdexcept>
#include "GSException.hpp"

BEGIN_NAMESPACE_UE_AC

class UE_AC_Error : public std::exception
{
  public:
	typedef enum
	{
		kNotIn3DView,
		kUserCancelled
	} EErrorCode;

	UE_AC_Error(const utf8_t* InWhat, EErrorCode InErrorCode);

	~UE_AC_Error();

	const utf8_t* what() const throw() { return What; }

	EErrorCode GetErrorCode() const throw() { return ErrorCode; }

  private:
	const utf8_t* What;
	EErrorCode	  ErrorCode;
};

extern void		 ShowAlert(const UE_AC_Error& InException, const utf8_t* InFct);
extern void		 ShowAlert(const GS::GSException& inGSException, const utf8_t* InFct);
extern void		 ShowAlert(const utf8_t* InWhat, const utf8_t* InFct);
extern GSErrCode TryFunction(const utf8_t* InFctName, GSErrCode (*InFct)(void* IOArg, void* IOArg2),
							 void* IOArg1 = nullptr, void* IOArg2 = nullptr);

END_NAMESPACE_UE_AC
