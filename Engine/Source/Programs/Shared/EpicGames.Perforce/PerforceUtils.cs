// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using Microsoft.Extensions.Logging;
using System;
using System.Collections.Generic;
using System.Diagnostics.CodeAnalysis;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using System.Threading.Tasks.Dataflow;

namespace EpicGames.Perforce
{
	/// <summary>
	/// Utility methods for dealing with Perforce paths
	/// </summary>
	public static class PerforceUtils
	{
		/// <summary>
		/// Escape a path to Perforce syntax
		/// </summary>
		static public string EscapePath(string Path)
		{
			string NewPath = Path;
			NewPath = NewPath.Replace("%", "%25", StringComparison.Ordinal);
			NewPath = NewPath.Replace("*", "%2A", StringComparison.Ordinal);
			NewPath = NewPath.Replace("#", "%23", StringComparison.Ordinal);
			NewPath = NewPath.Replace("@", "%40", StringComparison.Ordinal);
			return NewPath;
		}

		/// <summary>
		/// Remove escape characters from a path
		/// </summary>
		static public string UnescapePath(string Path)
		{
			string NewPath = Path;
			NewPath = NewPath.Replace("%40", "@", StringComparison.Ordinal);
			NewPath = NewPath.Replace("%23", "#", StringComparison.Ordinal);
			NewPath = NewPath.Replace("%2A", "*", StringComparison.Ordinal);
			NewPath = NewPath.Replace("%2a", "*", StringComparison.Ordinal);
			NewPath = NewPath.Replace("%25", "%", StringComparison.Ordinal);
			return NewPath;
		}

		/// <summary>
		/// Remove escape characters from a UTF8 path
		/// </summary>
		static public Utf8String UnescapePath(Utf8String Path)
		{
			ReadOnlySpan<byte> PathSpan = Path.Span;
			for (int InputIdx = 0; InputIdx < PathSpan.Length - 2; InputIdx++)
			{
				if(PathSpan[InputIdx] == '%')
				{
					// Allocate the output buffer
					byte[] Buffer = new byte[Path.Length];
					PathSpan.Slice(0, InputIdx).CopyTo(Buffer.AsSpan());

					// Copy the data to the output buffer
					int OutputIdx = InputIdx;
					while (InputIdx < PathSpan.Length)
					{
						// Parse the character code
						int Value = StringUtils.ParseHexByte(PathSpan, InputIdx + 1);
						if (Value == -1)
						{
							Buffer[OutputIdx++] = (byte)'%';
							InputIdx++;
						}
						else
						{
							Buffer[OutputIdx++] = (byte)Value;
							InputIdx += 3;
						}

						// Keep copying until we get to another percent character
						while (InputIdx < PathSpan.Length && (PathSpan[InputIdx] != '%' || InputIdx + 2 >= PathSpan.Length))
						{
							Buffer[OutputIdx++] = PathSpan[InputIdx++];
						}
					}

					// Copy the last chunk of data to the output buffer
					Path = new Utf8String(Buffer.AsMemory(0, OutputIdx));
					break;
				}
			}
			return Path;
		}

		/// <summary>
		/// 
		/// </summary>
		/// <param name="DepotPath"></param>
		/// <param name="DepotName"></param>
		/// <returns></returns>
		static public bool TryGetDepotName(string DepotPath, [NotNullWhen(true)] out string? DepotName)
		{
			return TryGetClientName(DepotPath, out DepotName);
		}

		/// <summary>
		/// 
		/// </summary>
		/// <param name="ClientPath"></param>
		/// <param name="ClientName"></param>
		/// <returns></returns>
		static public bool TryGetClientName(string ClientPath, [NotNullWhen(true)] out string? ClientName)
		{
			if (!ClientPath.StartsWith("//", StringComparison.Ordinal))
			{
				ClientName = null;
				return false;
			}

			int SlashIdx = ClientPath.IndexOf('/', 2);
			if (SlashIdx == -1)
			{
				ClientName = null;
				return false;
			}

			ClientName = ClientPath.Substring(2, SlashIdx - 2);
			return true;
		}

		/// <summary>
		/// 
		/// </summary>
		/// <param name="ClientFile"></param>
		/// <returns></returns>
		static public string GetClientOrDepotDirectoryName(string ClientFile)
		{
			int Index = ClientFile.LastIndexOf('/');
			if (Index == -1)
			{
				return "";
			}
			else
			{
				return ClientFile.Substring(0, Index);
			}
		}

		/// <summary>
		/// Get the relative path of a client file (eg. //ClientName/Foo/Bar.txt -> Foo/Bar.txt)
		/// </summary>
		/// <param name="ClientFile">Path to the client file</param>
		/// <returns></returns>
		/// <exception cref="ArgumentException"></exception>
		public static string GetClientRelativePath(string ClientFile)
		{
			if (!ClientFile.StartsWith("//", StringComparison.Ordinal))
			{
				throw new ArgumentException("Invalid client path", nameof(ClientFile));
			}

			int Idx = ClientFile.IndexOf('/', 2);
			if (Idx == -1)
			{
				throw new ArgumentException("Invalid client path", nameof(ClientFile));
			}

			return ClientFile.Substring(Idx + 1);
		}

		/// <summary>
		/// Get the relative path within a client from a filename
		/// </summary>
		/// <param name="WorkspaceRoot">Dierctory containing the file</param>
		/// <param name="WorkspaceFile">File to get the path for</param>
		/// <returns></returns>
		public static string GetClientRelativePath(DirectoryReference WorkspaceRoot, FileReference WorkspaceFile)
		{
			if (!WorkspaceFile.IsUnderDirectory(WorkspaceRoot))
			{
				throw new ArgumentException("File is not under workspace root", nameof(WorkspaceFile));
			}

			return WorkspaceFile.MakeRelativeTo(WorkspaceRoot).Replace(Path.DirectorySeparatorChar, '/');
		}
	}
}
