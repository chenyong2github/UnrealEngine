// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "PixelStreamingFrameAdapterProcess.h"
#include "FrameAdapter.h"
#include "IPixelStreamingInputFrame.h"
#include "IPixelStreamingVideoInput.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace UE::PixelStreaming
{
	class FMockSourceFrame : public IPixelStreamingInputFrame
	{
	public:
		FMockSourceFrame(int32 InWidth, int32 InHeight)
			: Width(InWidth)
			, Height(InHeight)
		{
		}

		virtual int32 GetType() const override { return EPixelStreamingInputFrameType::User; }
		virtual int32 GetWidth() const override { return Width; }
		virtual int32 GetHeight() const override { return Height; }

	private:
		int32 Width;
		int32 Height;
	};

	class FMockAdaptedOutputFrame : public IPixelStreamingAdaptedOutputFrame
	{
	public:
		FMockAdaptedOutputFrame(int32 InWidth, int32 InHeight)
			: Width(InWidth)
			, Height(InHeight)
		{
		}

		virtual ~FMockAdaptedOutputFrame() = default;

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

		int GetBufferCreateCount() const { return BuffersCreated; }
		
		FMockAdaptedOutputFrame* GetCurrentWriteBuffer() 
		{ 
			return StaticCast<FMockAdaptedOutputFrame*>(CurrentOutputBuffer.Get()); 
		}

		void MockFinish()
		{
			FMockAdaptedOutputFrame* WriteBuffer = StaticCast<FMockAdaptedOutputFrame*>(CurrentOutputBuffer.Get());
			WriteBuffer->SetWorking(false);
			EndProcess();
		}

	protected:
		virtual FString GetProcessName() const override { return "MockAdapterProcess"; }
		
		virtual TSharedPtr<IPixelStreamingAdaptedOutputFrame> CreateOutputBuffer(int32 SourceWidth, int32 SourceHeight) override
		{
			BuffersCreated++;
			return MakeShared<FMockAdaptedOutputFrame>(SourceWidth * Scale, SourceHeight * Scale);
		}

		virtual void BeginProcess(const IPixelStreamingInputFrame& SourceFrame, TSharedPtr<IPixelStreamingAdaptedOutputFrame> OutputBuffer) override
		{
			CurrentOutputBuffer = OutputBuffer;
			FMockAdaptedOutputFrame* WriteBuffer = StaticCast<FMockAdaptedOutputFrame*>(CurrentOutputBuffer.Get());
			WriteBuffer->SetWorking(true);
		}

	private:
		float Scale = 1.0f;
		int BuffersCreated = 0;
		TSharedPtr<IPixelStreamingAdaptedOutputFrame> CurrentOutputBuffer;
	};

	class FMockVideoInput : public IPixelStreamingVideoInput
	{
	public:
		FMockVideoInput() = default;
		virtual ~FMockVideoInput() = default;

		void OnFrame(const IPixelStreamingInputFrame&) override{};

	protected:
		virtual TSharedPtr<FPixelStreamingFrameAdapterProcess> CreateAdaptProcess(EPixelStreamingFrameBufferFormat FinalFormat, float FinalScale) override
		{
			return MakeShared<FMockAdapterProcess>(FinalScale);
		}
	};

	class FMockFrameAdapter : public FFrameAdapter
	{
	public:
		FMockFrameAdapter(TSharedPtr<IPixelStreamingVideoInput> InVideoInput, TArray<float> LayerScales)
			: FFrameAdapter(InVideoInput, LayerScales)
		{
		}

		virtual ~FMockFrameAdapter() = default;

		FMockAdapterProcess* GetLayerAdapter(int LayerIndex)
		{
			return StaticCast<FMockAdapterProcess*>(LayerAdapters[LayerIndex].Get());
		}
	};

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAdaptProcessTest, "System.Plugins.PixelStreaming.AdaptProcess", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
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
		TestTrue("No new buffers created after Finish.", MockAdapterProcess->GetBufferCreateCount() == 3);
		TestTrue("Output has correct width after Finish.", MockAdapterProcess->GetCurrentWriteBuffer()->GetWidth() == MockFrame1Width);
		TestTrue("Output has correct height after Finish.", MockAdapterProcess->GetCurrentWriteBuffer()->GetHeight() == MockFrame1Height);
		TestFalse("Output is no longer working after Finish.", MockAdapterProcess->GetCurrentWriteBuffer()->IsWorking());

		// disabling this section since resolution changes arent supported any more. (create a new adapter on resolution changes)
		//AddExpectedError("Adapter input resolution changes are not supported", EAutomationExpectedErrorFlags::Exact, 2);
		//FMockSourceFrame MockFrame2(MockFrame2Width, MockFrame2Height);
		//MockAdapterProcess->Process(MockFrame2);

		//TestTrue("Still Initialized after Process new res.", MockAdapterProcess->IsInitialized());
		//TestTrue("Busy after Process new res.", MockAdapterProcess->IsBusy());
		//TestFalse("HasOutput after Process new res.", MockAdapterProcess->HasOutput());
		//TestTrue("3 new buffers created after Process new res.", MockAdapterProcess->GetBufferCreateCount() == 6);
		//TestTrue("Output has correct width after Process new res.", MockAdapterProcess->GetCurrentWriteBuffer()->GetWidth() == MockFrame2Width);
		//TestTrue("Output has correct height after Process new res.", MockAdapterProcess->GetCurrentWriteBuffer()->GetHeight() == MockFrame2Height);
		//TestTrue("Output is working after Process new res.", MockAdapterProcess->GetCurrentWriteBuffer()->IsWorking());

		//MockAdapterProcess->MockFinish();

		FMockSourceFrame MockFrame3(MockFrame1Width, MockFrame1Height);
		MockAdapterProcess->Process(MockFrame3);
		MockAdapterProcess->MockFinish();

		TSharedPtr<FMockAdaptedOutputFrame> ReadBuffer0 = StaticCastSharedPtr<FMockAdaptedOutputFrame>(MockAdapterProcess->ReadOutput());
		TSharedPtr<FMockAdaptedOutputFrame> ReadBuffer1 = StaticCastSharedPtr<FMockAdaptedOutputFrame>(MockAdapterProcess->ReadOutput());
		TestTrue("Output buffer doesnt flip when input doesnt change", ReadBuffer0 == ReadBuffer1);

		MockAdapterProcess->Process(MockFrame3);

		TSharedPtr<FMockAdaptedOutputFrame> ReadBuffer2 = StaticCastSharedPtr<FMockAdaptedOutputFrame>(MockAdapterProcess->ReadOutput());
		TestTrue("Output buffer doesnt flip when process hasnt completed", ReadBuffer0 == ReadBuffer2);

		MockAdapterProcess->MockFinish();

		TSharedPtr<FMockAdaptedOutputFrame> ReadBuffer3 = StaticCastSharedPtr<FMockAdaptedOutputFrame>(MockAdapterProcess->ReadOutput());
		TestTrue("Output buffer does flip when process completed", ReadBuffer0 != ReadBuffer3);

		MockAdapterProcess->Process(MockFrame3);
		MockAdapterProcess->MockFinish();

		TSharedPtr<FMockAdaptedOutputFrame> ReadBuffer4 = StaticCastSharedPtr<FMockAdaptedOutputFrame>(MockAdapterProcess->ReadOutput());
		TestTrue("All three buffers read after the third finished process", ReadBuffer3 != ReadBuffer4 && ReadBuffer0 != ReadBuffer4);

		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFrameAdapterTest, "System.Plugins.PixelStreaming.FrameAdapterTest", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
	bool FFrameAdapterTest::RunTest(const FString& Parameters)
	{
		TSharedPtr<FMockVideoInput> MockVideoInput = MakeShared<FMockVideoInput>();
		TArray<float> LayerScales{ 1.0f, 0.5f, 0.25f };
		TSharedPtr<FMockFrameAdapter> MockFrameAdapter = MakeShared<FMockFrameAdapter>(MockVideoInput, LayerScales);

		// Bind OnFrameReady to call MockFrameAdapter::Process with the new frame when it comes through.
		MockVideoInput->OnFrameReady.AddLambda([MockFrameAdapter](const IPixelStreamingInputFrame& InputFrame){
			MockFrameAdapter->Process(InputFrame);
		});

		TestFalse("IsReady after create", MockFrameAdapter->IsReady());
		TestTrue("GetNumLayers() returns expected count", MockFrameAdapter->GetNumLayers() == 0);

		const int32 MockSourceFrameWidth = 1024;
		const int32 MockSourceFrameHeight = 768;
		FMockSourceFrame MockSourceFrame(MockSourceFrameWidth, MockSourceFrameHeight);
		MockVideoInput->OnFrameReady.Broadcast(MockSourceFrame);

		TestFalse("IsReady after OnFrame but before Adapt finishes.", MockFrameAdapter->IsReady());
		TestTrue("GetNumLayers() doesnt change after OnFrame", MockFrameAdapter->GetNumLayers() == 3);

		for (int i = 0; i < 3; ++i)
		{
			FMockAdapterProcess* Process = MockFrameAdapter->GetLayerAdapter(i);
			TestTrue("Adapter process is busy after OnFrame", Process->IsBusy());
			TestFalse("Adapter process has no output after OnFrame", Process->HasOutput());
		}

		for (int i = 0; i < 3; ++i)
		{
			FMockAdapterProcess* Process = MockFrameAdapter->GetLayerAdapter(i);
			Process->MockFinish();
		}

		for (int i = 0; i < 3; ++i)
		{
			FMockAdapterProcess* Process = MockFrameAdapter->GetLayerAdapter(i);
			TestFalse("Adapter process is not busy after MockFinish", Process->IsBusy());
			TestTrue("Adapter process has output after MockFinish", Process->HasOutput());
		}

		TestTrue("IsReady after all adapt processes finished.", MockFrameAdapter->IsReady());
		TestTrue("GetNumLayers() doesnt change after all adapt processes finished", MockFrameAdapter->GetNumLayers() == 3);

		for (int i = 0; i < 3; ++i)
		{
			const int32 OutputWidth = MockFrameAdapter->GetWidth(i);
			const int32 OutputHeight = MockFrameAdapter->GetHeight(i);
			const int32 ExpectedWidth = MockSourceFrameWidth * LayerScales[i];
			const int32 ExpectedHeight = MockSourceFrameHeight * LayerScales[i];

			TestTrue("Adapter process output width matches expected", OutputWidth == ExpectedWidth);
			TestTrue("Adapter process output height matches expected", OutputHeight == ExpectedHeight);

			TSharedPtr<FMockAdaptedOutputFrame> Output = StaticCastSharedPtr<FMockAdaptedOutputFrame>(MockFrameAdapter->ReadOutput(i));

			TestTrue("ReadOutput returns buffer with expected width", Output->GetWidth() == ExpectedWidth);
			TestTrue("ReadOutput returns buffer with expected height", Output->GetHeight() == ExpectedHeight);
		}


		return true;
	}

} // namespace UE::PixelStreaming

#endif // WITH_DEV_AUTOMATION_TESTS
