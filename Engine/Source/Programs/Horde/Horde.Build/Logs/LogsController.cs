// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.IO;
using System.Security.Claims;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using EpicGames.Horde.Storage;
using Horde.Build.Acls;
using Horde.Build.Issues;
using Horde.Build.Jobs;
using Horde.Build.Logs.Data;
using Horde.Build.Storage;
using Horde.Build.Utilities;
using Microsoft.AspNetCore.Authorization;
using Microsoft.AspNetCore.Http;
using Microsoft.AspNetCore.Mvc;
using Microsoft.Extensions.Options;
using MongoDB.Bson;

namespace Horde.Build.Logs
{
	using JobId = ObjectId<IJob>;
	using LogId = ObjectId<ILogFile>;

	/// <summary>
	/// Format for the returned data
	/// </summary>
	public enum LogOutputFormat
	{
		/// <summary>
		/// Plain text
		/// </summary>
		Text,

		/// <summary>
		/// Raw output (text/json)
		/// </summary>
		Raw,
	}

	/// <summary>
	/// Controller for the /api/logs endpoint
	/// </summary>
	[ApiController]
	[Authorize]
	[Route("[controller]")]
	public class LogsController : ControllerBase
	{
		private readonly ILogFileService _logFileService;
		private readonly IIssueCollection _issueCollection;
		private readonly AclService _aclService;
		private readonly JobService _jobService;
		private readonly StorageService _storageService;
		private readonly IOptions<ServerSettings> _settings;

		/// <summary>
		/// Constructor
		/// </summary>
		public LogsController(ILogFileService logFileService, IIssueCollection issueCollection, AclService aclService, JobService jobService, StorageService storageService, IOptions<ServerSettings> settings)
		{
			_logFileService = logFileService;
			_issueCollection = issueCollection;
			_aclService = aclService;
			_jobService = jobService;
			_storageService = storageService;
			_settings = settings;
 		}

		/// <summary>
		/// Retrieve metadata about a specific log file
		/// </summary>
		/// <param name="logFileId">Id of the log file to get information about</param>
		/// <param name="filter">Filter for the properties to return</param>
		/// <param name="cancellationToken">Cancellation token for the request</param>
		/// <returns>Information about the requested project</returns>
		[HttpGet]
		[Route("/api/v1/logs/{logFileId}")]
		[ProducesResponseType(typeof(GetLogFileResponse), 200)]
		public async Task<ActionResult<object>> GetLog(LogId logFileId, [FromQuery] PropertyFilter? filter = null, CancellationToken cancellationToken = default)
		{
			ILogFile? logFile = await _logFileService.GetLogFileAsync(logFileId, cancellationToken);
			if (logFile == null)
			{
				return NotFound();
			}
			if (!await AuthorizeAsync(logFile, AclAction.ViewLog, User, null))
			{
				return Forbid();
			}

			LogMetadata metadata = await _logFileService.GetMetadataAsync(logFile, cancellationToken);
			return new GetLogFileResponse(logFile, metadata).ApplyFilter(filter);       
		}

		/// <summary>
		/// Uploads a blob for a log file. See /api/v1/storage/XXX/blobs.
		/// </summary>
		/// <param name="logFileId">Id of the log file to get information about</param>
		/// <param name="file">Data for the blob</param>
		/// <param name="cancellationToken">Cancellation token for the request</param>
		/// <returns>Information about the requested project</returns>
		[HttpPost]
		[Route("/api/v1/logs/{logFileId}/blobs")]
		[ProducesResponseType(typeof(WriteBlobResponse), 200)]
		public async Task<ActionResult<WriteBlobResponse>> WriteLogBlob(LogId logFileId, IFormFile? file, CancellationToken cancellationToken = default)
		{
			ILogFile? logFile = await _logFileService.GetLogFileAsync(logFileId, cancellationToken);
			if (logFile == null)
			{
				return NotFound();
			}
			if (!await AuthorizeAsync(logFile, AclAction.WriteLogData, User, null))
			{
				return Forbid();
			}

			return await StorageController.WriteBlobAsync(_storageService, Namespace.Logs, file, $"{logFile.RefName}", cancellationToken);
		}

		/// <summary>
		/// Retrieve raw data for a log file
		/// </summary>
		/// <param name="logFileId">Id of the log file to get information about</param>
		/// <param name="format">Format for the returned data</param>
		/// <param name="offset">The log offset in bytes</param>
		/// <param name="length">Number of bytes to return</param>
		/// <param name="fileName">Name of the default filename to download</param>
		/// <param name="download">Whether to download the file rather than display in the browser</param>
		/// <param name="cancellationToken">Cancellation token for the request</param>
		/// <returns>Raw log data for the requested range</returns>
		[HttpGet]
		[Route("/api/v1/logs/{logFileId}/data")]
		public async Task<ActionResult> GetLogData(
			LogId logFileId,
			[FromQuery] LogOutputFormat format = LogOutputFormat.Raw,
			[FromQuery] long offset = 0,
			[FromQuery] long length = Int64.MaxValue,
			[FromQuery] string? fileName = null,
			[FromQuery] bool download = false,
			CancellationToken cancellationToken = default)
		{
			ILogFile? logFile = await _logFileService.GetLogFileAsync(logFileId, cancellationToken);
			if (logFile == null)
			{
				return NotFound();
			}
			if (!await AuthorizeAsync(logFile, AclAction.ViewLog, User, null))
			{
				return Forbid();
			}

			Func<Stream, ActionContext, Task> copyTask;
			if (format == LogOutputFormat.Text && logFile.Type == LogType.Json)
			{
				copyTask = (outputStream, context) => _logFileService.CopyPlainTextStreamAsync(logFile, offset, length, outputStream, cancellationToken);
			}
			else
			{
				copyTask = (outputStream, context) => _logFileService.CopyRawStreamAsync(logFile, offset, length, outputStream, cancellationToken);
			}

			return new CustomFileCallbackResult(fileName ?? $"log-{logFileId}.txt", "text/plain", !download, copyTask);
		}

		/// <summary>
		/// Retrieve line data for a logfile
		/// </summary>
		/// <param name="logFileId">Id of the log file to get information about</param>
		/// <param name="index">Index of the first line to retrieve</param>
		/// <param name="count">Number of lines to retrieve</param>
		/// <param name="cancellationToken">Cancellation token for the request</param>
		/// <returns>Information about the requested project</returns>
		[HttpGet]
		[Route("/api/v1/logs/{logFileId}/lines")]
		public async Task<ActionResult> GetLogLines(LogId logFileId, [FromQuery] int index = 0, [FromQuery] int count = Int32.MaxValue, CancellationToken cancellationToken = default)
		{
			ILogFile? logFile = await _logFileService.GetLogFileAsync(logFileId, cancellationToken);
			if (logFile == null)
			{
				return NotFound();
			}
			if (!await AuthorizeAsync(logFile, AclAction.ViewLog, User, null))
			{
				return Forbid();
			}

			LogMetadata metadata = await _logFileService.GetMetadataAsync(logFile, cancellationToken);

			(int minIndex, long minOffset) = await _logFileService.GetLineOffsetAsync(logFile, index, cancellationToken);
			(int maxIndex, long maxOffset) = await _logFileService.GetLineOffsetAsync(logFile, index + Math.Min(count, Int32.MaxValue - index), cancellationToken);
			index = minIndex;
			count = maxIndex - minIndex;

			byte[] result;
			using (System.IO.Stream stream = await _logFileService.OpenRawStreamAsync(logFile, minOffset, maxOffset - minOffset, cancellationToken))
			{
				result = new byte[stream.Length];
				await stream.ReadFixedSizeDataAsync(result, 0, result.Length);
			}

			using (MemoryStream stream = new MemoryStream(result.Length + (count * 20)))
			{
				stream.WriteByte((byte)'{');

				stream.Write(Encoding.UTF8.GetBytes($"\"index\":{index},"));
				stream.Write(Encoding.UTF8.GetBytes($"\"count\":{count},"));
				stream.Write(Encoding.UTF8.GetBytes($"\"maxLineIndex\":{metadata.MaxLineIndex},"));
				stream.Write(Encoding.UTF8.GetBytes($"\"format\":{ (logFile.Type == LogType.Json ? "\"JSON\"" : "\"TEXT\"")},"));

//				Stream.Write(Encoding.UTF8.GetBytes($"\"minIndex\":{MinIndex},"));
//				Stream.Write(Encoding.UTF8.GetBytes($"\"minOffset\":{MinOffset},"));
//				Stream.Write(Encoding.UTF8.GetBytes($"\"maxIndex\":{MaxIndex},"));
//				Stream.Write(Encoding.UTF8.GetBytes($"\"maxOffset\":{MaxOffset},"));
//				Stream.Write(Encoding.UTF8.GetBytes($"\"length\":{Result.Length},"));

				stream.Write(Encoding.UTF8.GetBytes($"\"lines\":["));
				stream.WriteByte((byte)'\n');

				int offset = 0;
				for (int line = index; line < index + count; line++)
				{
					stream.WriteByte((byte)' ');
					stream.WriteByte((byte)' ');

					if (logFile.Type == LogType.Json)
					{
						// Find the end of the line and output it as an opaque blob
						int startOffset = offset;
						for (; ; offset++)
						{
							if (offset == result.Length)
							{
								stream.WriteByte((byte)'{');
								stream.WriteByte((byte)'}');
								break;
							}
							else if (result[offset] == (byte)'\n')
							{
								await stream.WriteAsync(result.AsMemory(startOffset, offset - startOffset), cancellationToken);
								offset++;
								break;
							}
						}
					}
					else
					{
						stream.WriteByte((byte)'\"');
						for (; offset < result.Length; offset++)
						{
							if (result[offset] == '\\' || result[offset] == '\"')
							{
								stream.WriteByte((byte)'\\');
								stream.WriteByte(result[offset]);
							}
							else if (result[offset] == (byte)'\n')
							{
								offset++;
								break;
							}
							else if (result[offset] >= 32 && result[offset] <= 126)
							{
								stream.WriteByte(result[offset]);
							}
							else
							{
								stream.Write(Encoding.UTF8.GetBytes($"\\x{result[offset]:x2}"));
							}
						}
						stream.WriteByte((byte)'\"');
					}

					if (line + 1 < index + count)
					{
						stream.WriteByte((byte)',');
					}

					stream.WriteByte((byte)'\n');
				}

				if (logFile.Type == LogType.Json)
				{
					stream.Write(Encoding.UTF8.GetBytes($"]"));
				}

				stream.WriteByte((byte)'}');

				Response.ContentType = "application/json";
				Response.Headers.ContentLength = stream.Length;
				stream.Position = 0;
				await stream.CopyToAsync(Response.Body, cancellationToken);
			}
			return new EmptyResult();
		}

		/// <summary>
		/// Search log data
		/// </summary>
		/// <param name="logFileId">Id of the log file to get information about</param>
		/// <param name="text">Text to search for</param>
		/// <param name="firstLine">First line to search from</param>
		/// <param name="count">Number of results to return</param>
		/// <param name="cancellationToken">Cancellation token for the request</param>
		/// <returns>Raw log data for the requested range</returns>
		[HttpGet]
		[Route("/api/v1/logs/{logFileId}/search")]
		public async Task<ActionResult<SearchLogFileResponse>> SearchLogFileAsync(
			LogId logFileId,
			[FromQuery] string text,
			[FromQuery] int firstLine = 0,
			[FromQuery] int count = 5,
			CancellationToken cancellationToken = default)
		{
			ILogFile? logFile = await _logFileService.GetLogFileAsync(logFileId, cancellationToken);
			if (logFile == null)
			{
				return NotFound();
			}
			if (!await AuthorizeAsync(logFile, AclAction.ViewLog, User, null))
			{
				return Forbid();
			}

			SearchLogFileResponse response = new SearchLogFileResponse();
			response.Stats = new LogSearchStats();
			response.Lines = await _logFileService.SearchLogDataAsync(logFile, text, firstLine, count, response.Stats, cancellationToken);
			return response;
		}

		/// <summary>
		/// Retrieve events for a logfile
		/// </summary>
		/// <param name="logFileId">Id of the log file to get information about</param>
		/// <param name="index">Index of the first line to retrieve</param>
		/// <param name="count">Number of lines to retrieve</param>
		/// <param name="cancellationToken">Cancellation token for the request</param>
		/// <returns>Information about the requested project</returns>
		[HttpGet]
		[Route("/api/v1/logs/{logFileId}/events")]
		[ProducesResponseType(typeof(List<GetLogEventResponse>), 200)]
		public async Task<ActionResult<List<GetLogEventResponse>>> GetEventsAsync(LogId logFileId, [FromQuery] int? index = null, [FromQuery] int? count = null, CancellationToken cancellationToken = default)
		{
			ILogFile? logFile = await _logFileService.GetLogFileAsync(logFileId, cancellationToken);
			if (logFile == null)
			{
				return NotFound();
			}
			if (!await AuthorizeAsync(logFile, AclAction.ViewLog, User, null))
			{
				return Forbid();
			}

			List<ILogEvent> logEvents = await _logFileService.FindEventsAsync(logFile, null, index, count, cancellationToken);

			Dictionary<ObjectId, int?> spanIdToIssueId = new Dictionary<ObjectId, int?>();

			List<GetLogEventResponse> responses = new List<GetLogEventResponse>();
			foreach (ILogEvent logEvent in logEvents)
			{
				ILogEventData logEventData = await _logFileService.GetEventDataAsync(logFile, logEvent.LineIndex, logEvent.LineCount, cancellationToken);

				int? issueId = null;
				if (logEvent.SpanId != null && !spanIdToIssueId.TryGetValue(logEvent.SpanId.Value, out issueId))
				{
					IIssueSpan? span = await _issueCollection.GetSpanAsync(logEvent.SpanId.Value);
					issueId = span?.IssueId;
					spanIdToIssueId[logEvent.SpanId.Value] = issueId;
				}

				responses.Add(new GetLogEventResponse(logEvent, logEventData, issueId));
			}
			return responses;
		}

		/// <summary>
		/// Appends data to a log file
		/// </summary>
		/// <param name="logFileId">The logfile id</param>
		/// <param name="offset">Offset within the log file</param>
		/// <param name="lineIndex">The line index</param>
		/// <param name="cancellationToken">Cancellation token for the request</param>
		/// <returns>Http result code</returns>
		[HttpPost]
		[Route("/api/v1/logs/{logFileId}")]
		public async Task<ActionResult> WriteData(LogId logFileId, [FromQuery] long offset, [FromQuery] int lineIndex, CancellationToken cancellationToken)
		{
			ILogFile? logFile = await _logFileService.GetLogFileAsync(logFileId, cancellationToken);
			if (logFile == null)
			{
				return NotFound();
			}
			if (!await AuthorizeAsync(logFile, AclAction.WriteLogData, User, null))
			{
				return Forbid();
			}

			using (MemoryStream bodyStream = new MemoryStream())
			{
				await Request.Body.CopyToAsync(bodyStream, cancellationToken);
				await _logFileService.WriteLogDataAsync(logFile, offset, lineIndex, bodyStream.ToArray(), false, cancellationToken: cancellationToken);
			}
			return Ok();
		}

		/// <summary>
		/// Determines if the user is authorized to perform an action on a particular template
		/// </summary>
		/// <param name="logFile">The template to check</param>
		/// <param name="action">The action being performed</param>
		/// <param name="user">The principal to authorize</param>
		/// <param name="permissionsCache">Permissions cache</param>
		/// <returns>True if the action is authorized</returns>
		async Task<bool> AuthorizeAsync(ILogFile logFile, AclAction action, ClaimsPrincipal user, JobPermissionsCache? permissionsCache)
		{
			if (logFile.JobId != JobId.Empty && await _jobService.AuthorizeAsync(logFile.JobId, action, user, permissionsCache))
			{
				return true;
			}
			if (logFile.SessionId != null && await _aclService.AuthorizeAsync(AclAction.ViewSession, user, permissionsCache))
			{
				return true;
			}
			return false;
		}
	}
}
