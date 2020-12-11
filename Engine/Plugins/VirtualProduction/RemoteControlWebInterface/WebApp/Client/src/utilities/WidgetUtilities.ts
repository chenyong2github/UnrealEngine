import { IExposedFunction, IExposedProperty, IPreset, IWidget, PropertyType, WidgetType } from "src/shared";
import _ from 'lodash';

type Range = {
  min?: number;
  max?: number;
}

export class WidgetUtilities {

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
        if (widget === WidgetType.Joystick)
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
    const metadata = property.UnderlyingProperty?.Metadata;
    const prop = WidgetUtilities.verifyInRange({
                    min: parseFloat(metadata?.UIMin ?? metadata.ClampMin),
                    max: parseFloat(metadata?.UIMax ?? metadata.ClampMax),
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

    if (!isNaN(range.min))
      ret.min = isNaN(limits.min) ? range.min : Math.max(range.min, limits.min);

    if (!isNaN(range.max))
      ret.max = isNaN(limits.max) ? range.max : Math.min(range.max, limits.max);

    return ret;
  }

  static getProperty(preset: IPreset, widget: IWidget): IExposedProperty|IExposedFunction {
    if (!preset || !widget?.group || !widget.property)
      return;

    const group = _.find(preset.Groups, g => g.Name === widget.group);
    if (!group)
      return;

    const property = _.find(group.ExposedProperties, p => p.DisplayName === widget.property);
    if (property)
      return property;

    return _.find(group.ExposedFunctions, f => f.DisplayName === widget.property);
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
      case WidgetType.Level:
      case WidgetType.Sequence:
        return true;
    }

    return false;
  }

}