// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "targetver.h"

#include <stdio.h>
#include <tchar.h>
#include <assert.h>
#include <string>
#include <cstdarg>
#include <cctype>
#include <mutex>
#include <vector>
#include <algorithm>
#include <locale>
#include <queue>
#include <atomic>
#include <fstream>
#include <conio.h>
#include <functional>

#include <strsafe.h>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <shlwapi.h>

//
// cpprestsdk
//
#include "cpprest/json.h"
#include "cpprest/http_listener.h"
#include "cpprest/uri.h"
#include "cpprest/asyncrt_utils.h"
#include "cpprest/json.h"
#include "cpprest/filestream.h"
#include "cpprest/containerstream.h"
#include "cpprest/producerconsumerstream.h"

#pragma  warning(disable:4996)
#include "boost/asio.hpp"
