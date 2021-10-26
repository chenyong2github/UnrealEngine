// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Buffers;
using System.Collections.Generic;
using System.ComponentModel.DataAnnotations;
using System.IO;
using System.Linq;
using System.Net.Http;
using System.Security.Claims;
using System.Text;
using System.Text.Json;
using System.Threading.Tasks;
using HordeServer.Api;
using HordeServer.Collections;
using HordeServer.Logs;
using HordeServer.Models;
using HordeServer.Services;
using HordeServer.Utilities;
using Microsoft.AspNetCore.Authorization;
using Microsoft.AspNetCore.Http;
using Microsoft.AspNetCore.Mvc;
using MongoDB.Bson;

namespace HordeServer.Controllers
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
		/// <summary>
		/// Instance of the LogFile service
		/// </summary>
		private readonly ILogFileService LogFileService;

		/// <summary>
		/// Instance of the issue collection
		/// </summary>
		private readonly IIssueCollection IssueCollection;

		/// <summary>
		/// Instance of the ACL service
		/// </summary>
		private readonly AclService AclService;

		/// <summary>
		/// Instance of the Job service
		/// </summary>
		private readonly JobService JobService;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="LogFileService">The Logfile service</param>
		/// <param name="IssueCollection">The issue collection</param>
		/// <param name="AclService">The ACL service</param>
		/// <param name="JobService">The Job service</param>
		public LogsController(ILogFileService LogFileService, IIssueCollection IssueCollection, AclService AclService, JobService JobService)
		{
			this.LogFileService = LogFileService;
			this.IssueCollection = IssueCollection;
			this.AclService = AclService;
			this.JobService = JobService;
		}

		/// <summary>
		/// Retrieve metadata about a specific log file
		/// </summary>
		/// <param name="LogFileId">Id of the log file to get information about</param>
		/// <param name="Filter">Filter for the properties to return</param>
		/// <returns>Information about the requested project</returns>
		[HttpGet]
		[Route("/api/v1/logs/{LogFileId}")]
		[ProducesResponseType(typeof(GetLogFileResponse), 200)]
		public async Task<ActionResult<object>> GetLog(LogId LogFileId, [FromQuery] PropertyFilter? Filter = null)
		{
			ILogFile? LogFile = await LogFileService.GetLogFileAsync(LogFileId);
			if (LogFile == null)
			{
				return NotFound();
			}
			if (!await AuthorizeAsync(LogFile, AclAction.ViewLog, User, null))
			{
				return Forbid();
			}

			LogMetadata Metadata = await LogFileService.GetMetadataAsync(LogFile);
			return new GetLogFileResponse(LogFile, Metadata).ApplyFilter(Filter);       
		}

		/// <summary>
		/// Retrieve raw data for a log file
		/// </summary>
		/// <param name="LogFileId">Id of the log file to get information about</param>
		/// <param name="Format">Format for the returned data</param>
		/// <param name="Offset">The log offset in bytes</param>
		/// <param name="Length">Number of bytes to return</param>
		/// <param name="FileName">Name of the default filename to download</param>
		/// <param name="Download">Whether to download the file rather than display in the browser</param>
		/// <returns>Raw log data for the requested range</returns>
		[HttpGet]
		[Route("/api/v1/logs/{LogFileId}/data")]
		public async Task<ActionResult> GetLogData(LogId LogFileId, [FromQuery] LogOutputFormat Format = LogOutputFormat.Raw, [FromQuery] long Offset = 0, [FromQuery] long Length = long.MaxValue, [FromQuery] string? FileName = null, [FromQuery] bool Download = false)
		{
			ILogFile? LogFile = await LogFileService.GetLogFileAsync(LogFileId);
			if (LogFile == null)
			{
				return NotFound();
			}
			if (!await AuthorizeAsync(LogFile, AclAction.ViewLog, User, null))
			{
				return Forbid();
			}

			Func<Stream, ActionContext, Task> CopyTask;
			if (Format == LogOutputFormat.Text && LogFile.Type == LogType.Json)
			{
				CopyTask = (OutputStream, Context) => LogFileService.CopyPlainTextStreamAsync(LogFile, Offset, Length, OutputStream);
			}
			else
			{
				CopyTask = (OutputStream, Context) => LogFileService.CopyRawStreamAsync(LogFile, Offset, Length, OutputStream);
			}

			return new CustomFileCallbackResult(FileName ?? $"log-{LogFileId}.txt", "text/plain", !Download, CopyTask);
		}

		/// <summary>
		/// Retrieve line data for a logfile
		/// </summary>
		/// <param name="LogFileId">Id of the log file to get information about</param>
		/// <param name="Index">Index of the first line to retrieve</param>
		/// <param name="Count">Number of lines to retrieve</param>
		/// <returns>Information about the requested project</returns>
		[HttpGet]
		[Route("/api/v1/logs/{LogFileId}/lines")]
		public async Task<ActionResult> GetLogLines(LogId LogFileId, [FromQuery] int Index = 0, [FromQuery] int Count = int.MaxValue)
		{
			ILogFile? LogFile = await LogFileService.GetLogFileAsync(LogFileId);
			if (LogFile == null)
			{
				return NotFound();
			}
			if (!await AuthorizeAsync(LogFile, AclAction.ViewLog, User, null))
			{
				return Forbid();
			}

			LogMetadata Metadata = await LogFileService.GetMetadataAsync(LogFile);

			(int MinIndex, long MinOffset) = await LogFileService.GetLineOffsetAsync(LogFile, Index);
			(int MaxIndex, long MaxOffset) = await LogFileService.GetLineOffsetAsync(LogFile, Index + Math.Min(Count, int.MaxValue - Index));
			Index = MinIndex;
			Count = MaxIndex - MinIndex;

			byte[] Result;
			using (System.IO.Stream Stream = await LogFileService.OpenRawStreamAsync(LogFile, MinOffset, MaxOffset - MinOffset))
			{
				Result = new byte[Stream.Length];
				await Stream.ReadFixedSizeDataAsync(Result, 0, Result.Length);
			}

			using (MemoryStream Stream = new MemoryStream(Result.Length + (Count * 20)))
			{
				Stream.WriteByte((byte)'{');

				Stream.Write(Encoding.UTF8.GetBytes($"\"index\":{Index},"));
				Stream.Write(Encoding.UTF8.GetBytes($"\"count\":{Count},"));
				Stream.Write(Encoding.UTF8.GetBytes($"\"maxLineIndex\":{Metadata.MaxLineIndex},"));
				Stream.Write(Encoding.UTF8.GetBytes($"\"format\":{ (LogFile.Type == LogType.Json ? "\"JSON\"" : "\"TEXT\"")},"));

//				Stream.Write(Encoding.UTF8.GetBytes($"\"minIndex\":{MinIndex},"));
//				Stream.Write(Encoding.UTF8.GetBytes($"\"minOffset\":{MinOffset},"));
//				Stream.Write(Encoding.UTF8.GetBytes($"\"maxIndex\":{MaxIndex},"));
//				Stream.Write(Encoding.UTF8.GetBytes($"\"maxOffset\":{MaxOffset},"));
//				Stream.Write(Encoding.UTF8.GetBytes($"\"length\":{Result.Length},"));

				Stream.Write(Encoding.UTF8.GetBytes($"\"lines\":["));
				Stream.WriteByte((byte)'\n');

				int Offset = 0;
				for (int Line = Index; Line < Index + Count; Line++)
				{
					Stream.WriteByte((byte)' ');
					Stream.WriteByte((byte)' ');

					if (LogFile.Type == LogType.Json)
					{
						// Find the end of the line and output it as an opaque blob
						int StartOffset = Offset;
						for (; ; Offset++)
						{
							if (Offset == Result.Length)
							{
								Stream.WriteByte((byte)'{');
								Stream.WriteByte((byte)'}');
								break;
							}
							else if (Result[Offset] == (byte)'\n')
							{
								Stream.Write(Result, StartOffset, Offset - StartOffset);
								Offset++;
								break;
							}
						}
					}
					else
					{
						Stream.WriteByte((byte)'\"');
						for (; Offset < Result.Length; Offset++)
						{
							if (Result[Offset] == '\\' || Result[Offset] == '\"')
							{
								Stream.WriteByte((byte)'\\');
								Stream.WriteByte(Result[Offset]);
							}
							else if (Result[Offset] == (byte)'\n')
							{
								Offset++;
								break;
							}
							else if (Result[Offset] >= 32 && Result[Offset] <= 126)
							{
								Stream.WriteByte(Result[Offset]);
							}
							else
							{
								Stream.Write(Encoding.UTF8.GetBytes($"\\x{Result[Offset]:x2}"));
							}
						}
						Stream.WriteByte((byte)'\"');
					}

					if (Line + 1 < Index + Count)
					{
						Stream.WriteByte((byte)',');
					}

					Stream.WriteByte((byte)'\n');
				}

				if (LogFile.Type == LogType.Json)
				{
					Stream.Write(Encoding.UTF8.GetBytes($"]"));
				}

				Stream.WriteByte((byte)'}');

				Response.ContentType = "application/json";
				Response.Headers.ContentLength = Stream.Length;
				Stream.Position = 0;
				await Stream.CopyToAsync(Response.Body);
			}
			return new EmptyResult();
		}

		/// <summary>
		/// Search log data
		/// </summary>
		/// <param name="LogFileId">Id of the log file to get information about</param>
		/// <param name="Text">Text to search for</param>
		/// <param name="FirstLine">First line to search from</param>
		/// <param name="Count">Number of results to return</param>
		/// <returns>Raw log data for the requested range</returns>
		[HttpGet]
		[Route("/api/v1/logs/{LogFileId}/search")]
		public async Task<ActionResult<SearchLogFileResponse>> SearchLogFileAsync(LogId LogFileId, [FromQuery] string Text, [FromQuery] int FirstLine = 0, [FromQuery] int Count = 5)
		{
			ILogFile? LogFile = await LogFileService.GetLogFileAsync(LogFileId);
			if (LogFile == null)
			{
				return NotFound();
			}
			if (!await AuthorizeAsync(LogFile, AclAction.ViewLog, User, null))
			{
				return Forbid();
			}

			SearchLogFileResponse Response = new SearchLogFileResponse();
			Response.Stats = new LogSearchStats();
			Response.Lines = await LogFileService.SearchLogDataAsync(LogFile, Text, FirstLine, Count, Response.Stats);
			return Response;
		}

		/// <summary>
		/// Retrieve events for a logfile
		/// </summary>
		/// <param name="LogFileId">Id of the log file to get information about</param>
		/// <param name="Index">Index of the first line to retrieve</param>
		/// <param name="Count">Number of lines to retrieve</param>
		/// <returns>Information about the requested project</returns>
		[HttpGet]
		[Route("/api/v1/logs/{LogFileId}/events")]
		[ProducesResponseType(typeof(List<GetLogEventResponse>), 200)]
		public async Task<ActionResult<List<GetLogEventResponse>>> GetEventsAsync(LogId LogFileId, [FromQuery] int? Index = null, [FromQuery] int? Count = null)
		{
			ILogFile? LogFile = await LogFileService.GetLogFileAsync(LogFileId);
			if (LogFile == null)
			{
				return NotFound();
			}
			if (!await AuthorizeAsync(LogFile, AclAction.ViewLog, User, null))
			{
				return Forbid();
			}

			List<ILogEvent> LogEvents = await LogFileService.FindLogEventsAsync(LogFile, Index, Count);

			Dictionary<ObjectId, int?> SpanIdToIssueId = new Dictionary<ObjectId, int?>();

			List<GetLogEventResponse> Responses = new List<GetLogEventResponse>();
			foreach (ILogEvent LogEvent in LogEvents)
			{
				ILogEventData LogEventData = await LogFileService.GetEventDataAsync(LogFile, LogEvent.LineIndex, LogEvent.LineCount);

				int? IssueId = null;
				if (LogEvent.SpanId != null && !SpanIdToIssueId.TryGetValue(LogEvent.SpanId.Value, out IssueId))
				{
					IIssueSpan? Span = await IssueCollection.GetSpanAsync(LogEvent.SpanId.Value);
					IssueId = Span?.IssueId;
					SpanIdToIssueId[LogEvent.SpanId.Value] = IssueId;
				}

				Responses.Add(new GetLogEventResponse(LogEvent, LogEventData, IssueId));
			}
			return Responses;
		}

		/// <summary>
		/// Appends data to a log file
		/// </summary>
		/// <param name="LogFileId">The logfile id</param>
		/// <param name="Offset">Offset within the log file</param>
		/// <param name="LineIndex">The line index</param>
		/// <returns>Http result code</returns>
		[HttpPost]
		[Route("/api/v1/logs/{LogFileId}")]
		public async Task<ActionResult> WriteData(LogId LogFileId, [FromQuery] long Offset, [FromQuery] int LineIndex)
		{
			ILogFile? LogFile = await LogFileService.GetLogFileAsync(LogFileId);
			if (LogFile == null)
			{
				return NotFound();
			}
			if (!await AuthorizeAsync(LogFile, AclAction.WriteLogData, User, null))
			{
				return Forbid();
			}

			using (MemoryStream BodyStream = new MemoryStream())
			{
				await Request.Body.CopyToAsync(BodyStream);
				await LogFileService.WriteLogDataAsync(LogFile, Offset, LineIndex, BodyStream.ToArray(), false);
			}
			return Ok();
		}

		/// <summary>
		/// Determines if the user is authorized to perform an action on a particular template
		/// </summary>
		/// <param name="LogFile">The template to check</param>
		/// <param name="Action">The action being performed</param>
		/// <param name="User">The principal to authorize</param>
		/// <param name="PermissionsCache">Permissions cache</param>
		/// <returns>True if the action is authorized</returns>
		async Task<bool> AuthorizeAsync(ILogFile LogFile, AclAction Action, ClaimsPrincipal User, JobPermissionsCache? PermissionsCache)
		{
			if (LogFile.JobId != JobId.Empty && await JobService.AuthorizeAsync(LogFile.JobId, Action, User, PermissionsCache))
			{
				return true;
			}
			if (LogFile.SessionId != null && await AclService.AuthorizeAsync(AclAction.ViewSession, User, PermissionsCache))
			{
				return true;
			}
			return false;
		}
	}
}