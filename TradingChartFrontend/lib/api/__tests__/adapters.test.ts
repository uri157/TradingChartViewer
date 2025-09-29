import assert from "node:assert/strict"
import { describe, it } from "node:test"

import { tupleToDTO } from "../adapters"

describe("tupleToDTO", () => {
  it("normalizes second-based timestamps to milliseconds", () => {
    const dto = tupleToDTO([1_701_000_000, 1, 2, 0.5, 1.5, 10])
    assert.equal(dto.t, 1_701_000_000_000)
  })

  it("preserves millisecond timestamps", () => {
    const dto = tupleToDTO([1_701_000_000_123, 1, 2, 0.5, 1.5, 10])
    assert.equal(dto.t, 1_701_000_000_123)
  })

  it("rejects malformed tuples", () => {
    assert.throws(() => tupleToDTO([1, 2, 3] as unknown as any))
  })
})
