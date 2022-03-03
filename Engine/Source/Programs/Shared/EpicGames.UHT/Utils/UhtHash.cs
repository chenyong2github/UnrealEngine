// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using System;

namespace EpicGames.UHT.Utils
{

	/// <summary>
	/// Support for generating hash values using the existing UHT algorithms
	/// </summary>
	public struct UhtHash
	{
		private ulong HashInternal;

		/// <summary>
		/// Start the hash computation
		/// </summary>
		public void Begin()
		{
			this.HashInternal = 0;
		}

		/// <summary>
		/// Add a span of text to the current hash value
		/// </summary>
		/// <param name="Text"></param>
		public void Add(ReadOnlySpan<char> Text)
		{
			while (Text.Length != 0)
			{
				int CrPos = Text.IndexOf('\r');
				if (CrPos == -1)
				{
					HashText(Text.Slice(0, Text.Length));
					break;
				}
				else
				{
					HashText(Text.Slice(0, CrPos));
					Text = Text.Slice(CrPos + 1);
				}
			}
		}

		/// <summary>
		/// Return the final hash value
		/// </summary>
		/// <returns>Final hash value</returns>
		public uint End()
		{
			return (uint)(this.HashInternal + (this.HashInternal >> 32));
		}

		/// <summary>
		/// Generate the hash value for a block of text
		/// </summary>
		/// <param name="Text">Text to hash</param>
		/// <returns>Hash value</returns>
		public static uint GenenerateTextHash(ReadOnlySpan<char> Text)
		{
			UhtHash Hash = new UhtHash();
			Hash.Begin();
			Hash.Add(Text);
			return Hash.End();
		}

		private void HashText(ReadOnlySpan<char> Text)
		{
			if (Text.Length > 0)
			{
				unsafe
				{
					fixed (char* TextPtr = Text)
					{
						this.HashInternal = CityHash.CityHash64WithSeed((byte*)TextPtr, (uint)(Text.Length * sizeof(char)), this.HashInternal);
					}
				}
			}
		}
	}
}
