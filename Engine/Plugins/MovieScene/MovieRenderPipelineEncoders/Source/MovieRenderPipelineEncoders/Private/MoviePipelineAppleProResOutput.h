// Copyright Epic Games, Inc. All Rights Reserved.

#include "MoviePipelineVideoOutputBase.h"
#include "AppleProResEncoder/AppleProResEncoder.h"
#include "MoviePipelineAppleProResOutput.generated.h"

// Forward Declare


UCLASS(BlueprintType)
class UMoviePipelineAppleProResOutput : public UMoviePipelineVideoOutputBase
{
	GENERATED_BODY()

		UMoviePipelineAppleProResOutput()
		: UMoviePipelineVideoOutputBase()
		, Codec(EAppleProResEncoderCodec::ProRes_4444XQ)
		, bWriteAlpha(false)
		, bDropFrameTimecode(false)
		, bOverrideMaximumEncodingThreads(false)
		, MaxNumberOfEncodingThreads(0)
	{
	}

protected:
	// UMoviePipelineVideoOutputBase Interface
	virtual TUniquePtr<MovieRenderPipeline::IVideoCodecWriter> Initialize_GameThread(const FString& InFileName, FIntPoint InResolution, EImagePixelType InPixelType, ERGBFormat InPixelFormat, uint8 InBitDepth, uint8 InNumChannels) override;
	virtual void Initialize_EncodeThread(MovieRenderPipeline::IVideoCodecWriter* InWriter) override;
	virtual void WriteFrame_EncodeThread(MovieRenderPipeline::IVideoCodecWriter* InWriter, FImagePixelData* InPixelData) override;
	virtual void BeginFinalize_EncodeThread(MovieRenderPipeline::IVideoCodecWriter* InWriter);
	virtual void Finalize_EncodeThread(MovieRenderPipeline::IVideoCodecWriter* InWriter);
	virtual const TCHAR* GetFilenameExtension() const override { return TEXT("mov"); }
	virtual bool IsAudioSupported() const { return false; }
	// ~UMoviePipelineVideoOutputBase Interface

	// UMoviePipelineOutputBase Interface
	virtual FText GetDisplayText() const override;
	virtual bool IsAlphaSupportedImpl() const override
	{
		bool bSupportedCodec = Codec == EAppleProResEncoderCodec::ProRes_4444 || Codec == EAppleProResEncoderCodec::ProRes_4444XQ;
		return bSupportedCodec && bWriteAlpha;
	}
	// ~UMoviePipelineOutputBase Interface

public:
	/** Which Apple ProRes codec should we use? See Apple documentation for more specifics. Uses Rec 709 color primaries. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings")
	EAppleProResEncoderCodec Codec;

	/** Should we write alpha? Increases encoding time. Only works with some codecs. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(EditCondition="Codec==EAppleProResEncoderCodec::ProRes_4444 || Codec==EAppleProResEncoderCodec::ProRes_4444XQ"), Category = "Settings")
	bool bWriteAlpha;

	/** Should the embedded timecode track be written using drop-frame format? Only applicable if the sequence framerate is 29.97 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings")
	bool bDropFrameTimecode;

	/** Should we override the maximum number of encoding threads? */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings")
	bool bOverrideMaximumEncodingThreads;

	/** What is the maximum number of threads the encoder should use to encode frames with? Zero means auto-determine based on hardware. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(UIMin=0, MinValue=0, EditCondition="bOverrideMaximumEncodingThreads"), Category = "Settings")
	int32 MaxNumberOfEncodingThreads;

protected:
	struct FProResWriter : public MovieRenderPipeline::IVideoCodecWriter
	{
		TUniquePtr<FAppleProResEncoder> Writer;
	};
};