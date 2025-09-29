declare module "node:test" {
  type TestFn = (fn: () => void | Promise<void>) => void

  export function describe(name: string, fn: TestFn): void
  export function it(name: string, fn: TestFn): void
}

declare module "node:assert/strict" {
  const assert: {
    equal(actual: unknown, expected: unknown): void
    throws(fn: () => unknown): void
  }

  export default assert
}
