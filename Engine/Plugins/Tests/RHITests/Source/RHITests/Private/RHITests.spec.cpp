// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "CoreMinimal.h"
#include "RenderUtils.h"
#include "RenderingThread.h"
#include "RHIBufferTests.h"
#include "RHITextureTests.h"


BEGIN_DEFINE_SPEC(FAutomationExpectedErrorTest, "System.Automation.RHI", EAutomationTestFlags::EngineFilter | EAutomationTestFlags::ApplicationContextMask)
END_DEFINE_SPEC(FAutomationExpectedErrorTest)
void FAutomationExpectedErrorTest::Define()
{
	Describe("Test RHI Clear", [this]()
	{
		It("RHI Clear UINT VertexBuffer", [this]()
		{
			bool bResult = RunOnRenderThreadSynchronous(FRHIBufferTests::Test_RHIClearUAVUint_VertexBuffer);
			TestEqual("Clear UINT VertexBuffer failed", bResult, 1);
		});

		It("RHI Clear Float VertexBuffer", [this]()
		{
			bool bResult = RunOnRenderThreadSynchronous(FRHIBufferTests::Test_RHIClearUAVFloat_VertexBuffer);
			TestEqual("Clear Float VertexBuffer failed", bResult, 1);
		});

		It("RHI Clear UINT StructuredBuffer", [this]()
		{
			bool bResult = RunOnRenderThreadSynchronous(FRHIBufferTests::Test_RHIClearUAVUint_StructuredBuffer);
			TestEqual("Clear UINT StructuredBuffer failed", bResult, 1);
		});

		It("RHI Clear Float StructuredBuffer", [this]()
		{
			bool bResult = RunOnRenderThreadSynchronous(FRHIBufferTests::Test_RHIClearUAVFloat_StructuredBuffer);
			TestEqual("Clear Float StructuredBuffer failed", bResult, 1);
		});

		It("RHI Clear Texture2D", [this]()
		{
			bool bResult = RunOnRenderThreadSynchronous(FRHITextureTests::Test_RHIClearUAV_Texture2D);
			TestEqual("Clear Texture2D failed", bResult, 1);
		});

		It("RHI Clear Texture3D", [this]()
		{
			bool bResult = RunOnRenderThreadSynchronous(FRHITextureTests::Test_RHIClearUAV_Texture3D);
			TestEqual("Clear Texture3D failed", bResult, 1);
		});
	});

}