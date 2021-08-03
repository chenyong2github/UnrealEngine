import { Controller, Get, Post, Put, Delete } from '@overnightjs/core';
import { Request, Response } from 'express';
import { UnrealEngine } from './';


interface ISendOptions {
  type?: string;
  nullIsNotFound?: boolean;
}

@Controller('api')
export class Api {
  
  public async initialize(): Promise<void> {
  }

  @Get('connected')
  private connected(req: Request, res: Response) {
    this.send(res, Promise.resolve({ connected: UnrealEngine.isConnected() }));
  }

  @Get('presets')
  private presets(req: Request, res: Response) {
    this.send(res, UnrealEngine.getPresets());
  }

  @Get('payloads')
  private payloads(req: Request, res: Response) {
    this.send(res, UnrealEngine.getPayloads());
  }

  @Get('presets/:preset/load')
  private async load(req: Request, res: Response): Promise<void> {
    this.send(res, UnrealEngine.loadPreset(req.params.preset));
  }  

  @Get('presets/payload')
  private async payload(req: Request, res: Response): Promise<void> {
    this.send(res, UnrealEngine.getPayload(req.query.preset?.toString()));
  }

  @Get('presets/view')
  private view(req: Request, res: Response) {
    this.send(res, UnrealEngine.getView(req.query.preset?.toString()));
  }

  @Get('assets/search')
  private search(req: Request, res: Response) {
    const prefix = req.query.prefix?.toString() || '/Game';
    const query = req.query.q?.toString() ?? '';
    const count = parseInt(req.query.count?.toString()) || 50;
    const types = req.query.types?.toString().split(',') ?? [];
    this.send(res, UnrealEngine.search(query, types, prefix, count));
  }

  @Put('proxy')
  private proxy(req: Request, res: Response) {
    this.send(res, UnrealEngine.proxy(req.body?.method, req.body?.url, req.body?.body));
  }

  @Get('thumbnail')
  private thumbnail(req: Request, res: Response) {
    this.send(res, UnrealEngine.thumbnail(req.query.asset.toString()));
  }

  @Get('shutdown')
  private shutdown(req: Request, res: Response) {
    res.send({ message: 'ok' });
    setTimeout(() => process.exit(0), 1000);
  }

  protected async send(res: Response, promise: Promise<any>, options?: ISendOptions): Promise<any> {
    try {

      const result = await promise;
      if (result === null && options?.nullIsNotFound)
        return res.status(404).send({ status: 'ERROR', error: 'Not found' });

      if (options?.type)
        res.set('Content-Type', options.type);

      res.send(result);

    } catch (err) {
      const error = err?.message ?? err;
      res.status(500)
          .send({ status: 'ERROR', error });
    }
  }
}
