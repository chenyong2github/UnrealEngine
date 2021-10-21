// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using System;
using System.Collections.Generic;
using System.Text;

namespace EpicGames.Serialization.Converters
{
	/// <summary>
	/// Converter for raw CbField types
	/// </summary>
	/// <typeparam name="T"></typeparam>
	class CbFieldConverter : CbConverter<CbField>
	{
		/// <inheritdoc/>
		public override CbField Read(CbField Field)
		{
			return Field;
		}

		/// <inheritdoc/>
		public override void Write(CbWriter Writer, CbField Field)
		{
			Writer.WriteFieldValue(Field);
		}

		/// <inheritdoc/>
		public override void WriteNamed(CbWriter Writer, Utf8String Name, CbField Value)
		{
			Writer.WriteField(Name, Value);
		}
	}

	/// <summary>
	/// Converter for raw CbObject types
	/// </summary>
	/// <typeparam name="T"></typeparam>
	class CbObjectConverter : CbConverter<CbObject>
	{
		/// <inheritdoc/>
		public override CbObject Read(CbField Field)
		{
			return Field.AsObject();
		}

		/// <inheritdoc/>
		public override void Write(CbWriter Writer, CbObject Object)
		{
			Writer.WriteFieldValue(Object.AsField());
		}

		/// <inheritdoc/>
		public override void WriteNamed(CbWriter Writer, Utf8String Name, CbObject Object)
		{
			Writer.WriteField(Name, Object.AsField());
		}
	}
}
