import path from 'path';


class Program {
  readonly name: string = 'Conductor';
  readonly dev: boolean;
  readonly monitor: boolean;
  readonly port: number;
  readonly ue: number;
  readonly rootFolder: string;

  constructor() {
    this.dev = false;
    this.monitor = false;
    this.port = 7000;
    this.ue = 30020;

    for (let i = 2; i < process.argv.length; i++) {
      switch (process.argv[i]) {
        case '--dev':
          this.dev = true;
          this.port = 7001;
          break;

        case '--monitor':
          this.monitor = true;
          break;

        case '--port':
          this.port = parseInt(process.argv[i + 1]) || 7000;
          i++;
          break;

        case '--ue':
          this.ue = parseInt(process.argv[i + 1]) || 30020;
          i++;
          break;
      }
    }

    process.env.NODE_ENV = this.dev ? 'development' : 'production';
    this.rootFolder = path.resolve('./');
  }
}

const Instance = new Program();
export { Instance as Program };