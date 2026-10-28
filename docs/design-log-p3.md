# Project 3 Design Log
**Author:** <Benjamin Bratton> · **P2 code used:** My own (Benjamin Bratton)
**Tools used:** Claude (AI assistant) drafted the registry, `resolve`, `calc_evaluate`, the test suite and a first draft of this log; I reviewed, ran and can explain each part. <edit to match what you actually did>

## 1. Hash table

**Collision strategy: separate chaining.** Each bucket is a `std::vector<Entry>`, where an `Entry` holds the tool's name, its full 64-bit hash, and a `std::unique_ptr<Tool>`. I picked chaining over linear probing for three reasons. It degrades gracefully: a bad cluster only slows down one bucket, while with linear probing a cluster spills into its neighbours. It lets me use a load-factor threshold of 1.0 safely, because a "full" chained table still terminates on a miss. And `max_probe_length` is easy to interpret (it's just the longest chain). The registry holds a handful of tools in real use, so open addressing's cache advantage doesn't matter here.

**Hash function: FNV-1a (64-bit).** It XORs in each byte and then multiplies by the FNV prime, so it uses every character and is order-sensitive (`"ab"` ≠ `"ba"`). `hash()` returns the full value, and the index is computed separately as `hash & (bucket_count - 1)`, which works because the bucket count is always a power of two.

**Threshold and resize policy.** The table starts with 8 buckets. Before inserting, `add` checks whether `(size+1)/buckets` would go above **1.0**. If it would, it doubles the bucket count and re-inserts every entry using the new mask. Checking *before* inserting guarantees `load_factor() ≤ 1.0` every time `add` returns. With chaining, a load factor of 1.0 means the expected chain length is about one, so lookups stay cheap and memory isn't wasted on empty buckets.

**Measurements (10,000 tools `tool_0` … `tool_9999`):**

| Hash | bucket_count | load_factor | longest chain |
|---|---|---|---|
| FNV-1a (my table) | 16384 | 0.6104 | **6** |
| `s[0] + s.size()` (same bucket count) | 16384 | — | **9000** |

The weak hash gives only four distinct values (`'t'` + 6, 7, 8 or 9), so 9,000 of the 10,000 names (`tool_1000`–`tool_9999`) share one bucket and lookup becomes O(n).

**Why `add()` is amortized O(1).** A resize from *n* to *2n* buckets costs O(n), but after it there are about *n* cheap inserts before the next one. If you spread that O(n) over those *n* inserts, each insert pays O(1) extra. The total work for *N* inserts is bounded by *N* + (8 + 16 + … + N) < 3N.

I cache the name and hash in each `Entry` so that `find` and rehashing never call the virtual `name()` (which returns a `std::string` by value). The hash is also compared before the strings, which skips most string comparisons.

## 2. Pointer stability

The buckets store `std::unique_ptr<Tool>`, not `Tool` objects. Each tool is allocated on the heap once, by whoever called `make_unique`, and stays at that address until the registry is destroyed. A rehash moves only the `unique_ptr`s, which is a pointer copy plus nulling the source, into the new bucket vectors. So the `Tool*` that `find` returned points to the same object afterwards. Even if the vectors themselves reallocate, they only move the smart pointers. The test `reg_10000_tools` grabs `tool_0`'s address before the first insert and checks that it's unchanged after 11 resizes (8 → 16384 buckets).

## 3. Calculator trace: `2 * (3 + 4) - 5`

- `2`: push onto values → values `[2]`.
- `*`: ops is empty, so push → ops `[*]`.
- `(`: always push → ops `[*, (]`.
- `3`: values `[2, 3]`.
- `+`: the top of ops is `(`, which isn't an operator, so stop and push → ops `[*, (, +]`.
- `4`: values `[2, 3, 4]`.
- `)`: apply until we reach `(`. Pop `+`, right = 4, left = 3, push 7 → values `[2, 7]`. Then pop the `(` → ops `[*]`.
- `-`: the top is `*` (prec 2 ≥ 1), so apply it: 2 × 7 = 14 → values `[14]`, ops empty. Then push `-`.
- `5`: values `[14, 5]`.
- End: apply `-`: right = 5, left = 14 → 9. One value is left, so the answer is **9**.

The `>=` is what makes equal-precedence operators run left to right. Popping right before left is what makes `10 - 4` come out as 6. The loop is iterative, so the 5,000-deep parentheses test can't overflow the call stack.

## 4. Errors as values

In an agent loop, tool failure is expected input, not a bug. The model asks for files that don't exist, tries `../etc/passwd`, or divides by zero, and the right response is to tell the model so it can try something else. Returning `ToolResult::failure(...)` (or `false` plus a reason) makes the failure path an ordinary, visible part of every function signature. A caller can't forget to handle it the way it can forget a `try`. It also keeps a single model mistake from unwinding through the harness and killing the conversation. Exceptions are kept for things that should never happen if the C++ is correct, like calling `as_string()` on a number. Those mean a programmer has to fix the code, and they should be loud. The dispatcher's catch-all is a last safety net, not the normal error channel.

## 5. What I would design differently

`for_each` takes a `std::function`, which allocates and adds an indirect call each time. A template member, or returning a sorted list of `const Tool*`, would be simpler for `--tools`, which sorts anyway. I'd also consider making the registry generic (`HashMap<V>`) so the same table could be unit-tested with plain strings and reused elsewhere.
