// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using System;
using System.IO;
using System.Linq;
using System.Text;

namespace EpicGames.UHT.Utils
{

	/// <summary>
	/// Interface used to read/write files
	/// </summary>
	public interface IUhtFileManager
	{
		/// <summary>
		/// Return the full file path for a partial path
		/// </summary>
		/// <param name="FilePath">The partial file path</param>
		/// <returns>The full file path</returns>
		public string GetFullFilePath(string FilePath);

		/// <summary>
		/// Read the given source file
		/// </summary>
		/// <param name="FilePath">File path</param>
		/// <param name="Fragment">Read fragment information</param>
		/// <returns>True if the file was read</returns>
		public bool ReadSource(string FilePath, out UhtSourceFragment Fragment);

		/// <summary>
		/// Read the given source file
		/// </summary>
		/// <param name="FilePath">File path</param>
		/// <returns>Buffer containing the read data or null if not found.  The returned buffer must be returned to the cache via a call to UhtBuffer.Return</returns>
		public UhtBuffer? ReadOutput(string FilePath);

		/// <summary>
		/// Write the given contents to the file
		/// </summary>
		/// <param name="FilePath">Path to write to</param>
		/// <param name="Contents">Contents to write</param>
		/// <returns>True if the source was written</returns>
		public bool WriteOutput(string FilePath, ReadOnlySpan<char> Contents);

		/// <summary>
		/// Rename the given file
		/// </summary>
		/// <param name="OldFilePath">Old file path name</param>
		/// <param name="NewFilePath">New file path name</param>
		/// <returns>True if the file was renamed</returns>
		public bool RenameOutput(string OldFilePath, string NewFilePath);
	}

	/// <summary>
	/// Implementation of a file manager that reads/writes from disk
	/// </summary>
	public class UhtStdFileManager : IUhtFileManager
	{

		/// <summary>
		/// Construct a new file manager
		/// </summary>
		public UhtStdFileManager()
		{
		}

		/// <inheritdoc/>
		public string GetFullFilePath(string FilePath)
		{
			return FilePath;
		}

		/// <inheritdoc/>
		public bool ReadSource(string FilePath, out UhtSourceFragment Fragment)
		{
			if (ReadFile(FilePath, out StringView Data))
			{
				Fragment = new UhtSourceFragment { SourceFile = null, FilePath = FilePath, LineNumber = 0, Data = Data };
				return true;
			}
			Fragment = new UhtSourceFragment { SourceFile = null, FilePath = String.Empty, LineNumber = 0, Data = new StringView() };
			return false;
		}

		/// <inheritdoc/>
		public UhtBuffer? ReadOutput(string FilePath)
		{
			// Exceptions are very expensive.  Don't bother trying to open the file if it doesn't exist
			if (!File.Exists(FilePath))
			{
				return null;
			}

			try
			{
				using (FileStream fs = new FileStream(FilePath, FileMode.Open, FileAccess.Read, FileShare.Read, 4 * 1024, FileOptions.SequentialScan))
				{
					using (StreamReader sr = new StreamReader(fs, Encoding.UTF8, true))
					{

						// Try to read the whole file into a buffer created by hand.  This avoids a LOT of memory allocations which in turn reduces the
						// GC stress on the system.  Removing the StreamReader would be nice in the future.
						long RawFileLength = fs.Length + 32;
						UhtBuffer InitialBuffer = UhtBuffer.Borrow((int)RawFileLength);
						int ReadLength = sr.Read(InitialBuffer.Memory.Span);
						if (sr.EndOfStream)
						{
							InitialBuffer.Reset(ReadLength);
							return InitialBuffer;
						}
						else
						{
							string Remaining = sr.ReadToEnd();
							long TotalSize = ReadLength + Remaining.Length;
							UhtBuffer Combined = UhtBuffer.Borrow((int)TotalSize);
							Buffer.BlockCopy(InitialBuffer.Block, 0, Combined.Block, 0, ReadLength * sizeof(char));
							Buffer.BlockCopy(Remaining.ToArray(), 0, Combined.Block, ReadLength * sizeof(char), Remaining.Length * sizeof(char));
							UhtBuffer.Return(InitialBuffer);
							return Combined;
						}
					}
				}
			}
			catch (IOException)
			{
				return null;
			}
		}

		/// <inheritdoc/>
		public bool WriteOutput(string FilePath, ReadOnlySpan<char> Contents)
		{
			try
			{
				string? FileDirectory = Path.GetDirectoryName(FilePath);
				if (!string.IsNullOrEmpty(FileDirectory))
				{
					Directory.CreateDirectory(FileDirectory);
				}
				using (StreamWriter Writer = new StreamWriter(FilePath, false, new UTF8Encoding(false, true), 16 * 1024))
				{
					Writer.Write(Contents);
				}
				return true;
			}
			catch (Exception)
			{
				return false;
			}
		}

		/// <inheritdoc/>
		public bool RenameOutput(string OldFilePath, string NewFilePath)
		{
			try
			{
				File.Move(OldFilePath, OldFilePath, true);
				return true;
			}
			catch (Exception)
			{
				return false;
			}
		}

		///// <summary>
		///// Read the given source file
		///// </summary>
		///// <param name="FilePath">Full file path</param>
		///// <param name="Contents">Contents of the file</param>
		///// <returns>True if the file was read, false if not</returns>
		private bool ReadFile(string FilePath, out StringView Contents)
		{
			// Exceptions are very expensive.  Don't bother trying to open the file if it doesn't exist
			if (!File.Exists(FilePath))
			{
				Contents = new StringView();
				return false;
			}

			try
			{
				using (FileStream fs = new FileStream(FilePath, FileMode.Open, FileAccess.Read, FileShare.Read, 4 * 1024, FileOptions.SequentialScan))
				{
					using (StreamReader sr = new StreamReader(fs, Encoding.UTF8, true))
					{

						// Try to read the whole file into a buffer created by hand.  This avoids a LOT of memory allocations which in turn reduces the
						// GC stress on the system.  Removing the StreamReader would be nice in the future.
						long RawFileLength = fs.Length + 32;
						Memory<char> InitialBuffer = new Memory<char>(new char[RawFileLength]);
						int ReadLength = sr.Read(InitialBuffer.Span);
						if (sr.EndOfStream)
						{
							Contents = new StringView(InitialBuffer, 0, ReadLength);
						}
						else
						{
							string Remaining = sr.ReadToEnd();
							long TotalSize = ReadLength + Remaining.Length;
							Memory<char> Combined = new Memory<char>(new char[TotalSize]);
							Buffer.BlockCopy(InitialBuffer.Span.ToArray(), 0, Combined.Span.ToArray(), 0, ReadLength * sizeof(char));
							Buffer.BlockCopy(Remaining.ToArray(), 0, Combined.Span.ToArray(), ReadLength * sizeof(char), Remaining.Length * sizeof(char));
							Contents = new StringView(Combined);
						}
						return true;
					}
				}
			}
			catch (IOException)
			{
				Contents = new StringView();
				return false;
			}
		}
	}
}
