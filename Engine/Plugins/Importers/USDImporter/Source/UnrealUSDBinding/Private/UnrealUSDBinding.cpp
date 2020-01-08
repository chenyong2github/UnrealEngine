// Copyright Epic Games, Inc. All Rights Reserved.

#if USE_USD_SDK

#include "UnrealUSDWrapper.h"

#include "USDIncludesStart.h"
#include "boost/python.hpp"
#include "USDIncludesEnd.h"

BOOST_PYTHON_MODULE(UnrealUSDBinding)
{
	using namespace boost::python;

	// With this return policy, the returned Python object will not just be copy-constructed from a
	// pxr::UsdStageCache, but will actually contain a pointer to the existing C++ singleton.
	// Note: Only use this if you can guarantee the lifetime of the C++ object (in this case its static)
	// See https://www.boost.org/doc/libs/1_40_0/libs/python/doc/v2/reference_existing_object.html
	def("GetStageCache", UnrealUSDWrapper::GetUsdStageCache, return_value_policy<reference_existing_object>());
}

#endif // #if USE_USD_SDK