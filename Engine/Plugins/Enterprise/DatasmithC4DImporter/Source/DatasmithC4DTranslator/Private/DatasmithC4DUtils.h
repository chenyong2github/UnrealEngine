// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#ifdef _MELANGE_SDK_

#include "CoreMinimal.h"

#include "DatasmithC4DMelangeSDKEnterGuard.h"
#include "c4d.h"
#include "DatasmithC4DMelangeSDKLeaveGuard.h"

/**
 * Retrieves the value of a DA_LONG parameter of a melange object as a melange::Int32,
 * and static_casts it to an int32. Will return 0 if the parameter is invalid
 */
int32 MelangeGetInt32(melange::BaseList2D* Object, melange::Int32 Parameter);

/**
 * Retrieves the value of a DA_LLONG parameter of a melange object
 * and returns it as an int64. Will return 0 if the parameter is invalid
 */
int64 MelangeGetInt64(melange::BaseList2D* Object, melange::Int32 Parameter);

/**
 * Retrieves the value of a DA_LONG parameter of a melange object as a melange::Bool,
 * and returns it as a bool. Will return false if the parameter is invalid
 */
bool MelangeGetBool(melange::BaseList2D* Object, melange::Int32 Parameter);

/**
 * Retrieves the value of a DA_REAL parameter of a melange object
 * and returns it as a double. Will return 0.0 if the parameter is invalid
 */
double MelangeGetDouble(melange::BaseList2D* Object, melange::Int32 Parameter);

/**
 * Retrieves the value of a DA_VECTOR parameter of a melange object
 * and returns it as an FVector. No coordinate or color conversions are applied.
 * Will return FVector() if the parameter is invalid
 */
FVector MelangeGetVector(melange::BaseList2D* Object, melange::Int32 Parameter);

/**
 * Retrieves the value of a DA_MATRIX parameter of a melange object
 * and returns it as an FMatrix. Will return an identity matrix if the parameter is invalid
 */
FMatrix MelangeGetMatrix(melange::BaseList2D* Object, melange::Int32 Parameter);

/**
 * Retrieves the value of a DA_BYTEARRAY parameter of a melange object
 * and returns it as an array of bytes. Will return TArray<uint8>() if the parameter is invalid
 */
TArray<uint8> MelangeGetByteArray(melange::BaseList2D* Object, melange::Int32 Parameter);

/**
 * Retrieves the value of a DA_STRING or DA_FILENAME parameter of a melange object
 * and returns it as a string. Will return FString() if the parameter is invalid
 */
FString MelangeGetString(melange::BaseList2D* Object, melange::Int32 Parameter);

/**
 * Retrieves a the object pointed to by an DA_ALIASLINK parameter of a melange object
 * and returns it. Will return nullptr if the parameter is invalid
 */
melange::BaseList2D* MelangeGetLink(melange::BaseList2D* Object, melange::Int32 Parameter);

/**
 * Retrieves the value of a DA_REAL (double) parameter from a melange object and static_casts it to float.
 * Will return 0.0f if the parameter is invalid
 */
float MelangeGetFloat(melange::BaseList2D* Object, melange::Int32 Parameter);

/**
 * Converts a vector representing a position from the melange coordinate system to the UE4 coordinate system
 */
FVector ConvertMelangePosition(const melange::Vector32& MelangePosition, float WorldUnitScale = 1.0f);
FVector ConvertMelangePosition(const melange::Vector64& MelangePosition, float WorldUnitScale = 1.0f);
FVector ConvertMelangePosition(const FVector& MelangePosition, float WorldUnitScale = 1.0f);

/**
 * Converts a vector representing a direction from the melange coordinate system to the UE4 coordinate system
 */
FVector ConvertMelangeDirection(const melange::Vector32& MelangePosition);
FVector ConvertMelangeDirection(const melange::Vector64& MelangePosition);
FVector ConvertMelangeDirection(const FVector& MelangePosition);

/**
 * Converts a melange::Vector into an FVector
 */
FVector MelangeVectorToFVector(const melange::Vector32& MelangeVector);
FVector MelangeVectorToFVector(const melange::Vector64& MelangeVector);

/**
 * Converts a melange::Vector4d32 into an FVector4
 */
FVector4 MelangeVector4ToFVector4(const melange::Vector4d32& MelangeVector);

/**
 * Converts a melange::Vector4d64 into an FVector4
 */
FVector4 MelangeVector4ToFVector4(const melange::Vector4d64& MelangeVector);

/**
 * Converts a melange::Matrix into an FMatrix
 */
FMatrix MelangeMatrixToFMatrix(const melange::Matrix& Matrix);

/**
 * Converts a melange::String into an FString
 */
FString MelangeStringToFString(const melange::String& MelangeString);

/**
 * Uses the bytes of 'String' to generate an MD5 hash, and returns the bytes of the hash back as an FString
 */
FString MD5FromString(const FString& String);

/**
 * Converts a melange::Filename to an FString path
 */
FString MelangeFilenameToPath(const melange::Filename& Filename);

/**
 * Searches for a file in places where melange is likely to put them. Will try things like making the filename
 * relative to the C4dDocumentFilename's folder, searching the 'tex' folder, and etc. Use this to search for texture
 * and IES files as the path contained in melange is likely to be pointing somewhere else if the user didn't export
 * the scene just right.
 *
 * @param Filename				The path to the file we're trying to find. Can be relative or absolute
 * @param C4dDocumentFilename	Path to the main .c4d file being imported e.g. "C:\MyFolder\Scene.c4d"
 * @return						The full, absolute found filepath if it exists, or an empty string
 */
FString SearchForFile(FString Filename, const FString& C4dDocumentFilename);

/**
 * Gets the name of a melange object as an FString, or returns 'Invalid object' if the argument is nullptr
 */
FString MelangeObjectName(melange::BaseList2D* Object);

/**
 * Gets the type of a melange object as an FString (e.g. Ocloner, Onull, Opolygon), or 'Invalid object' if the
 * argument is nullptr
 */
FString MelangeObjectTypeName(melange::BaseList2D* Object);

/**
 * Gets the data stored within a melange::GeData object according to its .GetType() and returns that as a string.
 * Will try to convert to Unreal types first (like FVector or FMatrix) and convert those to string instead
 */
FString GeDataToString(const melange::GeData& Data);

/**
 * Converts the type returned by .GetType() of a melange::GeData instance from an melange::Int32 into
 * a human-readable string
 */
FString GeTypeToString(int32 GeType);

/**
 * Gets the corresponding parameter from Object, and returns the value stored in the resulting GeData as a string.
 * Will return an empty string if Object is nullptr, but it can return NIL if the parameter is not found
 */
FString MelangeParameterValueToString(melange::BaseList2D* Object, melange::Int32 ParameterID);

/**
 * Returns the full melange ID for the BaseList2D argument as an FString, which will include AppId
 */
FString GetMelangeBaseList2dID(melange::BaseList2D* BaseList);

#endif