// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
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
			NewPath = NewPath.Replace("%", "%25");
			NewPath = NewPath.Replace("*", "%2A");
			NewPath = NewPath.Replace("#", "%23");
			NewPath = NewPath.Replace("@", "%40");
			return NewPath;
		}

		/// <summary>
		/// Remove escape characters from a path
		/// </summary>
		static public string UnescapePath(string Path)
		{
			string NewPath = Path;
			NewPath = NewPath.Replace("%40", "@");
			NewPath = NewPath.Replace("%23", "#");
			NewPath = NewPath.Replace("%2A", "*");
			NewPath = NewPath.Replace("%2a", "*");
			NewPath = NewPath.Replace("%25", "%");
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
	}
}
