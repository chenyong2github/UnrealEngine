// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using System;
using System.Collections.Generic;
using System.Linq.Expressions;
using System.Reflection;
using System.Text;

namespace EpicGames.Serialization.Converters
{
	class CbPrimitiveConverter<T> : CbConverterBase<T>, ICbConverterMethods
	{
		public MethodInfo ReadMethod { get; }
		public Func<CbField, T> ReadFunc { get; }

		public MethodInfo WriteMethod { get; }
		public Action<CbWriter, T> WriteFunc { get; }

		public MethodInfo WriteNamedMethod { get; }
		public Action<CbWriter, Utf8String, T> WriteNamedFunc { get; }

		public CbPrimitiveConverter(Expression<Func<CbField, T>> read, Expression<Action<CbWriter, T>> write, Expression<Action<CbWriter, Utf8String, T>> writeNamed)
		{
			this.ReadMethod = ((MethodCallExpression)read.Body).Method;
			this.ReadFunc = read.Compile();

			this.WriteMethod = ((MethodCallExpression)write.Body).Method;
			this.WriteFunc = write.Compile();

			this.WriteNamedMethod = ((MethodCallExpression)writeNamed.Body).Method;
			this.WriteNamedFunc = writeNamed.Compile();
		}

		public override T Read(CbField field) => ReadFunc(field);

		public override void Write(CbWriter writer, T value) => WriteFunc(writer, value);

		public override void WriteNamed(CbWriter writer, Utf8String name, T value) => WriteNamedFunc(writer, name, value);
	}
}
