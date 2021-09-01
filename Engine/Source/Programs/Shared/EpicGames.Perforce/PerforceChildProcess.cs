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
	/// Wraps a call to a p4.exe child process, and allows reading data from it
	/// </summary>
	class PerforceChildProcess : IPerforceOutput, IDisposable
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
		/// Temp file containing file arguments
		/// </summary>
		string? TempFileName;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Command"></param>
		/// <param name="Arguments">Command line arguments</param>
		/// <param name="FileArguments">File arguments, which may be placed in a response file</param>
		/// <param name="InputData">Input data to pass to the child process</param>
		/// <param name="GlobalOptions"></param>
		/// <param name="Logger">Logging device</param>
		public PerforceChildProcess(string Command, IReadOnlyList<string> Arguments, IReadOnlyList<string>? FileArguments, byte[]? InputData, IReadOnlyList<string> GlobalOptions, ILogger Logger)
		{
			string PerforceFileName = GetExecutable();

			List<string> FullArguments = new List<string>();
			FullArguments.Add("-G");
			FullArguments.AddRange(GlobalOptions);
			if (FileArguments != null)
			{
				TempFileName = Path.GetTempFileName();
				File.WriteAllLines(TempFileName, FileArguments);
				FullArguments.Add($"-x{TempFileName}");
			}
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

		/// <inheritdoc/>
		public ValueTask DisposeAsync()
		{
			Dispose();
			return new ValueTask(Task.CompletedTask);
		}

		/// <inheritdoc/>
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
			if (TempFileName != null)
			{
				try { File.Delete(TempFileName); } catch { }
				TempFileName = null;
			}
		}

		/// <inheritdoc/>
		public async Task<bool> ReadAsync(CancellationToken CancellationToken)
		{
			// Update the buffer contents
			if (BufferEnd == Buffer.Length)
			{
				Array.Resize(ref Buffer, Math.Min(Buffer.Length + (32 * 1024 * 1024), Buffer.Length * 2));
			}

			// Try to read more data
			int PrevBufferEnd = BufferEnd;
			while (BufferEnd < Buffer.Length)
			{
				int Count = await ChildProcess!.ReadAsync(Buffer, BufferEnd, Buffer.Length - BufferEnd, CancellationToken);
				if (Count == 0)
				{
					break;
				}
				BufferEnd += Count;
			}
			return BufferEnd > PrevBufferEnd;
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
}
