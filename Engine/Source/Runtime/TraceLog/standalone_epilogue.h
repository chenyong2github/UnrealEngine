// Copyright Epic Games, Inc. All Rights Reserved.

// HEADER_UNIT_SKIP - Not included directly

// {{{1 amalgamation-tail //////////////////////////////////////////////////////

#if PLATFORM_WINDOWS
#	pragma warning(pop)
#endif

#if TRACE_UE_COMPAT_LAYER

namespace UE {
namespace Trace {

#if defined(TRACE_HAS_ANALYSIS)

inline void SerializeToCborImpl(TArray<uint8>&, const IAnalyzer::FEventData&, uint32)
{
	*(int*)0 = 0; // unsupported
}

#endif // TRACE_HAS_ANALYSIS

} // namespace Trace
} // namespace UE

#if PLATFORM_WINDOWS
#	if defined(UNICODE) || defined(_UNICODE)
#		undef TEXT
#		undef TCHAR
#		define TEXT(x)	L##x
#	endif
#endif

#endif // TRACE_UE_COMPAT_LAYER



// {{{1 library-setup //////////////////////////////////////////////////////////

#include <string_view>

#define TRACE_EVENT_DEFINE			UE_TRACE_EVENT_DEFINE
#define TRACE_EVENT_BEGIN			UE_TRACE_EVENT_BEGIN
#define TRACE_EVENT_BEGIN_EXTERN	UE_TRACE_EVENT_BEGIN_EXTERN
#define TRACE_EVENT_FIELD			UE_TRACE_EVENT_FIELD
#define TRACE_EVENT_END				UE_TRACE_EVENT_END
#define TRACE_LOG					UE_TRACE_LOG
#define TRACE_LOG_SCOPED			UE_TRACE_LOG_SCOPED
#define TRACE_LOG_SCOPED_T			UE_TRACE_LOG_SCOPED_T
#define TRACE_CHANNEL				UE_TRACE_CHANNEL
#define TRACE_CHANNEL_EXTERN		UE_TRACE_CHANNEL_EXTERN
#define TRACE_CHANNEL_DEFINE		UE_TRACE_CHANNEL_DEFINE

namespace trace = UE::Trace;

#define TRACE_PRIVATE_CONCAT_(x, y)		x##y
#define TRACE_PRIVATE_CONCAT(x, y)		TRACE_PRIVATE_CONCAT_(x, y)
#define TRACE_PRIVATE_UNIQUE_VAR(name)	TRACE_PRIVATE_CONCAT($trace_##name, __LINE__)



// {{{1 session-header /////////////////////////////////////////////////////////

namespace UE {
namespace Trace {

enum class Build
{
	Unknown,
	Debug,
	DebugGame,
	Development,
	Shipping,
	Test
};

void DescribeSession(
	const std::string_view& AppName,
	Build Variant=Build::Unknown,
	const std::string_view& CommandLine="",
	const std::string_view& BuildVersion="unknown_ver");

} // namespace Trace
} // namespace UE

// {{{1 session-source /////////////////////////////////////////////////////////

#if TRACE_IMPLEMENT

namespace UE {
namespace Trace {
namespace Private {

TRACE_EVENT_BEGIN(Diagnostics, Session2, NoSync|Important)
	TRACE_EVENT_FIELD(uint8, ConfigurationType)
	TRACE_EVENT_FIELD(trace::AnsiString, AppName)
	TRACE_EVENT_FIELD(trace::AnsiString, BuildVersion)
	TRACE_EVENT_FIELD(trace::AnsiString, Platform)
	TRACE_EVENT_FIELD(trace::AnsiString, CommandLine)
TRACE_EVENT_END()

} // namespace Private

////////////////////////////////////////////////////////////////////////////////
void DescribeSession(
	const std::string_view& AppName,
	Build Variant,
	const std::string_view& CommandLine,
	const std::string_view& BuildVersion)
{
	using namespace Private;
	using namespace std::literals;

	std::string_view Platform;
#if PLATFORM_WINDOWS
	Platform = "Windows"sv;
#elif PLATFORM_UNIX
	Platform = "Linux"sv;
#elif PLATFORM_MAC
	Platform = "Mac"sv;
#else
	Platform = "Unknown"sv;
#endif

	int DataSize = 0;
	DataSize += AppName.size();
	DataSize += BuildVersion.size();
	DataSize += Platform.size();
	DataSize += CommandLine.size();

	TRACE_LOG(Diagnostics, Session2, true, DataSize)
		<< Session2.AppName(AppName.data(), int(AppName.size()))
		<< Session2.BuildVersion(BuildVersion.data(), int(BuildVersion.size()))
		<< Session2.Platform(Platform.data(), int(Platform.size()))
		<< Session2.CommandLine(CommandLine.data(), int(CommandLine.size()))
		<< Session2.ConfigurationType(uint8(Variant));
}

} // namespace Trace
} // namespace UE

#endif // TRACE_IMPLEMENT



// {{{1 cpu-header /////////////////////////////////////////////////////////////

TRACE_CHANNEL_EXTERN(CpuChannel)

namespace UE {
namespace Trace {

enum CpuScopeFlags : int
{
	CpuFlush = 1 << 0,
};

struct TraceCpuScope
{
			~TraceCpuScope();
	void	Enter(int ScopeId, int Flags=0);
	int		_ScopeId = 0;
};

int ScopeNew(const std::string_view& Name);

} // namespace Trace
} // namespace UE

#define TRACE_CPU_SCOPE(name, ...) \
	trace::TraceCpuScope TRACE_PRIVATE_UNIQUE_VAR(cpu_scope); \
	if (CpuChannel) { \
		using namespace std::literals; \
		static int TRACE_PRIVATE_UNIQUE_VAR(scope_id); \
		if (0 == TRACE_PRIVATE_UNIQUE_VAR(scope_id)) \
			TRACE_PRIVATE_UNIQUE_VAR(scope_id) = trace::ScopeNew(name##sv); \
		TRACE_PRIVATE_UNIQUE_VAR(cpu_scope).Enter(TRACE_PRIVATE_UNIQUE_VAR(scope_id), ##__VA_ARGS__); \
	} \
	do {} while (0)

// {{{1 cpu-source /////////////////////////////////////////////////////////////

#if TRACE_IMPLEMENT

TRACE_CHANNEL_DEFINE(CpuChannel)

namespace UE {
namespace Trace {
namespace Private {

TRACE_EVENT_BEGIN(CpuProfiler, EventSpec, NoSync|Important)
	TRACE_EVENT_FIELD(uint32, Id)
	TRACE_EVENT_FIELD(trace::AnsiString, Name)
TRACE_EVENT_END()

TRACE_EVENT_BEGIN(CpuProfiler, EventBatch, NoSync)
	TRACE_EVENT_FIELD(uint8[], Data)
TRACE_EVENT_END()

////////////////////////////////////////////////////////////////////////////////
static int32_t encode32_7bit(int32_t value, void* __restrict out)
{
	// Calculate the number of bytes
	int32_t length = 1;
	length += (value >= (1 <<  7));
	length += (value >= (1 << 14));
	length += (value >= (1 << 21));

	// Add a gap every eigth bit for the continuations
	int32_t ret = value;
	ret = (ret & 0x0000'3fff) | ((ret & 0x0fff'c000) << 2);
	ret = (ret & 0x007f'007f) | ((ret & 0x3f80'3f80) << 1);

	// Set the bits indicating another byte follows
	int32_t continuations = 0x0080'8080;
	continuations >>= (sizeof(value) - length) * 8;
	ret |= continuations;

	::memcpy(out, &ret, sizeof(value));

	return length;
}

////////////////////////////////////////////////////////////////////////////////
static int32_t encode64_7bit(int64_t value, void* __restrict out)
{
	// Calculate the output length
	uint32_t length = 1;
	length += (value >= (1ll <<  7));
	length += (value >= (1ll << 14));
	length += (value >= (1ll << 21));
	length += (value >= (1ll << 28));
	length += (value >= (1ll << 35));
	length += (value >= (1ll << 42));
	length += (value >= (1ll << 49));

	// Add a gap every eigth bit for the continuations
	int64_t ret = value;
	ret = (ret & 0x0000'0000'0fff'ffffull) | ((ret & 0x00ff'ffff'f000'0000ull) << 4);
	ret = (ret & 0x0000'3fff'0000'3fffull) | ((ret & 0x0fff'c000'0fff'c000ull) << 2);
	ret = (ret & 0x007f'007f'007f'007full) | ((ret & 0x3f80'3f80'3f80'3f80ull) << 1);

	// Set the bits indicating another byte follows
	int64_t continuations = 0x0080'8080'8080'8080ull;
	continuations >>= (sizeof(value) - length) * 8;
	ret |= continuations;

	::memcpy(out, &ret, sizeof(value));

	return length;
}

////////////////////////////////////////////////////////////////////////////////
class ThreadBuffer
{
public:
	static void	Enter(uint64_t Timestamp, uint32_t ScopeId, int Flags);
	static void	Leave(uint64_t Timestamp);

private:
				~ThreadBuffer();
	void		Flush(bool Force);
	void		EnterImpl(uint64_t Timestamp, uint32_t ScopeId, int Flags);
	void		LeaveImpl(uint64_t Timestamp);
	enum
	{
		BufferSize	= 256,
		Overflow	= 16,
		EnterLsb	= 1,
		LeaveLsb	= 0,
	};
	uint64_t	PrevTimestamp = 0;
	uint8_t*	Cursor = Buffer;
	uint8_t		Buffer[BufferSize];

	static thread_local ThreadBuffer TlsInstance;
};

thread_local ThreadBuffer ThreadBuffer::TlsInstance;

////////////////////////////////////////////////////////////////////////////////
inline void	ThreadBuffer::Enter(uint64_t Timestamp, uint32_t ScopeId, int Flags)
{
	TlsInstance.EnterImpl(Timestamp, ScopeId, Flags);
}

////////////////////////////////////////////////////////////////////////////////
inline void	ThreadBuffer::Leave(uint64_t Timestamp)
{
	TlsInstance.LeaveImpl(Timestamp);
}

////////////////////////////////////////////////////////////////////////////////
ThreadBuffer::~ThreadBuffer()
{
	Flush(true);
}

////////////////////////////////////////////////////////////////////////////////
void ThreadBuffer::Flush(bool Force)
{
	using namespace Private;

	if (Cursor == Buffer)
		return;

	if (!Force && (Cursor <= (Buffer + BufferSize - Overflow)))
		return;

	TRACE_LOG(CpuProfiler, EventBatch, true)
		<< EventBatch.Data(Buffer, uint32(ptrdiff_t(Cursor - Buffer)));

	PrevTimestamp = 0;
	Cursor = Buffer;
}

////////////////////////////////////////////////////////////////////////////////
void ThreadBuffer::EnterImpl(uint64_t Timestamp, uint32_t ScopeId, int Flags)
{
	Timestamp -= PrevTimestamp;
	PrevTimestamp += Timestamp;
	Cursor += encode64_7bit((Timestamp) << 1 | EnterLsb, Cursor);
	Cursor += encode32_7bit(ScopeId, Cursor);

	bool ShouldFlush = (Flags & CpuScopeFlags::CpuFlush);
	Flush(ShouldFlush);
}

////////////////////////////////////////////////////////////////////////////////
void ThreadBuffer::LeaveImpl(uint64_t Timestamp)
{
	Timestamp -= PrevTimestamp;
	PrevTimestamp += Timestamp;
	Cursor += encode64_7bit((Timestamp << 1) | LeaveLsb, Cursor);

	Flush(false);
}

} // namespace Private



////////////////////////////////////////////////////////////////////////////////
int ScopeNew(const std::string_view& Name)
{
	using namespace Private;

	static int volatile NextSpecId = 1;
	int SpecId = AtomicAddRelaxed(&NextSpecId, 1);

	uint32 NameSize = uint32(Name.size());
	TRACE_LOG(CpuProfiler, EventSpec, true, NameSize)
		<< EventSpec.Id(uint32(SpecId))
		<< EventSpec.Name(Name.data(), NameSize);

	return SpecId;
}



////////////////////////////////////////////////////////////////////////////////
TraceCpuScope::~TraceCpuScope()
{
	using namespace Private;

	if (!_ScopeId)
		return;

	uint64 Timestamp = TimeGetTimestamp();
	ThreadBuffer::Leave(Timestamp);
}

////////////////////////////////////////////////////////////////////////////////
void TraceCpuScope::Enter(int ScopeId, int Flags)
{
	using namespace Private;

	_ScopeId = ScopeId;
	uint64 Timestamp = TimeGetTimestamp();
	ThreadBuffer::Enter(Timestamp, ScopeId, Flags);
}

} // namespace Trace
} // namespace UE

#endif // TRACE_IMPLEMENT



// {{{1 log-header /////////////////////////////////////////////////////////////

TRACE_CHANNEL_EXTERN(LogChannel)

namespace UE {
namespace Trace {
namespace Private {

void LogMessageImpl(int Id, const void* ParamBuffer, int ParamSize);
int	 LogMessageNew(const std::string_view& Format, const std::string_view& File, int Line);

template <typename... ARGS>
void LogMessage(int Id, ARGS&&... Args)
{
	LogMessageImpl(Id, nullptr, 0);
}

} // namespace Private
} // namespace Trace
} // namespace UE

#define TRACE_LOG_MESSAGE(format, ...) \
	if (LogChannel) { \
		using namespace std::literals; \
		static int message_id; \
		if (message_id == 0) \
			message_id = trace::Private::LogMessageNew( \
				format##sv, \
				TRACE_PRIVATE_CONCAT(__FILE__, sv), \
				__LINE__); \
		trace::Private::LogMessage(message_id, ##__VA_ARGS__); \
	} \
	do {} while (0)

// {{{1 log-source /////////////////////////////////////////////////////////////

#if TRACE_IMPLEMENT

TRACE_CHANNEL_DEFINE(LogChannel)

namespace UE {
namespace Trace {
namespace Private {

#if 0
TRACE_EVENT_BEGIN(Logging, LogCategory, NoSync|Important)
	TRACE_EVENT_FIELD(const void*, CategoryPointer)
	TRACE_EVENT_FIELD(uint8, DefaultVerbosity)
	TRACE_EVENT_FIELD(UE::Trace::AnsiString, Name)
TRACE_EVENT_END()
#endif

TRACE_EVENT_BEGIN(Logging, LogMessageSpec, NoSync|Important)
	TRACE_EVENT_FIELD(uint32, LogPoint)
	//TRACE_EVENT_FIELD(uint16, CategoryPointer)
	TRACE_EVENT_FIELD(uint16, Line)
	TRACE_EVENT_FIELD(UE::Trace::AnsiString, FileName)
	TRACE_EVENT_FIELD(UE::Trace::AnsiString, FormatString)
TRACE_EVENT_END()

TRACE_EVENT_BEGIN(Logging, LogMessage, NoSync)
	TRACE_EVENT_FIELD(uint32, LogPoint)
	TRACE_EVENT_FIELD(uint64, Cycle)
	TRACE_EVENT_FIELD(uint8[], FormatArgs)
TRACE_EVENT_END()

////////////////////////////////////////////////////////////////////////////////
void LogMessageImpl(int Id, const void* ParamBuffer, int ParamSize)
{
	(void)ParamBuffer;
	(void)ParamSize;

	uint64 Timestamp = TimeGetTimestamp();

	TRACE_LOG(Logging, LogMessage, true)
		<< LogMessage.LogPoint(Id)
		<< LogMessage.Cycle(Timestamp);
}

////////////////////////////////////////////////////////////////////////////////
int LogMessageNew(
	const std::string_view& Format,
	const std::string_view& File,
	int Line)
{
	static int volatile NextId = 1;
	int Id = AtomicAddRelaxed(&NextId, 1);

	int DataSize = 0;
	DataSize += Format.size();
	DataSize += File.size();

	TRACE_LOG(Logging, LogMessageSpec, true, DataSize)
		<< LogMessageSpec.LogPoint(Id)
		<< LogMessageSpec.Line(uint16(Line))
		<< LogMessageSpec.FileName(File.data(), int(File.size()))
		<< LogMessageSpec.FormatString(Format.data(), int(Format.size()));

	return Id;
}

} // namespace Private
} // namespace Trace
} // namespace UE

#endif // TRACE_IMPLEMENT

/* vim: set noet foldlevel=1 foldmethod=marker : */
