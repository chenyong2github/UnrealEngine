import { IExposedFunction, IExposedProperty, IPreset, IWidget, PropertyType, WidgetType, WidgetTypes } from "src/shared";
import _ from 'lodash';

type Range = {
  min?: number;
  max?: number;
}

export type WidgetProperties<TPropertyValue = any> = {
  widget?: IWidget;
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
  
  static getDefaultRange(property: PropertyType, widget: WidgetType): Range {
    switch (property) {
      case PropertyType.Rotator:
        return { min: 0, max: 360	};

      case PropertyType.LinearColor:
      case PropertyType.Vector4:
        return { min: 0, max: 1 };

      case PropertyType.Color:
        return { min: 0, max: 255 };

      case PropertyType.Vector:
        if (widget === WidgetTypes.Joystick)
          return { min: -100000, max: 100000 };
        break;
    }

    return { min: 0, max: 100 };
  }

  static getPropertyLimits(property: IExposedProperty): Range {
    switch (property.UnderlyingProperty.Type) {
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
        return { min: -1.17549e+38, max: 3.402823466E+38 };

      case PropertyType.Rotator:
        return { min: 0, max: 360	};

      case PropertyType.Double:
        return { min: Number.MIN_VALUE, max: Number.MAX_VALUE };

      case PropertyType.LinearColor:
        return { min: 0, max: 1 };

      case PropertyType.Color:
        return { min: 0, max: 255 };
    }
  }

  static getMinMax(preset: IPreset, widget: IWidget): Range {
    const property = WidgetUtilities.getProperty(preset, widget) as IExposedProperty;
    if (!property?.UnderlyingProperty)
      return;

    // Property hard limits
    const limits = WidgetUtilities.getPropertyLimits(property);

    // Get range from UE meta data fields
    const prop = WidgetUtilities.verifyInRange({
                    min: parseFloat(property.Metadata?.Min),
                    max: parseFloat(property.Metadata?.Max),
                  },
                  limits);

    let range = WidgetUtilities.verifyInRange(widget.meta, limits);

    // Make sure it is in the allowed prop range
    range = WidgetUtilities.verifyInRange(range, prop);

    const defRange = WidgetUtilities.getDefaultRange(property.UnderlyingProperty.Type, widget.type);

    return {
      min: range.min ?? prop.min ?? defRange.min ?? limits.min,
      max: range.max ?? prop.max ?? defRange.max ?? limits.max,
    };
  }

  static verifyInRange(range: Range, limits: Range): Range {
    const ret: Range = {};

    if (!isNaN(range?.min))
      ret.min = isNaN(limits?.min) ? range.min : Math.max(range.min, limits.min);

    if (!isNaN(range?.max))
      ret.max = isNaN(limits?.max) ? range.max : Math.min(range.max, limits.max);

    return ret;
  }

  static getProperty(preset: IPreset, widget: IWidget): IExposedProperty|IExposedFunction {
    if (!preset || !widget?.property)
      return;

    return preset.Exposed[widget.property];
  }

  static getPropertyType(preset: IPreset, widget: IWidget): PropertyType {
    const property = WidgetUtilities.getProperty(preset, widget) as IExposedProperty;
    return property?.UnderlyingProperty?.Type;
  }

  static getDefaultWidgetProps(propertyType: PropertyType) {
    switch (propertyType) {
      case PropertyType.LinearColor:
        return { R: true, G: true, B: true, value: { R: 1, G: 1, B: 1 } };

      case PropertyType.Color:
        return { R: true, G: true, B: true, value: { R: 255, G: 255, B: 255 } };

      case PropertyType.Vector2D:
        return { X: true, Y: true, value: { X: 1, Y: 1 } };

      case PropertyType.Vector:
        return { X: true, Y: true, Z: true, value: { X: 1, Y: 1, Z: 1 } };

      case PropertyType.Vector4:
        return { X: true, Y: true, Z: true, W: false, value: { X: 1, Y: 1, Z: 1, W: 1 } };

      case PropertyType.Rotator:
        return { X: true, Y: true, value: { Roll: 360, Pitch: 360 } };
    }

    return { value: 0 };
  }

  static getSubProperties(type: PropertyType) {
    switch (type) {
      case PropertyType.Vector2D:
        return ['X', 'Y'];

      case PropertyType.Vector:
        return ['X', 'Y', 'Z'];

      case PropertyType.Vector4:
        return ['X', 'Y', 'Z', 'W'];

      case PropertyType.Rotator:
        return ['X', 'Y', 'Z'];

      case PropertyType.Color:
      case PropertyType.LinearColor:
        return ['R', 'G', 'B', 'A'];
    }

    return [];
  }

  static isAsset(type: WidgetType): boolean {
    switch (type) {
      case WidgetTypes.Level:
      case WidgetTypes.Sequence:
        return true;
    }

    return false;
  }

  static compatibleWidgets(type: PropertyType | string): WidgetType[] {
    const widgets = [];

    switch (type) {
      case PropertyType.Boolean:
      case PropertyType.Uint8:
        widgets.push(WidgetTypes.Toggle);
        break;

      case PropertyType.Int8:
      case PropertyType.Int16:
      case PropertyType.Int32:
      case PropertyType.Int64:
      case PropertyType.Uint16:
      case PropertyType.Uint32:
      case PropertyType.Uint64:
      case PropertyType.Float:
      case PropertyType.Double:
        widgets.push(WidgetTypes.Gauge, WidgetTypes.Slider);
        break;

      case PropertyType.Color:
      case PropertyType.LinearColor:
        widgets.push(WidgetTypes.ColorPicker, WidgetTypes.Sliders);
        break;

      case PropertyType.Vector:
        widgets.push(WidgetTypes.Joystick, WidgetTypes.Sliders, WidgetTypes.ScaleSlider);
        break;

      case PropertyType.Vector2D:
        widgets.push(WidgetTypes.Sliders, WidgetTypes.ScaleSlider);
        break;

      case PropertyType.Vector4:
        widgets.push(WidgetTypes.ColorPicker, WidgetTypes.Sliders);
        break;

      case PropertyType.Rotator:
        widgets.push(WidgetTypes.Gauge, WidgetTypes.Joystick, WidgetTypes.Sliders);
        break;

      case PropertyType.Function:
        widgets.push(WidgetTypes.Button);
        break;

      case PropertyType.String:
      case PropertyType.Text:
        widgets.push(WidgetTypes.Text);
        break;

      case 'World':
        widgets.push(WidgetTypes.Level);
        break;

      case 'LevelSequence':
        widgets.push(WidgetTypes.Sequence);
        break;
    }

    const custom = WidgetUtilities.propertyWidgets[type];
    if (custom)
      widgets.push(...custom.map(custom => custom.type));

    return widgets;
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

  static getPropertyTypeData(propertyType: PropertyType) {
    let keys = [];
    let min = 0;
    let max = 100;
    let percision = 2;

    switch (propertyType) {
      case PropertyType.Int8:
      case PropertyType.Int16:
      case PropertyType.Int32:
        min = -100;
        max = 100;
        percision = 0;
        break;

      case PropertyType.Float:
      case PropertyType.Double:
        min = -100;
        max = 100;
        break;

      case PropertyType.Uint16:
      case PropertyType.Uint32:
        min = 0;
        max = 100;
        percision = 0;
        break;

      case PropertyType.Rotator:
        keys = ['Roll', 'Pitch', 'Yaw'];
        break;

      case PropertyType.Vector:
        keys = ['X', 'Y', 'Z'];
        break;

      case PropertyType.Vector2D:
        keys = ['X', 'Y'];
        break;

      case PropertyType.Vector4:
        keys = ['X', 'Y', 'Z', 'W'];
        break;

      case PropertyType.LinearColor:
        keys = ['R', 'G', 'B'];
        break;

      case PropertyType.Color:
        max = 255;
        keys = ['R', 'G', 'B'];
        break;
    }

    return { keys, min, max, percision };
  };
}