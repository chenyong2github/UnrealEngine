// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.IO;
using System.Linq;
using System.Text;
using EpicGames.Core;

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
		/// <param name="filePath">The partial file path</param>
		/// <returns>The full file path</returns>
		public string GetFullFilePath(string filePath);

		/// <summary>
		/// Read the given source file
		/// </summary>
		/// <param name="filePath">File path</param>
		/// <param name="fragment">Read fragment information</param>
		/// <returns>True if the file was read</returns>
		public bool ReadSource(string filePath, out UhtSourceFragment fragment);

		/// <summary>
		/// Read the given source file
		/// </summary>
		/// <param name="filePath">File path</param>
		/// <returns>Buffer containing the read data or null if not found.  The returned buffer must be returned to the cache via a call to UhtBuffer.Return</returns>
		public UhtBuffer? ReadOutput(string filePath);

		/// <summary>
		/// Write the given contents to the file
		/// </summary>
		/// <param name="filePath">Path to write to</param>
		/// <param name="contents">Contents to write</param>
		/// <returns>True if the source was written</returns>
		public bool WriteOutput(string filePath, ReadOnlySpan<char> contents);

		/// <summary>
		/// Rename the given file
		/// </summary>
		/// <param name="oldFilePath">Old file path name</param>
		/// <param name="newFilePath">New file path name</param>
		/// <returns>True if the file was renamed</returns>
		public bool RenameOutput(string oldFilePath, string newFilePath);
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
		public string GetFullFilePath(string filePath)
		{
			return filePath;
		}

		/// <inheritdoc/>
		public bool ReadSource(string filePath, out UhtSourceFragment fragment)
		{
			if (ReadFile(filePath, out StringView data))
			{
				fragment = new UhtSourceFragment { SourceFile = null, FilePath = filePath, LineNumber = 0, Data = data };
				return true;
			}
			fragment = new UhtSourceFragment { SourceFile = null, FilePath = String.Empty, LineNumber = 0, Data = new StringView() };
			return false;
		}

		/// <inheritdoc/>
		public UhtBuffer? ReadOutput(string filePath)
		{
			// Exceptions are very expensive.  Don't bother trying to open the file if it doesn't exist
			if (!File.Exists(filePath))
			{
				return null;
			}

			try
			{
				using FileStream fs = new(filePath, FileMode.Open, FileAccess.Read, FileShare.Read, 4 * 1024, FileOptions.SequentialScan);
				using StreamReader sr = new(fs, Encoding.UTF8, true);

				// Try to read the whole file into a buffer created by hand.  This avoids a LOT of memory allocations which in turn reduces the
				// GC stress on the system.  Removing the StreamReader would be nice in the future.
				long rawFileLength = fs.Length;
				UhtBuffer initialBuffer = UhtBuffer.Borrow((int)rawFileLength);
				int readLength = sr.Read(initialBuffer.Memory.Span);
				if (sr.EndOfStream)
				{
					initialBuffer.Reset(readLength);
					return initialBuffer;
				}
				else
				{
					string remaining = sr.ReadToEnd();
					long totalSize = readLength + remaining.Length;
					UhtBuffer combined = UhtBuffer.Borrow((int)totalSize);
					Buffer.BlockCopy(initialBuffer.Block, 0, combined.Block, 0, readLength * sizeof(char));
					Buffer.BlockCopy(remaining.ToArray(), 0, combined.Block, readLength * sizeof(char), remaining.Length * sizeof(char));
					UhtBuffer.Return(initialBuffer);
					return combined;
				}
			}
			catch (IOException)
			{
				return null;
			}
		}

		/// <inheritdoc/>
		public bool WriteOutput(string filePath, ReadOnlySpan<char> contents)
		{
			try
			{
				string? fileDirectory = Path.GetDirectoryName(filePath);
				if (!String.IsNullOrEmpty(fileDirectory))
				{
					Directory.CreateDirectory(fileDirectory);
				}
				using StreamWriter writer = new(filePath, false, new UTF8Encoding(false, true), 16 * 1024);
				writer.Write(contents);
				return true;
			}
			catch (Exception)
			{
				return false;
			}
		}

		/// <inheritdoc/>
		public bool RenameOutput(string oldFilePath, string newFilePath)
		{
			try
			{
				File.Move(oldFilePath, newFilePath, true);
				return true;
			}
			catch (Exception)
			{
				return false;
			}
		}

		/// <summary>
		/// Read the given source file
		/// </summary>
		/// <param name="filePath">Full file path</param>
		/// <param name="contents">Contents of the file</param>
		/// <returns>True if the file was read, false if not</returns>
		private static bool ReadFile(string filePath, out StringView contents)
		{
			// Exceptions are very expensive.  Don't bother trying to open the file if it doesn't exist
			if (!File.Exists(filePath))
			{
				contents = new StringView();
				return false;
			}

			try
			{
				using FileStream fs = new(filePath, FileMode.Open, FileAccess.Read, FileShare.Read, 4 * 1024, FileOptions.SequentialScan);
				using StreamReader sr = new(fs, Encoding.UTF8, true);

				// Try to read the whole file into a buffer created by hand.  This avoids a LOT of memory allocations which in turn reduces the
				// GC stress on the system.  Removing the StreamReader would be nice in the future.
				long rawFileLength = fs.Length;
				char[] initialBuffer = new char[rawFileLength];
				int readLength = sr.Read(initialBuffer, 0, (int)rawFileLength);
				if (sr.EndOfStream)
				{
					contents = new StringView(new ReadOnlyMemory<char>(initialBuffer, 0, readLength));
				}
				else
				{
					string remaining = sr.ReadToEnd();
					contents = new StringView(String.Concat(new ReadOnlySpan<char>(initialBuffer, 0, readLength), remaining));
				}
				return true;
			}
			catch (IOException)
			{
				contents = new StringView();
				return false;
			}
		}
	}
}
