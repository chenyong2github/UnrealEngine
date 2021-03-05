import { IPayload, PropertyValue } from 'src/shared';

export type ActorWidgetProps = {
  payload: IPayload;
  actors: string[];
  getValue: (actor: string, path?: string) => any;
  onPropertyValueChange: (actor: string, path: string, value: PropertyValue) => void;
};

export * from './Cards';
export * from './Camera';
export * from './GreenScreen';
export * from './Location';
export * from './Save';
export * from './Snapshot';
export * from './TabWidget';
export * from './Walls';
export * from './components/ActorColor';
export * from './components/ActorList';
export * from './components/ActorSlider';