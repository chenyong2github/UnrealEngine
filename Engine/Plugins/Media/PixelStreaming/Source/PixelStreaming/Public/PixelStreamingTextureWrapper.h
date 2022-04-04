// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Templates/SharedPointer.h"

//--------------------------------FPixelStreamingTextureWrapper---------------------------------------------------------

/*
* Wraps a "texture" for use with the TextureSource.
* In this context a texture can be any type you want as long as it is useful to the eventual TextureSource that uses it. 
*/
class PIXELSTREAMING_API FPixelStreamingTextureWrapper
{
private:
	/* A simple id incrementer, every call increments by 1 */
	static int NextTypeId()
	{
		static int Id(0);
		return Id++;
	}

	/* TypeIdOf<T> will get the unique id for specified type - this is because static values inside template are specific to each type. */
	template <typename T>
	static int TypeIdOf()
	{
		static int Id(NextTypeId());
		return Id;
	}

	/* Base "any Texture" type - all it does it have a type id as a member. */
	struct FPixelStreamingAnyTextureBase
	{
		int TypeId;
		FPixelStreamingAnyTextureBase(const int InTypeId)
			: TypeId(InTypeId) {}
		virtual ~FPixelStreamingAnyTextureBase() {}
	};

	/* A wrapper around any type that could represent a texture. */
	template <typename T>
	struct FPixelStreamingAnyTexture : FPixelStreamingAnyTextureBase
	{
		T Value;

		FPixelStreamingAnyTexture(const T& InValue)
			: FPixelStreamingAnyTextureBase(TypeIdOf<T>()), Value(InValue) {}
	};

public:
	/**
	* Constructor where we wrap any "texture" type in this class.
	* @param InTexture - The "texture" to wrap.
	*/
	template <typename T>
	FPixelStreamingTextureWrapper(const T& InTexture)
		: WrappedTexture(new FPixelStreamingAnyTexture<T>(InTexture))
	{
	}

	/**
	* Gets the underlying wrapped "texture" and attempts to cast it to type specified in <T>.
	* Note: This method with fire an verify assertion if the type requested by this method does not exactly match the underlying type.
	* Even the difference between Type& and Type will cause this assertion to fire.
	* @return The "texture" type unwrapped and cast to <T>.
	*/
	template <typename T>
	T GetTexture()
	{
		verifyf(TypeIdOf<T>() == WrappedTexture->TypeId, TEXT("Type ids mismatch, this indicates you are trying to get a texture with a different type than it was created with. %d =/= %d"), TypeIdOf<T>(), WrappedTexture->TypeId);
		return StaticCastSharedPtr<const FPixelStreamingAnyTexture<T>>(WrappedTexture)->Value;
	}

private:
	/* The actual underlying member that wraps the "texture". */
	TSharedPtr<FPixelStreamingAnyTextureBase> WrappedTexture;
};