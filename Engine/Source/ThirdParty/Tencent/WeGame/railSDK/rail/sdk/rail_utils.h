// Copyright (c) 2016, Entropy Game Global Limited.
// All rights reserved.

#ifndef RAIL_SDK_RAIL_UTILS_H
#define RAIL_SDK_RAIL_UTILS_H

#include "rail/sdk/base/rail_define.h"
#include "rail/sdk/rail_utils_define.h"

namespace rail {
#pragma pack(push, RAIL_SDK_PACKING)

class IRailUtils {
  public:
    // return number of seconds since the game launched
    virtual uint32_t GetTimeCountSinceGameLaunch() = 0;

    // return number of seconds since the computer launched
    virtual uint32_t GetTimeCountSinceComputerLaunch() = 0;

    // return rail server time, number of seconds since Jan 1, 1970
    virtual uint32_t GetTimeFromServer() = 0;

    // get an image data asynchronously
    // set image_path to a link like "http://" will download the web image and information
    // for you, and if a local path is set, you will get local image information.
    // you can set scale_to_width, scale_to_height to non-zero to scale image,
    // or zero to keep the corresponding dimension unchanged.
    // callback will be RailGetImageDataResult with raw data and image meta info,
    // currently all the image returned are standard RGBA format.
    virtual RailResult AsyncGetImageData(const RailString& image_path,
                        uint32_t scale_to_width,   // 0, no scale
                        uint32_t scale_to_height,  // 0, no scale
                        const RailString& user_data) = 0;

    virtual void GetErrorString(RailResult result, RailString* error_string) = 0;

    // can check dirty words at most kRailUsersDirtyWordsOnceCheckLimit one time,
    // string length of each word should be under kRailUsersDirtyWordMaxLength.
    virtual RailResult DirtyWordsFilter(const RailString& words,
                        bool replace_sensitive,
                        RailDirtyWordsCheckResult* check_result) = 0;

    // return rail platform type
    virtual EnumRailPlatformType GetRailPlatformType() = 0;

    virtual RailResult GetLaunchAppParameters(EnumRailLaunchAppType app_type,
                        RailString* parameter) = 0;

    virtual RailResult GetPlatformLanguageCode(RailString* language_code) = 0;

    // register callback for rail crash. When game crashes, the callback_func will
    // be called, and game developers should use the buffer that sent to callback
    // to write more infos about crash. Callback should be written according to following rules
    // and should take care to read only safe data.
    //    1. Use of the application heap is forbidden.
    //    2. Resource allocation must be severely limited.
    //    3. Library code (like STL) that may lead to heap allocation should be avoided.
    virtual RailResult RegisterCrashCallback(
                        const RailUtilsCrashCallbackFunction callback_func) = 0;

    virtual RailResult UnRegisterCrashCallback() = 0;

    // set warning message callback function
    virtual RailResult SetWarningMessageCallback(RailWarningMessageCallbackFunction callback) = 0;

    // this method will query the country code(ISO 3166-1-alpha-2 format, i.e. "CN", "US", "HK")
    // of the running client from backend the first time, and then cache the result to local memory.
    // later calls will return the cached country code directly.
    virtual RailResult GetCountryCodeOfCurrentLoggedInIP(RailString* country_code) = 0;
};

#pragma pack(pop)
}  // namespace rail

#endif  // RAIL_SDK_RAIL_UTILS_H
