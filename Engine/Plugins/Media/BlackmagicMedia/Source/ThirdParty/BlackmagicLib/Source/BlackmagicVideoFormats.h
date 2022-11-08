#pragma once

#include <vector>


namespace BlackmagicDesign
{
	namespace Private
	{
		/* VideoFormatsScanner definition
		*****************************************************************************/
		class VideoFormatsScanner
		{
		public:
			VideoFormatsScanner(int32_t InDeviceId, bool bForOutput);

			static BlackmagicVideoFormats::VideoFormatDescriptor GetVideoFormat(IDeckLinkDisplayMode* InBlackmagicVideoMode);

			std::vector<BlackmagicVideoFormats::VideoFormatDescriptor> FormatList;
		};
	}
}
