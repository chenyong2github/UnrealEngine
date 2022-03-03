// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.UHT.Utils;
using System;
using System.Collections.Generic;
using EpicGames.UHT.Tables;
using EpicGames.Core;
using EpicGames.UHT.Tokenizer;

namespace EpicGames.UHT.Parsers
{

	/// <summary>
	/// Class responsible for parsing specifiers and the field meta data.  To reduce allocations, one specifier parser is shared between all objects in
	/// a given header file.  This makes the Action pattern being used a bit more obtuse, but it does help performance by reducing the allocations fairly
	/// significantly.
	/// </summary>
	public class UhtSpecifierParser : IUhtMessageExtraContext
	{
		struct DeferredSpecifier
		{
			public UhtSpecifier Specifier;
			public object? Value;
		}

		private static List<KeyValuePair<StringView, StringView>> EmptyKVPValues = new List<KeyValuePair<StringView, StringView>>();

		private UhtSpecifierContext SpecifierContext;
		private IUhtTokenReader TokenReader;
		private UhtSpecifierTable Table;
		private StringView Context;
		private StringView CurrentSpecifier = new StringView();
		private List<DeferredSpecifier> DeferredSpecifiers = new List<DeferredSpecifier>();
		private bool bIsParsingFieldMetaData = false;
		private List<KeyValuePair<StringView, StringView>>? CurrentKVPValues = null;
		private List<StringView>? CurrentStringValues = null;
		private int UMetaElementsParsed = 0;

		private readonly Action ParseAction;
		private readonly Action ParseFieldMetaDataAction;
		private readonly Action ParseKVPValueAction;
		private readonly Action ParseStringViewListAction;

		public UhtSpecifierParser(UhtSpecifierContext SpecifierContext, StringView Context, UhtSpecifierTable Table)
		{
			this.SpecifierContext = SpecifierContext;
			this.TokenReader = SpecifierContext.Scope.TokenReader;
			this.Context = Context;
			this.Table = Table;

			this.ParseAction = ParseInternal;
			this.ParseFieldMetaDataAction = ParseFieldMetaDataInternal;
			this.ParseKVPValueAction = () => 
			{
				if (this.CurrentKVPValues == null)
				{
					this.CurrentKVPValues = new List<KeyValuePair<StringView, StringView>>();
				}
				this.CurrentKVPValues.Add(ReadKVP()); 
			};
			this.ParseStringViewListAction = () => 
			{ 
				if (this.CurrentStringValues == null)
				{
					this.CurrentStringValues = new List<StringView>();
				}
				this.CurrentStringValues.Add(ReadValue()); 
			};
		}

		public void Reset(UhtSpecifierContext SpecifierContext, StringView Context, UhtSpecifierTable Table)
		{
			this.SpecifierContext = SpecifierContext;
			this.TokenReader = SpecifierContext.Scope.TokenReader;
			this.Context = Context;
			this.Table = Table;
		}

		public UhtSpecifierParser ParseSpecifiers()
		{
			this.bIsParsingFieldMetaData = false;
			this.SpecifierContext.MetaData.LineNumber = TokenReader.InputLine;

			using (var TokenContext = new UhtMessageContext(this.TokenReader, this))
			{
				this.TokenReader.RequireList('(', ')', ',', false, this.ParseAction);
			}
			return this;
		}

		public UhtSpecifierParser ParseFieldMetaData(StringView Context)
		{
			this.TokenReader = this.SpecifierContext.Scope.TokenReader;
			this.bIsParsingFieldMetaData = true;

			using (var TokenContext = new UhtMessageContext(this.TokenReader, this))
			{
				if (TokenReader.TryOptional("UMETA"))
				{
					this.UMetaElementsParsed = 0;
					TokenReader.RequireList('(', ')', ',', false, this.ParseFieldMetaDataAction);
					if (this.UMetaElementsParsed == 0)
					{
						this.TokenReader.LogError($"No metadata specified while parsing {UhtMessage.FormatContext(this)}");
					}
				}
			}
			return this;
		}

		public void ParseDeferred()
		{
			foreach (var Deferred in this.DeferredSpecifiers)
			{
				Dispatch(Deferred.Specifier, Deferred.Value);
			}
			this.DeferredSpecifiers.Clear();
		}

		#region IMessageExtraContext implementation
		public IEnumerable<object?>? MessageExtraContext
		{
			get
			{
				Stack<object?> ExtraContext = new Stack<object?>(1);
				string What = this.bIsParsingFieldMetaData ? "metadata" : "specifiers";
				if (this.Context.Span.Length > 0)
				{
					ExtraContext.Push($"{this.Context} {What}");
				}
				else
				{
					ExtraContext.Push(What);
				}
				return ExtraContext;
			}
		}
		#endregion

		private void ParseInternal()
		{
			UhtToken Identifier = this.TokenReader.GetIdentifier();

			this.CurrentSpecifier = Identifier.Value;
			UhtSpecifier? Specifier;
			if (this.Table.TryGetValue(CurrentSpecifier, out Specifier))
			{
				if (TryParseValue(Specifier.ValueType, out object? Value))
				{
					if (Specifier.When == UhtSpecifierWhen.Deferred)
					{
						if (this.DeferredSpecifiers == null)
						{
							this.DeferredSpecifiers = new List<DeferredSpecifier>();
						}
						this.DeferredSpecifiers.Add(new DeferredSpecifier { Specifier = Specifier, Value = Value });
					}
					else
					{
						Dispatch(Specifier, Value);
					}
				}
			}
			else
			{
				this.TokenReader.LogError($"Unknown specifier '{CurrentSpecifier}' found while parsing {UhtMessage.FormatContext(this)}");
			}
		}

		private void ParseFieldMetaDataInternal()
		{
			UhtToken Key;
			if (!TokenReader.TryOptionalIdentifier(out Key))
			{
				throw new UhtException(this.TokenReader, $"UMETA expects a key and optional value", this);
			}

			StringViewBuilder SVB = new StringViewBuilder();
			if (TokenReader.TryOptional('='))
			{
				if (!ReadValue(TokenReader, SVB, true))
				{
					throw new UhtException(this.TokenReader, $"UMETA key '{Key.Value}' expects a value", this);
				}
			}
			
			++this.UMetaElementsParsed;
			this.SpecifierContext.MetaData.Add(Key.Value.ToString(), this.SpecifierContext.MetaNameIndex, SVB.ToString());
		}

		private void Dispatch(UhtSpecifier Specifier, object? Value)
		{
			UhtSpecifierDispatchResults Results = Specifier.Dispatch(this.SpecifierContext, Value);
			if (Results == UhtSpecifierDispatchResults.Unknown)
			{
				this.TokenReader.LogError($"Unknown specifier '{Specifier.Name}' found while parsing {UhtMessage.FormatContext(this)}");
			}
		}

		private bool TryParseValue(UhtSpecifierValueType ValueType, out object? Value)
		{
			Value = null;

			switch (ValueType)
			{
				case UhtSpecifierValueType.NotSet:
					throw new UhtIceException("NotSet is an invalid value for value types");

				case UhtSpecifierValueType.None:
					if (this.TokenReader.TryOptional('='))
					{
						ReadValue(); // consume the value;
						this.TokenReader.LogError($"The specifier '{this.CurrentSpecifier}' found a value when none was expected", this);
						return false;
					}
					return true;

				case UhtSpecifierValueType.String:
					if (!this.TokenReader.TryOptional('='))
					{
						this.TokenReader.LogError($"The specifier '{this.CurrentSpecifier}' expects a value", this);
						return false;
					}
					Value = ReadValue();
					return true;

				case UhtSpecifierValueType.OptionalString:
					{
						List<StringView>? StringList = ReadValueList();
						if (StringList != null && StringList.Count > 0)
						{
							Value = StringList[0];
						}
						return true;
					}

				case UhtSpecifierValueType.SingleString:
					{
						List<StringView>? StringList = ReadValueList();
						if (StringList == null || StringList.Count != 1)
						{
							this.TokenReader.LogError($"The specifier '{this.CurrentSpecifier}' expects a single value", this);
							return false;
						}
						Value = StringList[0];
						return true;
					}

				case UhtSpecifierValueType.StringList:
					Value = ReadValueList();
					return true;

				case UhtSpecifierValueType.Legacy:
					Value = ReadValueList();
					return true;

				case UhtSpecifierValueType.NonEmptyStringList:
					{
						List<StringView>? StringList = ReadValueList();
						if (StringList == null || StringList.Count == 0)
						{
							this.TokenReader.LogError($"The specifier '{this.CurrentSpecifier}' expects at list one value", this);
							return false;
						}
						Value = StringList;
						return true;
					}

				case UhtSpecifierValueType.KeyValuePairList:
					{
						this.CurrentKVPValues = null;
						this.TokenReader
							.Require('=')
							.RequireList('(', ')', ',', false, this.ParseKVPValueAction);
						List<KeyValuePair<StringView, StringView>> Return = this.CurrentKVPValues ?? EmptyKVPValues;
						this.CurrentKVPValues = null;
						Value = Return;
						return true;
					}

				case UhtSpecifierValueType.OptionalEqualsKeyValuePairList:
					{
						this.CurrentKVPValues = null;
						// This parser isn't as strict as the other parsers...
						if (this.TokenReader.TryOptional('='))
						{
							if (!this.TokenReader.TryOptionalList('(', ')', ',', false, this.ParseKVPValueAction))
							{
								this.ParseKVPValueAction();
							}
						}
						else
						{
							this.TokenReader.TryOptionalList('(', ')', ',', false, this.ParseKVPValueAction);
						}
						List<KeyValuePair<StringView, StringView>> Return = this.CurrentKVPValues ?? EmptyKVPValues;
						this.CurrentKVPValues = null;
						Value = Return;
						return true;
					}

				default:
					throw new UhtIceException("Unknown value type");
			}
		}

		private KeyValuePair<StringView, StringView> ReadKVP()
		{
			UhtToken Key;
			if (!this.TokenReader.TryOptionalIdentifier(out Key))
			{
				throw new UhtException(this.TokenReader, $"The specifier '{this.CurrentSpecifier}' expects a key and optional value", this);
			}

			StringView Value = "";
			if (this.TokenReader.TryOptional('='))
			{
				Value = ReadValue();
			}
			return new KeyValuePair<StringView, StringView>(Key.Value, Value);
		}

		private List<StringView>? ReadValueList()
		{
			this.CurrentStringValues = null;

			// This parser isn't as strict as the other parsers...
			if (this.TokenReader.TryOptional('='))
			{
				if (!this.TokenReader.TryOptionalList('(', ')', ',', false, this.ParseStringViewListAction))
				{
					this.ParseStringViewListAction();
				}
			}
			else
			{
				this.TokenReader.TryOptionalList('(', ')', ',', false, this.ParseStringViewListAction);
			}
			List<StringView>? StringValues = this.CurrentStringValues;
			this.CurrentStringValues = null;
			return StringValues;
		}

		private StringView ReadValue()
		{
			StringViewBuilder SVB = new StringViewBuilder();
			if (!ReadValue(this.TokenReader, SVB, false))
			{
				throw new UhtException(this.TokenReader, $"The specifier '{this.CurrentSpecifier}' expects a value", this);
			}
			return SVB.ToStringView();
		}


		/// <summary>
		/// Parse the sequence of meta data
		/// </summary>
		/// <param name="TokenReader">Input token reader</param>
		/// <param name="SVB">Output string builder</param>
		/// <param name="bRespectQuotes">If true, do not convert \" to " in string constants.  This is required for UMETA data</param>
		/// <returns>True if data was read</returns>
		private static bool ReadValue(IUhtTokenReader TokenReader, StringViewBuilder SVB, bool bRespectQuotes)
		{
			UhtToken Token = TokenReader.GetToken();
			switch (Token.TokenType)
			{
				case UhtTokenType.EndOfFile:
				case UhtTokenType.EndOfDefault:
				case UhtTokenType.EndOfType:
				case UhtTokenType.EndOfDeclaration:
					return false;

				case UhtTokenType.Identifier:
					// We handle true/false differently for compatibility with old UHT
					if (Token.IsValue("true", true))
					{
						SVB.Append("TRUE");
					}
					else if (Token.IsValue("false", true))
					{
						SVB.Append("FALSE");
					}
					else
					{
						SVB.Append(Token.Value);
					}
					if (TokenReader.TryOptional('='))
					{
						SVB.Append('=');
						if (!ReadValue(TokenReader, SVB, bRespectQuotes))
						{
							return false;
						}
					}
					break;

				case UhtTokenType.Symbol:
					SVB.Append(Token.Value);
					if (TokenReader.TryOptional('='))
					{
						SVB.Append('=');
						if (!ReadValue(TokenReader, SVB, bRespectQuotes))
						{
							return false;
						}
					}
					break;

				default:
					SVB.Append(Token.GetConstantValue(bRespectQuotes));
					break;
			}

			return true;
		}
	}
}
