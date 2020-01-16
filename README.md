# @yagisumi/win-output-debug-string

Windows' `OutputDebugString()` and monitor.

[![NPM version][npm-image]][npm-url] [![install size][packagephobia-image]][packagephobia-url] [![DefinitelyTyped][dts-image]][dts-url]  
[![Build Status][githubactions-image]][githubactions-url]

## Installation

```sh
$ npm i @yagisumi/win-output-debug-string
```

## Usage

```ts
import { OutputDebugString, monitor } from '@yagisumi/win-output-debug-string'

const r = monitor.start(({ pid, message }) => {
  console.log(`[${pid}] ${message}`)
})

if (r.error) {
  console.error(r.error)
  process.exit()
}

const iid = setInterval(() => {
  OutputDebugString('message')
}, 1000)

setTimeout(() => {
  clearInterval(iid)
  monitor.stop()
}, 10000)
```

## API

### `OutputDebugString(message: string): void`

Executes `OutputDebugString()`.

### `monitor.start(cb: (info: { pid: number, message: string }) => void): OK | ERR`

Starts monitoring OutputDebugString outputs.<br>
The character encoding of `message` is converted to system default Windows ANSI code page.

```
type OK = { ok: true, error: undefined }
type ERR = { ok: false, error: { name: string, message: string }}
```

### `monitor.stop(): boolean`

Returns true if stop operation is performed, otherwise returns false.

## License

[MIT License](https://opensource.org/licenses/MIT)

[githubactions-image]: https://img.shields.io/github/workflow/status/yagisumi/node-win-output-debug-string/build?logo=github&style=flat-square
[githubactions-url]: https://github.com/yagisumi/node-win-output-debug-string/actions
[npm-image]: https://img.shields.io/npm/v/@yagisumi/win-output-debug-string.svg?style=flat-square
[npm-url]: https://npmjs.org/package/@yagisumi/win-output-debug-string
[packagephobia-image]: https://flat.badgen.net/packagephobia/install/@yagisumi/win-output-debug-string
[packagephobia-url]: https://packagephobia.now.sh/result?p=@yagisumi/win-output-debug-string
[dts-image]: https://img.shields.io/badge/DefinitelyTyped-.d.ts-blue.svg?style=flat-square
[dts-url]: http://definitelytyped.org
