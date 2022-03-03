// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.UHT.Types;
using System;
using System.Collections.Generic;
using System.Text;

namespace EpicGames.UHT.Utils
{
	static class UhtStringBuilderExtensions
	{
		/// <summary>
		/// String of tabs used to generate code with proper indentation
		/// </summary>
		public static StringView TabsString = new StringView(new string('\t', 128));

		/// <summary>
		/// String of spaces used to generate code with proper indentation
		/// </summary>
		public static StringView SpacesString = new StringView(new string(' ', 128));

		/// <summary>
		/// Append tabs to the builder
		/// </summary>
		/// <param name="Builder">Destination builder</param>
		/// <param name="Tabs">Number of tabs to insert</param>
		/// <returns>Destination builder</returns>
		/// <exception cref="ArgumentOutOfRangeException">Thrown if the number of tabs is out of range</exception>
		public static StringBuilder AppendTabs(this StringBuilder Builder, int Tabs)
		{
			if (Tabs < 0 || Tabs > TabsString.Length)
			{
				throw new ArgumentOutOfRangeException();
			}
			else if (Tabs > 0)
			{
				Builder.Append(TabsString.Span.Slice(0, Tabs));
			}
			return Builder;
		}

		/// <summary>
		/// Append spaces to the builder
		/// </summary>
		/// <param name="Builder">Destination builder</param>
		/// <param name="Spaces">Number of spaces to insert</param>
		/// <returns>Destination builder</returns>
		/// <exception cref="ArgumentOutOfRangeException">Thrown if the number of spaces is out of range</exception>
		public static StringBuilder AppendSpaces(this StringBuilder Builder, int Spaces)
		{
			if (Spaces < 0 || Spaces > SpacesString.Length)
			{
				throw new ArgumentOutOfRangeException();
			}
			else if (Spaces > 0)
			{
				Builder.Append(SpacesString.Span.Slice(0, Spaces));
			}
			return Builder;
		}

		/// <summary>
		/// Append a name declaration to the builder
		/// </summary>
		/// <param name="Builder">Destination builder</param>
		/// <param name="NamePrefix">Name prefix</param>
		/// <param name="Name">Name</param>
		/// <param name="NameSuffix">Optional name suffix</param>
		/// <returns>Destination builder</returns>
		public static StringBuilder AppendNameDecl(this StringBuilder Builder, string? NamePrefix, string Name, string? NameSuffix)
		{
			return Builder.Append(NamePrefix).Append(Name).Append(NameSuffix);
		}

		/// <summary>
		/// Append a name declaration to the builder
		/// </summary>
		/// <param name="Builder">Destination builder</param>
		/// <param name="Context">Property context used to get the name prefix</param>
		/// <param name="Name">Name</param>
		/// <param name="NameSuffix">Optional name suffix</param>
		/// <returns>Destination builder</returns>
		public static StringBuilder AppendNameDecl(this StringBuilder Builder, IUhtPropertyMemberContext Context, string Name, string NameSuffix)
		{
			return Builder.AppendNameDecl(Context.NamePrefix, Name, NameSuffix);
		}

		/// <summary>
		/// Append a name definition to the builder
		/// </summary>
		/// <param name="Builder">Destination builder</param>
		/// <param name="StaticsName">Optional name of the statics block which will be output in the form of "StaticsName::"</param>
		/// <param name="NamePrefix">Name prefix</param>
		/// <param name="Name">Name</param>
		/// <param name="NameSuffix">Optional name suffix</param>
		/// <returns>Destination builder</returns>
		public static StringBuilder AppendNameDef(this StringBuilder Builder, string? StaticsName, string? NamePrefix, string Name, string? NameSuffix)
		{
			if (!string.IsNullOrEmpty(StaticsName))
			{
				Builder.Append(StaticsName).Append("::");
			}
			return Builder.AppendNameDecl(NamePrefix, Name, NameSuffix);
		}

		/// <summary>
		/// Append a name definition to the builder
		/// </summary>
		/// <param name="Builder">Destination builder</param>
		/// <param name="Context">Property context used to get the statics name and name prefix</param>
		/// <param name="Name">Name</param>
		/// <param name="NameSuffix">Optional name suffix</param>
		/// <returns>Destination builder</returns>
		public static StringBuilder AppendNameDef(this StringBuilder Builder, IUhtPropertyMemberContext Context, string Name, string NameSuffix)
		{
			return Builder.AppendNameDef(Context.StaticsName, Context.NamePrefix, Name, NameSuffix);
		}

		/// <summary>
		/// Append the meta data parameters.  This is intended to be used as arguments to a function call.
		/// </summary>
		/// <param name="Builder">Destination builder</param>
		/// <param name="Type">Source type containing the meta data</param>
		/// <param name="StaticsName">Optional name of the statics block which will be output in the form of "StaticsName::"</param>
		/// <param name="NamePrefix">Name prefix</param>
		/// <param name="Name">Name</param>
		/// <param name="NameSuffix">Optional name suffix</param>
		/// <param name="MetaNameSuffix">Suffix to be added to the meta data name</param>
		/// <returns>Destination builder</returns>
		private static StringBuilder AppendMetaDataParams(this StringBuilder Builder, UhtType Type, string? StaticsName, string? NamePrefix, string Name, string? NameSuffix, string? MetaNameSuffix)
		{
			if (!Type.MetaData.IsEmpty())
			{
				return Builder
					.Append("METADATA_PARAMS(")
					.AppendNameDef(StaticsName, NamePrefix, Name, NameSuffix).Append(MetaNameSuffix)
					.Append(", UE_ARRAY_COUNT(")
					.AppendNameDef(StaticsName, NamePrefix, Name, NameSuffix).Append(MetaNameSuffix)
					.Append("))");
			}
			else
			{
				return Builder.Append("METADATA_PARAMS(nullptr, 0)");
			}
		}

		/// <summary>
		/// Append the meta data parameters.  This is intended to be used as arguments to a function call.
		/// </summary>
		/// <param name="Builder">Destination builder</param>
		/// <param name="Type">Source type containing the meta data</param>
		/// <param name="StaticsName">Optional name of the statics block which will be output in the form of "StaticsName::"</param>
		/// <param name="Name">Name</param>
		/// <returns>Destination builder</returns>
		public static StringBuilder AppendMetaDataParams(this StringBuilder Builder, UhtType Type, string? StaticsName, string Name)
		{
			return Builder.AppendMetaDataParams(Type, StaticsName, null, Name, null, null);
		}

		/// <summary>
		/// Append the meta data parameters.  This is intended to be used as arguments to a function call.
		/// </summary>
		/// <param name="Builder">Destination builder</param>
		/// <param name="Property">Source type containing the meta data</param>
		/// <param name="Context">Property context used to get the statics name and name prefix</param>
		/// <param name="Name">Name</param>
		/// <param name="NameSuffix">Optional name suffix</param>
		/// <returns>Destination builder</returns>
		public static StringBuilder AppendMetaDataParams(this StringBuilder Builder, UhtProperty Property, IUhtPropertyMemberContext Context, string Name, string NameSuffix)
		{
			return Builder.AppendMetaDataParams(Property, Context.StaticsName, Context.NamePrefix, Name, NameSuffix, Context.MetaDataSuffix);
		}

		/// <summary>
		/// Append the meta data declaration.
		/// </summary>
		/// <param name="Builder">Destination builder</param>
		/// <param name="Type">Source type containing the meta data</param>
		/// <param name="NamePrefix">Name prefix</param>
		/// <param name="Name">Name</param>
		/// <param name="NameSuffix">Optional name suffix</param>
		/// <param name="MetaNameSuffix">Optional meta data name suffix</param>
		/// <param name="Tabs">Number of tabs to indent</param>
		/// <returns>Destination builder</returns>
		private static StringBuilder AppendMetaDataDecl(this StringBuilder Builder, UhtType Type, string? NamePrefix, string Name, string? NameSuffix, string? MetaNameSuffix, int Tabs)
		{
			if (!Type.MetaData.IsEmpty())
			{
				Builder.Append("#if WITH_METADATA\r\n");
				Builder.AppendTabs(Tabs).Append("static const UECodeGen_Private::FMetaDataPairParam ").AppendNameDecl(NamePrefix, Name, NameSuffix).Append(MetaNameSuffix).Append("[];\r\n");
				Builder.Append("#endif\r\n");
			}
			return Builder;
		}

		/// <summary>
		/// Append the meta data declaration
		/// </summary>
		/// <param name="Builder">Destination builder</param>
		/// <param name="Type">Source type containing the meta data</param>
		/// <param name="Name">Name</param>
		/// <param name="Tabs">Number of tabs to indent</param>
		/// <returns>Destination builder</returns>
		public static StringBuilder AppendMetaDataDecl(this StringBuilder Builder, UhtType Type, string Name, int Tabs)
		{
			return Builder.AppendMetaDataDecl(Type, null, Name, null, null, Tabs);
		}

		/// <summary>
		/// Append the meta data declaration
		/// </summary>
		/// <param name="Builder">Destination builder</param>
		/// <param name="Property">Source type containing the meta data</param>
		/// <param name="Context">Property context used to get the statics name and name prefix</param>
		/// <param name="Name">Name</param>
		/// <param name="NameSuffix">Optional name suffix</param>
		/// <param name="Tabs">Number of tabs to indent</param>
		/// <returns>Destination builder</returns>
		public static StringBuilder AppendMetaDataDecl(this StringBuilder Builder, UhtProperty Property, IUhtPropertyMemberContext Context, string Name, string NameSuffix, int Tabs)
		{
			return Builder.AppendMetaDataDecl(Property, Context.NamePrefix, Name, NameSuffix, Context.MetaDataSuffix, Tabs);
		}

		/// <summary>
		/// Append the meta data definition.
		/// </summary>
		/// <param name="Builder">Destination builder</param>
		/// <param name="Type">Source type containing the meta data</param>
		/// <param name="StaticsName">Optional name of the statics block which will be output in the form of "StaticsName::"</param>
		/// <param name="NamePrefix">Name prefix</param>
		/// <param name="Name">Name</param>
		/// <param name="NameSuffix">Optional name suffix</param>
		/// <param name="MetaNameSuffix">Optional meta data name suffix</param>
		/// <param name="Tabs">Number of tabs to indent</param>
		/// <returns>Destination builder</returns>
		private static StringBuilder AppendMetaDataDef(this StringBuilder Builder, UhtType Type, string? StaticsName, string? NamePrefix, string Name, string? NameSuffix, string? MetaNameSuffix, int Tabs)
		{
			if (!Type.MetaData.IsEmpty())
			{
				List<KeyValuePair<string, string>> SortedMetaData = Type.MetaData.GetSorted();

				Builder.Append("#if WITH_METADATA\r\n");
				Builder.AppendTabs(Tabs).Append("const UECodeGen_Private::FMetaDataPairParam ").AppendNameDef(StaticsName, NamePrefix, Name, NameSuffix).Append(MetaNameSuffix).Append("[] = {\r\n");

				foreach (KeyValuePair<string, string> Kvp in SortedMetaData)
				{
					Builder.AppendTabs(Tabs + 1).Append("{ ").AppendUTF8LiteralString(Kvp.Key).Append(", ").AppendUTF8LiteralString(Kvp.Value).Append(" },\r\n");
				}

				Builder.AppendTabs(Tabs).Append("};\r\n");
				Builder.Append("#endif\r\n");
			}
			return Builder;
		}

		/// <summary>
		/// Append the meta data definition
		/// </summary>
		/// <param name="Builder">Destination builder</param>
		/// <param name="Type">Source type containing the meta data</param>
		/// <param name="StaticsName">Optional name of the statics block which will be output in the form of "StaticsName::"</param>
		/// <param name="Name">Name</param>
		/// <param name="Tabs">Number of tabs to indent</param>
		/// <returns>Destination builder</returns>
		public static StringBuilder AppendMetaDataDef(this StringBuilder Builder, UhtType Type, string? StaticsName, string Name, int Tabs)
		{
			return Builder.AppendMetaDataDef(Type, StaticsName, null, Name, null, null, Tabs);
		}

		/// <summary>
		/// Append the meta data definition
		/// </summary>
		/// <param name="Builder">Destination builder</param>
		/// <param name="Property">Source type containing the meta data</param>
		/// <param name="Context">Property context used to get the statics name and name prefix</param>
		/// <param name="Name">Name</param>
		/// <param name="NameSuffix">Optional name suffix</param>
		/// <param name="Tabs">Number of tabs to indent</param>
		/// <returns>Destination builder</returns>
		public static StringBuilder AppendMetaDataDef(this StringBuilder Builder, UhtProperty Property, IUhtPropertyMemberContext Context, string Name, string NameSuffix, int Tabs)
		{
			return Builder.AppendMetaDataDef(Property, Context.StaticsName, Context.NamePrefix, Name, NameSuffix, Context.MetaDataSuffix, Tabs);
		}

		/// <summary>
		/// Append the given text as a UTF8 encoded string
		/// </summary>
		/// <param name="Builder">Destination builder</param>
		/// <param name="bUseText">If false, don't encode the text but include a nullptr</param>
		/// <param name="Text">Text to include or an empty string if null.</param>
		/// <returns>Destination builder</returns>
		public static StringBuilder AppendUTF8LiteralString(this StringBuilder Builder, bool bUseText, string? Text)
		{
			if (!bUseText)
			{
				Builder.Append("nullptr");
			}
			else if (Text == null)
			{
				Builder.Append("");
			}
			else
			{
				Builder.AppendUTF8LiteralString(Text);
			}
			return Builder;
		}

		/// <summary>
		/// Append the given text as a UTF8 encoded string
		/// </summary>
		/// <param name="Builder">Destination builder</param>
		/// <param name="Text">Text to be encoded</param>
		/// <returns>Destination builder</returns>
		public static StringBuilder AppendUTF8LiteralString(this StringBuilder Builder, StringView Text)
		{
			Builder.Append("\"");

			ReadOnlySpan<char> Span = Text.Span;
			int Length = Span.Length;

			if (Length > 0)
			{

				bool bTrailingHex = false;
				int Index = 0;
				while (true)
				{
					// Scan forward looking for anything that can just be blindly copied
					int StartIndex = Index;
					while (Index < Length)
					{
						char cskip = Span[Index];
						if (cskip < 31 || cskip > 127 || cskip == '"' || cskip == '\\')
						{
							break;
						}
						++Index;
					}

					// If we found anything
					if (StartIndex < Index)
					{
						// We close and open the literal here in order to ensure that successive hex characters aren't appended to the hex sequence, causing a different number
						if (bTrailingHex && UhtFCString.IsHexDigit(Span[StartIndex]))
						{
							Builder.Append("\"\"");
						}
						Builder.Append(Span.Slice(StartIndex, Index - StartIndex));
					}

					// We have either reached the end of the string, break
					if (Index == Length)
					{
						break;
					}

					// This character requires special processing
					char c = Span[Index++];
					switch (c)
					{
						case '\r':
							bTrailingHex = false;
							break;
						case '\n':
							bTrailingHex = false;
							Builder.Append("\\n");
							break;
						case '\\':
							bTrailingHex = false;
							Builder.Append("\\\\");
							break;
						case '\"':
							bTrailingHex = false;
							Builder.Append("\\\"");
							break;
						default:
							if (c < 31)
							{
								bTrailingHex = true;
								//Builder.Append($"\\x{(uint)c:x2}");
								Builder.Append("\\x").AppendFormat("{0:x2}", (uint)c);
							}
							else
							{
								bTrailingHex = false;
								if (char.IsHighSurrogate(c))
								{
									if (Index == Length)
									{
										Builder.Append('?');
										break;
									}

									char clow = Span[Index];
									if (char.IsLowSurrogate(clow))
									{
										++Index;
										Builder.AppendEscapedUtf32((ulong)char.ConvertToUtf32(c, clow));
										bTrailingHex = true;
									}
									else
									{
										Builder.Append('?');
									}
								}
								else if (char.IsLowSurrogate(c))
								{
									Builder.Append('?');
								}
								else
								{
									Builder.AppendEscapedUtf32(c);
									bTrailingHex = true;
								}
							}
							break;
					}
				} while (Index < Length);
			}

			Builder.Append("\"");
			return Builder;
		}

		/// <summary>
		/// Encode a single UTF32 value as UTF8 characters
		/// </summary>
		/// <param name="Builder">Destination builder</param>
		/// <param name="c">Character to encode</param>
		/// <returns>Destination builder</returns>
		public static StringBuilder AppendEscapedUtf32(this StringBuilder Builder, ulong c)
		{
			if (c < 0x80)
			{
				Builder
					.Append("\\x").AppendFormat("{0:x2}", c);
			}
			else if (c < 0x800)
			{
				Builder
					.Append("\\x").AppendFormat("{0:x2}", 0xC0 + (c >> 6))
					.Append("\\x").AppendFormat("{0:x2}", 0x80 + (c & 0x3f));
			}
			else if (c < 0x10000)
			{
				Builder
					.Append("\\x").AppendFormat("{0:x2}", 0xE0 + (c >> 12))
					.Append("\\x").AppendFormat("{0:x2}", 0x80 + ((c >> 6) & 0x3f))
					.Append("\\x").AppendFormat("{0:x2}", 0x80 + (c & 0x3f));
			}
			else
			{
				Builder
					.Append("\\x").AppendFormat("{0:x2}", 0xF0 + (c >> 18))
					.Append("\\x").AppendFormat("{0:x2}", 0x80 + ((c >> 12) & 0x3f))
					.Append("\\x").AppendFormat("{0:x2}", 0x80 + ((c >> 6) & 0x3f))
					.Append("\\x").AppendFormat("{0:x2}", 0x80 + (c & 0x3f));
			}
			return Builder;
		}

		/// <summary>
		/// Append the given name of the class but always encode interfaces as the native interface name (i.e. "I...")
		/// </summary>
		/// <param name="Builder">Destination builder</param>
		/// <param name="Class">Class to append name</param>
		/// <returns>Destination builder</returns>
		/// <exception cref="NotImplementedException">Class has an unexpected class type</exception>
		public static StringBuilder AppendClassSourceNameOrInterfaceName(this StringBuilder Builder, UhtClass Class)
		{
			switch (Class.ClassType)
			{
				case UhtClassType.Class:
				case UhtClassType.NativeInterface:
					return Builder.Append(Class.SourceName);
				case UhtClassType.Interface:
					return Builder.Append('I').Append(Class.EngineName);
				default:
					throw new NotImplementedException();
			}
		}
	}

	/// <summary>
	/// Provides a cache of StringBuilders
	/// </summary>
	public class StringBuilderCache
	{

		/// <summary>
		/// Cache of StringBuilders with large initial buffer sizes
		/// </summary>
		public static StringBuilderCache Big = new StringBuilderCache(256, 256 * 1024);

		/// <summary>
		/// Cache of StringBuilders with small initial buffer sizes
		/// </summary>
		public static StringBuilderCache Small = new StringBuilderCache(256, 1 * 1024);

		/// <summary>
		/// Capacity of the cache
		/// </summary>
		private int Capacity;

		/// <summary>
		/// Initial buffer size for new StringBuilders.  Resetting StringBuilders might result
		/// in the initial chunk size being smaller.
		/// </summary>
		private int InitialBufferSize;

		/// <summary>
		/// Stack of cached StringBuilders
		/// </summary>
		private Stack<StringBuilder> Stack;

		/// <summary>
		/// Create a new StringBuilder cache
		/// </summary>
		/// <param name="Capacity">Maximum number of StringBuilders to cache</param>
		/// <param name="InitialBufferSize">Initial buffer size for newly created StringBuilders</param>
		public StringBuilderCache(int Capacity, int InitialBufferSize)
		{
			this.Capacity = Capacity;
			this.InitialBufferSize = InitialBufferSize;
			this.Stack = new Stack<StringBuilder>(this.Capacity);
		}

		/// <summary>
		/// Borrow a StringBuilder from the cache.
		/// </summary>
		/// <returns></returns>
		public StringBuilder Borrow()
		{
			lock (this.Stack)
			{
				if (this.Stack.Count > 0)
				{
					return this.Stack.Pop();
				}
			}

			return new StringBuilder(this.InitialBufferSize);
		}

		/// <summary>
		/// Return a StringBuilder to the cache
		/// </summary>
		/// <param name="Builder">The builder being returned</param>
		public void Return(StringBuilder Builder)
		{
			// Sadly, clearing the builder (sets length to 0) will reallocate chunks.
			Builder.Clear();
			lock (this.Stack)
			{
				if (this.Stack.Count < this.Capacity)
				{
					this.Stack.Push(Builder);
				}
			}
		}
	}

	/// <summary>
	/// Structure to automate the borrowing and returning of a StringBuilder.
	/// Use some form of a "using" pattern.
	/// </summary>
	public struct BorrowStringBuilder : IDisposable
	{

		/// <summary>
		/// Owning cache
		/// </summary>
		private readonly StringBuilderCache Cache;

		/// <summary>
		/// Borrowed string builder
		/// </summary>
		public readonly StringBuilder StringBuilder;

		/// <summary>
		/// Borrow a string builder from the given cache
		/// </summary>
		/// <param name="Cache">String builder cache</param>
		public BorrowStringBuilder(StringBuilderCache Cache)
		{
			this.Cache = Cache;
			this.StringBuilder = this.Cache.Borrow();
		}

		/// <summary>
		/// Return the string builder to the cache
		/// </summary>
		public void Dispose()
		{
			this.Cache.Return(this.StringBuilder);
		}
	}
}
