// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using Microsoft.Extensions.Logging;
using System;
using System.Buffers.Binary;
using System.Buffers.Text;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Diagnostics;
using System.Diagnostics.CodeAnalysis;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Security.Cryptography;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading;
using System.Threading.Tasks;

namespace EpicGames.Perforce
{
	/// <summary>
	/// Interface for the result of a Perforce operation
	/// </summary>
	public interface IPerforceResponse : IDisposable
	{
		/// <summary>
		/// Data containing the result
		/// </summary>
		ReadOnlyMemory<byte> Data { get; }

		/// <summary>
		/// Waits until more data has been read into the buffer. 
		/// </summary>
		/// <returns>True if more data was read, false otherwise</returns>
		Task<bool> ReadAsync(CancellationToken Token);

		/// <summary>
		/// Discard bytes from the start of the result buffer
		/// </summary>
		/// <param name="NumBytes">Number of bytes to discard</param>
		void Discard(int NumBytes);
	}

	/// <summary>
	/// Wraps a call to a p4.exe child process, and allows reading data from it
	/// </summary>
	class PerforceChildProcess : IPerforceResponse, IDisposable
	{
		/// <summary>
		/// The process group
		/// </summary>
		ManagedProcessGroup ChildProcessGroup;

		/// <summary>
		/// The child process instance
		/// </summary>
		ManagedProcess ChildProcess;

		/// <summary>
		/// Scope object for tracing
		/// </summary>
		ITraceSpan Scope;

		/// <summary>
		/// The buffer data
		/// </summary>
		byte[] Buffer;

		/// <summary>
		/// End of the valid portion of the buffer (exclusive)
		/// </summary>
		int BufferEnd;

		/// <inheritdoc/>
		public ReadOnlyMemory<byte> Data => Buffer.AsMemory(0, BufferEnd);

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Command"></param>
		/// <param name="Arguments">Command line arguments</param>
		/// <param name="InputData">Input data to pass to the child process</param>
		/// <param name="GlobalOptions"></param>
		/// <param name="Logger">Logging device</param>
		public PerforceChildProcess(string Command, IReadOnlyList<string> Arguments, byte[]? InputData, IReadOnlyList<string> GlobalOptions, ILogger Logger)
		{
			string PerforceFileName = GetExecutable();

			List<string> FullArguments = new List<string>();
			FullArguments.Add("-G");
			FullArguments.AddRange(GlobalOptions);
			FullArguments.Add(Command);
			FullArguments.AddRange(Arguments);

			string FullArgumentList = CommandLineArguments.Join(FullArguments);
			Logger.LogDebug("Running {0} {1}", PerforceFileName, FullArgumentList);

			Scope = TraceSpan.Create(Command, Service: "perforce");
			Scope.AddMetadata("arguments", FullArgumentList);

			ChildProcessGroup = new ManagedProcessGroup();
			ChildProcess = new ManagedProcess(ChildProcessGroup, PerforceFileName, FullArgumentList, null, null, InputData, ProcessPriorityClass.Normal);

			Buffer = new byte[64 * 1024];
		}

		/// <summary>
		/// Gets the path to the P4.EXE executable
		/// </summary>
		/// <returns>Path to the executable</returns>
		public static string GetExecutable()
		{
			string PerforceFileName;
			if (RuntimePlatform.IsWindows)
			{
				PerforceFileName = "p4.exe";
			}
			else
			{
				PerforceFileName = File.Exists("/usr/local/bin/p4") ? "/usr/local/bin/p4" : "/usr/bin/p4";
			}
			return PerforceFileName;
		}

		/// <summary>
		/// Dispose of resources owned by this object
		/// </summary>
		public void Dispose()
		{
			if (ChildProcess != null)
			{
				ChildProcess.Dispose();
				ChildProcess = null!;
			}
			if (ChildProcessGroup != null)
			{
				ChildProcessGroup.Dispose();
				ChildProcessGroup = null!;
			}
			if (Scope != null)
			{
				Scope.Dispose();
				Scope = null!;
			}
		}

		/// <inheritdoc/>
		public async Task<bool> ReadAsync(CancellationToken CancellationToken)
		{
			// Update the buffer contents
			if (BufferEnd == Buffer.Length)
			{
				Array.Resize(ref Buffer, Buffer.Length + 65536);
			}

			// Try to read more data
			int Count = await ChildProcess!.ReadAsync(Buffer, BufferEnd, Buffer.Length - BufferEnd, CancellationToken);
			if (Count == 0)
			{
				return false;
			}

			BufferEnd += Count;
			return true;
		}

		/// <inheritdoc/>
		public void Discard(int NumBytes)
		{
			if (NumBytes > 0)
			{
				Array.Copy(Buffer, NumBytes, Buffer, 0, BufferEnd - NumBytes);
				BufferEnd -= NumBytes;
			}
		}

		/// <summary>
		/// Reads all output from the child process as a string
		/// </summary>
		/// <param name="CancellationToken">Cancellation token to abort the read</param>
		/// <returns>Exit code and output from the process</returns>
		public async Task<Tuple<bool, string>> TryReadToEndAsync(CancellationToken CancellationToken)
		{
			MemoryStream Stream = new MemoryStream();

			while (await ReadAsync(CancellationToken))
			{
				ReadOnlyMemory<byte> DataCopy = Data;
				Stream.Write(DataCopy.Span);
				Discard(DataCopy.Length);
			}

			string String = Encoding.Default.GetString(Stream.ToArray());
			return Tuple.Create(ChildProcess.ExitCode == 0, String);
		}
	}

	/// <summary>
	/// Wraps a call to a p4.exe child process, and allows reading data from it
	/// </summary>
	static class PerforceResultExtensions
	{
		/// <summary>
		/// String constants for records
		/// </summary>
		static class ReadOnlyUtf8StringConstants
		{
			public static Utf8String Code = "code";
			public static Utf8String Stat = "stat";
			public static Utf8String Info = "info";
			public static Utf8String Error = "error";
		}

		/// <summary>
		/// Standard prefix for a returned record: record indicator, string, 4 bytes, 'code', string, [value]
		/// </summary>
		static readonly byte[] RecordPrefix = { (byte)'{', (byte)'s', 4, 0, 0, 0, (byte)'c', (byte)'o', (byte)'d', (byte)'e', (byte)'s' };

		/// <summary>
		/// Formats the current contents of the buffer to a string
		/// </summary>
		/// <param name="Data">The next byte that was read</param>
		/// <returns>String representation of the buffer</returns>
		private static string FormatDataAsString(ReadOnlySpan<byte> Data)
		{
			StringBuilder Result = new StringBuilder();
			if (Data.Length > 0)
			{
				for (int Idx = 0; Idx < Data.Length && Idx < 1024;)
				{
					Result.Append("\n   ");

					// Output to the end of the line
					for (; Idx < Data.Length && Idx < 1024 && Data[Idx] != '\r' && Data[Idx] != '\n'; Idx++)
					{
						if (Data[Idx] == '\t')
						{
							Result.Append("\t");
						}
						else if (Data[Idx] == '\\')
						{
							Result.Append("\\");
						}
						else if (Data[Idx] >= 0x20 && Data[Idx] <= 0x7f)
						{
							Result.Append((char)Data[Idx]);
						}
						else
						{
							Result.AppendFormat("\\x{0:x2}", Data[Idx]);
						}
					}

					// Skip the newline characters
					if (Idx < Data.Length && Data[Idx] == '\r')
					{
						Idx++;
					}
					if (Idx < Data.Length && Data[Idx] == '\n')
					{
						Idx++;
					}
				}
			}
			return Result.ToString();
		}

		/// <summary>
		/// Formats the current contents of the buffer to a string
		/// </summary>
		/// <param name="Buffer">The buffer to read from</param>
		/// <param name="BufferIdx">The next byte that was read</param>
		/// <returns>String representation of the buffer</returns>
		private static string FormatDataAsHexDump(ReadOnlySpan<byte> Buffer, int BufferIdx)
		{
			return FormatDataAsHexDump(Buffer.Slice(BufferIdx, Math.Min(Buffer.Length - BufferIdx, 1024)));
		}

		/// <summary>
		/// Formats the current contents of the buffer to a string
		/// </summary>
		/// <param name="Data">The data to format</param>
		/// <returns>String representation of the buffer</returns>
		private static string FormatDataAsHexDump(ReadOnlySpan<byte> Data)
		{
			// Format the result
			StringBuilder Result = new StringBuilder();

			const int RowLength = 16;
			for (int BaseIdx = 0; BaseIdx < Data.Length; BaseIdx += RowLength)
			{
				Result.Append("\n    ");
				for (int Offset = 0; Offset < RowLength; Offset++)
				{
					int Idx = BaseIdx + Offset;
					if (Idx >= Data.Length)
					{
						Result.Append("   ");
					}
					else
					{
						Result.AppendFormat("{0:x2} ", Data[Idx]);
					}
				}
				Result.Append("   ");
				for (int Offset = 0; Offset < RowLength; Offset++)
				{
					int Idx = BaseIdx + Offset;
					if (Idx >= Data.Length)
					{
						break;
					}
					else if (Data[Idx] < 0x20 || Data[Idx] >= 0x7f)
					{
						Result.Append('.');
					}
					else
					{
						Result.Append((char)Data[Idx]);
					}
				}
			}
			return Result.ToString();
		}

		/// <summary>
		/// Read a list of responses from the child process
		/// </summary>
		/// <param name="Perforce">The response to read from</param>
		/// <param name="StatRecordInfo">The type of stat record to parse</param>
		/// <param name="CancellationToken">Cancellation token for the read</param>
		/// <returns>Async task</returns>
		public static async Task<List<PerforceResponse>> ReadResponsesAsync(this IPerforceResponse Perforce, CachedRecordInfo? StatRecordInfo, CancellationToken CancellationToken)
		{
			List<PerforceResponse> Responses = new List<PerforceResponse>();

			// Read all the records into a list
			Task<bool> ReadTask = Perforce.ReadAsync(CancellationToken);
			while (await ReadTask)
			{
				// Start a read to add more data
				ReadTask = Perforce.ReadAsync(CancellationToken);

				// Check for the whole message not being a marshalled python object, and produce a better response in that scenario
				ReadOnlyMemory<byte> Data = Perforce.Data;
				if (Data.Length > 0 && Responses.Count == 0 && Data.Span[0] != '{')
				{
					throw new PerforceException("Unexpected response from server:{0}", FormatDataAsString(Data.Span));
				}

				// Parse the responses from the current buffer
				int BufferPos = 0;
				for (; ; )
				{
					int NewBufferPos = BufferPos;
					if (!TryReadResponse(Data.Span, ref NewBufferPos, StatRecordInfo, out PerforceResponse? Response))
					{
						break;
					}
					if (Response.Error == null || Response.Error.Generic != PerforceGenericCode.Empty)
					{
						Responses.Add(Response);
					}
					BufferPos = NewBufferPos;
				}

				// Discard all the data that we've processed
				Perforce.Discard(BufferPos);
			}

			// If the stream is complete but we couldn't parse a response from the server, treat it as an error
			if (Perforce.Data.Length > 0)
			{
				throw new PerforceException("Unexpected response from server:{0}", FormatDataAsString(Perforce.Data.Span));
			}

			return Responses;
		}

		/// <summary>
		/// Read a list of responses from the child process
		/// </summary>
		/// <param name="Perforce">The Perforce response</param>
		/// <param name="HandleRecord">Delegate to invoke for each record read</param>
		/// <param name="CancellationToken">Cancellation token for the read</param>
		/// <returns>Async task</returns>
		public static async Task ReadRecordsAsync(this IPerforceResponse Perforce, Action<PerforceRecord> HandleRecord, CancellationToken CancellationToken)
		{
			PerforceRecord Record = new PerforceRecord();
			Record.Rows = new List<KeyValuePair<Utf8String, PerforceValue>>();

			// Read all the records into a list
			Task<bool> ReadTask = Perforce.ReadAsync(CancellationToken);
			while (await ReadTask)
			{
				// Start a read to add more data
				ReadTask = Perforce.ReadAsync(CancellationToken);
				ReadOnlyMemory<byte> Data = Perforce.Data;

				// Parse the responses from the current buffer
				int BufferPos = 0;
				for (; ; )
				{
					int InitialBufferPos = BufferPos;
					if(!ReadRecord(Data.Span, ref BufferPos, Record.Rows))
					{
						BufferPos = InitialBufferPos;
						break;
					}
					HandleRecord(Record);
				}
				Perforce.Discard(BufferPos);
			}

			// If the stream is complete but we couldn't parse a response from the server, treat it as an error
			if (Perforce.Data.Length > 0)
			{
				throw new PerforceException("Unexpected response from server:{0}", FormatDataAsString(Perforce.Data.Span));
			}
		}

		/// <summary>
		/// Reads from the buffer into a record object
		/// </summary>
		/// <param name="Buffer">The buffer to read from</param>
		/// <param name="BufferPos">Current read position within the buffer</param>
		/// <param name="Rows">List of rows to read into</param>
		/// <returns>True if a record could be read; false if more data is required</returns>
		static bool ReadRecord(ReadOnlySpan<byte> Buffer, ref int BufferPos, List<KeyValuePair<Utf8String, PerforceValue>> Rows)
		{
			Rows.Clear();

			// Check we can read the initial record marker
			if (BufferPos >= Buffer.Length)
			{
				return false;
			}
			if (Buffer[BufferPos] != '{')
			{
				throw new PerforceException("Invalid record start");
			}
			BufferPos++;

			// Capture the start of the string
			int StartPos = BufferPos;
			for (; ; )
			{
				// Check that we've got a string field
				if (BufferPos >= Buffer.Length)
				{
					return false;
				}

				// If this is the end of the record, break out
				byte KeyType = Buffer[BufferPos++];
				if (KeyType == '0')
				{
					break;
				}
				else if (KeyType != 's')
				{
					throw new PerforceException("Unexpected key field type while parsing marshalled output ({0}) - expected 's', got: {1}", (int)KeyType, FormatDataAsHexDump(Buffer.Slice(BufferPos - 1)));
				}

				// Read the tag
				Utf8String Key;
				if (!TryReadString(Buffer, ref BufferPos, out Key))
				{
					return false;
				}

				// Remember the start of the value
				int ValueOffset = BufferPos;

				// Read the value type
				byte ValueType;
				if (!TryReadByte(Buffer, ref BufferPos, out ValueType))
				{
					return false;
				}

				// Parse the appropriate value
				PerforceValue Value;
				if (ValueType == 's')
				{
					Utf8String String;
					if (!TryReadString(Buffer, ref BufferPos, out String))
					{
						return false;
					}
					Value = new PerforceValue(Buffer.Slice(ValueOffset, BufferPos - ValueOffset).ToArray());
				}
				else if (ValueType == 'i')
				{
					int Integer;
					if(!TryReadInt(Buffer, ref BufferPos, out Integer))
					{
						return false;
					}
					Value = new PerforceValue(Buffer.Slice(ValueOffset, BufferPos - ValueOffset).ToArray());
				}
				else
				{
					throw new PerforceException("Unrecognized value type {0}", ValueType);
				}

				// Construct the response object with the record
				Rows.Add(KeyValuePair.Create(Key, Value));
			}
			return true;
		}

		/// <summary>
		/// Reads a response object from the buffer
		/// </summary>
		/// <param name="Buffer">The buffer to read from</param>
		/// <param name="BufferPos">Current read position within the buffer</param>
		/// <param name="StatRecordInfo">The type of record expected to parse from the response</param>
		/// <param name="Response">Receives the response object on success</param>
		/// <returns>True if a response was read, false if the buffer needs more data</returns>
		static bool TryReadResponse(ReadOnlySpan<byte> Buffer, ref int BufferPos, CachedRecordInfo? StatRecordInfo, [NotNullWhen(true)] out PerforceResponse? Response)
		{
			if (BufferPos + RecordPrefix.Length + 4 > Buffer.Length)
			{
				Response = null;
				return false;
			}

			ReadOnlySpan<byte> Prefix = Buffer.Slice(BufferPos, RecordPrefix.Length);
			if (!Prefix.SequenceEqual(RecordPrefix))
			{
				throw new PerforceException("Expected 'code' field at the start of record");
			}
			BufferPos += Prefix.Length;

			Utf8String Code;
			if (!TryReadString(Buffer, ref BufferPos, out Code))
			{
				Response = null;
				return false;
			}

			// Dispatch it to the appropriate handler
			object? Record;
			if (Code == ReadOnlyUtf8StringConstants.Stat && StatRecordInfo != null)
			{
				if(!TryReadTypedRecord(Buffer, ref BufferPos, Utf8String.Empty, StatRecordInfo, out Record))
				{
					Response = null;
					return false;
				}
			}
			else if (Code == ReadOnlyUtf8StringConstants.Info)
			{
				if(!TryReadTypedRecord(Buffer, ref BufferPos, Utf8String.Empty, PerforceReflection.InfoRecordInfo, out Record))
				{
					Response = null;
					return false;
				}
			}
			else if (Code == ReadOnlyUtf8StringConstants.Error)
			{
				if (!TryReadTypedRecord(Buffer, ref BufferPos, Utf8String.Empty, PerforceReflection.ErrorRecordInfo, out Record))
				{
					Response = null;
					return false;
				}
			}
			else
			{
				throw new PerforceException("Unknown return code for record: {0}", Code);
			}

			// Skip over the record terminator
			if(BufferPos >= Buffer.Length || Buffer[BufferPos] != '0')
			{
				throw new PerforceException("Unexpected record terminator");
			}
			BufferPos++;

			// Create the response
			Response = new PerforceResponse(Record);
			return true;
		}

		/// <summary>
		/// Parse an individual record from the server.
		/// </summary>
		/// <param name="Buffer">The buffer to read from</param>
		/// <param name="BufferPos">Current read position within the buffer</param>
		/// <param name="RequiredSuffix">The required suffix for any subobject arrays.</param>
		/// <param name="RecordInfo">Reflection information for the type being serialized into.</param>
		/// <param name="Record">Receives the record on success</param>
		/// <returns>The parsed object.</returns>
		static bool TryReadTypedRecord(ReadOnlySpan<byte> Buffer, ref int BufferPos, Utf8String RequiredSuffix, CachedRecordInfo RecordInfo, [NotNullWhen(true)] out object? Record)
		{
			// Create a bitmask for all the required tags
			ulong RequiredTagsBitMask = 0;

			// Create the new record
			object? NewRecord = RecordInfo.CreateInstance();
			if (NewRecord == null)
			{
				throw new InvalidDataException($"Unable to construct record of type {RecordInfo.Type}");
			}

			// Get the record info, and parse it into the object
			for(; ;)
			{
				// Check that we've got a string field
				if(BufferPos >= Buffer.Length)
				{
					Record = null;
					return false;
				}

				// If this is the end of the record, break out
				byte KeyType = Buffer[BufferPos];
				if (KeyType == '0')
				{
					break;
				}
				else if (KeyType != 's')
				{
					throw new PerforceException("Unexpected key field type while parsing marshalled output ({0}) - expected 's', got: {1}", (int)KeyType, FormatDataAsHexDump(Buffer.Slice(BufferPos)));
				}

				// Capture the initial buffer position, in case we have to roll back
				int StartBufferPos = BufferPos;
				BufferPos++;

				// Read the tag
				Utf8String Tag;
				if(!TryReadString(Buffer, ref BufferPos, out Tag))
				{
					Record = null;
					return false;
				}

				// Find the start of the array suffix
				int SuffixIdx = Tag.Length;
				while (SuffixIdx > 0 && (Tag[SuffixIdx - 1] == (byte)',' || (Tag[SuffixIdx - 1] >= '0' && Tag[SuffixIdx - 1] <= '9')))
				{
					SuffixIdx--;
				}

				// Separate the key into tag and suffix
				Utf8String Suffix = Tag.Slice(SuffixIdx);
				Tag = Tag.Slice(0, SuffixIdx);

				// Try to find the matching field
				CachedTagInfo? TagInfo;
				if (RecordInfo.NameToInfo.TryGetValue(Tag, out TagInfo))
				{
					RequiredTagsBitMask |= TagInfo.RequiredTagBitMask;
				}

				// Check whether it's a subobject or part of the current object.
				if (Suffix == RequiredSuffix)
				{
					if(!TryReadValue(Buffer, ref BufferPos, NewRecord, TagInfo))
					{
						Record = null;
						return false;
					}
				}
				else if (Suffix.StartsWith(RequiredSuffix) && (RequiredSuffix.Length == 0 || Suffix[RequiredSuffix.Length] == ','))
				{
					// Part of a subobject. If this record doesn't have any listed subobject type, skip the field and continue.
					if (TagInfo != null)
					{
						// Get the list field
						System.Collections.IList? List = (System.Collections.IList?)TagInfo.Field.GetValue(NewRecord);
						if (List == null)
						{
							throw new PerforceException($"Empty list for {TagInfo.Field.Name}");
						}

						// Check the suffix matches the index of the next element
						if (!IsCorrectIndex(Suffix, RequiredSuffix, List.Count))
						{
							throw new PerforceException("Subobject element received out of order: got {0}", Suffix);
						}

						// Add it to the list
						if (!TryReadValue(Buffer, ref BufferPos, NewRecord, TagInfo))
						{
							Record = null;
							return false;
						}
					}
					else if (RecordInfo.SubElementField != null)
					{
						// Move back to the start of this tag
						BufferPos = StartBufferPos;

						// Get the list field
						System.Collections.IList? List = (System.Collections.IList?)RecordInfo.SubElementField.GetValue(NewRecord);
						if (List == null)
						{
							throw new PerforceException($"Invalid field for {RecordInfo.SubElementField.Name}");
						}

						// Check the suffix matches the index of the next element
						if (!IsCorrectIndex(Suffix, RequiredSuffix, List.Count))
						{
							throw new PerforceException("Subobject element received out of order: got {0}", Suffix);
						}

						// Parse the subobject and add it to the list
						object? SubRecord;
						if (!TryReadTypedRecord(Buffer, ref BufferPos, Suffix, RecordInfo.SubElementRecordInfo!, out SubRecord))
						{
							Record = null;
							return false;
						}
						List.Add(SubRecord);
					}
					else
					{
						// Just discard the value
						if (!TryReadValue(Buffer, ref BufferPos, NewRecord, TagInfo))
						{
							Record = null;
							return false;
						}
					}
				}
				else
				{
					// Roll back
					BufferPos = StartBufferPos;
					break;
				}
			}

			// Make sure we've got all the required tags we need
			if (RequiredTagsBitMask != RecordInfo.RequiredTagsBitMask)
			{
				string MissingTagNames = String.Join(", ", RecordInfo.NameToInfo.Where(x => (RequiredTagsBitMask | x.Value.RequiredTagBitMask) != RequiredTagsBitMask).Select(x => x.Key));
				throw new PerforceException("Missing '{0}' tag when parsing '{1}'", MissingTagNames, RecordInfo.Type.Name);
			}

			// Construct the response object with the record
			Record = NewRecord;
			return true;
		}

		/// <summary>
		/// Reads a value from the input buffer
		/// </summary>
		/// <param name="Buffer">The buffer to read from</param>
		/// <param name="BufferPos">Current read position within the buffer</param>
		/// <param name="NewRecord">The new record</param>
		/// <param name="TagInfo">The current tag</param>
		/// <returns></returns>
		static bool TryReadValue(ReadOnlySpan<byte> Buffer, ref int BufferPos, object NewRecord, CachedTagInfo? TagInfo)
		{
			// Read the value type
			byte ValueType;
			if (!TryReadByte(Buffer, ref BufferPos, out ValueType))
			{
				return false;
			}

			// Parse the appropriate value
			if (ValueType == 's')
			{
				Utf8String String;
				if (!TryReadString(Buffer, ref BufferPos, out String))
				{
					return false;
				}
				if (TagInfo != null)
				{
					TagInfo.SetFromString(NewRecord, String);
				}
			}
			else if (ValueType == 'i')
			{
				int Integer;
				if (!TryReadInt(Buffer, ref BufferPos, out Integer))
				{
					return false;
				}
				if (TagInfo != null)
				{
					TagInfo.SetFromInteger(NewRecord, Integer);
				}
			}
			else
			{
				throw new PerforceException("Unrecognized value type {0}", ValueType);
			}

			return true;
		}

		/// <summary>
		/// Attempts to read a single byte from the buffer
		/// </summary>
		/// <param name="Buffer">The buffer to read from</param>
		/// <param name="BufferPos">Current read position within the buffer</param>
		/// <param name="Value">Receives the byte that was read</param>
		/// <returns>True if a byte was read from the buffer, false if there was not enough data</returns>
		static bool TryReadByte(ReadOnlySpan<byte> Buffer, ref int BufferPos, out byte Value)
		{
			if (BufferPos >= Buffer.Length)
			{
				Value = 0;
				return false;
			}

			Value = Buffer[BufferPos];
			BufferPos++;
			return true;
		}

		/// <summary>
		/// Attempts to read a single int from the buffer
		/// </summary>
		/// <param name="Buffer">The buffer to read from</param>
		/// <param name="BufferPos">Current read position within the buffer</param>
		/// <param name="Value">Receives the value that was read</param>
		/// <returns>True if an int was read from the buffer, false if there was not enough data</returns>
		static bool TryReadInt(ReadOnlySpan<byte> Buffer, ref int BufferPos, out int Value)
		{
			if(BufferPos + 4 > Buffer.Length)
			{
				Value = 0;
				return false;
			}

			Value = Buffer[BufferPos + 0] | (Buffer[BufferPos + 1] << 8) | (Buffer[BufferPos + 2] << 16) | (Buffer[BufferPos + 3] << 24);
			BufferPos += 4;
			return true;
		}

		/// <summary>
		/// Attempts to read a string with type indicator from the buffer
		/// </summary>
		/// <param name="Buffer">The buffer to read from</param>
		/// <param name="BufferPos">Current read position within the buffer</param>
		/// <param name="String">Receives the value that was read</param>
		/// <returns>True if a string was read from the buffer, false if there was not enough data</returns>
		static bool TryReadStringWithType(ReadOnlySpan<byte> Buffer, ref int BufferPos, out Utf8String String)
		{
			byte ValueType;
			if(!TryReadByte(Buffer, ref BufferPos, out ValueType))
			{
				String = new Utf8String();
				return false;
			}
			if (ValueType != 's')
			{
				throw new PerforceException("Expected string value, got '{0}'", ValueType);
			}
			return TryReadString(Buffer, ref BufferPos, out String);
		}

		/// <summary>
		/// Attempts to read a string from the buffer
		/// </summary>
		/// <param name="Buffer">The buffer to read from</param>
		/// <param name="BufferPos">Current read position within the buffer</param>
		/// <param name="String">Receives the value that was read</param>
		/// <returns>True if a string was read from the buffer, false if there was not enough data</returns>
		static bool TryReadString(ReadOnlySpan<byte> Buffer, ref int BufferPos, out Utf8String String)
		{
			int Length;
			if(!TryReadInt(Buffer, ref BufferPos, out Length))
			{
				String = new Utf8String();
				return false;
			}

			if(BufferPos + Length > Buffer.Length)
			{
				String = new Utf8String();
				return false;
			}

			String = new Utf8String(Buffer.Slice(BufferPos, Length).ToArray());
			BufferPos += Length;
			return true;
		}

		/// <summary>
		/// Determines if the given text contains the expected prefix followed by an array index
		/// </summary>
		/// <param name="Text">The text to check</param>
		/// <param name="Prefix">The required prefix</param>
		/// <param name="Index">The required index</param>
		/// <returns>True if the index is correct</returns>
		static bool IsCorrectIndex(Utf8String Text, Utf8String Prefix, int Index)
		{
			if (Prefix.Length > 0)
			{
				return Text.StartsWith(Prefix) && Text.Length > Prefix.Length && Text[Prefix.Length] == (byte)',' && IsCorrectIndex(Text.Span.Slice(Prefix.Length + 1), Index);
			}
			else
			{
				return IsCorrectIndex(Text.Span, Index);
			}
		}

		/// <summary>
		/// Determines if the given text matches the expected array index
		/// </summary>
		/// <param name="Span">The text to check</param>
		/// <param name="ExpectedIndex">The expected array index</param>
		/// <returns>True if the span matches</returns>
		static bool IsCorrectIndex(ReadOnlySpan<byte> Span, int ExpectedIndex)
		{
			int Index;
			int BytesConsumed;
			return Utf8Parser.TryParse(Span, out Index, out BytesConsumed) && BytesConsumed == Span.Length && Index == ExpectedIndex;
		}
	}
}
