export enum PropertyType {
  Boolean =     'bool',
  Int8 =        'int8',
  Int16 =       'int16',
  Int32 =       'int32',
  Int64 =       'int64',
  Uint8 =       'uint8',
  Uint16 =      'uint16',
  Uint32 =      'uint32',
  Uint64 =      'uint64',
  Float =       'float',
  Double =      'double',
  Vector =      'FVector',
  Vector2D =    'FVector2D',
  Vector4 =     'FVector4',
  Rotator =     'FRotator',
  Color =       'FColor',
  LinearColor = 'FLinearColor',
  String =      'FString',
  Text =        'FText',

  Function =    'Function',
  Asset =       'Asset',
}

export enum AssetAction {
  SequencePlay =     'SEQUENCE_PLAY',
}

export interface ColorProperty {
  R: number;
  G: number;
  B: number;
  A: number;
}

export interface VectorProperty {
  X: number;
  Y: number;
  Z: number;
  W?: number;
}

export interface RotatorProperty {
  Pitch: number;
  Yaw: number;
  Roll: number;
}

export type PropertyValue = boolean | number | string | VectorProperty | RotatorProperty | ColorProperty;


export interface IFunctionParameter {
  Name: string;
  Description: string;
  Type: PropertyType;
  Optional?: boolean;
  OutParameter?: boolean;
}

export interface IFunction {
  Name: string;
  Description?: string;
  Arguments: IFunctionParameter[];
}

export interface IExposedFunction {
  DisplayName: string;
  UnderlyingFunction: IFunction;
  Metadata: { [key: string]: string };
}

export interface IProperty {
  Name: string;
  Description: string;
  Type: PropertyType;
  Metadata: { [key: string]: string };
}

export interface IExposedProperty {
  DisplayName: string;
  UnderlyingProperty: IProperty;
}

export interface IGroup {
  Name: string;
  ExposedProperties: IExposedProperty[];
  ExposedFunctions: IExposedFunction[];
}

export interface IPreset {
  Path: string;
  Name: string;
  Groups: IGroup[];
}

export interface IAsset {
  Name: string;
  Class: string;
  Path: string;
}

export type IGroupPayload = { [property: string]: PropertyValue };

export type IPayload = { [object: string]: IGroupPayload };

export type IPayloads = { [preset: string]: IPayload };

export enum WidgetType {
  Gauge =           'Gauge',
  Slider =          'Slider',
  Sliders =         'Sliders',
  ScaleSlider =     'Scale Slider',
  ColorPicker =     'Color Picker',
  Toggle =          'Toggle',
  Joystick =        'Joystick',
  Button =          'Button',
  Text =            'Text',

  Level =           'Level',
  Sequence =        'Sequence',
}

export type IWidgetMeta = { [key: string]: any } & {
  default?: PropertyValue;
  min?: number;
  max?: number;
};

export interface IWidget {
  title?: string;
  type?: WidgetType;
  group?: string;
  property?: string;
  meta?: IWidgetMeta;
  order?: number;
}

export interface IPanel {
  name: string;
  stack?: boolean;
  widgets: IWidget[];
}

export enum PanelsLayout {
  OneByOne =     '1x1',
  OneByTwo =     '1x2',
  TwoByOne =     '2x1',
  TwoByTwo =     '2x2',
}

export interface ITab {
  name: string;
  icon: string;
  layout: PanelsLayout;
  panels: IPanel[];
}

export interface IView {
  tabs: ITab[];
}