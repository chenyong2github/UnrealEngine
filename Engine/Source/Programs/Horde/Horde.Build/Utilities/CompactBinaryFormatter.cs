// Copyright Epic Games, Inc. All Rights Reserved.

using Microsoft.AspNetCore.Mvc.Formatters;
using System;
using System.Collections.Generic;
using System.Linq;
using Microsoft.Net.Http.Headers;
using System.Threading.Tasks;
using System.IO;
using EpicGames.Serialization;
using EpicGames.Serialization.Converters;
using System.Reflection;
using System.Net.Mime;
using EpicGames.Core;
using Microsoft.Extensions.Primitives;

namespace HordeServer.Utilities
{
	/// <summary>
	/// Global constants
	/// </summary>
	public static partial class CustomMediaTypeNames
	{
		/// <summary>
		/// Media type for compact binary
		/// </summary>
		public const string UnrealCompactBinary = "application/x-ue-cb";
	}

	/// <summary>
	/// Converter to allow reading compact binary objects as request bodies
	/// </summary>
	public class CbInputFormatter : InputFormatter
	{
		/// <summary>
		/// Constructor
		/// </summary>
		public CbInputFormatter()
		{
			SupportedMediaTypes.Add(MediaTypeHeaderValue.Parse(CustomMediaTypeNames.UnrealCompactBinary));
		}

		/// <inheritdoc/>
		protected override bool CanReadType(Type Type)
		{
			return true;
		}

		/// <inheritdoc/>
		public override async Task<InputFormatterResult> ReadRequestBodyAsync(InputFormatterContext Context)
		{
			// Buffer the data into an array
			byte[] Data;
			using (MemoryStream Stream = new MemoryStream())
			{
				await Context.HttpContext.Request.Body.CopyToAsync(Stream);
				Data = Stream.ToArray();
			}

			// Serialize the object
			CbField Field;
			try
			{
				Field = new CbField(Data);
			}
			catch (Exception Ex)
			{
				Serilog.Log.Logger.Error("Unable to parse compact binary: {Dump}", FormatHexDump(Data, 256));
				foreach ((string Name, StringValues Values) in Context.HttpContext.Request.Headers)
				{
					foreach (string Value in Values)
					{
						Serilog.Log.Logger.Information("Header {Name}: {Value}", Name, Value);
					}
				}
				throw new Exception($"Unable to parse compact binary request: {FormatHexDump(Data, 256)}", Ex);
			}
			return await InputFormatterResult.SuccessAsync(CbSerializer.Deserialize(new CbField(Data), Context.ModelType)!);
		}

		static string FormatHexDump(byte[] Data, int MaxLength)
		{
			ReadOnlySpan<byte> Span = Data.AsSpan(0, Math.Min(Data.Length, MaxLength));
			string HexString = StringUtils.FormatHexString(Span);

			char[] HexDump = new char[Span.Length * 3];
			for (int Idx = 0; Idx < Span.Length; Idx++)
			{
				HexDump[(Idx * 3) + 0] = ((Idx & 15) == 0) ? '\n' : ' ';
				HexDump[(Idx * 3) + 1] = HexString[(Idx * 2) + 0];
				HexDump[(Idx * 3) + 2] = HexString[(Idx * 2) + 1];
			}
			return new string(HexDump);
		}
	}

	/// <summary>
	/// Converter to allow writing compact binary objects as responses
	/// </summary>
	public class CbOutputFormatter : OutputFormatter
	{
		/// <summary>
		/// Constructor
		/// </summary>
		public CbOutputFormatter()
		{
			SupportedMediaTypes.Add(MediaTypeHeaderValue.Parse(CustomMediaTypeNames.UnrealCompactBinary));
		}

		/// <inheritdoc/>
		protected override bool CanWriteType(Type? Type)
		{
			return true;
		}

		/// <inheritdoc/>
		public override async Task WriteResponseBodyAsync(OutputFormatterWriteContext Context)
		{
			ReadOnlyMemory<byte> Data;
			if (Context.Object is CbObject Object)
			{
				Data = Object.GetView();
			}
			else
			{
				Data = CbSerializer.Serialize(Context.ObjectType!, Context.Object!).GetView();
			}
			await Context.HttpContext.Response.BodyWriter.WriteAsync(Data);
		}
	}

	/// <summary>
	/// Special version of <see cref="CbOutputFormatter"/> which returns native CbObject encoded data. Can be 
	/// inserted at a high priority in the output formatter list to prevent transcoding to json.
	/// </summary>
	public class CbPreferredOutputFormatter : CbOutputFormatter
	{
		/// <inheritdoc/>
		protected override bool CanWriteType(Type? Type)
		{
			return Type == typeof(CbObject);
		}
	}
}
