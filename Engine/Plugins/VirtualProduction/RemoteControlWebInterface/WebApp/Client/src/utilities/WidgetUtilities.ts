import { IPreset, PropertyType, WidgetType, WidgetTypes } from "src/shared";


type Range = {
  min?: number;
  max?: number;
}

export type WidgetProperties<TPropertyValue = any> = {
  stack?: boolean;
  type?: PropertyType;
  value?: TPropertyValue;
  onChange?: (value: TPropertyValue) => void;
};

type CustomWidget = {
  type: string;
  render: (props: any) => React.ReactNode;
};

export class WidgetUtilities {

  static propertyWidgets: Record<string, CustomWidget[]> = {};
  static customWidgets: Record<string, CustomWidget> = {};
  
  static getPropertyLimits(type: PropertyType): Range {
    switch (type) {
      case PropertyType.Int8:
        return { min: -128, max: 127 };

      case PropertyType.Int16:
        return { min: -32768, max: 32767 };

      case PropertyType.Int32:
        return { min: -2147483648, max: 2147483647 };

      case PropertyType.Uint16:
        return { min: 0, max: 65535 };

      case PropertyType.Uint32:
        return { min: 0, max: 4294967295 };

      case PropertyType.Float:
      case PropertyType.Vector:
      case PropertyType.Vector2D:
      case PropertyType.Vector4:
      case PropertyType.LinearColor:
        return { min: -1.17549e+38, max: 3.402823466E+38 };

      case PropertyType.Rotator:
        return { min: 0, max: 360	};

      case PropertyType.Double:
        return { min: Number.MIN_VALUE, max: Number.MAX_VALUE };

      case PropertyType.Color:
        return { min: 0, max: 255 };
    }

    return {};
  }

  static parseNumber(value: string): number {
    const number = parseFloat(value);
    if (!isNaN(number))
      return number;
  }

  static getMinMax(preset: IPreset, exposed: string): Range {
    const property = preset.Exposed[exposed];

    return {
      min: WidgetUtilities.parseNumber(property?.Metadata?.Min),
      max: WidgetUtilities.parseNumber(property?.Metadata?.Max),
    };
  }

  static isAsset(type: WidgetType): boolean {
    switch (type) {
      case WidgetTypes.Level:
      case WidgetTypes.Sequence:
        return true;
    }

    return false;
  }

  static registerWidget(type: string, properties: PropertyType[], render: (props: WidgetProperties) => React.ReactNode): void {
    const custom: CustomWidget = { type, render };
    WidgetUtilities.customWidgets[type] = custom;
    
    for (const property of properties) {
      if (!WidgetUtilities.propertyWidgets[property])
        WidgetUtilities.propertyWidgets[property] = [];

      WidgetUtilities.propertyWidgets[property].push(custom);
    }
  }

  static getPropertyPercision(propertyType: PropertyType) {
    switch (propertyType) {
      case PropertyType.Float:
      case PropertyType.Double:
      case PropertyType.Vector:
      case PropertyType.Vector2D:
      case PropertyType.Vector4:
      case PropertyType.Rotator:
      case PropertyType.LinearColor:
        return 3;
    }

    return 0;
  }

  static getPropertyKeys(propertyType: PropertyType) {
    switch (propertyType) {
      case PropertyType.Rotator:
        return ['Roll', 'Pitch', 'Yaw'];

      case PropertyType.Vector:
        return ['X', 'Y', 'Z'];

      case PropertyType.Vector2D:
        return ['X', 'Y'];

      case PropertyType.Vector4:
        return ['X', 'Y', 'Z', 'W'];

      case PropertyType.Color:
      case PropertyType.LinearColor:
        return ['R', 'G', 'B'];
    }

    return [];
  };
}