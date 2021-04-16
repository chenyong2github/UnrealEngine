// Copyright Epic Games, Inc. All Rights Reserved.

#include "Error.h"
#include "AddonTools.h"
#include "ResourcesUtils.h"

#include "DG.h"

BEGIN_NAMESPACE_UE_AC

UE_AC_Error::UE_AC_Error(const utf8_t* InWhat, UE_AC_Error::EErrorCode InErrorCode)
	: What(InWhat)
	, ErrorCode(InErrorCode)
{
}

UE_AC_Error::~UE_AC_Error() {}

void ShowAlert(const UE_AC_Error& InException, const utf8_t* InFct)
{
	UE_AC_DebugF("Caught an exception \"%s\" in %s\n", InException.what(), InFct);

	short AlertId = 0;
	switch (InException.GetErrorCode())
	{
		case UE_AC_Error::kNotIn3DView:
			AlertId = LocalizeResId(kAlertNot3DViewError);
			break;
		case UE_AC_Error::kUserCancelled:
			AlertId = LocalizeResId(kAlertUserCancelledError);
			break;
		default:
			AlertId = LocalizeResId(kAlertUnhandledError);
			break;
	}

	DGResAlert(ACAPI_GetOwnResModule(), AlertId);
}

void ShowAlert(const GS::GSException& inGSException, const utf8_t* InFct)
{
	UE_AC_DebugF("Caught a GS exception (%d) \"%s\" in %s\n", (int)inGSException.GetID(), inGSException.GetName(),
				 InFct);

	DGResAlert(ACAPI_GetOwnResModule(), LocalizeResId(kAlertACDBError));
}

void ShowAlert(const utf8_t* InWhat, const utf8_t* InFct)
{
	UE_AC_DebugF("Caught an exception \"%s\" in %s\n", InWhat, InFct);

	DGResAlert(ACAPI_GetOwnResModule(), LocalizeResId(kAlertPlugInError));
}

GSErrCode TryFunction(const utf8_t* InFctName, GSErrCode (*InFct)(void* IOArg, void* IOArg2), void* IOArg1,
					  void* IOArg2)
{
	GSErrCode GSErr = APIERR_GENERAL;

	try
	{
		GSErr = InFct(IOArg1, IOArg2);
	}
	catch (UE_AC_Error& e)
	{
		ShowAlert(e, InFctName);
		if (e.GetErrorCode() == UE_AC_Error::kUserCancelled)
		{
			GSErr = APIERR_CANCEL;
		}
	}
	catch (std::exception& e)
	{
		ShowAlert(e.what(), InFctName);
	}
	catch (GS::GSException& gs)
	{
		ShowAlert(gs, InFctName);
	}
	catch (...)
	{
		ShowAlert(GetStdName(kName_Unknown), InFctName);
	}

	return GSErr;
}

END_NAMESPACE_UE_AC
