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

export type PropertyValue = boolean | number | string | VectorProperty | RotatorProperty | ColorProperty | IPayload;

export type JoystickValue = { [key: string]: number };

export type AxisInfo = [string, number];

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
  Id: string;
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
  Id: string;
  DisplayName: string;
  Metadata: Record<string, string>;
  Widget: WidgetType;
  UnderlyingProperty: IProperty;
}

export interface IActor {
  Name: string;
  Path: string;
  Class: string;
}

export interface IExposedActor {
  DisplayName: string;
  UnderlyingActor: IActor;
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
  
  ExposedProperties?: IExposedProperty[];
  ExposedFunctions?: IExposedFunction[];
  Exposed: Record<string, IExposedProperty | IExposedFunction>;
}

export interface IAsset {
  Name: string;
  Class: string;
  Path: string;
  Metadata: Record<string, string>;
}

export type IPayload = { [property: string]: PropertyValue | IPayload };

export type IPayloads = { [preset: string]: IPayload };

export enum WidgetTypes {
  Gauge =           'Gauge',
  Slider =          'Slider',
  Sliders =         'Sliders',
  ScaleSlider =     'Scale Slider',
  ColorPicker =     'Color Picker',
  Toggle =          'Toggle',
  Joystick =        'Joystick',
  Button =          'Button',
  Text =            'Text',
  Label =           'Label',
  Dropdown =        'Dropdown',
  ImageSelector =   'Image Selector',
  Vector =          'Vector',

  Level =           'Level',
  Sequence =        'Sequence',
}

export type WidgetType = keyof typeof WidgetTypes | string;


export type IWidgetMeta = { [key: string]: any } & {
  default?: PropertyValue;
  min?: number;
  max?: number;
};

export interface IWidget {
  title?: string;
  type?: WidgetType;
  property?: string;
  meta?: IWidgetMeta;
  order?: number;
}

export enum IPanelType {
  Panel = 'PANEL',
  List  = 'LIST',
}

export interface IPanel {
  id?: string;
  title?: string;
  type: IPanelType;
  widgets?: ICustomStackWidget[];
  
  items?: ICustomStackListItem[];
  addFunction?: ICustomStackFunction;
  removeFunction?: ICustomStackFunction;
}

export enum TabLayout {
  Panel1X1 =      '1x1',
  Panel1x2 =      '1x2',
  Panel2x1 =      '2x1',
  Panel2x2 =      '2x2',
  Stack =         'Stack',

  CustomActor =   'CustomActor',
}

export interface IActorWidget {
  type: 'Walls' | 'Cards' | 'Location' | 'Camera' | 'Save' | 'Another' | 'GreenScreen' | 'Snapshot';
  actors: string[];
}

export interface IDropdownOption {
  value: string;
  label?: string;
}

export interface ICustomStackProperty {
  id?: string;
  actor?: string;
  property: string;
  propertyType: PropertyType;
  widget: WidgetType;
  label?: string;
  reset?: PropertyValue;

  min?: number;
  max?: number;

  // Sliders & Vector only
  lock?: boolean;

  // Vector only
  widgets?: (WidgetTypes.Joystick | WidgetTypes.Sliders | WidgetTypes.Gauge)[];
  speedMin?: number;
  speedMax?: number;

  // Dropdown only
  options?: IDropdownOption[];
}

export interface ICustomStackFunction {
  actor: string;
  function: string;
}

export interface ICustomStackItem {
  id?: string;
  label: string;
  widgets: ICustomStackWidget[];
}

export interface ICustomStackTabs {
  id?: string;
  widget: 'Tabs';
  tabs: ICustomStackItem[];
}

export interface ICustomStackListItem {
  id?: string;
  label: string;
  check?: { actor: string; property: string; };
  panels: IPanel[];
}

export type ICustomStackWidget = ICustomStackProperty | ICustomStackTabs;

export interface ITab {
  name: string;
  icon: string;
  layout: TabLayout;
  panels?: IPanel[];
  stack?: ICustomStackWidget[];

  actor?: IActorWidget;
}

export interface IView {
  tabs: ITab[];
}