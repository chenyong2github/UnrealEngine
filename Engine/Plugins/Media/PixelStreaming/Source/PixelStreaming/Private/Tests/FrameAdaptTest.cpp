// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "PixelStreamingFrameAdapterProcess.h"
#include "PixelStreamingFrameAdapter.h"
#include "PixelStreamingSourceFrame.h"
#include "PixelStreamingVideoInput.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace UE::PixelStreaming
{
	class FMockSourceFrame : public FPixelStreamingSourceFrame
	{
	public:
		FMockSourceFrame(int32 InWidth, int32 InHeight)
			: FPixelStreamingSourceFrame(nullptr)
			, Width(InWidth)
			, Height(InHeight)
		{
		}

		virtual int32 GetWidth() const override { return Width; }
		virtual int32 GetHeight() const override { return Height; }

	private:
		int32 Width;
		int32 Height;
	};

	class FMockAdaptedVideoFrameLayer : public IPixelStreamingAdaptedVideoFrameLayer
	{
	public:
		FMockAdaptedVideoFrameLayer(int32 InWidth, int32 InHeight)
			: Width(InWidth)
			, Height(InHeight)
		{
		}

		virtual ~FMockAdaptedVideoFrameLayer() = default;

		virtual int32 GetWidth() const override { return Width; }
		virtual int32 GetHeight() const override { return Height; }

		void SetWorking(bool InWorking) { Working = InWorking; }
		bool IsWorking() const { return Working; }

	private:
		int32 Width = 0;
		int32 Height = 0;
		bool Working = false;
	};

	class FMockAdapterProcess : public FPixelStreamingFrameAdapterProcess
	{
	public:
		FMockAdapterProcess(float InScale)
		:Scale(InScale)
		{
		}

		virtual ~FMockAdapterProcess() = default;

		bool HasResChanged() const { return ResChanged; }
		int GetBufferCreateCount() const { return BuffersCreated; }
		FMockAdaptedVideoFrameLayer* GetCurrentWriteBuffer() { return StaticCast<FMockAdaptedVideoFrameLayer*>(GetWriteBuffer().Get()); }

		void MockFinish()
		{
			FMockAdaptedVideoFrameLayer* WriteBuffer = StaticCast<FMockAdaptedVideoFrameLayer*>(GetWriteBuffer().Get());
			WriteBuffer->SetWorking(false);
			EndProcess();
		}

	protected:
		virtual void OnSourceResolutionChanged(int32 OldWidth, int32 OldHeight, int32 NewWidth, int32 NewHeight) override
		{
			ResChanged = true;
		}

		virtual TSharedPtr<IPixelStreamingAdaptedVideoFrameLayer> CreateOutputBuffer(int32 SourceWidth, int32 SourceHeight) override
		{
			BuffersCreated++;
			return MakeShared<FMockAdaptedVideoFrameLayer>(SourceWidth * Scale, SourceHeight * Scale);
		}

		virtual void BeginProcess(const FPixelStreamingSourceFrame& SourceFrame) override
		{
			FMockAdaptedVideoFrameLayer* WriteBuffer = StaticCast<FMockAdaptedVideoFrameLayer*>(GetWriteBuffer().Get());
			WriteBuffer->SetWorking(true);
		}

	private:
		float Scale = 1.0f;
		bool ResChanged = false;
		int BuffersCreated = 0;
	};

	class FMockFrameAdapter : public FPixelStreamingFrameAdapter
	{
	public:
		FMockFrameAdapter(TSharedPtr<FPixelStreamingVideoInput> VideoInput)
			: FPixelStreamingFrameAdapter(VideoInput)
		{
		}

		FMockFrameAdapter(TSharedPtr<FPixelStreamingVideoInput> VideoInput, TArray<float> LayerScales)
			: FPixelStreamingFrameAdapter(VideoInput, LayerScales)
		{
		}

		virtual ~FMockFrameAdapter() = default;

		FMockAdapterProcess* GetLayerAdapter(int LayerIndex)
		{
			return StaticCast<FMockAdapterProcess*>(LayerAdapters[LayerIndex].Get());
		}

	protected:
		virtual TSharedPtr<FPixelStreamingFrameAdapterProcess> CreateAdaptProcess(float Scale) override
		{
			return MakeShared<FMockAdapterProcess>(Scale);
		}
	};

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAdaptProcessTest, "PixelStreaming.AdaptProcess", EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::EngineFilter)
	bool FAdaptProcessTest::RunTest(const FString& Parameters)
	{
		const int32 MockFrame1Width = 32;
		const int32 MockFrame1Height = 96;

		const int32 MockFrame2Width = 67;
		const int32 MockFrame2Height = 101;

		TSharedPtr<FMockAdapterProcess> MockAdapterProcess = MakeShared<FMockAdapterProcess>(1.0f);
		TestFalse("Initialized after create.", MockAdapterProcess->IsInitialized());
		TestFalse("Busy after create.", MockAdapterProcess->IsBusy());
		TestFalse("HasOutput after create.", MockAdapterProcess->HasOutput());

		FMockSourceFrame MockFrame1(MockFrame1Width, MockFrame1Height);
		MockAdapterProcess->Process(MockFrame1);

		TestTrue("Initialized after Process.", MockAdapterProcess->IsInitialized());
		TestTrue("Busy after Process.", MockAdapterProcess->IsBusy());
		TestFalse("HasOutput after Process.", MockAdapterProcess->HasOutput());
		TestFalse("OnSourceResolutionChanged after Process.", MockAdapterProcess->HasResChanged());
		TestTrue("Three output buffers created after Process.", MockAdapterProcess->GetBufferCreateCount() == 3);
		TestTrue("Output has correct width after Process.", MockAdapterProcess->GetCurrentWriteBuffer()->GetWidth() == MockFrame1Width);
		TestTrue("Output has correct height after Process.", MockAdapterProcess->GetCurrentWriteBuffer()->GetHeight() == MockFrame1Height);
		TestTrue("Output is working after Process.", MockAdapterProcess->GetCurrentWriteBuffer()->IsWorking());

		MockAdapterProcess->MockFinish();

		TestTrue("Still Initialized after Finish.", MockAdapterProcess->IsInitialized());
		TestFalse("Busy after Finish.", MockAdapterProcess->IsBusy());
		TestTrue("HasOutput after Finish.", MockAdapterProcess->HasOutput());
		TestTrue("GetOutputLayerWidth correct after Finish.", MockAdapterProcess->GetOutputLayerWidth() == MockFrame1Width);
		TestTrue("GetOutputLayerHeight correct after Finish.", MockAdapterProcess->GetOutputLayerHeight() == MockFrame1Height);
		TestFalse("OnSourceResolutionChanged after Finish.", MockAdapterProcess->HasResChanged());
		TestTrue("No new buffers created after Finish.", MockAdapterProcess->GetBufferCreateCount() == 3);
		TestTrue("Output has correct width after Finish.", MockAdapterProcess->GetCurrentWriteBuffer()->GetWidth() == MockFrame1Width);
		TestTrue("Output has correct height after Finish.", MockAdapterProcess->GetCurrentWriteBuffer()->GetHeight() == MockFrame1Height);
		TestFalse("Output is no longer working after Finish.", MockAdapterProcess->GetCurrentWriteBuffer()->IsWorking());

		FMockSourceFrame MockFrame2(MockFrame2Width, MockFrame2Height);
		MockAdapterProcess->Process(MockFrame2);

		TestTrue("Still Initialized after Process new res.", MockAdapterProcess->IsInitialized());
		TestTrue("Busy after Process new res.", MockAdapterProcess->IsBusy());
		TestFalse("HasOutput after Process new res.", MockAdapterProcess->HasOutput());
		TestTrue("OnSourceResolutionChanged after Process new res.", MockAdapterProcess->HasResChanged());
		TestTrue("3 new buffers created after Process new res.", MockAdapterProcess->GetBufferCreateCount() == 6);
		TestTrue("Output has correct width after Process new res.", MockAdapterProcess->GetCurrentWriteBuffer()->GetWidth() == MockFrame2Width);
		TestTrue("Output has correct height after Process new res.", MockAdapterProcess->GetCurrentWriteBuffer()->GetHeight() == MockFrame2Height);
		TestTrue("Output is working after Process new res.", MockAdapterProcess->GetCurrentWriteBuffer()->IsWorking());

		MockAdapterProcess->MockFinish();

		FMockSourceFrame MockFrame3(MockFrame1Width, MockFrame1Height);
		MockAdapterProcess->Process(MockFrame3);
		MockAdapterProcess->MockFinish();

		TSharedPtr<FMockAdaptedVideoFrameLayer> ReadBuffer0 = StaticCastSharedPtr<FMockAdaptedVideoFrameLayer>(MockAdapterProcess->ReadOutput());
		TSharedPtr<FMockAdaptedVideoFrameLayer> ReadBuffer1 = StaticCastSharedPtr<FMockAdaptedVideoFrameLayer>(MockAdapterProcess->ReadOutput());
		TestTrue("Output buffer doesnt flip when input doesnt change", ReadBuffer0 == ReadBuffer1);

		MockAdapterProcess->Process(MockFrame3);

		TSharedPtr<FMockAdaptedVideoFrameLayer> ReadBuffer2 = StaticCastSharedPtr<FMockAdaptedVideoFrameLayer>(MockAdapterProcess->ReadOutput());
		TestTrue("Output buffer doesnt flip when process hasnt completed", ReadBuffer0 == ReadBuffer2);

		MockAdapterProcess->MockFinish();

		TSharedPtr<FMockAdaptedVideoFrameLayer> ReadBuffer3 = StaticCastSharedPtr<FMockAdaptedVideoFrameLayer>(MockAdapterProcess->ReadOutput());
		TestTrue("Output buffer does flip when process completed", ReadBuffer0 != ReadBuffer3);

		MockAdapterProcess->Process(MockFrame3);
		MockAdapterProcess->MockFinish();

		TSharedPtr<FMockAdaptedVideoFrameLayer> ReadBuffer4 = StaticCastSharedPtr<FMockAdaptedVideoFrameLayer>(MockAdapterProcess->ReadOutput());
		TestTrue("All three buffers read after the third finished process", ReadBuffer3 != ReadBuffer4 && ReadBuffer0 != ReadBuffer4);

		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFrameAdapterTest, "PixelStreaming.FrameAdapterTest", EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::EngineFilter)
	bool FFrameAdapterTest::RunTest(const FString& Parameters)
	{
		TSharedPtr<FPixelStreamingVideoInput> MockVideoInput = MakeShared<FPixelStreamingVideoInput>();
		TArray<float> LayerScales{ 1.0f, 0.5f, 0.25f };
		FMockFrameAdapter MockFrameAdapter(MockVideoInput, LayerScales);

		TestFalse("IsReady after create", MockFrameAdapter.IsReady());
		TestTrue("GetNumLayers() returns expected count", MockFrameAdapter.GetNumLayers() == 0);

		const int32 MockSourceFrameWidth = 1024;
		const int32 MockSourceFrameHeight = 768;
		FMockSourceFrame MockSourceFrame(1024, 768);
		MockVideoInput->OnFrame.Broadcast(MockSourceFrame);

		TestFalse("IsReady after OnFrame but before Adapt finishes.", MockFrameAdapter.IsReady());
		TestTrue("GetNumLayers() doesnt change after OnFrame", MockFrameAdapter.GetNumLayers() == 3);

		for (int i = 0; i < 3; ++i)
		{
			FMockAdapterProcess* Process = MockFrameAdapter.GetLayerAdapter(i);
			TestTrue("Adapter process is busy after OnFrame", Process->IsBusy());
			TestFalse("Adapter process has no output after OnFrame", Process->HasOutput());
		}

		for (int i = 0; i < 3; ++i)
		{
			FMockAdapterProcess* Process = MockFrameAdapter.GetLayerAdapter(i);
			Process->MockFinish();
		}

		for (int i = 0; i < 3; ++i)
		{
			FMockAdapterProcess* Process = MockFrameAdapter.GetLayerAdapter(i);
			TestFalse("Adapter process is not busy after MockFinish", Process->IsBusy());
			TestTrue("Adapter process has output after MockFinish", Process->HasOutput());
		}

		TestTrue("IsReady after all adapt processes finished.", MockFrameAdapter.IsReady());
		TestTrue("GetNumLayers() doesnt change after all adapt processes finished", MockFrameAdapter.GetNumLayers() == 3);

		for (int i = 0; i < 3; ++i)
		{
			const int32 OutputWidth = MockFrameAdapter.GetWidth(i);
			const int32 OutputHeight = MockFrameAdapter.GetHeight(i);
			const int32 ExpectedWidth = MockSourceFrameWidth * LayerScales[i];
			const int32 ExpectedHeight = MockSourceFrameHeight * LayerScales[i];

			TestTrue("Adapter process output width matches expected", OutputWidth == ExpectedWidth);
			TestTrue("Adapter process output height matches expected", OutputHeight == ExpectedHeight);

			TSharedPtr<FMockAdaptedVideoFrameLayer> Output = StaticCastSharedPtr<FMockAdaptedVideoFrameLayer>(MockFrameAdapter.ReadOutput(i));

			TestTrue("ReadOutput returns buffer with expected width", Output->GetWidth() == ExpectedWidth);
			TestTrue("ReadOutput returns buffer with expected height", Output->GetHeight() == ExpectedHeight);
		}


		return true;
	}

} // namespace UE::PixelStreaming

#endif // WITH_DEV_AUTOMATION_TESTS
