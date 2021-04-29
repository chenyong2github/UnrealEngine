// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace HordeServer.Logs
{
	/// <summary>
	/// Stores cached information about a utf8 search term 
	/// </summary>
	public class SearchText
	{
		/// <summary>
		/// The search text
		/// </summary>
		public string Text { get; }

		/// <summary>
		/// The utf-8 bytes to search for
		/// </summary>
		public ReadOnlyMemory<byte> Bytes { get; }

		/// <summary>
		/// Normalized (lowercase) utf-8 bytes to search for
		/// </summary>
		byte[] SearchBytes;

		/// <summary>
		/// Skip table for comparisons
		/// </summary>
		byte[] SkipTable;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Text">The text to search for</param>
		public SearchText(string Text)
		{
			byte[] Bytes = Encoding.UTF8.GetBytes(Text);
			this.Bytes = Bytes;
			this.Text = Text;

			// Find the byte sequence to search for, in lowercase
			SearchBytes = new byte[Bytes.Length];
			for (int Idx = 0; Idx < SearchBytes.Length; Idx++)
			{
				if (Bytes[Idx] >= 'A' && Bytes[Idx] <= 'Z')
				{
					SearchBytes[Idx] = (byte)('a' + (Bytes[Idx] - 'A'));
				}
				else
				{
					SearchBytes[Idx] = Bytes[Idx];
				}
			}

			// Build a table indicating how many characters to skip before attempting the next comparison
			SkipTable = new byte[256];
			for (int Idx = 0; Idx < 256; Idx++)
			{
				SkipTable[Idx] = (byte)SearchBytes.Length;
			}
			for (int Idx = 0; Idx < SearchBytes.Length - 1; Idx++)
			{
				byte Character = SearchBytes[Idx];

				byte SkipBytes = (byte)(SearchBytes.Length - 1 - Idx);
				SkipTable[Character] = SkipBytes;

				if (Character >= 'a' && Character <= 'z')
				{
					SkipTable['A' + (Character - 'a')] = SkipBytes;
				}
			}
		}

		/// <summary>
		/// Find all ocurrences of the text in the given buffer
		/// </summary>
		/// <param name="Buffer">The buffer to search</param>
		/// <param name="Text">The text to search for</param>
		/// <returns>Sequence of offsets within the buffer</returns>
		public static IEnumerable<int> FindOcurrences(ReadOnlyMemory<byte> Buffer, SearchText Text)
		{
			for(int Offset = 0; ;Offset++)
			{
				Offset = FindNextOcurrence(Buffer.Span, Offset, Text);
				if(Offset == -1)
				{
					break;
				}
				yield return Offset;
			}
		}

		/// <summary>
		/// Perform a case sensitive search for the next occurerence of the search term in a given buffer
		/// </summary>
		/// <param name="Buffer">The buffer to search</param>
		/// <param name="Offset">Starting offset for the search</param>
		/// <param name="Text">The text to search for</param>
		/// <returns>Offset of the next occurence, or -1</returns>
		public static int FindNextOcurrence(ReadOnlySpan<byte> Buffer, int Offset, SearchText Text)
		{
			while (Offset + Text.SearchBytes.Length <= Buffer.Length)
			{
				if (Matches(Buffer, Offset, Text))
				{
					return Offset;
				}
				else
				{
					Offset += Text.SkipTable[Buffer[Offset + Text.SearchBytes.Length - 1]];
				}
			}
			return -1;
		}

		/// <summary>
		/// Compare the search term against the given buffer
		/// </summary>
		/// <param name="Buffer">The buffer to search</param>
		/// <param name="Offset">Starting offset for the search</param>
		/// <param name="Text">The text to search for</param>
		/// <returns>True if the text matches, false otherwise</returns>
		public static bool Matches(ReadOnlySpan<byte> Buffer, int Offset, SearchText Text)
		{
			for (int Idx = Text.SearchBytes.Length - 1; Idx >= 0; Idx--)
			{
				byte Char = Buffer[Offset + Idx];
				if (Char >= 'A' && Char <= 'Z')
				{
					Char = (byte)('a' + (Char - 'A'));
				}
				if (Char != Text.SearchBytes[Idx])
				{
					return false;
				}
			}
			return true;
		}
	}

	/// <summary>
	/// Stores cached information about a utf8 search term 
	/// </summary>
	public static class SearchTextExtensions
	{
		/// <summary>
		/// Find all ocurrences of the text in the given buffer
		/// </summary>
		/// <param name="Buffer">The buffer to search</param>
		/// <param name="Text">The text to search for</param>
		/// <returns>Sequence of offsets within the buffer</returns>
		public static IEnumerable<int> FindOcurrences(this ReadOnlyMemory<byte> Buffer, SearchText Text)
		{
			return SearchText.FindOcurrences(Buffer, Text);
		}

		/// <summary>
		/// Perform a case sensitive search for the next occurerence of the search term in a given buffer
		/// </summary>
		/// <param name="Buffer">The buffer to search</param>
		/// <param name="Offset">Starting offset for the search</param>
		/// <param name="Text">The text to search for</param>
		/// <returns>Offset of the next occurence, or -1</returns>
		public static int FindNextOcurrence(this ReadOnlySpan<byte> Buffer, int Offset, SearchText Text)
		{
			return SearchText.FindNextOcurrence(Buffer, Offset, Text);
		}

		/// <summary>
		/// Compare the search term against the given buffer
		/// </summary>
		/// <param name="Buffer">The buffer to search</param>
		/// <param name="Offset">Starting offset for the search</param>
		/// <param name="Text">The text to search for</param>
		/// <returns>True if the text matches, false otherwise</returns>
		public static bool Matches(this ReadOnlySpan<byte> Buffer, int Offset, SearchText Text)
		{
			return SearchText.Matches(Buffer, Offset, Text);
		}
	}
}
