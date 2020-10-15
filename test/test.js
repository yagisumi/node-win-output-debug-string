const assert = require('assert')
const { OutputDebugString, monitor } = require('../lib')

function sleep(msec) {
  return new Promise((resolve) => {
    setTimeout(() => {
      resolve()
    }, msec)
  })
}

async function test_win() {
  assert.strictEqual(monitor.stop(), false)

  const outputs = []
  const r1 = monitor.start(({ message }) => {
    outputs.push(message)
  })

  assert.strictEqual(r1.error, undefined)
  assert.strictEqual(r1.ok, true)

  OutputDebugString('test1')
  OutputDebugString('test2')

  await sleep(300)

  assert.deepStrictEqual(outputs, ['test1', 'test2'])

  const r2 = monitor.start(() => {})

  assert.strictEqual(r2.ok, false)
  assert.strictEqual(typeof r2.error, 'object')
  assert.strictEqual(r2.error.name, 'AlreadyStartingError')

  assert.strictEqual(monitor.stop(), true)

  const r3 = monitor.start()
  assert.strictEqual(r3.ok, false)
  assert.strictEqual(typeof r3.error, 'object')
  assert.strictEqual(r3.error.name, 'ArgumentError')

  const r4 = monitor.start('test')
  assert.strictEqual(r4.ok, false)
  assert.strictEqual(typeof r4.error, 'object')
  assert.strictEqual(r4.error.name, 'ArgumentError')
}

async function test_not_win() {
  assert.strictEqual(OutputDebugString(), undefined)
  assert.strictEqual(monitor.start(), undefined)
  assert.strictEqual(
    monitor.start(() => {}),
    undefined
  )
  assert.strictEqual(monitor.stop(), undefined)
}

async function main() {
  const err = await (async () => {
    if (process.platform === 'win32') {
      return await test_win().catch((err) => err)
    } else {
      return await test_not_win().catch((err) => err)
    }
  })()

  monitor.stop()

  if (err) {
    console.error(err)
    process.exit(1)
  } else {
    console.log('test passed')
  }
}

main()
