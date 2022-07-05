// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "RHI.h"
#include "PixelStreamingInputFrameRHI.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace UE::PixelStreaming
{

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSourceFrameTest, "System.Plugins.PixelStreaming.SourceFrame", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
	bool FSourceFrameTest::RunTest(const FString& Parameters)
	{
		FRHITextureCreateDesc TextureDesc =
			FRHITextureCreateDesc::Create2D(TEXT("Test Texture"), 512, 128, EPixelFormat::PF_B8G8R8A8)
			.SetClearValue(FClearValueBinding::None)
			.SetFlags(ETextureCreateFlags::None)
			.SetInitialState(ERHIAccess::Present)
			.DetermineInititialState();

		FTexture2DRHIRef TestTexture = GDynamicRHI->RHICreateTexture(TextureDesc);
		const uint64 PreCreateTime = FPlatformTime::Cycles64();

		FPlatformProcess::Sleep(0.1f);

		FPixelStreamingInputFrameRHI Frame(TestTexture);

		FPlatformProcess::Sleep(0.1f);

		const uint64 PostCreateTime = FPlatformTime::Cycles64();

		TestTrue("Frame Width", Frame.GetWidth() == 512);
		TestTrue("Frame Height", Frame.GetHeight() == 128);
		TestTrue("Frame Create Time", Frame.Metadata.SourceTime > PreCreateTime && Frame.Metadata.SourceTime < PostCreateTime);

		return true;
	}

} // namespace UE::PixelStreaming

#endif // WITH_DEV_AUTOMATION_TESTS
