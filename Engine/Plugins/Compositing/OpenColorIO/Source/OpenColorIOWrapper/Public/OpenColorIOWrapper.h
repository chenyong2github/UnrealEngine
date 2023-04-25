// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Containers/ContainersFwd.h"
#include "Containers/StringView.h"
#include "Containers/UnrealString.h"
#include "Containers/Map.h"
#include "Templates/PimplPtr.h"
#include "Templates/UniquePtr.h"

#include "OpenColorIOWrapperDefines.h"

struct FImageView;
enum TextureFilter : int;

namespace OpenColorIOWrapper
{
	/** OCIO_NAMESPACE::ROLE_INTERCHANGE_SCENE equivalent, since we currently cannot delay-load this definition. */
	static constexpr ANSICHAR InterchangeName[] = "aces_interchange";
	
	/** Color space name of the engine's working color space we insert in OpenColorIO configs. */
	static constexpr ANSICHAR WorkingColorSpaceName[] = "Working Color Space";

	/** Default generated shader function name. */
	static constexpr TCHAR ShaderFunctionName[] = TEXT("OCIOConvert");

	/** Default LUT size used in the legacy gpu processor */
	static constexpr uint32 Legacy3dEdgeLength = 65;

	/** Get the OpenColorIO version string. */
	OPENCOLORIOWRAPPER_API const TCHAR* GetVersion();
}

class OPENCOLORIOWRAPPER_API FOpenColorIOConfigWrapper final
{
public:

	/** Config initialization options. */
	struct FInitializationOptions
	{
		FInitializationOptions() : bAddWorkingColorSpace(false) { };

		bool bAddWorkingColorSpace;
	};

	/** Constructor. */
	FOpenColorIOConfigWrapper();

	/**
	* Constructor.
	* 
	* @param FilePath Config absolute file path.
	* @param InOptions Initialization options.
	*/
	FOpenColorIOConfigWrapper(FStringView FilePath, FInitializationOptions InOptions);

	/** Valid when the native config has been successfully created and isn't null. */
	bool IsValid() const;
	
	/** Get the number of color spaces in the configuration. */
	int32 GetNumColorSpaces() const;
	
	/** Get a color space name at an index. */
	FString GetColorSpaceName(int32 Index) const;
	
	/** Get the index of a color space, -1 if none. */
	int32 GetColorSpaceIndex(const TCHAR* InColorSpaceName);
	
	/** Get the family name for a color space. */
	FString GetColorSpaceFamilyName(const TCHAR* InColorSpaceName) const;
	
	/** Get the number of displays in the configuration. */
	int32 GetNumDisplays() const;
	
	/** Get a display name at an index. */
	FString GetDisplayName(int32 Index) const;
	
	/** Get the number of views for a display. */
	int32 GetNumViews(const TCHAR* InDisplayName) const;
	
	/** Get a view name for its display and index. */
	FString GetViewName(const TCHAR* InDisplayName, int32 Index) const;

	/** Get a display-view trnasform name. */
	FString GetDisplayViewTransformName(const TCHAR* InDisplayName, const TCHAR* InViewName) const;

	/** Get the string hash of the config. */
	FString GetCacheID() const;

private:

	/** Convenience to create a config between the working color space and the default interchange one. */
	static TUniquePtr<FOpenColorIOConfigWrapper> CreateWorkingColorSpaceToInterchangeConfig();

	TPimplPtr<struct FOpenColorIOConfigPimpl, EPimplPtrMode::DeepCopy> Pimpl;

	friend class FOpenColorIOWrapperModule;
	friend class FOpenColorIOProcessorWrapper;
	friend class FOpenColorIOCPUProcessorWrapper;
	friend class FOpenColorIOGPUProcessorWrapper;
};

class OPENCOLORIOWRAPPER_API FOpenColorIOProcessorWrapper final
{
public:
	/**
	* Constructor.
	*
	* @param InConfig Owner config.
	* @param SourceColorSpace Source color space name.
	* @param DestinationColorSpace Destination color space name.
	* @param WorkingColorSpaceTransformType (Optional) Type of additional conversion from/to the working color space.
	* @param ContextKeyValues (Optional) ContextKeyValues Additional context modifiers.
	*/
	FOpenColorIOProcessorWrapper(
		const FOpenColorIOConfigWrapper* InConfig,
		FStringView SourceColorSpace,
		FStringView DestinationColorSpace,
		EOpenColorIOWorkingColorSpaceTransform WorkingColorSpaceTransformType = EOpenColorIOWorkingColorSpaceTransform::None,
		const TMap<FString, FString>& ContextKeyValues = {});

	/**
	* Constructor.
	*
	* @param InConfig Owner config.
	* @param SourceColorSpace Source color space name.
	* @param Display Display name in display-view transform.
	* @param View View name in display-view transform.
	* @param bInverseDirection Flag for inverse transform direction.
	* @param WorkingColorSpaceTransformType (Optional) Type of additional conversion from/to the working color space.
	* @param ContextKeyValues (Optional) ContextKeyValues Additional context modifiers.
	*/
	FOpenColorIOProcessorWrapper(
		const FOpenColorIOConfigWrapper* InConfig,
		FStringView SourceColorSpace,
		FStringView Display,
		FStringView View,
		bool bInverseDirection = false,
		EOpenColorIOWorkingColorSpaceTransform WorkingColorSpaceTransformType = EOpenColorIOWorkingColorSpaceTransform::None,
		const TMap<FString, FString>& ContextKeyValues = {});

	/** Valid when the processor has been successfully created and isn't null. */
	bool IsValid() const;

private:

	TPimplPtr<struct FOpenColorIOProcessorPimpl, EPimplPtrMode::DeepCopy> Pimpl;

	const FOpenColorIOConfigWrapper* OwnerConfig;

	EOpenColorIOWorkingColorSpaceTransform WorkingColorSpaceTransformType;

	friend class FOpenColorIOCPUProcessorWrapper;
	friend class FOpenColorIOGPUProcessorWrapper;
};


class OPENCOLORIOWRAPPER_API FOpenColorIOCPUProcessorWrapper final
{
public:
	/**
	* Constructor.
	*
	* @param InProcessor Parent processor.
	*/
	FOpenColorIOCPUProcessorWrapper(FOpenColorIOProcessorWrapper InProcessor);

	/** Valid when the processor has been successfully created and isn't null. */
	bool IsValid() const;

	/** Apply the color transform in-place to the specified image. */
	bool TransformImage(const FImageView& InOutImage) const;

	/** Apply the color transform from the source image to the destination image. (The destination FImageView is const but what it points at is not.) */
	bool TransformImage(const FImageView& SrcImage, const FImageView& DestImage) const;

private:

	const FOpenColorIOProcessorWrapper ParentProcessor;
};

class OPENCOLORIOWRAPPER_API FOpenColorIOGPUProcessorWrapper final
{
public:
	/** Gpu processor initialization options. */
	struct FInitializationOptions
	{
		FInitializationOptions() : bIsLegacy(false) { }

		bool bIsLegacy;
	};

	/**
	* Constructor.
	*
	* @param InProcessor Parent processor.
	* @param InOptions Initialization options.
	*/
	FOpenColorIOGPUProcessorWrapper(
		FOpenColorIOProcessorWrapper InProcessor,
		FInitializationOptions InOptions = FInitializationOptions()
	);

	/** Valid when the processor has been successfully created and isn't null. */
	bool IsValid() const;

	/**
	* Get the generated shader.
	*
	* @param OutShaderCode Generated shader text.
	* @param OutShaderCacheID Generated shader hash string.
	* @return True when the result is valid.
	*/
	bool GetShader(FString& OutShaderCode, FString& OutShaderCacheID) const;

	/** Get the number of 3D LUT textures. */
	uint32 GetNum3DTextures() const;

	/**
	* Get the index-specific 3D LUT texture used by the shader transform.
	*
	* @param InIndex Index of the texture.
	* @param OutName Shader parameter name of the texture.
	* @param OutEdgeLength Size of the texture.
	* @param OutTextureFilter Texture filter type.
	* @param OutData Raw texel data.
	* @return True when the result is valid.
	*/
	bool Get3DTexture(uint32 InIndex, FName& OutName, uint32& OutEdgeLength, TextureFilter& OutTextureFilter, const float*& OutData) const;

	/** Get the number of 2D LUT textures. (1D resources are disabled in our case.) */
	uint32 GetNumTextures() const;

	/**
	* Get the index-specific 2D LUT texture used by the shader transform.
	*
	* @param InIndex Index of the texture.
	* @param OutName Shader parameter name of the texture.
	* @param OutWidth Width of the texture.
	* @param OutHeight Height of the texture.
	* @param OutTextureFilter Texture filter type.
	* @param bOutRedChannelOnly Flag indicating whether the texture has a single channel or is RGB.
	* @param OutData Raw texel data.
	* @return True when the result is valid.
	*/
	bool GetTexture(uint32 InIndex, FName& OutName, uint32& OutWidth, uint32& OutHeight, TextureFilter& OutTextureFilter, bool& bOutRedChannelOnly, const float*& OutData) const;

	/** Get the string hash of the gpu processor. */
	FString GetCacheID() const;

private:

	const FOpenColorIOProcessorWrapper ParentProcessor;

	TPimplPtr<struct FOpenColorIOGPUProcessorPimpl, EPimplPtrMode::DeepCopy> GPUPimpl;
};
