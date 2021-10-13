// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "Engine/Texture2D.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace Texture2DTest
{

constexpr const uint32 TestFlags = EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::EngineFilter;

// A simple test to make sure that basica functionality in UTexture2D::CreateTransient works as it seems to be a 
// fairly uncommon code path in our samples/test games etc.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTexture2DTestTransient, "System.Engine.Texture2D.CreateTransient", TestFlags)
bool FTexture2DTestTransient::RunTest(const FString& Parameters)
{
	{
		// Each test in this scope is expected to give one warning about invalid parameters
		AddExpectedError(TEXT("Invalid parameters specified for UTexture2D::CreateTransient()"), EAutomationExpectedErrorFlags::Contains, 3);

		UTexture2D* ZeroSizedTexture = UTexture2D::CreateTransient(0, 0);
		TestTrue(TEXT("Creating a transient texture with a zero length dimension should fail!"), ZeroSizedTexture == nullptr);

		UTexture2D* ZeroWidthTexture = UTexture2D::CreateTransient(0, 32);
		TestTrue(TEXT("Creating a transient texture with a zero length dimension should fail!"), ZeroWidthTexture == nullptr);

		UTexture2D* ZeroHeightTexture = UTexture2D::CreateTransient(32, 0);
		TestTrue(TEXT("Creating a transient texture with a zero length dimension should fail!"), ZeroHeightTexture == nullptr);
	}

	UTexture2D* TransientTexture = UTexture2D::CreateTransient(32, 32);
	TestTrue(TEXT("Failed to create a 32*32 transient texture!"), TransientTexture != nullptr);

	return true;
}

#if WITH_EDITORONLY_DATA

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTexture2DTestLockingWhenEmpty, "System.Engine.Texture2D.LockingWhenEmpty", TestFlags)
bool FTexture2DTestLockingWhenEmpty::RunTest(const FString& Parameters)
{
	// Create a texture with no valid dimensions and no data
	{
		UTexture2D* EmptyTexture = NewObject<UTexture2D>(GetTransientPackage());
		EmptyTexture->Source.Init2DWithMipChain(0, 0, ETextureSourceFormat::TSF_BGRA8);

		// Test that we can lock and unlock repeatedly
		uint8* FirstLockPtr = EmptyTexture->Source.LockMip(0);
		TestNull(TEXT("Locking an empty texture"), FirstLockPtr);
		EmptyTexture->Source.UnlockMip(0);

		// If a lock on the primary mip (0) fails because the bulkdata is empty we still
		// get a successful lock and so need to unlocok it
		uint8* SecondLockPtr = EmptyTexture->Source.LockMip(0);
		TestNull(TEXT("Locking an empty texture a second time"), SecondLockPtr);
		EmptyTexture->Source.UnlockMip(0);

		// If we fail to get the lock because the mip does not exist then we do not get
		// a lock and don't need to unlock it.
		uint8* InvalidLockPtr = EmptyTexture->Source.LockMip(1);
		TestNull(TEXT("Locking a submip of an empty texture"), InvalidLockPtr);
	}

	// Create a texture with  valid dimensions and default data
	{
		UTexture2D* Texture = NewObject<UTexture2D>(GetTransientPackage());
		Texture->Source.Init2DWithMipChain(1024, 1024, ETextureSourceFormat::TSF_BGRA8);

		// Test that we can lock and unlock repeatedly
		uint8* FirstLockPtr = Texture->Source.LockMip(0);
		TestNotNull(TEXT("Locking a valid texture"), FirstLockPtr);
		Texture->Source.UnlockMip(0);

		uint8* SecondLockPtr = Texture->Source.LockMip(0);
		TestNotNull(TEXT("Locking a valid a second time"), SecondLockPtr);
		Texture->Source.UnlockMip(0);

		// Test that we can lock each mip before unlocking them all
		for (int32 MipIndex = 0; MipIndex < Texture->Source.GetNumMips(); ++MipIndex)
		{
			uint8* MipPtr = Texture->Source.LockMip(MipIndex);
			TestNotNull(TEXT("Locking a valid texture mip"), MipPtr);
		}

		for (int32 MipIndex = 0; MipIndex < Texture->Source.GetNumMips(); ++MipIndex)
		{
			Texture->Source.UnlockMip(MipIndex);
		}
	}

	return true;
}

#endif //WITH_EDITORONLY_DATA

}

#endif 
